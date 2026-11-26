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

void UIMGTraversalComponent::RetirePredictedReplay(FGameplayAbilitySpecHandle AbilitySpec, int16 PredictionKey)
{
	if (ReplaySessionValid && ActiveState.AbilitySpec == AbilitySpec && ActiveState.ActivationPredictionKey == PredictionKey)
	{
#if IMG_TRAVERSAL_DEBUG_ENABLED
		IMGTraversalDebug::FScope DebugScope(GetOwner(), IMGTraversalDebug::EPhase::Replay);
		IMGTraversalDebug::State(GetOwner(), TEXT("PredictedReplayRetired"), ActiveState);
#endif
		*ReplaySessionValid = false;
	}
}

void UIMGTraversalComponent::CaptureMovementReplay(const FSavedMove_Character& Move, TArray<TFunction<TFunction<void()>()>>& OutSetups)
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
	auto ReplayExit = [WeakThis, WeakMovement](uint8 PackedMode)
	{
		const ThisClass* Current = WeakThis.Get();
		UIMGCharacterMovementComponent* Movement = WeakMovement.Get();
		if (!Current || !Current->IsTraversalReady() || Current->BoundMovement != WeakMovement ||
		    !Movement || !Movement->IsFlying()) { return; }
		const UIMGHealthComponent* Health = UIMGHealthComponent::FindHealthComponent(Current->GetOwner());
		if (Health && Health->IsDeadOrDying()) { return; }
		TEnumAsByte<EMovementMode> Mode(MOVE_None), GroundMode(MOVE_None);
		uint8 CustomMode = 0;
		Movement->UnpackNetworkMovementMode(PackedMode, Mode, CustomMode, GroundMode);
		if (Mode == MOVE_Walking || Mode == MOVE_NavWalking || Mode == MOVE_Falling)
		{
			FFindFloorResult Floor;
			if (!Movement->UpdatedComponent) { return; }
			Movement->FindFloor(Movement->UpdatedComponent->GetComponentLocation(), Floor, false);
			if (Movement->IsFlying()) { Movement->SetMovementMode(Floor.IsWalkableFloor() ? MOVE_Walking : MOVE_Falling); }
		}
	};
	if (!ActiveState.bActive)
	{
		if (bCaptureTraversalExit)
		{
			const uint8 ExitMode = Move.StartPackedMovementMode;
			OutSetups.Add([ReplayExit, ExitMode]() -> FMovementReplayRestore { ReplayExit(ExitMode); return {}; });
			bCaptureTraversalExit = false;
		}
		return;
	}
	bCaptureTraversalExit = false;
	ReplayMoveExecutions.Add(&Move, ExecutionId);
#if IMG_TRAVERSAL_DEBUG_ENABLED
	const FGameplayAbilitySpecHandle DebugSpec = ActiveState.AbilitySpec;
	const int16 DebugPrediction = ActiveState.ActivationPredictionKey;
	const uint16 DebugSequence = ActiveState.Sequence;
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
	const TSharedPtr<bool> SessionValid = ReplaySessionValid;
	const TSharedPtr<bool> GroundReleased = ReplayGroundReleased;
	const float HandoffTime = ActiveState.GroundHandoffTime;
	const float MaxGroundAdjustment = ActiveState.MaxGroundAdjustment;
	const FIMGTraversalPlaybackPath Playback = GroundPlayback;
	const uint32 HistoricalExecutionId = ExecutionId;
	const TSharedPtr<bool> RecordedHandoff = MakeShared<bool>(false);
	RecordGroundHandoff = [RecordedHandoff]() { *RecordedHandoff = true; };
	TEnumAsByte<EMovementMode> EntryMode(MOVE_None), EntryGroundMode(MOVE_None);
	uint8 EntryCustomMode = 0;
	if (BoundMovement.IsValid())
	{
		BoundMovement->UnpackNetworkMovementMode(Move.StartPackedMovementMode, EntryMode, EntryCustomMode, EntryGroundMode);
	}
	const bool bEnteredFlying = EntryMode == MOVE_Flying;
	const TWeakObjectPtr<UAnimMontage> HistoricalMontage = ActiveState.Montage;
	const TWeakObjectPtr<UMotionWarpingComponent> WeakWarping = BoundMotionWarpingComponent;
	const TWeakObjectPtr<UCapsuleComponent> WeakCapsule(Character->GetCapsuleComponent());
	const TArray<TWeakObjectPtr<UPrimitiveComponent>> HistoricalIgnores =
	    ActiveState.MovementPhase == EIMGTraversalMovementPhase::Warping ? AddedMovementIgnores : GroundHandoffIgnores;
	const TArray<FIMGTraversalWarpTarget> Targets = ActiveState.WarpTargets;
	const auto Modifiers = MakeShared<TArray<TStrongObjectPtr<URootMotionModifier>>>();
	const auto States = MakeShared<TArray<TSharedPtr<FRootMotionModifierSwapState>>>();
	const auto Animations = MakeShared<TArray<TWeakObjectPtr<const UAnimSequenceBase>>>();
	const auto ResolvedTargets = MakeShared<TArray<FMotionWarpingTarget>>();
	const auto ReplayModifiers = MakeShared<TArray<TStrongObjectPtr<URootMotionModifier>>>();
	// Native OnPreUpdate runs after discovering windows, before changing modifier state.
	RecordWarpContext = [WeakWarping, Targets, Modifiers, States, Animations, ResolvedTargets]()
	{
		UMotionWarpingComponent* Warping = WeakWarping.Get();
		if (!Warping) { return; }
		Modifiers->Reset();
		States->Reset();
		Animations->Reset();
		ResolvedTargets->Reset();
		for (URootMotionModifier* Modifier : Warping->GetModifiers())
		{
			if (Modifier->GetState() == ERootMotionModifierState::MarkedForRemoval) { continue; }
			Modifiers->Emplace(Modifier);
			States->Add(Modifier->GetSwapState());
			Animations->Add(Modifier->Animation);
		}
		for (const FIMGTraversalWarpTarget& Target : Targets)
		{
			if (const FMotionWarpingTarget* Resolved = Warping->FindWarpTarget(Target.Name))
			{
				ResolvedTargets->Emplace(Target.Name, Resolved->GetTargetTrasform());
			}
		}
	};
	OutSetups.Add([WeakThis, WeakWarping, WeakMovement, WeakCapsule, HistoricalIgnores, Modifiers, States, Animations,
					  ResolvedTargets, ReplayExit, RecordedMove = &Move, SessionValid, HistoricalMontage,
					  bEnteredFlying, ReplayModifiers, GroundReleased, HandoffTime, MaxGroundAdjustment, Playback, HistoricalExecutionId, RecordedHandoff
#if IMG_TRAVERSAL_DEBUG_ENABLED
					  ,
					  DebugSpec, DebugPrediction, DebugSequence
#endif
	]() -> FMovementReplayRestore {
		UIMGCharacterMovementComponent* Movement = WeakMovement.Get();
		if (!Movement)
		{
			return {};
		}
#if IMG_TRAVERSAL_DEBUG_ENABLED
		IMGTraversalDebug::FScope DebugScope(Movement->GetOwner(), IMGTraversalDebug::EPhase::Replay);
		const FString DebugIdentity = IMGTraversalDebug::IsObserving(Movement->GetOwner())
			? FString::Printf(TEXT("HistoricalSpec=%s Pred=%d Seq=%u Montage=%s Move=%.6f"), *DebugSpec.ToString(),
				  DebugPrediction, DebugSequence, *GetNameSafe(HistoricalMontage.Get()), RecordedMove->TimeStamp)
			: FString();
#endif
		auto DiscardHistoricalMotion = [Movement, RecordedMove, HistoricalMontage]()
		{
			if (RecordedMove->RootMotionMontage == HistoricalMontage) { Movement->RootMotionParams.Clear(); }
		};
		ThisClass* Self = WeakThis.Get();
		UMotionWarpingComponent* Warping = WeakWarping.Get();
		UCapsuleComponent* Capsule = WeakCapsule.Get();
		if (!Self || !Self->IsTraversalReady() || !Warping || !Capsule ||
		    Self->BoundMovement != WeakMovement || Self->BoundMotionWarpingComponent != WeakWarping || Self->bReplayingTraversalMove)
		{
#if IMG_TRAVERSAL_DEBUG_ENABLED
			IMGTraversalDebug::Event(Movement->GetOwner(), TEXT("ReplayDiscarded"),
				DebugIdentity + TEXT(" | Bindings unavailable or replay reentered"), false,
				IMGTraversalDebug::EStage::Replay);
#endif
			DiscardHistoricalMotion();
			return {};
		}
		if (!Movement->IsRegistered() || !Movement->IsActive())
		{
#if IMG_TRAVERSAL_DEBUG_ENABLED
			IMGTraversalDebug::Event(Movement->GetOwner(), TEXT("ReplayDiscarded"),
				DebugIdentity + TEXT(" | Movement inactive"), false, IMGTraversalDebug::EStage::Replay);
#endif
			DiscardHistoricalMotion();
			return {};
		}
		const UIMGHealthComponent* Health = UIMGHealthComponent::FindHealthComponent(Self->GetOwner());
		if (!SessionValid || !*SessionValid ||
		    (Health && Health->IsDeadOrDying()) ||
		    !(Movement->IsFlying() || Movement->IsMovingOnGround() || Movement->IsFalling()))
		{
			// A terminal authority result must not resurrect the predicted montage through old moves.
#if IMG_TRAVERSAL_DEBUG_ENABLED
			IMGTraversalDebug::Event(Movement->GetOwner(), TEXT("ReplayDiscarded"),
				DebugIdentity + TEXT(" | Session retired, death or movement takeover"), false,
				IMGTraversalDebug::EStage::Replay);
#endif
			DiscardHistoricalMotion();
			return {};
		}
		Self->bReplayingTraversalMove = true;
#if IMG_TRAVERSAL_DEBUG_ENABLED
		IMGTraversalDebug::Event(
			Self->GetOwner(), TEXT("ReplayBegin"), DebugIdentity, false, IMGTraversalDebug::EStage::Replay);
#endif
		if (bEnteredFlying && (HandoffTime < 0.f || !GroundReleased || !*GroundReleased) &&
		    (Movement->IsMovingOnGround() || Movement->IsFalling()))
		{
			Movement->SetMovementMode(MOVE_Flying);
		}
		if (!Self->IsTraversalReady() || Self->BoundMovement != WeakMovement || Self->BoundMotionWarpingComponent != WeakWarping)
		{
			Self->bReplayingTraversalMove = false;
			DiscardHistoricalMotion();
			return {};
		}
		const uint32 Revision = Self->TraversalTargetsRevision;
		const FIMGTraversalRepState LiveState = Self->ActiveState;
		TArray<FMotionWarpingTarget> PreviousTargets;
		for (const FMotionWarpingTarget& Target : *ResolvedTargets)
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
		for (int32 Index = 0; Index < Modifiers->Num(); ++Index)
		{
			if (!ReplayModifiers->IsValidIndex(Index))
			{
				ReplayModifiers->Emplace(DuplicateObject<URootMotionModifier>((*Modifiers)[Index].Get(), Warping));
			}
			URootMotionModifier* ReplayModifier = (*ReplayModifiers)[Index].Get();
			ReplayModifier->RestoreFromSwapState((*States)[Index]);
			ReplayModifier->Animation = (*Animations)[Index];
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
		for (const TWeakObjectPtr<UPrimitiveComponent>& Obstacle : HistoricalIgnores)
		{
			if (HandoffTime >= 0.f && GroundReleased && *GroundReleased) { break; }
			if (Obstacle.IsValid() && !Capsule->GetMoveIgnoreComponents().Contains(Obstacle.Get()))
			{
				Capsule->IgnoreComponentWhenMoving(Obstacle.Get(), true);
				TemporaryIgnores.Add(Obstacle);
			}
		}
		Self->ReplayPostMovement = [WeakThis, SessionValid, WeakMovement, WeakCapsule, HistoricalIgnores, GroundReleased,
		    HandoffTime, MaxGroundAdjustment, Playback, RecordedMove, RecordedHandoff]()
		{
			UIMGCharacterMovementComponent* ReplayedMovement = WeakMovement.Get();
			UCapsuleComponent* ReplayedCapsule = WeakCapsule.Get();
			float Position = -1.f;
			if (!ReplayedMovement || !ReplayedCapsule || !GroundReleased || *GroundReleased || HandoffTime < 0.f ||
			    !ReplayedMovement->IsFlying()) { return; }
			const bool bCompletedDuringMove = !RecordedMove->RootMotionMontage.IsValid() && *RecordedHandoff;
			if (!bCompletedDuringMove &&
			    (!Playback.GetPlaybackPosition(RecordedMove->RootMotionTrackPosition, Position) || Position < HandoffTime))
			{
				return;
			}
			for (const TWeakObjectPtr<UPrimitiveComponent>& Obstacle : HistoricalIgnores)
			{
				if (Obstacle.IsValid()) { ReplayedCapsule->IgnoreComponentWhenMoving(Obstacle.Get(), false); }
			}
			*GroundReleased = true;
			const bool bGrounded = IMGGroundPlacement::FindGround(*ReplayedMovement, MaxGroundAdjustment);
			if (WeakThis.IsValid() && SessionValid && *SessionValid && ReplayedMovement->IsFlying())
			{
				ReplayedMovement->SetMovementMode(bGrounded ? MOVE_Walking : MOVE_Falling);
#if IMG_TRAVERSAL_DEBUG_ENABLED
				IMGTraversalDebug::Event(ReplayedMovement->GetOwner(), TEXT("ReplayGroundHandoff"),
				    FString::Printf(TEXT("Move=%.6f Playback=%.6f Boundary=%.6f Grounded=%d"),
				        RecordedMove->TimeStamp, Position, HandoffTime, bGrounded), !bGrounded,
				    IMGTraversalDebug::EStage::Replay);
#endif
			}
		};
		return [WeakThis, WeakWarping, WeakCapsule, WeakMovement, Revision, LiveState, ResolvedTargets,
				   GroundReleased, HandoffTime, HistoricalExecutionId,
				   PreviousTargets = MoveTemp(PreviousTargets), LiveModifiers = MoveTemp(LiveModifiers),
				   LiveStates = MoveTemp(LiveStates), LiveAnimations = MoveTemp(LiveAnimations),
				   TemporaryIgnores = MoveTemp(TemporaryIgnores), ReplayExit, RecordedMove
#if IMG_TRAVERSAL_DEBUG_ENABLED
				   ,
				   DebugIdentity
#endif
		]() {
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
				for (const FMotionWarpingTarget& Target : *ResolvedTargets)
				{
					const bool bSessionEnded = Current && Revision != Current->TraversalTargetsRevision &&
					    LiveState.WarpTargets.ContainsByPredicate([&Target](const FIMGTraversalWarpTarget& Live) { return Live.Name == Target.Name; });
					const FMotionWarpingTarget* Previous = (bSessionEnded ? LiveState.PreviousWarpTargets : PreviousTargets)
					    .FindByPredicate([&Target](const FMotionWarpingTarget& Live) { return Live.Name == Target.Name; });
					CurrentWarping->RemoveWarpTarget(Target.Name);
					if (Previous)
					{
						CurrentWarping->AddOrUpdateWarpTarget(*Previous);
					}
				}
			}
			if (Current && Current->ActiveState.bActive && Current->ExecutionId == HistoricalExecutionId &&
			    HandoffTime >= 0.f && GroundReleased)
			{
				if (UIMGCharacterMovementComponent* ReplayedMovement = WeakMovement.Get())
				{
					Current->ActiveState.MovementPhase = !*GroundReleased ? EIMGTraversalMovementPhase::Warping
					    : ReplayedMovement->IsMovingOnGround() ? EIMGTraversalMovementPhase::GroundExit
					    : EIMGTraversalMovementPhase::Falling;
					if (*GroundReleased)
					{
						if (Current->GroundHandoffIgnores.IsEmpty()) { Current->GroundHandoffIgnores = Current->AddedMovementIgnores; }
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
			if (HandoffTime < 0.f) { ReplayExit(RecordedMove->EndPackedMovementMode); }
#if IMG_TRAVERSAL_DEBUG_ENABLED
			IMGTraversalDebug::Event(Current ? Current->GetOwner() : nullptr, TEXT("ReplayRestored"), DebugIdentity,
				false, IMGTraversalDebug::EStage::Replay);
#endif
		};
	});
}

void UIMGTraversalComponent::HandleClientCorrectionApplied(const FCharacterMoveResponseDataContainer& Response,
    const FSavedMove_Character& Move)
{
	const uint32* RecordedExecution = ReplayMoveExecutions.Find(&Move);
	if (!RecordedExecution || *RecordedExecution != ExecutionId || !ReplayGroundReleased ||
	    ActiveState.GroundHandoffTime < 0.f ||
	    (Move.RootMotionMontage.IsValid() && Move.RootMotionMontage != ActiveState.Montage.Get()) ||
	    !ReplaySessionValid || !*ReplaySessionValid || !BoundMovement.IsValid()) { return; }
	TEnumAsByte<EMovementMode> Mode(MOVE_None), GroundMode(MOVE_None);
	uint8 CustomMode = 0;
	BoundMovement->UnpackNetworkMovementMode(Response.ClientAdjustment.MovementMode, Mode, CustomMode, GroundMode);
	if (Mode != MOVE_Flying && Mode != MOVE_Walking && Mode != MOVE_NavWalking && Mode != MOVE_Falling)
	{
		*ReplaySessionValid = false;
		return;
	}
	// This is the authority's phase at the acknowledged move, not the latest OnRep snapshot.
	*ReplayGroundReleased = Mode != MOVE_Flying;
	ActiveState.MovementPhase = Mode == MOVE_Flying ? EIMGTraversalMovementPhase::Warping
	    : Mode == MOVE_Falling ? EIMGTraversalMovementPhase::Falling : EIMGTraversalMovementPhase::GroundExit;
	if (*ReplayGroundReleased)
	{
		if (GroundHandoffIgnores.IsEmpty()) { GroundHandoffIgnores = AddedMovementIgnores; }
		RestoreMovementIgnores();
		BoundMovement->bForceNextFloorCheck = true;
	}
#if IMG_TRAVERSAL_DEBUG_ENABLED
	IMGTraversalDebug::Event(GetOwner(), TEXT("GroundHandoffCorrection"),
	    FString::Printf(TEXT("Move=%.6f Released=%d Mode=%u"), Move.TimeStamp, *ReplayGroundReleased, static_cast<uint8>(Mode)), false, IMGTraversalDebug::EStage::Network,
	    ActiveState.AbilitySpec.ToString(), ActiveState.ActivationPredictionKey);
#endif
}
