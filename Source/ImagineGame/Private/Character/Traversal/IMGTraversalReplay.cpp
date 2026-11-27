#include "Character/Traversal/IMGTraversalComponent.h"

#include "Animation/AnimMontage.h"
#include "Character/IMGCharacter.h"
#include "Character/IMGCharacterMovementComponent.h"
#include "Character/IMGHealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "MotionWarpingComponent.h"
#include "UObject/StrongObjectPtr.h"
#include "Character/Traversal/IMGTraversalDebug.h"
#include "Character/Traversal/Execution/IMGGroundPlacement.h"

/** Immutable/shared per-execution replay context, allocated once per autonomous traversal. */
struct FIMGTraversalReplaySession
{
	bool bValid = true;
	bool bGroundReleased = false;
	uint32 ExecutionId = 0;
	float HandoffTime = -1.f;
	float MaxGroundAdjustment = 0.f;
	FIMGTraversalPlaybackPath Playback;
	TWeakObjectPtr<UAnimMontage> Montage;
	FGameplayAbilitySpecHandle AbilitySpec;
	int16 ActivationPredictionKey = 0;
	uint16 Sequence = 0;
};

namespace
{
void ApplyReplayExitMode(const UIMGTraversalComponent* Component, UIMGCharacterMovementComponent* Movement,
    uint8 PackedMode)
{
	if (!Component || !Component->IsTraversalReady() || !Movement || !Movement->IsFlying())
	{
		return;
	}
	const UIMGHealthComponent* Health = UIMGHealthComponent::FindHealthComponent(Component->GetOwner());
	if (Health && Health->IsDeadOrDying())
	{
		return;
	}
	TEnumAsByte<EMovementMode> Mode(MOVE_None), GroundMode(MOVE_None);
	uint8 CustomMode = 0;
	Movement->UnpackNetworkMovementMode(PackedMode, Mode, CustomMode, GroundMode);
	if (Mode == MOVE_Walking || Mode == MOVE_NavWalking || Mode == MOVE_Falling)
	{
		FFindFloorResult Floor;
		if (!Movement->UpdatedComponent)
		{
			return;
		}
		Movement->FindFloor(Movement->UpdatedComponent->GetComponentLocation(), Floor, false);
		if (Movement->IsFlying())
		{
			Movement->SetMovementMode(Floor.IsWalkableFloor() ? MOVE_Walking : MOVE_Falling);
		}
	}
}

/** Single-move replay snapshot; consolidates all per-SavedMove data into one heap allocation. */
struct FIMGTraversalReplayMove
{
	const FSavedMove_Character* RecordedMove = nullptr;
	bool bEnteredFlying = false;
	bool bRecordedHandoff = false;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> HistoricalIgnores;
	TArray<FIMGTraversalWarpTarget> Targets;
	TArray<TStrongObjectPtr<URootMotionModifier>> Modifiers;
	TArray<TSharedPtr<FRootMotionModifierSwapState>> States;
	TArray<TWeakObjectPtr<const UAnimSequenceBase>> Animations;
	TArray<FMotionWarpingTarget> ResolvedTargets;
	TArray<TStrongObjectPtr<URootMotionModifier>> ReplayModifiers;

	void CaptureWarpContext(UMotionWarpingComponent* Warping)
	{
		if (!Warping)
		{
			return;
		}
		Modifiers.Reset();
		States.Reset();
		Animations.Reset();
		ResolvedTargets.Reset();
		for (URootMotionModifier* Modifier : Warping->GetModifiers())
		{
			if (Modifier->GetState() == ERootMotionModifierState::MarkedForRemoval)
			{
				continue;
			}
			Modifiers.Emplace(Modifier);
			States.Add(Modifier->GetSwapState());
			Animations.Add(Modifier->Animation);
		}
		for (const FIMGTraversalWarpTarget& Target : Targets)
		{
			if (const FMotionWarpingTarget* Resolved = Warping->FindWarpTarget(Target.Name))
			{
				ResolvedTargets.Emplace(Target.Name, Resolved->GetTargetTrasform());
			}
		}
	}

	void HandlePostMovement(const TWeakObjectPtr<UIMGTraversalComponent>& WeakComponent,
	    const TSharedPtr<FIMGTraversalReplaySession>& Session, UIMGCharacterMovementComponent* Movement,
	    UCapsuleComponent* Capsule) const
	{
		float Position = -1.f;
		if (!Movement || !Capsule || !Session || Session->bGroundReleased || Session->HandoffTime < 0.f ||
		    !Movement->IsFlying())
		{
			return;
		}
		const bool bCompletedDuringMove = !RecordedMove->RootMotionMontage.IsValid() && bRecordedHandoff;
		if (!bCompletedDuringMove &&
		    (!Session->Playback.GetPlaybackPosition(RecordedMove->RootMotionTrackPosition, Position) ||
		        Position < Session->HandoffTime))
		{
			return;
		}
		for (const TWeakObjectPtr<UPrimitiveComponent>& Obstacle : HistoricalIgnores)
		{
			if (Obstacle.IsValid())
			{
				Capsule->IgnoreComponentWhenMoving(Obstacle.Get(), false);
			}
		}
		Session->bGroundReleased = true;
		const bool bGrounded = IMGGroundPlacement::FindGround(*Movement, Session->MaxGroundAdjustment);
		if (WeakComponent.IsValid() && Session->bValid && Movement->IsFlying())
		{
			Movement->SetMovementMode(bGrounded ? MOVE_Walking : MOVE_Falling);
#if IMG_TRAVERSAL_DEBUG_ENABLED
			if (IMGTraversalDebug::IsObserving(Movement->GetOwner()))
			{
				IMGTraversalDebug::Event(Movement->GetOwner(), TEXT("ReplayGroundHandoff"),
				    FString::Printf(TEXT("Move=%.6f Playback=%.6f Boundary=%.6f Grounded=%d"),
				        RecordedMove->TimeStamp, Position, Session->HandoffTime, bGrounded),
				    !bGrounded, IMGTraversalDebug::EStage::Replay);
			}
#endif
		}
	}
};
} // namespace

UIMGTraversalComponent::~UIMGTraversalComponent() = default;

void UIMGTraversalComponent::BeginReplaySession()
{
	ReplaySession = MakeShared<FIMGTraversalReplaySession>();
	ReplaySession->bValid = true;
	ReplaySession->bGroundReleased = false;
	ReplaySession->ExecutionId = ExecutionId;
	ReplaySession->HandoffTime = ActiveState.GroundHandoffTime;
	ReplaySession->MaxGroundAdjustment = ActiveState.MaxGroundAdjustment;
	ReplaySession->Playback = GroundPlayback;
	ReplaySession->Montage = ActiveState.Montage;
	ReplaySession->AbilitySpec = ActiveState.AbilitySpec;
	ReplaySession->ActivationPredictionKey = ActiveState.ActivationPredictionKey;
	ReplaySession->Sequence = ActiveState.Sequence;
}

void UIMGTraversalComponent::InvalidateReplaySession()
{
	if (ReplaySession)
	{
		ReplaySession->bValid = false;
	}
}

void UIMGTraversalComponent::MarkReplayGroundReleased()
{
	if (ReplaySession)
	{
		ReplaySession->bGroundReleased = true;
	}
}

void UIMGTraversalComponent::ResetReplaySession()
{
	ReplaySession.Reset();
}

void UIMGTraversalComponent::RetirePredictedReplay(FGameplayAbilitySpecHandle AbilitySpec, int16 PredictionKey)
{
	if (ReplaySession && ActiveState.AbilitySpec == AbilitySpec && ActiveState.ActivationPredictionKey == PredictionKey)
	{
#if IMG_TRAVERSAL_DEBUG_ENABLED
		IMGTraversalDebug::FScope DebugScope(GetOwner(), IMGTraversalDebug::EPhase::Replay);
		IMGTraversalDebug::State(GetOwner(), TEXT("PredictedReplayRetired"), ActiveState);
#endif
		ReplaySession->bValid = false;
	}
}

void UIMGTraversalComponent::CaptureMovementReplay(
    const FSavedMove_Character& Move, TArray<TFunction<TFunction<void()>()>>& OutSetups)
{
	RecordWarpContext = nullptr;
	RecordGroundHandoff = nullptr;
	AIMGCharacter* Character = Cast<AIMGCharacter>(GetOwner());
	if (!Character || Character->GetLocalRole() != ROLE_AutonomousProxy || bReplayingTraversalMove)
	{
		return;
	}
	// SavedMove objects are pooled; their previous execution identity must not survive reuse.
	ReplayMoveExecutions.Remove(&Move);
	const TWeakObjectPtr<ThisClass> WeakThis(this);
	const TWeakObjectPtr<UIMGCharacterMovementComponent> WeakMovement = BoundMovement;
	if (!ActiveState.bActive)
	{
		if (bCaptureTraversalExit)
		{
			const uint8 ExitMode = Move.StartPackedMovementMode;
			OutSetups.Add([WeakThis, WeakMovement, ExitMode]() -> FMovementReplayRestore
			{
				const ThisClass* Current = WeakThis.Get();
				UIMGCharacterMovementComponent* Movement = WeakMovement.Get();
				if (Current && Current->BoundMovement == WeakMovement)
				{
					ApplyReplayExitMode(Current, Movement, ExitMode);
				}
				return {};
			});
			bCaptureTraversalExit = false;
		}
		return;
	}
	bCaptureTraversalExit = false;
	ReplayMoveExecutions.Add(&Move, ExecutionId);
	if (ReplaySession)
	{
		ReplaySession->HandoffTime = ActiveState.GroundHandoffTime;
		ReplaySession->MaxGroundAdjustment = ActiveState.MaxGroundAdjustment;
		if (ReplaySession->Playback.GetRanges().IsEmpty() && !GroundPlayback.GetRanges().IsEmpty())
		{
			ReplaySession->Playback = GroundPlayback;
		}
	}
#if IMG_TRAVERSAL_DEBUG_ENABLED
	if (IMGTraversalDebug::IsObserving(GetOwner()))
	{
		const FString DebugIdentity = FString::Printf(TEXT("HistoricalSpec=%s Pred=%d Seq=%u Montage=%s Move=%.6f"),
		    *ActiveState.AbilitySpec.ToString(), ActiveState.ActivationPredictionKey, ActiveState.Sequence,
		    *GetNameSafe(ActiveState.Montage.Get()), Move.TimeStamp);
		IMGTraversalDebug::FScope DebugScope(GetOwner(), IMGTraversalDebug::EPhase::Replay);
		IMGTraversalDebug::Event(
		    GetOwner(), TEXT("ReplayCaptured"), DebugIdentity, false, IMGTraversalDebug::EStage::Replay);
	}
#endif
	const TSharedPtr<FIMGTraversalReplaySession> Session = ReplaySession;
	const TSharedRef<FIMGTraversalReplayMove> MoveData = MakeShared<FIMGTraversalReplayMove>();
	MoveData->RecordedMove = &Move;
	TEnumAsByte<EMovementMode> EntryMode(MOVE_None), EntryGroundMode(MOVE_None);
	uint8 EntryCustomMode = 0;
	if (BoundMovement.IsValid())
	{
		BoundMovement->UnpackNetworkMovementMode(Move.StartPackedMovementMode, EntryMode, EntryCustomMode, EntryGroundMode);
	}
	MoveData->bEnteredFlying = EntryMode == MOVE_Flying;
	MoveData->HistoricalIgnores =
	    ActiveState.MovementPhase == EIMGTraversalMovementPhase::Warping ? AddedMovementIgnores : GroundHandoffIgnores;
	MoveData->Targets = ActiveState.WarpTargets;

	const TWeakObjectPtr<UMotionWarpingComponent> WeakWarping = BoundMotionWarpingComponent;
	const TWeakObjectPtr<UCapsuleComponent> WeakCapsule(Character->GetCapsuleComponent());
	RecordGroundHandoff = [MoveData]() { MoveData->bRecordedHandoff = true; };
	// Native OnPreUpdate runs after discovering windows, before changing modifier state.
	RecordWarpContext = [WeakWarping, MoveData]() { MoveData->CaptureWarpContext(WeakWarping.Get()); };

	OutSetups.Add([WeakThis, WeakWarping, WeakMovement, WeakCapsule, Session, MoveData]() -> FMovementReplayRestore
	{
		UIMGCharacterMovementComponent* Movement = WeakMovement.Get();
		if (!Movement)
		{
			return {};
		}
#if IMG_TRAVERSAL_DEBUG_ENABLED
		IMGTraversalDebug::FScope DebugScope(Movement->GetOwner(), IMGTraversalDebug::EPhase::Replay);
		const FString DebugIdentity = Session && IMGTraversalDebug::IsObserving(Movement->GetOwner())
		    ? FString::Printf(TEXT("HistoricalSpec=%s Pred=%d Seq=%u Montage=%s Move=%.6f"),
		          *Session->AbilitySpec.ToString(), Session->ActivationPredictionKey, Session->Sequence,
		          *GetNameSafe(Session->Montage.Get()), MoveData->RecordedMove->TimeStamp)
		    : FString();
#endif
		auto DiscardHistoricalMotion = [Movement, MoveData, Session]()
		{
			if (Session && MoveData->RecordedMove->RootMotionMontage == Session->Montage)
			{
				Movement->RootMotionParams.Clear();
			}
		};
		ThisClass* Self = WeakThis.Get();
		UMotionWarpingComponent* Warping = WeakWarping.Get();
		UCapsuleComponent* Capsule = WeakCapsule.Get();
		if (!Self || !Self->IsTraversalReady() || !Warping || !Capsule ||
		    Self->BoundMovement != WeakMovement || Self->BoundMotionWarpingComponent != WeakWarping ||
		    Self->bReplayingTraversalMove)
		{
#if IMG_TRAVERSAL_DEBUG_ENABLED
			if (IMGTraversalDebug::IsObserving(Movement->GetOwner()))
			{
				IMGTraversalDebug::Event(Movement->GetOwner(), TEXT("ReplayDiscarded"),
				    DebugIdentity + TEXT(" | Bindings unavailable or replay reentered"), false,
				    IMGTraversalDebug::EStage::Replay);
			}
#endif
			DiscardHistoricalMotion();
			return {};
		}
		if (!Movement->IsRegistered() || !Movement->IsActive())
		{
#if IMG_TRAVERSAL_DEBUG_ENABLED
			if (IMGTraversalDebug::IsObserving(Movement->GetOwner()))
			{
				IMGTraversalDebug::Event(Movement->GetOwner(), TEXT("ReplayDiscarded"),
				    DebugIdentity + TEXT(" | Movement inactive"), false, IMGTraversalDebug::EStage::Replay);
			}
#endif
			DiscardHistoricalMotion();
			return {};
		}
		const UIMGHealthComponent* Health = UIMGHealthComponent::FindHealthComponent(Self->GetOwner());
		if (!Session || !Session->bValid || (Health && Health->IsDeadOrDying()) ||
		    !(Movement->IsFlying() || Movement->IsMovingOnGround() || Movement->IsFalling()))
		{
			// A terminal authority result must not resurrect the predicted montage through old moves.
#if IMG_TRAVERSAL_DEBUG_ENABLED
			if (IMGTraversalDebug::IsObserving(Movement->GetOwner()))
			{
				IMGTraversalDebug::Event(Movement->GetOwner(), TEXT("ReplayDiscarded"),
				    DebugIdentity + TEXT(" | Session retired, death or movement takeover"), false,
				    IMGTraversalDebug::EStage::Replay);
			}
#endif
			DiscardHistoricalMotion();
			return {};
		}
		Self->bReplayingTraversalMove = true;
#if IMG_TRAVERSAL_DEBUG_ENABLED
		IMGTraversalDebug::Event(
		    Self->GetOwner(), TEXT("ReplayBegin"), DebugIdentity, false, IMGTraversalDebug::EStage::Replay);
#endif
		if (MoveData->bEnteredFlying && (Session->HandoffTime < 0.f || !Session->bGroundReleased) &&
		    (Movement->IsMovingOnGround() || Movement->IsFalling()))
		{
			Movement->SetMovementMode(MOVE_Flying);
		}
		if (!Self->IsTraversalReady() || Self->BoundMovement != WeakMovement ||
		    Self->BoundMotionWarpingComponent != WeakWarping)
		{
			Self->bReplayingTraversalMove = false;
			DiscardHistoricalMotion();
			return {};
		}
		const uint32 Revision = Self->TraversalTargetsRevision;
		const FIMGTraversalRepState LiveState = Self->ActiveState;
		TArray<FMotionWarpingTarget> PreviousTargets;
		for (const FMotionWarpingTarget& Target : MoveData->ResolvedTargets)
		{
			if (const FMotionWarpingTarget* Previous = Warping->FindWarpTarget(Target.Name))
			{
				PreviousTargets.Add(*Previous);
			}
			Warping->AddOrUpdateWarpTarget(Target);
		}
		TArray<TStrongObjectPtr<URootMotionModifier>> LiveModifiers;
		TArray<TSharedPtr<FRootMotionModifierSwapState>> LiveStates;
		TArray<TWeakObjectPtr<const UAnimSequenceBase>> LiveAnimations;
		// Keep live objects intact, and replay copies through the native modifier implementation.
		for (URootMotionModifier* Modifier : Warping->GetModifiers())
		{
			if (!Modifier->Animation.IsValid() && Modifier->GetState() == ERootMotionModifierState::MarkedForRemoval)
			{
				continue;
			}
			LiveModifiers.Emplace(Modifier);
			LiveStates.Add(Modifier->GetSwapState());
			LiveAnimations.Add(Modifier->Animation);
			const auto InertState = Modifier->GetSwapState();
			InertState->State = ERootMotionModifierState::MarkedForRemoval;
			Modifier->RestoreFromSwapState(InertState);
			Modifier->Animation.Reset();
		}
		for (int32 Index = 0; Index < MoveData->Modifiers.Num(); ++Index)
		{
			if (!MoveData->ReplayModifiers.IsValidIndex(Index))
			{
				MoveData->ReplayModifiers.Emplace(
				    DuplicateObject<URootMotionModifier>(MoveData->Modifiers[Index].Get(), Warping));
			}
			URootMotionModifier* ReplayModifier = MoveData->ReplayModifiers[Index].Get();
			ReplayModifier->RestoreFromSwapState(MoveData->States[Index]);
			ReplayModifier->Animation = MoveData->Animations[Index];
			// Replay must not deliver the notify's gameplay callbacks a second time.
			ReplayModifier->OnActivateDelegate.Unbind();
			ReplayModifier->OnUpdateDelegate.Unbind();
			ReplayModifier->OnDeactivateDelegate.Unbind();
			if (!Warping->GetModifiers().Contains(ReplayModifier))
			{
				Warping->AddModifier(ReplayModifier);
			}
		}
		for (const TWeakObjectPtr<UPrimitiveComponent>& Obstacle : Self->AddedMovementIgnores)
		{
			if (Obstacle.IsValid())
			{
				Capsule->IgnoreComponentWhenMoving(Obstacle.Get(), false);
			}
		}
		TArray<TWeakObjectPtr<UPrimitiveComponent>> TemporaryIgnores;
		for (const TWeakObjectPtr<UPrimitiveComponent>& Obstacle : MoveData->HistoricalIgnores)
		{
			if (Session->HandoffTime >= 0.f && Session->bGroundReleased)
			{
				break;
			}
			if (Obstacle.IsValid() && !Capsule->GetMoveIgnoreComponents().Contains(Obstacle.Get()))
			{
				Capsule->IgnoreComponentWhenMoving(Obstacle.Get(), true);
				TemporaryIgnores.Add(Obstacle);
			}
		}
		Self->ReplayPostMovement = [WeakThis, Session, WeakMovement, WeakCapsule, MoveData]()
		{
			MoveData->HandlePostMovement(WeakThis, Session, WeakMovement.Get(), WeakCapsule.Get());
		};
		return [WeakThis, WeakWarping, WeakCapsule, WeakMovement, Session, MoveData, Revision, LiveState,
		           PreviousTargets = MoveTemp(PreviousTargets), LiveModifiers = MoveTemp(LiveModifiers),
		           LiveStates = MoveTemp(LiveStates), LiveAnimations = MoveTemp(LiveAnimations),
		           TemporaryIgnores = MoveTemp(TemporaryIgnores)
#if IMG_TRAVERSAL_DEBUG_ENABLED
		           ,
		           DebugIdentity
#endif
		]()
		{
			ThisClass* Current = WeakThis.Get();
#if IMG_TRAVERSAL_DEBUG_ENABLED
			IMGTraversalDebug::FScope DebugScope(
			    Current ? Current->GetOwner() : nullptr, IMGTraversalDebug::EPhase::Replay);
#endif
			if (UMotionWarpingComponent* CurrentWarping = WeakWarping.Get())
			{
				for (URootMotionModifier* Modifier : CurrentWarping->GetModifiers())
				{
					const auto InertState = Modifier->GetSwapState();
					InertState->State = ERootMotionModifierState::MarkedForRemoval;
					Modifier->RestoreFromSwapState(InertState);
					Modifier->Animation.Reset();
				}
				for (int32 Index = 0; Index < LiveModifiers.Num(); ++Index)
				{
					URootMotionModifier* Modifier = LiveModifiers[Index].Get();
					Modifier->Animation = LiveAnimations[Index];
					Modifier->RestoreFromSwapState(LiveStates[Index]);
					if (!CurrentWarping->GetModifiers().Contains(Modifier))
					{
						CurrentWarping->AddModifier(Modifier);
					}
				}
				for (const FMotionWarpingTarget& Target : MoveData->ResolvedTargets)
				{
					const bool bSessionEnded = Current && Revision != Current->TraversalTargetsRevision &&
					    LiveState.WarpTargets.ContainsByPredicate(
					        [&Target](const FIMGTraversalWarpTarget& Live) { return Live.Name == Target.Name; });
					const FMotionWarpingTarget* Previous = (bSessionEnded ? LiveState.PreviousWarpTargets : PreviousTargets)
					    .FindByPredicate([&Target](const FMotionWarpingTarget& Live) { return Live.Name == Target.Name; });
					CurrentWarping->RemoveWarpTarget(Target.Name);
					if (Previous)
					{
						CurrentWarping->AddOrUpdateWarpTarget(*Previous);
					}
				}
			}
			if (Current && Current->ActiveState.bActive && Current->ExecutionId == Session->ExecutionId &&
			    Session->HandoffTime >= 0.f)
			{
				if (UIMGCharacterMovementComponent* ReplayedMovement = WeakMovement.Get())
				{
					Current->ActiveState.MovementPhase = !Session->bGroundReleased
					    ? EIMGTraversalMovementPhase::Warping
					    : ReplayedMovement->IsMovingOnGround() ? EIMGTraversalMovementPhase::GroundExit
					                                           : EIMGTraversalMovementPhase::Falling;
					if (Session->bGroundReleased)
					{
						if (Current->GroundHandoffIgnores.IsEmpty())
						{
							Current->GroundHandoffIgnores = Current->AddedMovementIgnores;
						}
						Current->RestoreMovementIgnores();
					}
				}
			}
			if (UCapsuleComponent* CurrentCapsule = WeakCapsule.Get())
			{
				for (const TWeakObjectPtr<UPrimitiveComponent>& Obstacle : TemporaryIgnores)
				{
					if (Obstacle.IsValid())
					{
						CurrentCapsule->IgnoreComponentWhenMoving(Obstacle.Get(), false);
					}
				}
				if (Current)
				{
					for (const TWeakObjectPtr<UPrimitiveComponent>& Obstacle : Current->AddedMovementIgnores)
					{
						if (Obstacle.IsValid())
						{
							CurrentCapsule->IgnoreComponentWhenMoving(Obstacle.Get(), true);
						}
					}
				}
			}
			if (Current)
			{
				Current->ReplayPostMovement = nullptr;
				Current->bReplayingTraversalMove = false;
				if (Current->ActiveState.bActive && Current->IsTraversalReady())
				{
					Current->RefreshWarpTargets();
				}
			}
			if (Session->HandoffTime < 0.f && Current && Current->BoundMovement == WeakMovement)
			{
				ApplyReplayExitMode(Current, WeakMovement.Get(), MoveData->RecordedMove->EndPackedMovementMode);
			}
#if IMG_TRAVERSAL_DEBUG_ENABLED
			IMGTraversalDebug::Event(Current ? Current->GetOwner() : nullptr, TEXT("ReplayRestored"), DebugIdentity,
			    false, IMGTraversalDebug::EStage::Replay);
#endif
		};
	});
}

void UIMGTraversalComponent::HandleClientCorrectionApplied(
    const FCharacterMoveResponseDataContainer& Response, const FSavedMove_Character& Move)
{
	const uint32* RecordedExecution = ReplayMoveExecutions.Find(&Move);
	if (!RecordedExecution || *RecordedExecution != ExecutionId || !ReplaySession || !ReplaySession->bValid ||
	    ActiveState.GroundHandoffTime < 0.f ||
	    (Move.RootMotionMontage.IsValid() && Move.RootMotionMontage != ActiveState.Montage.Get()) ||
	    !BoundMovement.IsValid())
	{
		return;
	}
	TEnumAsByte<EMovementMode> Mode(MOVE_None), GroundMode(MOVE_None);
	uint8 CustomMode = 0;
	BoundMovement->UnpackNetworkMovementMode(Response.ClientAdjustment.MovementMode, Mode, CustomMode, GroundMode);
	if (Mode != MOVE_Flying && Mode != MOVE_Walking && Mode != MOVE_NavWalking && Mode != MOVE_Falling)
	{
		ReplaySession->bValid = false;
		return;
	}
	// This is the authority's phase at the acknowledged move, not the latest OnRep snapshot.
	ReplaySession->bGroundReleased = Mode != MOVE_Flying;
	if (!ActiveState.bActive)
	{
		// The execution already ended locally; only unacknowledged-move replay needs the authority boundary.
		return;
	}
	ActiveState.MovementPhase = Mode == MOVE_Flying ? EIMGTraversalMovementPhase::Warping
	    : Mode == MOVE_Falling                      ? EIMGTraversalMovementPhase::Falling
	                                                : EIMGTraversalMovementPhase::GroundExit;
	if (ReplaySession->bGroundReleased)
	{
		if (GroundHandoffIgnores.IsEmpty())
		{
			GroundHandoffIgnores = AddedMovementIgnores;
		}
		RestoreMovementIgnores();
		BoundMovement->bForceNextFloorCheck = true;
	}
#if IMG_TRAVERSAL_DEBUG_ENABLED
	if (IMGTraversalDebug::IsObserving(GetOwner()))
	{
		IMGTraversalDebug::Event(GetOwner(), TEXT("GroundHandoffCorrection"),
		    FString::Printf(TEXT("Move=%.6f Released=%d Mode=%u"), Move.TimeStamp, ReplaySession->bGroundReleased,
		        static_cast<uint8>(Mode)),
		    false, IMGTraversalDebug::EStage::Network, ActiveState.AbilitySpec.ToString(),
		    ActiveState.ActivationPredictionKey);
	}
#endif
}
