#include "Character/Traversal/IMGTraversalComponent.h"

#include "AbilitySystem/IMGAbilitySystemComponent.h"
#include "Animation/AnimMontage.h"
#include "Character/IMGCharacter.h"
#include "Character/IMGCharacterMovementComponent.h"
#include "Character/IMGHealthComponent.h"
#include "Character/IMGPawnExtensionComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "GameFramework/Controller.h"
#include "MotionWarpingComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "Net/UnrealNetwork.h"
#include "RootMotionModifier.h"
#include "Character/Traversal/IMGTraversalDetector.h"
#include "Character/Traversal/Execution/IMGTraversalWarpWindows.h"
#include "IMGLogChannels.h"

#include "Character/Traversal/IMGTraversalDebug.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGTraversalComponent)

FVector UIMGTraversalComponent::GetMovementIntent(EIMGTraversalRequestSource Source) const
{
	const AIMGCharacter* Character = Cast<AIMGCharacter>(GetOwner());
	if (!IsValid(Character)) { return FVector::ZeroVector; }
	FVector Intent = Character->GetPendingMovementInputVector().GetSafeNormal2D();
	if (Intent.IsNearlyZero())
	{
		const UIMGCharacterMovementComponent* Movement = Character->GetIMGMovementComponent();
		Intent = Movement ? Movement->GetCurrentAcceleration().GetSafeNormal2D() : FVector::ZeroVector;
	}
	if (Intent.IsNearlyZero() && Source == EIMGTraversalRequestSource::AI)
	{
		const AController* Controller = Character->GetController();
		const UPathFollowingComponent* PathFollowing =
		    IsValid(Controller) ? Controller->FindComponentByClass<UPathFollowingComponent>() : nullptr;
		// Direct-velocity path following need not populate CMC acceleration, even while pushing into a wall.
		if (IsValid(PathFollowing) && PathFollowing->GetStatus() == EPathFollowingStatus::Moving && PathFollowing->HasValidPath())
		{
			Intent = PathFollowing->GetCurrentTargetLocation() - Character->GetActorLocation();
		}
	}
	return Intent.ContainsNaN() ? FVector::ZeroVector : Intent.GetSafeNormal2D();
}

UIMGTraversalComponent::UIMGTraversalComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	SetIsReplicatedByDefault(true);
	bAutoActivate = true;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	for (const EIMGTraversalAction Action :
	    {EIMGTraversalAction::Mantle, EIMGTraversalAction::Vault, EIMGTraversalAction::Hurdle})
	{
		FIMGTraversalActionSettings& Entry = ActionSettings.AddDefaulted_GetRef();
		Entry.Action = Action;
		if (Action == EIMGTraversalAction::Mantle)
		{
			Entry.Settings.MinVerticalClearance = 180.f;
		}
		if (Action == EIMGTraversalAction::Hurdle)
		{
			Entry.Settings.Landing.Mode = EIMGTraversalLandingMode::DistanceCurve;
		}
	}
}

UIMGTraversalComponent* UIMGTraversalComponent::Get(const AActor* Actor)
{
	return IsValid(Actor) ? Actor->FindComponentByClass<UIMGTraversalComponent>() : nullptr;
}

bool UIMGTraversalComponent::Detect(FIMGTraversalCandidate& OutCandidate, EIMGTraversalFailure& OutFailure,
    FVector Direction, EIMGTraversalRequestSource Source) const
{
	AIMGCharacter* Character = Cast<AIMGCharacter>(GetOwner());
	FIMGTraversalQuery Query;
	Query.Character = Character;
	if (Direction.IsNearlyZero() && Character && Source == EIMGTraversalRequestSource::AI)
	{
		Direction = GetMovementIntent(Source);
	}
	Query.Direction = Direction.IsNearlyZero() && Character ? Character->GetActorForwardVector() : Direction;
	Query.Source = Source;
	Query.Settings = DetectorSettings;
	return FIMGTraversalDetector::Detect(Query, OutCandidate, OutFailure);
}

UMotionWarpingComponent* UIMGTraversalComponent::GetMotionWarpingComponent() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UMotionWarpingComponent>() : nullptr;
}

bool UIMGTraversalComponent::CanInitiate(
    const FIMGTraversalCandidate& Candidate, const FVector& MovementIntent, bool bRemoteRequest) const
{
	auto Reject = [this](const TCHAR* Reason) {
#if IMG_TRAVERSAL_DEBUG_ENABLED
		IMGTraversalDebug::Event(
			GetOwner(), TEXT("InitiationRejected"), Reason, false, IMGTraversalDebug::EStage::Validate);
#endif
		return false;
	};
	const AIMGCharacter* Character = Cast<AIMGCharacter>(GetOwner());
	const UIMGCharacterMovementComponent* Movement = Character ? Character->GetIMGMovementComponent() : nullptr;
	if (!Movement || !Candidate.bHasFrontLedge)
	{
		return Reject(TEXT("Invalid movement or missing front ledge"));
	}
	const FIMGTraversalInitiationSettings& Settings = InitiationSettings;
	if (!Settings.bEnabled)
	{
		return true;
	}
	const float Distances[] = {Settings.ManualMaxDistance, Settings.StandingMaxDistance,
	    Settings.AutomaticMaxDistance, Settings.ServerDistanceTolerance};
	for (float Value : Distances)
	{
		if (!FMath::IsFinite(Value) || Value < 0.f)
		{
			return Reject(TEXT("Non-finite or negative distance settings"));
		}
	}
	const FVector ToLedgeOffset = Candidate.FrontLedge.GetLocation() - Character->GetActorLocation();
	const double PlanarDistance = ToLedgeOffset.Size2D();
	if (!FMath::IsFinite(Settings.ManualMovementDirectionDot) ||
	    FMath::Abs(Settings.ManualMovementDirectionDot) > 1.f ||
	    !FMath::IsFinite(Settings.AutomaticMovementDirectionDot) ||
	    FMath::Abs(Settings.AutomaticMovementDirectionDot) > 1.f || !FMath::IsFinite(PlanarDistance))
	{
		return Reject(TEXT("Invalid direction-dot settings or distance"));
	}
	const FVector ToLedge = ToLedgeOffset.GetSafeNormal2D();
	// A player request carries the facing used for its search. AI can search along its path before turning.
	const FVector Facing = Candidate.RequestSource == EIMGTraversalRequestSource::AI
	    ? Character->GetActorForwardVector().GetSafeNormal2D() : Candidate.ApproachDirection;
	const float MinFacingDot = Candidate.RequestSource == EIMGTraversalRequestSource::Automatic
	    ? DetectorSettings.AutomaticMinFacingDot : DetectorSettings.MinFacingDot;
	if (!FMath::IsFinite(MinFacingDot) || FMath::Abs(MinFacingDot) > 1.f ||
	    FVector::DotProduct(Facing, ToLedge) < MinFacingDot)
	{
		return Reject(TEXT("Facing outside allowed angle"));
	}
	const FVector Intent = MovementIntent.GetSafeNormal2D();
	const bool bAutomatic = Candidate.RequestSource == EIMGTraversalRequestSource::Automatic;
	const float DistanceTolerance = bRemoteRequest ? Settings.ServerDistanceTolerance : 0.f;
	float AllowedDistance = 0.f;
	if (bAutomatic)
	{
		if ((!Settings.bAllowAutomaticInAir && !Movement->IsMovingOnGround()) || Intent.IsNearlyZero() ||
		    FVector::DotProduct(Intent, ToLedge) < Settings.AutomaticMovementDirectionDot)
		{
			return Reject(TEXT("Automatic movement mode or intent rejected"));
		}
		AllowedDistance = Settings.AutomaticMaxDistance;
	}
	else
	{
		const bool bMovingTowardLedge =
		    !Intent.IsNearlyZero() && FVector::DotProduct(Intent, ToLedge) >= Settings.ManualMovementDirectionDot;
		if (!Settings.bAllowStandingManual && !bMovingTowardLedge)
		{
			return Reject(TEXT("Standing manual initiation disabled"));
		}
		if (Settings.bAllowStandingManual)
		{
			AllowedDistance = Settings.StandingMaxDistance;
		}
		if (bMovingTowardLedge)
		{
			AllowedDistance = FMath::Max(AllowedDistance, Settings.ManualMaxDistance);
		}
	}
	const bool bAllowed = PlanarDistance <= AllowedDistance + DistanceTolerance;
#if IMG_TRAVERSAL_DEBUG_ENABLED
	if (!bAllowed)
	{
		if (IMGTraversalDebug::IsObserving(GetOwner()))
		{
			IMGTraversalDebug::Event(GetOwner(), TEXT("InitiationRejected"),
				FString::Printf(TEXT("PlanarDistance=%.2f Allowed=%.2f RemoteTolerance=%.2f"), PlanarDistance,
					AllowedDistance, DistanceTolerance),
				false, IMGTraversalDebug::EStage::Validate);
		}
	}
#endif
	return bAllowed;
}

const FIMGTraversalExecutionSettings* UIMGTraversalComponent::FindExecutionSettings(
    EIMGTraversalAction Action, const UAnimMontage* Montage) const
{
	if (!ReplicatedMontages.IsEmpty() && !IsListedReplicatedMontage(Montage)) { return nullptr; }
	const FIMGTraversalExecutionSettings* Result = nullptr;
	for (const FIMGTraversalMontageSettings& Entry : MontageSettings)
	{
		if (Entry.Montage == Montage && Entry.Action == Action)
		{
			if (Result)
			{
				return nullptr;
			}
			Result = &Entry.Settings;
		}
	}
	if (Result)
	{
		return Result;
	}
	for (const FIMGTraversalActionSettings& Entry : ActionSettings)
	{
		if (Entry.Action == Action)
		{
			if (Result)
			{
				return nullptr;
			}
			Result = &Entry.Settings;
		}
	}
	return Result;
}

const FIMGTraversalWarpTarget* UIMGTraversalComponent::FindWarpTarget(EIMGTraversalAnchor Anchor, FName TargetName) const
{
	if (!IsTraversalAvailable() || bReplayingTraversalMove || !ActiveState.bActive) { return nullptr; }
	if (!TargetName.IsNone())
	{
		return ActiveState.WarpTargets.FindByPredicate(
		    [TargetName](const FIMGTraversalWarpTarget& Target) { return Target.Name == TargetName; });
	}
	const FIMGTraversalExecutionSettings* Settings = FindExecutionSettings(ActiveState.Action, ActiveState.Montage);
	if (!Settings)
	{
		return nullptr;
	}
	const FIMGTraversalWarpTarget* Result = nullptr;
	for (const FIMGTraversalWarpTarget& Target : ActiveState.WarpTargets)
	{
		if (Settings->WarpBindings.ContainsByPredicate([&Target, Anchor](const FIMGTraversalWarpBinding& Binding) {
			    return Binding.Anchor == Anchor && Binding.TargetName == Target.Name;
		    }))
		{
			if (Result)
			{
				return nullptr;
			}
			Result = &Target;
		}
	}
	return Result;
}

void UIMGTraversalComponent::BeginPlay()
{
	Super::BeginPlay();
	InitializeBindings();
}

void UIMGTraversalComponent::OnRegister()
{
	Super::OnRegister();
	if (HasBegunPlay()) { InitializeBindings(); }
}

void UIMGTraversalComponent::OnUnregister()
{
	ReleaseBindings();
	Super::OnUnregister();
}

void UIMGTraversalComponent::Activate(bool bReset)
{
	Super::Activate(bReset);
	if (HasBegunPlay()) { InitializeBindings(); }
}

void UIMGTraversalComponent::Deactivate()
{
	ReleaseBindings();
	Super::Deactivate();
}

void UIMGTraversalComponent::OnRep_IsActive()
{
	Super::OnRep_IsActive();
	if (IsActive() && HasBegunPlay()) { InitializeBindings(); }
	else if (!IsActive()) { ReleaseBindings(); }
}

void UIMGTraversalComponent::InitializeBindings()
{
	if (!IsTraversalAvailable() || !GetOwner() || GetOwner()->IsActorBeingDestroyed()) { return; }
	if (UMotionWarpingComponent* Warping = GetMotionWarpingComponent())
	{
		if (BoundMotionWarpingComponent.IsValid() && BoundMotionWarpingComponent != Warping)
		{
			const TWeakObjectPtr<UMotionWarpingComponent> PreviousWarping = BoundMotionWarpingComponent;
			InvalidateTraversal();
			if (PreviousWarping.IsValid())
			{
				PreviousWarping->OnPreUpdate.RemoveDynamic(this, &ThisClass::HandleMotionWarpingPreUpdate);
			}
			if (!IsTraversalAvailable() || !IsValid(Warping) || GetMotionWarpingComponent() != Warping) { return; }
		}
		BoundMotionWarpingComponent = Warping;
		Warping->OnPreUpdate.AddUniqueDynamic(this, &ThisClass::HandleMotionWarpingPreUpdate);
	}
	if (const AIMGCharacter* Character = Cast<AIMGCharacter>(GetOwner()))
	{
		if (UIMGCharacterMovementComponent* Movement = Character->GetIMGMovementComponent())
		{
			if (BoundMovement != Movement)
			{
				if (BoundMovement.IsValid())
				{
					const TWeakObjectPtr<UIMGCharacterMovementComponent> PreviousMovement = BoundMovement;
					InvalidateTraversal();
					if (PreviousMovement.IsValid())
					{
						PreviousMovement->MovementReplayCapture.Remove(MovementReplayCaptureHandle);
						PreviousMovement->RemoveTickPrerequisiteComponent(this);
					}
					if (!IsTraversalAvailable() || !IsValid(Character) || !IsValid(Movement) ||
					    Character->GetIMGMovementComponent() != Movement) { return; }
				}
				MovementReplayCaptureHandle = Movement->MovementReplayCapture.AddUObject(this, &ThisClass::CaptureMovementReplay);
			}
			BoundMovement = Movement;
			// Proxy collision restoration must precede CMC floor queries, even without a root-motion update.
			Movement->AddTickPrerequisiteComponent(this);
		}
	}
	if (UIMGPawnExtensionComponent* Extension = UIMGPawnExtensionComponent::FindPawnExtensionComponent(GetOwner()))
	{
		if (BoundPawnExtension == Extension)
		{
			HandleAbilitySystemInitialized();
			return;
		}
		if (BoundPawnExtension.IsValid()) { BoundPawnExtension->UnregisterAbilitySystemDelegates(this); }
		BoundPawnExtension = Extension;
		Extension->OnAbilitySystemUninitialized_Register(
		    FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &ThisClass::HandleAbilitySystemUninitialized));
		Extension->OnAbilitySystemInitialized_RegisterAndCall(
		    FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &ThisClass::HandleAbilitySystemInitialized));
	}
}

void UIMGTraversalComponent::SetTargetTickPrerequisites(bool bAdd)
{
	if (!bAdd)
	{
		for (const TWeakObjectPtr<UPrimitiveComponent>& Component : TargetTickPrerequisites)
		{
			if (Component.IsValid())
			{
				RemoveTickPrerequisiteComponent(Component.Get());
			}
		}
		TargetTickPrerequisites.Reset();
		return;
	}
	// Read carrier transforms after their component ticks; this does not order actor ticks or physics.
	auto Add = [this](UPrimitiveComponent* Component)
	{
		if (IsValid(Component) && !TargetTickPrerequisites.Contains(Component))
		{
			AddTickPrerequisiteComponent(Component);
			TargetTickPrerequisites.Add(Component);
		}
	};
	for (UPrimitiveComponent* Component : ActiveState.ObstacleComponents)
	{
		Add(Component);
	}
	for (const FIMGTraversalWarpTarget& Target : ActiveState.WarpTargets)
	{
		Add(Target.Component);
	}
	for (auto It = TargetTickPrerequisites.CreateIterator(); It; ++It)
	{
		if (!It->IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

bool UIMGTraversalComponent::BeginTraversal(const FIMGTraversalCandidate& Candidate,
    const FIMGTraversalAnimationSelection& Selection, const TArray<FIMGTraversalWarpTarget>& WarpTargets,
    FGameplayAbilitySpecHandle AbilitySpec, int16 PredictionKey, uint32& OutExecutionId)
{
#if IMG_TRAVERSAL_DEBUG_ENABLED
	IMGTraversalDebug::FScope DebugScope(GetOwner(), IMGTraversalDebug::EPhase::Execution);
#endif
	OutExecutionId = 0;
	AIMGCharacter* Character = Cast<AIMGCharacter>(GetOwner());
	UIMGCharacterMovementComponent* Movement = BoundMovement.Get();
	if (!IsTraversalReady() || bReplayingTraversalMove || ActiveState.bActive || !IsValid(Character) || Character->IsActorBeingDestroyed() ||
	    Character->GetLocalRole() == ROLE_SimulatedProxy || !Movement || Movement != Character->GetIMGMovementComponent())
	{
		return false;
	}
	if (!Movement->IsRegistered() || !Movement->IsActive()) { return false; }
	if (++ExecutionId == 0) { ++ExecutionId; }
	OutExecutionId = ExecutionId;
	ActiveState = FIMGTraversalRepState();
	ActiveState.Sequence = ReplicatedState.Sequence + 1;
	ActiveState.AbilitySpec = AbilitySpec;
	ActiveState.ActivationPredictionKey = PredictionKey;
	ActiveState.Action = Selection.Action;
	ActiveState.Montage = Selection.Montage;
	ActiveState.MontageStartTime = Selection.StartTime;
	ActiveState.ObstacleComponents = Candidate.ObstacleComponents;
	ActiveState.FloorOnlyComponents = Candidate.FloorOnlyComponents;
	ActiveState.WarpTargets = WarpTargets;
	ActiveState.bActive = true;
	bLocallyPredictedTraversal = Character->GetLocalRole() == ROLE_AutonomousProxy;
	ReplaySessionValid = bLocallyPredictedTraversal ? MakeShared<bool>(true) : TSharedPtr<bool>();
	bAuthorityTargetsApplied = false;
	++TraversalTargetsRevision;
	if (const UMotionWarpingComponent* Warping = GetMotionWarpingComponent())
	{
		for (const FIMGTraversalWarpTarget& Target : WarpTargets)
		{
			if (const FMotionWarpingTarget* Existing = Warping->FindWarpTarget(Target.Name))
			{
				ActiveState.PreviousWarpTargets.Add(*Existing);
			}
		}
	}
	SetTargetTickPrerequisites(true);
	RefreshWarpTargets();
	SetComponentTickEnabled(true);
#if IMG_TRAVERSAL_DEBUG_ENABLED
	IMGTraversalDebug::State(GetOwner(), TEXT("ExecutionAcquired"), ActiveState,
	    *FString::Printf(TEXT("AddedIgnores=%d"), AddedMovementIgnores.Num()));
#endif
	Movement->SetMovementMode(MOVE_Flying);
	// Movement-mode callbacks may synchronously cancel the ability or release this component.
	return IsTraversalReady() && OwnsExecution(OutExecutionId) && IsValid(Character) &&
	    BoundMovement.Get() == Movement && IsValid(Movement) && Movement->IsFlying();
}

bool UIMGTraversalComponent::PublishTraversalState(uint32 ExpectedExecutionId, uint8 MontagePlayInstanceId)
{
#if IMG_TRAVERSAL_DEBUG_ENABLED
	IMGTraversalDebug::FScope DebugScope(GetOwner(), IMGTraversalDebug::EPhase::Replication);
#endif
	if (!IsTraversalReady() || !OwnsExecution(ExpectedExecutionId) ||
	    !GetOwner()->HasAuthority() || !IsValid(ActiveState.Montage))
	{
		return false;
	}
	ActiveState.MontagePlayInstanceId = MontagePlayInstanceId;
	ReplicatedState = ActiveState;
#if IMG_TRAVERSAL_DEBUG_ENABLED
	IMGTraversalDebug::State(GetOwner(), TEXT("SnapshotPublished"), ReplicatedState);
#endif
	GetOwner()->ForceNetUpdate();
	return true;
}

bool UIMGTraversalComponent::IsTraversalReady() const
{
	const AIMGCharacter* Character = Cast<AIMGCharacter>(GetOwner());
	const UIMGCharacterMovementComponent* Movement = BoundMovement.Get();
	const UMotionWarpingComponent* Warping = BoundMotionWarpingComponent.Get();
	return IsTraversalAvailable() && IsValid(Character) && !Character->IsActorBeingDestroyed() &&
	    Movement && Movement == Character->GetIMGMovementComponent() &&
	    Movement->IsRegistered() && Movement->IsActive() && Warping && Warping == GetMotionWarpingComponent() &&
	    Warping->IsRegistered() &&
	    (Character->GetLocalRole() != ROLE_SimulatedProxy || HasReplicatedMontageBinding());
}

void UIMGTraversalComponent::EndTraversal(uint32 ExpectedExecutionId)
{
	if (OwnsExecution(ExpectedExecutionId)) { ReleaseExecution(); }
}

void UIMGTraversalComponent::InvalidateTraversal()
{
	if (bInvalidatingTraversal) { return; }
	TGuardValue<bool> Invalidating(bInvalidatingTraversal, true);
	if (ReplaySessionValid) { *ReplaySessionValid = false; }
	OnTraversalUnavailable.Broadcast();
	// Unavailable or destroyed owners may no longer be able to run the GA's end path.
	ReleaseExecution();
}

void UIMGTraversalComponent::ReleaseExecution()
{
#if IMG_TRAVERSAL_DEBUG_ENABLED
	IMGTraversalDebug::FScope DebugScope(GetOwner(), IMGTraversalDebug::EPhase::Lifecycle);
#endif
	if (!ActiveState.bActive)
	{
		return;
	}
#if IMG_TRAVERSAL_DEBUG_ENABLED
	IMGTraversalDebug::State(GetOwner(), TEXT("ExecutionReleasing"), ActiveState,
	    *FString::Printf(TEXT("AddedIgnores=%d"), AddedMovementIgnores.Num()));
#endif
	const uint16 NextSequence = ReplicatedState.Sequence + 1;
	FIMGTraversalRepState EndState;
	EndState.Sequence = NextSequence;
	// A terminal snapshot must identify the instance even if its start snapshot was never received.
	EndState.Montage = ActiveState.Montage;
	EndState.MontagePlayInstanceId = ActiveState.MontagePlayInstanceId;
	EndState.AbilitySpec = ActiveState.AbilitySpec;
	EndState.ActivationPredictionKey = ActiveState.ActivationPredictionKey;
	SetTargetTickPrerequisites(false);
	RestoreMovementIgnores();
	RemoveWarpTargets();
	ActiveState = MoveTemp(EndState);
	++TraversalTargetsRevision;
	bCaptureTraversalExit |= GetOwner() && GetOwner()->GetLocalRole() == ROLE_AutonomousProxy && !bReplayingTraversalMove;
	bLocallyPredictedTraversal = false;
	bAuthorityTargetsApplied = false;
	SetComponentTickEnabled(false);
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		ReplicatedState = ActiveState;
		GetOwner()->ForceNetUpdate();
	}
	if (UIMGAbilitySystemComponent* ASC = BoundAbilitySystemComponent.Get())
	{
		ASC->RetryReplicatedMontage();
	}
	RestoreMovementMode();
#if IMG_TRAVERSAL_DEBUG_ENABLED
	IMGTraversalDebug::State(GetOwner(), TEXT("ExecutionReleased"), ActiveState);
#endif
}

void UIMGTraversalComponent::OnRep_TraversalState()
{
#if IMG_TRAVERSAL_DEBUG_ENABLED
	IMGTraversalDebug::FScope DebugScope(GetOwner(), IMGTraversalDebug::EPhase::Replication);
	IMGTraversalDebug::State(GetOwner(), TEXT("SnapshotReceived"), ReplicatedState);
#endif
	if (!HasBegunPlay() || !IsTraversalAvailable() ||
	    (GetOwner() && GetOwner()->GetLocalRole() == ROLE_SimulatedProxy && !HasReplicatedMontageBinding())) { return; }
	if (GetOwner() && GetOwner()->GetLocalRole() == ROLE_AutonomousProxy)
	{
		ReconcilePredictedTargets();
		return;
	}
	if (!ReplicatedState.bActive && ShouldDeferSimulatedEnd())
	{
#if IMG_TRAVERSAL_DEBUG_ENABLED
		IMGTraversalDebug::State(
			GetOwner(), TEXT("SimulatedEndDeferred"), ReplicatedState, TEXT("Native montage still playing"));
#endif
		if (UIMGAbilitySystemComponent* ASC = BoundAbilitySystemComponent.Get())
		{
			ASC->RetryReplicatedMontage();
		}
		return;
	}
	const bool bSameInstance = ActiveState.bActive && ReplicatedState.bActive &&
	    ActiveState.Sequence == ReplicatedState.Sequence && ActiveState.Montage == ReplicatedState.Montage &&
	    ActiveState.MontagePlayInstanceId == ReplicatedState.MontagePlayInstanceId;
	if (!bSameInstance)
	{
		SetTargetTickPrerequisites(false);
		RestoreMovementIgnores();
		RemoveWarpTargets();
	}
	ActiveState = ReplicatedState;
#if IMG_TRAVERSAL_DEBUG_ENABLED
	IMGTraversalDebug::State(GetOwner(), TEXT("SnapshotApplied"), ActiveState);
#endif
	if (ActiveState.bActive)
	{
		SetTargetTickPrerequisites(true);
		RefreshWarpTargets();
	}
	SetComponentTickEnabled(ActiveState.bActive);
	if (UIMGAbilitySystemComponent* ASC = BoundAbilitySystemComponent.Get())
	{
		ASC->RetryReplicatedMontage();
	}
}

void UIMGTraversalComponent::ReconcilePredictedTargets()
{
#if IMG_TRAVERSAL_DEBUG_ENABLED
	IMGTraversalDebug::FScope DebugScope(GetOwner(), IMGTraversalDebug::EPhase::Replication);
#endif
	const AIMGCharacter* Character = Cast<AIMGCharacter>(GetOwner());
	if (!bLocallyPredictedTraversal || !ActiveState.bActive || !ReplicatedState.bActive ||
	    !Character || Character->GetLocalRole() != ROLE_AutonomousProxy || !BoundMovement.IsValid() ||
	    BoundMovement.Get() != Character->GetIMGMovementComponent() || !BoundMovement->IsFlying() ||
	    ActiveState.AbilitySpec != ReplicatedState.AbilitySpec || ActiveState.Action != ReplicatedState.Action ||
	    ActiveState.ActivationPredictionKey == 0 ||
	    ActiveState.ActivationPredictionKey != ReplicatedState.ActivationPredictionKey ||
	    ActiveState.Montage != ReplicatedState.Montage || ActiveState.MontageStartTime != ReplicatedState.MontageStartTime ||
	    (bAuthorityTargetsApplied && AppliedAuthoritySequence == ReplicatedState.Sequence))
	{
		return;
	}
	// References may resolve after the snapshot. Keep predicting until the entire target set is ready.
	if (ReplicatedState.WarpTargets.Num() != ActiveState.WarpTargets.Num())
	{
#if IMG_TRAVERSAL_DEBUG_ENABLED
		IMGTraversalDebug::State(GetOwner(), TEXT("PredictedTargetsWaiting"), ReplicatedState, TEXT("Target count"));
#endif
		return;
	}
	for (const UPrimitiveComponent* Obstacle : ReplicatedState.ObstacleComponents)
	{
		if (!IsValid(Obstacle))
		{
#if IMG_TRAVERSAL_DEBUG_ENABLED
			IMGTraversalDebug::State(
				GetOwner(), TEXT("PredictedTargetsWaiting"), ReplicatedState, TEXT("Obstacle reference"));
#endif
			return;
		}
	}
	for (const FIMGTraversalWarpTarget& Target : ReplicatedState.WarpTargets)
	{
		if (!IsValid(Target.Component) || !Target.LocalTransform.IsValid() ||
		    !(Target.LocalTransform * Target.Component->GetComponentTransform()).IsValid() ||
		    !ActiveState.WarpTargets.ContainsByPredicate(
		        [&Target](const FIMGTraversalWarpTarget& Existing) { return Existing.Name == Target.Name; }))
		{
#if IMG_TRAVERSAL_DEBUG_ENABLED
			IMGTraversalDebug::State(GetOwner(), TEXT("PredictedTargetsWaiting"), ReplicatedState,
				TEXT("Target reference, transform or binding"));
#endif
			return;
		}
	}
	SetTargetTickPrerequisites(false);
	RestoreMovementIgnores();
	ActiveState.ObstacleComponents = ReplicatedState.ObstacleComponents;
	ActiveState.FloorOnlyComponents = ReplicatedState.FloorOnlyComponents;
	ActiveState.WarpTargets = ReplicatedState.WarpTargets;
	// The owner restores its own pre-traversal targets, independently of the authority's history.
	AppliedAuthoritySequence = ReplicatedState.Sequence;
	bAuthorityTargetsApplied = true;
	++TraversalTargetsRevision;
	RefreshWarpTargets();
	UE_LOG(LogIMGAbilitySystem, Verbose,
	    TEXT("Traversal owner targets reconciled: Avatar=[%s] Spec=%s PredKey=%d Sequence=%u Targets=%d"),
	    *GetNameSafe(Character), *ActiveState.AbilitySpec.ToString(), ActiveState.ActivationPredictionKey,
	    AppliedAuthoritySequence, ActiveState.WarpTargets.Num());
#if IMG_TRAVERSAL_DEBUG_ENABLED
	IMGTraversalDebug::State(GetOwner(), TEXT("PredictedTargetsReconciled"), ActiveState);
#endif
}

bool UIMGTraversalComponent::ShouldDeferSimulatedEnd() const
{
	const AIMGCharacter* Character = Cast<AIMGCharacter>(GetOwner());
	const UIMGAbilitySystemComponent* ASC = BoundAbilitySystemComponent.Get();
	const UAnimInstance* AnimInstance =
	    Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
	return Character && Character->GetLocalRole() == ROLE_SimulatedProxy && ActiveState.bActive && ASC &&
	    ASC->MatchesReplicatedMontageInstance(ActiveState.Montage, ActiveState.MontagePlayInstanceId) && AnimInstance &&
	    AnimInstance->GetInstanceForMontage(ActiveState.Montage) != nullptr;
}

void UIMGTraversalComponent::RefreshMovementIgnores()
{
	const AIMGCharacter* Character = Cast<AIMGCharacter>(GetOwner());
	UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	if (!Capsule)
	{
		return;
	}
	if (Character->GetLocalRole() == ROLE_SimulatedProxy)
	{
		const UIMGCharacterMovementComponent* Movement = Character->GetIMGMovementComponent();
		TEnumAsByte<EMovementMode> NetMode(MOVE_None);
		TEnumAsByte<EMovementMode> NetGroundMode(MOVE_None);
		uint8 NetCustomMode = 0;
		if (Movement)
		{
			Movement->UnpackNetworkMovementMode(
			    Character->GetReplicatedMovementMode(), NetMode, NetCustomMode, NetGroundMode);
		}
		// Authority may leave Flying before the local montage exits; floor queries must see the landing obstacle.
		if (NetMode != MOVE_Flying)
		{
			RestoreMovementIgnores();
			return;
		}
	}
	for (UPrimitiveComponent* Obstacle : ActiveState.ObstacleComponents)
	{
		if (IsValid(Obstacle) && !Capsule->GetMoveIgnoreComponents().Contains(Obstacle))
		{
			Capsule->IgnoreComponentWhenMoving(Obstacle, true);
			AddedMovementIgnores.AddUnique(Obstacle);
		}
	}
}

void UIMGTraversalComponent::RestoreMovementIgnores()
{
	const AIMGCharacter* Character = Cast<AIMGCharacter>(GetOwner());
	if (UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr)
	{
		for (const TWeakObjectPtr<UPrimitiveComponent>& Obstacle : AddedMovementIgnores)
		{
			if (Obstacle.IsValid())
			{
				Capsule->IgnoreComponentWhenMoving(Obstacle.Get(), false);
			}
		}
	}
	AddedMovementIgnores.Reset();
}

void UIMGTraversalComponent::RestoreMovementMode()
{
	const AIMGCharacter* Character = Cast<AIMGCharacter>(GetOwner());
	UIMGCharacterMovementComponent* Movement = BoundMovement.Get();
	if (!IsValid(Character) || Character->IsActorBeingDestroyed() || Character->GetLocalRole() == ROLE_SimulatedProxy ||
	    !Movement || Movement != Character->GetIMGMovementComponent() || !Movement->IsFlying() || !Movement->UpdatedComponent)
	{
		return;
	}
	const UIMGHealthComponent* Health = UIMGHealthComponent::FindHealthComponent(Character);
	if (Health && Health->IsDeadOrDying()) { return; }
	FFindFloorResult Floor;
	Movement->FindFloor(Movement->UpdatedComponent->GetComponentLocation(), Floor, false);
	if (BoundMovement.Get() == Movement && IsValid(Movement) && !ActiveState.bActive && Movement->IsFlying())
	{
		Movement->SetMovementMode(Floor.IsWalkableFloor() ? MOVE_Walking : MOVE_Falling);
	}
}

void UIMGTraversalComponent::HandleMotionWarpingPreUpdate(UMotionWarpingComponent* Warping)
{
	if (bReplayingTraversalMove) { return; }
	if (ActiveState.bActive && Warping == BoundMotionWarpingComponent.Get())
	{
		// Native target replication can arrive independently of the traversal snapshot.
		RefreshWarpTargets();
	}
	if (RecordWarpContext)
	{
		TFunction<void()> Record = MoveTemp(RecordWarpContext);
		Record();
	}
}

void UIMGTraversalComponent::HandleAbilitySystemInitialized()
{
	if (!IsTraversalAvailable() || !GetOwner() || GetOwner()->IsActorBeingDestroyed()) { return; }
	const UIMGPawnExtensionComponent* Extension = BoundPawnExtension.Get();
	UIMGAbilitySystemComponent* ASC = Extension ? Extension->GetIMGAbilitySystemComponent() : nullptr;
	if (ASC && BoundAbilitySystemComponent == ASC)
	{
		if (GetOwner()->GetLocalRole() == ROLE_SimulatedProxy) { OnRep_TraversalState(); }
		return;
	}
	// First attachment can follow an OnSpawn activation inside InitAbilityActorInfo.
	// Only replacing an existing binding invalidates that binding's execution.
	if (!BoundAbilitySystemComponent.IsExplicitlyNull()) { HandleAbilitySystemUninitialized(); }
	if (!IsValid(ASC) || !IsValid(Extension) || BoundPawnExtension.Get() != Extension ||
	    Extension->GetIMGAbilitySystemComponent() != ASC || !GetOwner() ||
	    ASC->GetAvatarActor() != GetOwner() || !IsTraversalAvailable()) { return; }
	BoundAbilitySystemComponent = ASC;
	if (GetOwner()->GetLocalRole() == ROLE_SimulatedProxy)
	{
		MontagePrerequisiteHandle = ASC->RegisterReplicatedMontagePrerequisite(GetOwner(), TEXT("Traversal"),
		    FIMGReplicatedMontagePrerequisite::CreateUObject(this, &ThisClass::CheckReplicatedMontagePrerequisite));
		if (!MontagePrerequisiteHandle.IsValid())
		{
			BoundAbilitySystemComponent.Reset();
			UE_LOG(LogIMGAbilitySystem, Error, TEXT("Traversal failed to register montage prerequisites for [%s]."), *GetNameSafe(GetOwner()));
			return;
		}
		OnRep_TraversalState();
	}
}

void UIMGTraversalComponent::HandleAbilitySystemUninitialized()
{
	InvalidateTraversal();
	if (UIMGAbilitySystemComponent* ASC = BoundAbilitySystemComponent.Get())
	{
		ASC->UnregisterReplicatedMontagePrerequisite(MontagePrerequisiteHandle);
	}
	MontagePrerequisiteHandle.Reset();
	BoundAbilitySystemComponent.Reset();
#if IMG_TRAVERSAL_DEBUG_ENABLED
	IMGTraversalDebug::Event(
		GetOwner(), TEXT("AbilitySystemDetached"), TEXT("Execution, montage prerequisite and predicted replay retired"));
	IMGTraversalDebug::ReleaseActor(GetOwner());
#endif
}

bool UIMGTraversalComponent::HasReplicatedMontageBinding() const
{
	const UIMGAbilitySystemComponent* ASC = BoundAbilitySystemComponent.Get();
	return IsValid(GetOwner()) && ASC && ASC->GetAvatarActor() == GetOwner() && MontagePrerequisiteHandle.IsValid();
}

bool UIMGTraversalComponent::IsListedReplicatedMontage(const UAnimMontage* Montage) const
{
	return Montage && (ReplicatedMontages.Contains(Montage) || MontageSettings.ContainsByPredicate(
	    [Montage](const FIMGTraversalMontageSettings& Entry) { return Entry.Montage == Montage; }));
}

FIMGReplicatedMontagePrerequisiteResult UIMGTraversalComponent::CheckReplicatedMontagePrerequisite(
    const UAnimMontage* Montage, uint8 MontagePlayInstanceId) const
{
#if IMG_TRAVERSAL_DEBUG_ENABLED
	IMGTraversalDebug::FScope DebugScope(GetOwner(), IMGTraversalDebug::EPhase::Replication);
#endif
	auto Result = [this, Montage, MontagePlayInstanceId](EIMGReplicatedMontageReadiness Readiness, float Start,
					  FName Reason) -> FIMGReplicatedMontagePrerequisiteResult {
#if IMG_TRAVERSAL_DEBUG_ENABLED
		if (IMGTraversalDebug::IsObserving(GetOwner()))
		{
			IMGTraversalDebug::Event(GetOwner(), TEXT("MontagePrerequisite"),
				FString::Printf(TEXT("Montage=%s Instance=%u Readiness=%d Reason=%s LogicalStart=%.3f"),
					*GetNameSafe(Montage), MontagePlayInstanceId, static_cast<int32>(Readiness), *Reason.ToString(),
					Start),
				false, IMGTraversalDebug::EStage::Network);
		}
#endif
		return {Readiness, Start, Reason};
	};
	if (!Montage) { return {}; }
	if (!ReplicatedState.bActive && ReplicatedState.Montage == Montage &&
	    ReplicatedState.MontagePlayInstanceId == MontagePlayInstanceId && !ShouldDeferSimulatedEnd())
	{
		return Result(EIMGReplicatedMontageReadiness::Discard, -1.f, TEXT("TraversalEnded"));
	}
	if (!ActiveState.bActive || ActiveState.Montage != Montage ||
	    ActiveState.MontagePlayInstanceId != MontagePlayInstanceId)
	{
		if (IsListedReplicatedMontage(Montage))
		{
			return Result(EIMGReplicatedMontageReadiness::Waiting, -1.f, TEXT("TraversalSnapshot"));
		}

		if (!ReplicatedMontages.IsEmpty()) { return {}; }
		TArray<FIMGTraversalWarpWindow> Windows;
		const UMotionWarpingComponent* Warping = GetMotionWarpingComponent();
		IMGTraversalWarpWindows::Collect(Montage, Warping && Warping->bSearchForWindowsInAnimsWithinMontages, Windows);
		for (const EIMGTraversalAction Action :
		    {EIMGTraversalAction::Mantle, EIMGTraversalAction::Vault, EIMGTraversalAction::Hurdle})
		{
			const FIMGTraversalExecutionSettings* Settings = FindExecutionSettings(Action, Montage);
			if (!Settings)
			{
				continue;
			}
			for (const FIMGTraversalWarpWindow& Window : Windows)
			{
				if (Window.Modifier && Settings->WarpBindings.ContainsByPredicate([&Window](const FIMGTraversalWarpBinding& Binding)
				        { return Binding.TargetName == Window.Modifier->WarpTargetName; }))
				{
					return Result(EIMGReplicatedMontageReadiness::Waiting, -1.f, TEXT("TraversalSnapshot"));
				}
			}
		}
		return {};
	}
	const UMotionWarpingComponent* Warping = GetMotionWarpingComponent();
	const AIMGCharacter* Character = Cast<AIMGCharacter>(GetOwner());
	const UIMGCharacterMovementComponent* Movement = Character ? Character->GetIMGMovementComponent() : nullptr;
	// Movement mode and GAS montage arrive through different actors; do not start with falling Z constraints.
	if (!Warping || !Character || !Character->GetCapsuleComponent() || !Movement ||
	    !Movement->IsRegistered() || !Movement->IsActive() || !Movement->IsFlying())
	{
		return Result(EIMGReplicatedMontageReadiness::Waiting, -1.f, TEXT("TraversalMovementMode"));
	}
	for (UPrimitiveComponent* Obstacle : ActiveState.ObstacleComponents)
	{
		if (!IsValid(Obstacle) || !Character->GetCapsuleComponent()->GetMoveIgnoreComponents().Contains(Obstacle))
		{
			return Result(EIMGReplicatedMontageReadiness::Waiting, -1.f, TEXT("TraversalCollisionIgnores"));
		}
	}
	for (const FIMGTraversalWarpTarget& Target : ActiveState.WarpTargets)
	{
		const FMotionWarpingTarget* Resolved = Warping->FindWarpTarget(Target.Name);
		if (!IsValid(Target.Component) || !Resolved || !Resolved->GetTargetTrasform().IsValid())
		{
			return Result(EIMGReplicatedMontageReadiness::Waiting, -1.f, TEXT("TraversalWarpTargets"));
		}
	}
	return Result(EIMGReplicatedMontageReadiness::Ready, ActiveState.MontageStartTime, NAME_None);
}

void UIMGTraversalComponent::RefreshWarpTargets()
{
#if IMG_TRAVERSAL_DEBUG_ENABLED
	IMGTraversalDebug::FScope DebugScope(GetOwner(), IMGTraversalDebug::EPhase::Execution);
#endif
	RefreshMovementIgnores();
#if IMG_TRAVERSAL_DEBUG_ENABLED
	IMGTraversalDebug::State(GetOwner(), TEXT("ExecutionState"), ActiveState);
#endif
	UMotionWarpingComponent* Warping = GetMotionWarpingComponent();
	if (!Warping)
	{
		return;
	}
	SetTargetTickPrerequisites(true);
#if IMG_TRAVERSAL_DEBUG_ENABLED
	if (IMGTraversalDebug::IsEnabled())
	{
		const ACharacter* DebugCharacter = Cast<ACharacter>(GetOwner());
		if (DebugCharacter && DebugCharacter->GetCapsuleComponent())
		{
			const UCapsuleComponent* Capsule = DebugCharacter->GetCapsuleComponent();
			IMGTraversalDebug::Capsule(GetWorld(), Capsule->GetComponentLocation(),
				Capsule->GetScaledCapsuleHalfHeight(), Capsule->GetScaledCapsuleRadius(), FColor::White,
				IMGTraversalDebug::ELifetime::Live);
		}
	}
#endif
	for (const FIMGTraversalWarpTarget& Target : ActiveState.WarpTargets)
	{
		if (!IsValid(Target.Component))
		{
			Warping->RemoveWarpTarget(Target.Name);
			continue;
		}
		const FTransform Transform = Target.LocalTransform * Target.Component->GetComponentTransform();
		if (!Transform.IsValid())
		{
			Warping->RemoveWarpTarget(Target.Name);
			continue;
		}
		const FMotionWarpingTarget Resolved(Target.Name, Transform);
		const FMotionWarpingTarget* Existing = Warping->FindWarpTarget(Target.Name);
		if (!Existing || *Existing != Resolved)
		{
			Warping->AddOrUpdateWarpTarget(Resolved);
		}
#if IMG_TRAVERSAL_DEBUG_ENABLED
		if (IMGTraversalDebug::IsEnabled())
		{
			IMGTraversalDebug::Point(
				GetWorld(), Transform.GetLocation(), FColor::Yellow, 3.f, IMGTraversalDebug::ELifetime::Live);
			IMGTraversalDebug::Arrow(GetWorld(), Transform.GetLocation(),
				Transform.GetLocation() + Transform.GetUnitAxis(EAxis::X) * 45.f, FColor::Yellow,
				IMGTraversalDebug::ELifetime::Live);
		}
#endif
	}
}

void UIMGTraversalComponent::RemoveWarpTargets()
{
	if (UMotionWarpingComponent* Warping = BoundMotionWarpingComponent.Get())
	{
		for (const FIMGTraversalWarpTarget& Target : ActiveState.WarpTargets)
		{
			Warping->RemoveWarpTarget(Target.Name);
		}
		for (const FMotionWarpingTarget& Target : ActiveState.PreviousWarpTargets)
		{
			Warping->AddOrUpdateWarpTarget(Target);
		}
	}
}

void UIMGTraversalComponent::TickComponent(
    float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (bReleasingBindings || !IsRegistered() || !IsActive()) { return; }
	const bool bSimulated = GetOwner() && GetOwner()->GetLocalRole() == ROLE_SimulatedProxy;
	// Proxy movement and traversal snapshots arrive independently; the montage prerequisite waits for movement.
	if (ActiveState.bActive && (bSimulated ? !HasReplicatedMontageBinding() : !IsTraversalReady()))
	{
		InvalidateTraversal();
		return;
	}
	if (bSimulated && ActiveState.bActive &&
	    !ReplicatedState.bActive && !ShouldDeferSimulatedEnd())
	{
		ReleaseExecution();
	}
	if (ActiveState.bActive)
	{
		ReconcilePredictedTargets();
		RefreshWarpTargets();
		// A replicated component reference may resolve after the snapshot's first OnRep.
		if (UIMGAbilitySystemComponent* ASC = BoundAbilitySystemComponent.Get())
		{
			ASC->RetryReplicatedMontage();
		}
	}
}

void UIMGTraversalComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseBindings();
	Super::EndPlay(EndPlayReason);
}

void UIMGTraversalComponent::ReleaseBindings()
{
#if IMG_TRAVERSAL_DEBUG_ENABLED
	IMGTraversalDebug::FScope DebugScope(GetOwner(), IMGTraversalDebug::EPhase::Lifecycle);
#endif
	if (bReleasingBindings) { return; }
	TGuardValue<bool> Releasing(bReleasingBindings, true);
	SetComponentTickEnabled(false);
	if (UMotionWarpingComponent* Warping = BoundMotionWarpingComponent.Get())
	{
		Warping->OnPreUpdate.RemoveDynamic(this, &ThisClass::HandleMotionWarpingPreUpdate);
	}
	if (UIMGPawnExtensionComponent* Extension = BoundPawnExtension.Get())
	{
		Extension->UnregisterAbilitySystemDelegates(this);
	}
	BoundPawnExtension.Reset();
	HandleAbilitySystemUninitialized();
	if (UIMGCharacterMovementComponent* Movement = BoundMovement.Get())
	{
		Movement->MovementReplayCapture.Remove(MovementReplayCaptureHandle);
		Movement->RemoveTickPrerequisiteComponent(this);
	}
	MovementReplayCaptureHandle.Reset();
	RecordWarpContext = nullptr;
	bCaptureTraversalExit = false;
	ReplaySessionValid.Reset();
	ReleaseExecution();
	BoundMovement.Reset();
	BoundMotionWarpingComponent.Reset();
#if IMG_TRAVERSAL_DEBUG_ENABLED
	IMGTraversalDebug::Event(
		GetOwner(), TEXT("BindingsReleased"), TEXT("Targets, collision ignores, replay and delegates released"));
	IMGTraversalDebug::ReleaseActor(GetOwner());
#endif
}

void UIMGTraversalComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UIMGTraversalComponent, ReplicatedState);
}
