#include "Character/Traversal/IMGTraversalComponent.h"

#include "AbilitySystem/IMGAbilitySystemComponent.h"
#include "Animation/AnimMontage.h"
#include "AnimNotifyState_MotionWarping.h"
#include "Character/IMGCharacter.h"
#include "Character/IMGCharacterMovementComponent.h"
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

#if ENABLE_DRAW_DEBUG
#include "IMGTraversalDebug.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGTraversalComponent)

namespace
{
FVector GetTraversalMovementIntent(const AIMGCharacter& Character, EIMGTraversalRequestSource Source)
{
	FVector Intent = Character.GetPendingMovementInputVector().GetSafeNormal2D();
	if (Intent.IsNearlyZero())
	{
		const UIMGCharacterMovementComponent* Movement = Character.GetIMGMovementComponent();
		Intent = Movement ? Movement->GetCurrentAcceleration().GetSafeNormal2D() : FVector::ZeroVector;
	}
	if (Intent.IsNearlyZero() && Source == EIMGTraversalRequestSource::AI)
	{
		const AController* Controller = Character.GetController();
		const UPathFollowingComponent* PathFollowing =
		    IsValid(Controller) ? Controller->FindComponentByClass<UPathFollowingComponent>() : nullptr;
		// Direct-velocity path following need not populate CMC acceleration, even while pushing into a wall.
		if (IsValid(PathFollowing) && PathFollowing->GetStatus() == EPathFollowingStatus::Moving && PathFollowing->HasValidPath())
		{
			Intent = PathFollowing->GetCurrentTargetLocation() - Character.GetActorLocation();
		}
	}
	return Intent.ContainsNaN() ? FVector::ZeroVector : Intent.GetSafeNormal2D();
}
} // namespace

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
		Direction = GetTraversalMovementIntent(*Character, Source);
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

bool UIMGTraversalComponent::CanInitiate(const FIMGTraversalCandidate& Candidate, bool bRemoteRequest) const
{
	const AIMGCharacter* Character = Cast<AIMGCharacter>(GetOwner());
	const UIMGCharacterMovementComponent* Movement = Character ? Character->GetIMGMovementComponent() : nullptr;
	if (!Movement || !Candidate.bHasFrontLedge)
	{
		return false;
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
			return false;
		}
	}
	if (!FMath::IsFinite(Settings.ManualMovementDirectionDot) ||
	    FMath::Abs(Settings.ManualMovementDirectionDot) > 1.f ||
	    !FMath::IsFinite(Settings.AutomaticMovementDirectionDot) ||
	    FMath::Abs(Settings.AutomaticMovementDirectionDot) > 1.f || !FMath::IsFinite(Candidate.PlanarDistanceToLedge) ||
	    Candidate.PlanarDistanceToLedge < 0.f)
	{
		return false;
	}
	const FVector ToLedge = (Candidate.FrontLedge.GetLocation() - Character->GetActorLocation()).GetSafeNormal2D();
	const float MinFacingDot = Candidate.RequestSource == EIMGTraversalRequestSource::Automatic
	    ? DetectorSettings.AutomaticMinFacingDot : DetectorSettings.MinFacingDot;
	if (!FMath::IsFinite(MinFacingDot) || FMath::Abs(MinFacingDot) > 1.f ||
	    FVector::DotProduct(Character->GetActorForwardVector().GetSafeNormal2D(), ToLedge) < MinFacingDot)
	{
		return false;
	}
	const FVector Intent = GetTraversalMovementIntent(*Character, Candidate.RequestSource);
	const bool bAutomatic = Candidate.RequestSource == EIMGTraversalRequestSource::Automatic;
	const float DistanceTolerance = bRemoteRequest ? Settings.ServerDistanceTolerance : 0.f;
	float AllowedDistance = 0.f;
	if (bAutomatic)
	{
		if ((!Settings.bAllowAutomaticInAir && !Movement->IsMovingOnGround()) || Intent.IsNearlyZero() ||
		    FVector::DotProduct(Intent, ToLedge) < Settings.AutomaticMovementDirectionDot)
		{
			return false;
		}
		AllowedDistance = Settings.AutomaticMaxDistance;
	}
	else
	{
		const bool bMovingTowardLedge =
		    !Intent.IsNearlyZero() && FVector::DotProduct(Intent, ToLedge) >= Settings.ManualMovementDirectionDot;
		if (!Settings.bAllowStandingManual && !bMovingTowardLedge)
		{
			return false;
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
	const bool bAllowed = Candidate.PlanarDistanceToLedge <= AllowedDistance + DistanceTolerance;
#if ENABLE_DRAW_DEBUG
	if (!bAllowed)
	{
		IMGTraversalDebug::String(GetWorld(), Candidate.FrontLedge.GetLocation() + FVector::UpVector * 45.f,
		    FString::Printf(TEXT("Initiation: planar %.0f > %.0f"), Candidate.PlanarDistanceToLedge,
		        AllowedDistance + DistanceTolerance),
		    FColor::Yellow, 2);
	}
#endif
	return bAllowed;
}

const FIMGTraversalAnimationSettings* UIMGTraversalComponent::FindAnimationSettings(
    EIMGTraversalAction Action, const UAnimMontage* Montage) const
{
	const FIMGTraversalAnimationSettings* Result = nullptr;
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

void UIMGTraversalComponent::GetWarpWindows(
    const UAnimMontage* Montage, TArray<FIMGTraversalWarpWindow>& OutWindows) const
{
	OutWindows.Reset();
	if (!Montage)
	{
		return;
	}
	TArray<FVector2D> ModifierRanges;
	auto Collect = [&OutWindows, &ModifierRanges](
	                   const UAnimSequenceBase* Animation, float Offset, float RangeStart, float RangeEnd)
	{
		if (!Animation)
		{
			return;
		}
		for (const FAnimNotifyEvent& Event : Animation->Notifies)
		{
			const UAnimNotifyState_MotionWarping* Notify = Cast<UAnimNotifyState_MotionWarping>(Event.NotifyStateClass);
			if (!Notify || !Notify->RootMotionModifier)
			{
				continue;
			}
			const float Start = FMath::Clamp(Event.GetTriggerTime(), 0.f, Animation->GetPlayLength()) + Offset;
			const float End = FMath::Clamp(Event.GetEndTriggerTime(), 0.f, Animation->GetPlayLength()) + Offset;
			if (End <= RangeStart || Start >= RangeEnd)
			{
				continue;
			}
			// UE deduplicates modifiers by animation and time range, irrespective of target name.
			const FVector2D Range(Start, End);
			if (ModifierRanges.Contains(Range))
			{
				continue;
			}
			ModifierRanges.Add(Range);
			const URootMotionModifier_Warp* Modifier = Cast<URootMotionModifier_Warp>(Notify->RootMotionModifier);
			if (!Modifier)
			{
				continue;
			}
			FIMGTraversalWarpWindow& Window = OutWindows.AddDefaulted_GetRef();
			Window.Modifier = Modifier;
			Window.StartTime = Start;
			Window.EndTime = End;
		}
	};
	Collect(Montage, 0.f, 0.f, Montage->GetPlayLength());
	if (const UMotionWarpingComponent* Warping = GetMotionWarpingComponent();
	    Warping && Warping->bSearchForWindowsInAnimsWithinMontages)
	{
		for (const FSlotAnimationTrack& Slot : Montage->SlotAnimTracks)
		{
			for (const FAnimSegment& Segment : Slot.AnimTrack.AnimSegments)
			{
				// Match the runtime MotionWarping component's segment-time conversion.
				Collect(Segment.GetAnimReference(), Segment.StartPos - Segment.AnimStartTime, Segment.StartPos,
				    Segment.GetEndPos());
			}
		}
	}
	OutWindows.StableSort(
	    [](const FIMGTraversalWarpWindow& A, const FIMGTraversalWarpWindow& B) { return A.StartTime < B.StartTime; });
}

const FIMGTraversalWarpWindow* UIMGTraversalComponent::FindTranslationWindow(
    TConstArrayView<FIMGTraversalWarpWindow> Windows, FName TargetName, int32 WindowIndex, float MinEndTime,
    float MaxEndTime)
{
	if (TargetName.IsNone() || WindowIndex < -1)
	{
		return nullptr;
	}
	const FIMGTraversalWarpWindow* Result = nullptr;
	int32 Ordinal = 0;
	for (const FIMGTraversalWarpWindow& Window : Windows)
	{
		if (Window.Modifier && Window.Modifier->WarpTargetName == TargetName && Window.Modifier->bWarpTranslation &&
		    Window.EndTime > Window.StartTime)
		{
			const bool bEligible = Window.EndTime > MinEndTime && Window.EndTime <= MaxEndTime;
			// An explicit authored ordinal must not shift when PoseSearch skips an earlier window.
			if (WindowIndex == Ordinal)
			{
				return bEligible ? &Window : nullptr;
			}
			if (bEligible)
			{
				Result = &Window;
			}
			++Ordinal;
		}
	}
	return WindowIndex == -1 ? Result : nullptr;
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
	if (bReleasingBindings || !IsRegistered() || !IsActive() || IsBeingDestroyed() || !GetOwner() || GetOwner()->IsActorBeingDestroyed()) { return; }
	if (UMotionWarpingComponent* Warping = GetMotionWarpingComponent())
	{
		if (BoundMotionWarpingComponent.IsValid() && BoundMotionWarpingComponent != Warping)
		{
			BoundMotionWarpingComponent->OnPreUpdate.RemoveDynamic(this, &ThisClass::HandleMotionWarpingPreUpdate);
		}
		BoundMotionWarpingComponent = Warping;
		Warping->OnPreUpdate.AddUniqueDynamic(this, &ThisClass::HandleMotionWarpingPreUpdate);
	}
	if (const AIMGCharacter* Character = Cast<AIMGCharacter>(GetOwner()))
	{
		if (UIMGCharacterMovementComponent* Movement = Character->GetIMGMovementComponent())
		{
			if (BoundMovement.IsValid() && BoundMovement != Movement) { BoundMovement->RemoveTickPrerequisiteComponent(this); }
			BoundMovement = Movement;
			// Proxy collision restoration must precede CMC floor queries, even without a root-motion update.
			Movement->AddTickPrerequisiteComponent(this);
		}
	}
	if (UIMGPawnExtensionComponent* Extension = UIMGPawnExtensionComponent::FindPawnExtensionComponent(GetOwner()))
	{
		if (BoundPawnExtension == Extension) { return; }
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

void UIMGTraversalComponent::BeginTraversal(const FIMGTraversalCandidate& Candidate,
    const FIMGTraversalAnimationSelection& Selection, const TArray<FIMGTraversalWarpTarget>& WarpTargets)
{
	if (bReleasingBindings || !IsRegistered() || !IsActive()) { return; }
	ActiveState = FIMGTraversalRepState();
	ActiveState.Sequence = ReplicatedState.Sequence + 1;
	ActiveState.Action = Selection.Action;
	ActiveState.Montage = Selection.Montage;
	ActiveState.MontageStartTime = Selection.StartTime;
	ActiveState.ObstacleComponents = Candidate.ObstacleComponents;
	ActiveState.WarpTargets = WarpTargets;
	ActiveState.bActive = true;
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
}

bool UIMGTraversalComponent::PublishTraversalState(uint8 MontagePlayInstanceId)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !ActiveState.bActive || !IsValid(ActiveState.Montage))
	{
		return false;
	}
	ActiveState.MontagePlayInstanceId = MontagePlayInstanceId;
	ReplicatedState = ActiveState;
	GetOwner()->ForceNetUpdate();
	return true;
}

void UIMGTraversalComponent::EndTraversal()
{
	if (!ActiveState.bActive)
	{
		return;
	}
	const uint16 NextSequence = ReplicatedState.Sequence + 1;
	SetTargetTickPrerequisites(false);
	RestoreMovementIgnores();
	RemoveWarpTargets();
	ActiveState = FIMGTraversalRepState();
	ActiveState.Sequence = NextSequence;
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
}

void UIMGTraversalComponent::OnRep_TraversalState()
{
	if (bReleasingBindings || !HasBegunPlay() || !IsRegistered() || !IsActive()) { return; }
	if (!ReplicatedState.bActive && ShouldDeferSimulatedEnd())
	{
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

void UIMGTraversalComponent::HandleMotionWarpingPreUpdate(UMotionWarpingComponent* Warping)
{
	if (ActiveState.bActive && Warping == BoundMotionWarpingComponent.Get())
	{
		// Native target replication can arrive independently of the traversal snapshot.
		RefreshWarpTargets();
	}
}

void UIMGTraversalComponent::HandleAbilitySystemInitialized()
{
	if (bReleasingBindings || !IsRegistered() || !IsActive()) { return; }
	HandleAbilitySystemUninitialized();
	if (!GetOwner() || GetOwner()->GetLocalRole() != ROLE_SimulatedProxy)
	{
		return;
	}
	const UIMGPawnExtensionComponent* Extension = UIMGPawnExtensionComponent::FindPawnExtensionComponent(GetOwner());
	UIMGAbilitySystemComponent* ASC = Extension ? Extension->GetIMGAbilitySystemComponent() : nullptr;
	if (!ASC)
	{
		return;
	}
	BoundAbilitySystemComponent = ASC;
	ASC->SetReplicatedMontageReadyDelegate(
	    FIMGReplicatedMontageReadyDelegate::CreateUObject(this, &ThisClass::IsReplicatedMontageReady));
	OnRep_TraversalState();
}

void UIMGTraversalComponent::HandleAbilitySystemUninitialized()
{
	if (UIMGAbilitySystemComponent* ASC = BoundAbilitySystemComponent.Get())
	{
		ASC->ClearReplicatedMontageReadyDelegate(this);
	}
	BoundAbilitySystemComponent.Reset();
	if (GetOwner() && GetOwner()->GetLocalRole() == ROLE_SimulatedProxy)
	{
		EndTraversal();
	}
}

bool UIMGTraversalComponent::IsReplicatedMontageReady(
    const UAnimMontage* Montage, uint8 MontagePlayInstanceId, bool bIsStopped, float& OutLogicalStartTime) const
{
	OutLogicalStartTime = -1.f;
	if (!Montage || bIsStopped)
	{
		return true;
	}
	if (!ActiveState.bActive || ActiveState.Montage != Montage ||
	    ActiveState.MontagePlayInstanceId != MontagePlayInstanceId)
	{
		if (MontageSettings.ContainsByPredicate(
		        [Montage](const FIMGTraversalMontageSettings& Entry) { return Entry.Montage == Montage; }))
		{
			return false;
		}
		TArray<FIMGTraversalWarpWindow> Windows;
		GetWarpWindows(Montage, Windows);
		for (const EIMGTraversalAction Action :
		    {EIMGTraversalAction::Mantle, EIMGTraversalAction::Vault, EIMGTraversalAction::Hurdle})
		{
			const FIMGTraversalAnimationSettings* Settings = FindAnimationSettings(Action, Montage);
			if (!Settings)
			{
				continue;
			}
			for (const FIMGTraversalWarpWindow& Window : Windows)
			{
				if (Settings->WarpBindings.ContainsByPredicate([&Window](const FIMGTraversalWarpBinding& Binding)
				        { return Binding.TargetName == Window.Modifier->WarpTargetName; }))
				{
					return false;
				}
			}
		}
		return true;
	}
	const UMotionWarpingComponent* Warping = GetMotionWarpingComponent();
	const AIMGCharacter* Character = Cast<AIMGCharacter>(GetOwner());
	const UIMGCharacterMovementComponent* Movement = Character ? Character->GetIMGMovementComponent() : nullptr;
	// Movement mode and GAS montage arrive through different actors; do not start with falling Z constraints.
	if (!Warping || !Character || !Character->GetCapsuleComponent() || !Movement || !Movement->IsFlying())
	{
		return false;
	}
	for (UPrimitiveComponent* Obstacle : ActiveState.ObstacleComponents)
	{
		if (!IsValid(Obstacle) || !Character->GetCapsuleComponent()->GetMoveIgnoreComponents().Contains(Obstacle))
		{
			return false;
		}
	}
	for (const FIMGTraversalWarpTarget& Target : ActiveState.WarpTargets)
	{
		const FMotionWarpingTarget* Resolved = Warping->FindWarpTarget(Target.Name);
		if (!IsValid(Target.Component) || !Resolved || !Resolved->GetTargetTrasform().IsValid())
		{
			return false;
		}
	}
	OutLogicalStartTime = ActiveState.MontageStartTime;
	return true;
}

void UIMGTraversalComponent::RefreshWarpTargets()
{
	RefreshMovementIgnores();
	UMotionWarpingComponent* Warping = GetMotionWarpingComponent();
	if (!Warping)
	{
		return;
	}
	SetTargetTickPrerequisites(true);
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
#if ENABLE_DRAW_DEBUG
		if (IMGTraversalDebug::IsEnabled(1) && IMGTraversalDebug::IsStageEnabled(IMGTraversalDebug::EStage::Execution))
		{
			IMGTraversalDebug::Point(GetWorld(), Transform.GetLocation(), FColor::Yellow, 1, 12.f, 0.f);
			IMGTraversalDebug::Arrow(GetWorld(), Transform.GetLocation(),
			    Transform.GetLocation() + Transform.GetUnitAxis(EAxis::X) * 45.f, FColor::Yellow, 1, 0.f);
			if (IMGTraversalDebug::IsEnabled(2))
			{
				const AIMGCharacter* DebugCharacter = Cast<AIMGCharacter>(GetOwner());
				const UIMGCharacterMovementComponent* DebugMovement = DebugCharacter ? DebugCharacter->GetIMGMovementComponent() : nullptr;
				IMGTraversalDebug::String(GetWorld(), Transform.GetLocation(),
				    FString::Printf(TEXT("%s | %s | %s | %s | Mode %s | AddedIgnore %d"), *GetNameSafe(GetOwner()), *Target.Name.ToString(),
				        GetOwner() ? *UEnum::GetValueAsString(GetOwner()->GetLocalRole()) : TEXT("No owner"), *GetNameSafe(Target.Component.Get()),
				        DebugMovement ? *UEnum::GetValueAsString(DebugMovement->MovementMode) : TEXT("No CMC"), AddedMovementIgnores.Num()),
				    FColor::White, 2, 0.f, IMGTraversalDebug::EStage::Execution, Target.Name, GetOwner());
			}
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
	if (GetOwner() && GetOwner()->GetLocalRole() == ROLE_SimulatedProxy && ActiveState.bActive &&
	    !ReplicatedState.bActive && !ShouldDeferSimulatedEnd())
	{
		EndTraversal();
	}
	if (ActiveState.bActive)
	{
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
	if (bReleasingBindings) { return; }
	TGuardValue<bool> Releasing(bReleasingBindings, true);
	const bool bWasActive = ActiveState.bActive;
	const TWeakObjectPtr<UIMGCharacterMovementComponent> ExecutionMovement = BoundMovement;
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
		Movement->RemoveTickPrerequisiteComponent(this);
	}
	BoundMovement.Reset();
	EndTraversal();
	BoundMotionWarpingComponent.Reset();
	if (bWasActive && ExecutionMovement.IsValid() && GetOwner() && GetOwner()->GetLocalRole() != ROLE_SimulatedProxy
		&& !GetOwner()->IsActorBeingDestroyed() && ExecutionMovement->IsFlying() && ExecutionMovement->UpdatedComponent)
	{
		FFindFloorResult Floor;
		ExecutionMovement->FindFloor(ExecutionMovement->UpdatedComponent->GetComponentLocation(), Floor, false);
		if (ExecutionMovement.IsValid() && ExecutionMovement->IsFlying())
		{
			ExecutionMovement->SetMovementMode(Floor.IsWalkableFloor() ? MOVE_Walking : MOVE_Falling);
		}
	}
}

void UIMGTraversalComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UIMGTraversalComponent, ReplicatedState, COND_SkipOwner);
}
