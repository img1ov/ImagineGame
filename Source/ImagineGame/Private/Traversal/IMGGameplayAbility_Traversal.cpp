#include "Traversal/IMGGameplayAbility_Traversal.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "AnimationWarpingLibrary.h"
#include "Animation/AnimMontage.h"
#include "Character/IMGCharacter.h"
#include "Character/IMGCharacterMovementComponent.h"
#include "Character/IMGHealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "MotionWarpingComponent.h"
#include "Physics/IMGCollisionChannels.h"
#include "Traversal/IMGTraversalComponent.h"
#include "Traversal/IMGTraversalTargetData.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGGameplayAbility_Traversal)

UIMGGameplayAbility_Traversal::UIMGGameplayAbility_Traversal(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	ActivationGroup = EIMGAbilityActivationGroup::Exclusive_Replaceable;
	bServerRespectsRemoteAbilityCancellation = false;
}

bool UIMGGameplayAbility_Traversal::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags)
		|| !ActorInfo || !ActorInfo->AvatarActor.IsValid())
	{
		return false;
	}
	const AIMGCharacter* Character = Cast<AIMGCharacter>(ActorInfo->AvatarActor.Get());
	const UIMGTraversalComponent* Component = UIMGTraversalComponent::Get(Character);
	const UIMGHealthComponent* Health = UIMGHealthComponent::FindHealthComponent(Character);
	return Component && !Component->IsTraversalActive() && Component->GetMotionWarpingComponent()
		&& Character->GetIMGMovementComponent() && (!Health || !Health->IsDeadOrDying());
}

void UIMGGameplayAbility_Traversal::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	bLocalRequest = false;
	bTraversalStarted = false;
	bMontageCompleted = false;
	ActiveAction = EIMGTraversalAction::None;
	LocalCandidate = FIMGTraversalCandidate();
	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	UIMGTraversalComponent* Component = UIMGTraversalComponent::Get(GetAvatarActorFromActorInfo());
	if (!ASC || !Component)
	{
		K2_EndAbility();
		return;
	}
	if (ActorInfo->IsNetAuthority())
	{
		TargetDataDelegateHandle = ASC->AbilityTargetDataSetDelegate(Handle, ActivationInfo.GetActivationPredictionKey())
			.AddUObject(this, &ThisClass::OnTargetDataReady);
		ASC->CallReplicatedTargetDataDelegatesIfSet(Handle, ActivationInfo.GetActivationPredictionKey());
	}
	AIMGCharacter* Character = GetIMGCharacterFromActorInfo();
	const bool bServerControlledAI = ActorInfo->IsNetAuthority() && Character
		&& Character->GetController() && !Character->IsPlayerControlled();
	const bool bShouldBuildLocalRequest = ActorInfo->IsLocallyControlled() || bServerControlledAI;
	if (!bShouldBuildLocalRequest)
	{
		return;
	}
	bLocalRequest = true;
	EIMGTraversalFailure Failure;
	if (!Component->Detect(LocalCandidate, Failure, FVector::ZeroVector))
	{
		K2_EndAbility();
		return;
	}
	const FIMGTraversalAnimationSelection Selection = SelectTraversalAnimation(LocalCandidate);
	if (!IsSelectionValid(Selection, LocalCandidate, false))
	{
		K2_EndAbility();
		return;
	}
	auto* Request = new FIMGTraversalTargetData();
	Request->Direction = LocalCandidate.ApproachDirection;
	Request->Action = Selection.Action;
	Request->Montage = Selection.Montage;
	Request->StartTime = Selection.StartTime;
	Request->PlayRate = Selection.PlayRate;
	FGameplayAbilityTargetDataHandle Data;
	Data.Add(Request);
	OnTargetDataReady(Data, FGameplayTag());
}

void UIMGGameplayAbility_Traversal::OnTargetDataReady(const FGameplayAbilityTargetDataHandle& Data, FGameplayTag ApplicationTag)
{
	if (!IsActive() || bTraversalStarted || !CurrentActorInfo || !CurrentActorInfo->AbilitySystemComponent.IsValid())
	{
		return;
	}
	UAbilitySystemComponent* ASC = CurrentActorInfo->AbilitySystemComponent.Get();
	FScopedPredictionWindow PredictionWindow(ASC);
	const FIMGTraversalTargetData* Request = Data.Num() == 1 && Data.Get(0)
		&& Data.Get(0)->GetScriptStruct() == FIMGTraversalTargetData::StaticStruct()
		? static_cast<const FIMGTraversalTargetData*>(Data.Get(0)) : nullptr;
	UIMGTraversalComponent* Component = UIMGTraversalComponent::Get(GetAvatarActorFromActorInfo());
	AIMGCharacter* Character = GetIMGCharacterFromActorInfo();
	if (!Request || !Component || !Character)
	{
		K2_EndAbility();
		return;
	}
	FIMGTraversalCandidate Candidate;
	const bool bRemoteRequest = CurrentActorInfo->IsNetAuthority() && !bLocalRequest;
	if (bRemoteRequest)
	{
		EIMGTraversalFailure Failure;
		if (FVector::DotProduct(FVector(Request->Direction).GetSafeNormal2D(), Character->GetActorForwardVector()) < 0.25f
			|| !Component->Detect(Candidate, Failure, Request->Direction))
		{
			K2_EndAbility();
			return;
		}
	}
	else
	{
		Candidate = LocalCandidate;
	}
	FIMGTraversalAnimationSelection Selection;
	Selection.Action = Request->Action;
	Selection.Montage = Request->Montage;
	Selection.StartTime = Request->StartTime;
	Selection.PlayRate = Request->PlayRate;
	if (!IsSelectionValid(Selection, Candidate, bRemoteRequest))
	{
		K2_EndAbility();
		return;
	}
	if (!PrepareWarpTargets(Candidate, Selection))
	{
		K2_EndAbility();
		return;
	}
	if (CurrentActorInfo->IsLocallyControlled() && !CurrentActorInfo->IsNetAuthority())
	{
		ASC->CallServerSetReplicatedTargetData(CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey(),
			Data, ApplicationTag, ASC->ScopedPredictionKey);
	}
	if (!CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo))
	{
		K2_EndAbility();
		return;
	}
	ActiveComponent = Component;
	IgnoredComponent = Candidate.HitComponent;
	ActiveBackFloorComponent = Candidate.BackFloorComponent;
	PreviousTargetTransform = Candidate.HitComponent->GetComponentTransform();
	PreviousBackFloorTransform = IsValid(Candidate.BackFloorComponent)
		? Candidate.BackFloorComponent->GetComponentTransform() : FTransform::Identity;
	PreviousTargetCheckTime = GetWorld()->GetTimeSeconds();
	bTraversalStarted = true;
	ActiveAction = Selection.Action;
	Component->BeginTraversal(Candidate, Selection.Action);
	Character->GetCapsuleComponent()->IgnoreComponentWhenMoving(Candidate.HitComponent.Get(), true);
	Character->GetIMGMovementComponent()->SetMovementMode(MOVE_Flying);
	GetWorld()->GetTimerManager().SetTimer(TargetCheckTimer, this, &ThisClass::CheckActiveTarget, 0.05f, true);
	UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, Selection.Montage, Selection.PlayRate, NAME_None, true, 1.f, Selection.StartTime, true);
	MontageTask->OnCompleted.AddDynamic(this, &ThisClass::OnMontageCompleted);
	MontageTask->OnInterrupted.AddDynamic(this, &ThisClass::OnMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(this, &ThisClass::OnMontageInterrupted);
	MontageTask->ReadyForActivation();
	if (IsActive())
	{
		OnTraversalStarted(Candidate, Selection.Action);
	}
	ASC->ConsumeClientReplicatedTargetData(CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey());
}

bool UIMGGameplayAbility_Traversal::IsSelectionValid(const FIMGTraversalAnimationSelection& Selection,
	const FIMGTraversalCandidate& Candidate, bool bRemoteRequest) const
{
	const bool bAllowedRemoteSelection = !bRemoteRequest || AllowedMontages.ContainsByPredicate(
		[&Selection](const FIMGTraversalAllowedMontage& Allowed)
		{
			return Allowed.Action == Selection.Action && Allowed.Montage == Selection.Montage;
		});
	return Candidate.bHasFrontLedge && IsValid(Candidate.HitComponent)
		&& Candidate.Supports(Selection.Action) && IsValid(Selection.Montage)
		&& (Selection.Action != EIMGTraversalAction::Hurdle || IsValid(Candidate.BackFloorComponent))
		&& FMath::IsFinite(Selection.StartTime) && Selection.StartTime >= 0.f
		&& Selection.StartTime < Selection.Montage->GetPlayLength()
		&& FMath::IsFinite(Selection.PlayRate) && Selection.PlayRate >= 0.1f && Selection.PlayRate <= 3.f
		&& bAllowedRemoteSelection;
}

bool UIMGGameplayAbility_Traversal::PrepareWarpTargets(FIMGTraversalCandidate& Candidate,
	const FIMGTraversalAnimationSelection& Selection) const
{
	if (!IsValid(Selection.Montage) || !IsValid(Candidate.HitComponent))
	{
		return false;
	}
	auto GetWindowEndTime = [Montage = Selection.Montage.Get()](FName TargetName, float& OutEndTime)
	{
		TArray<FMotionWarpingWindowData> Windows;
		UMotionWarpingUtilities::GetMotionWarpingWindowsForWarpTargetFromAnimation(Montage, TargetName, Windows);
		if (Windows.Num() != 1)
		{
			return false;
		}
		OutEndTime = Windows[0].EndTime;
		return true;
	};
	float FrontLedgeWindowEnd = 0.f;
	if (!GetWindowEndTime(TEXT("FrontLedge"), FrontLedgeWindowEnd))
	{
		return false;
	}
	float BackLedgeWindowEnd = 0.f;
	if ((Selection.Action == EIMGTraversalAction::Hurdle || Selection.Action == EIMGTraversalAction::Vault)
		&& (!Candidate.bHasBackLedge || !GetWindowEndTime(TEXT("BackLedge"), BackLedgeWindowEnd)))
	{
		return false;
	}
	if (Selection.Action != EIMGTraversalAction::Hurdle)
	{
		return true;
	}
	if (!Candidate.bHasBackFloor)
	{
		return false;
	}
	float BackFloorWindowEnd = 0.f;
	if (!GetWindowEndTime(TEXT("BackFloor"), BackFloorWindowEnd))
	{
		return false;
	}
	float BackLedgeDistance = 0.f;
	float BackFloorDistance = 0.f;
	if (!UAnimationWarpingLibrary::GetCurveValueFromAnimation(
		Selection.Montage, DistanceFromLedgeCurveName, BackLedgeWindowEnd, BackLedgeDistance)
		|| !UAnimationWarpingLibrary::GetCurveValueFromAnimation(
			Selection.Montage, DistanceFromLedgeCurveName, BackFloorWindowEnd, BackFloorDistance))
	{
		return false;
	}
	FVector LandingDirection = FVector(Candidate.BackLedgeNormal.X, Candidate.BackLedgeNormal.Y, 0.f).GetSafeNormal();
	if (LandingDirection.IsNearlyZero())
	{
		LandingDirection = FVector(Candidate.ApproachDirection.X, Candidate.ApproachDirection.Y, 0.f).GetSafeNormal();
	}
	const float LandingDistance = FMath::Abs(BackFloorDistance - BackLedgeDistance);
	FVector LandingPoint = Candidate.BackLedge.GetLocation() + LandingDirection * LandingDistance;
	AIMGCharacter* Character = GetIMGCharacterFromActorInfo();
	if (!Character || !Character->GetWorld() || !Character->GetCapsuleComponent() || !Character->GetIMGMovementComponent())
	{
		return false;
	}
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const UIMGTraversalComponent* Traversal = UIMGTraversalComponent::Get(Character);
	if (!Traversal)
	{
		return false;
	}
	const FIMGTraversalQuery& Settings = Traversal->GetQuerySettings();
	FHitResult FloorHit;
	FCollisionQueryParams FloorParams(SCENE_QUERY_STAT(IMGTraversalSelectedLanding), false, Character);
	const FCollisionResponseParams Responses(Capsule->GetCollisionResponseToChannels());
	const FVector TraceStart(LandingPoint.X, LandingPoint.Y, Candidate.BackLedge.GetLocation().Z + HalfHeight);
	const FVector TraceEnd(LandingPoint.X, LandingPoint.Y,
		Candidate.BackFloor.GetLocation().Z - FMath::Max(Settings.MaxDrop, HalfHeight));
	if (!Character->GetWorld()->LineTraceSingleByChannel(FloorHit, TraceStart, TraceEnd,
		IMG_TraceChannel_Traversal, FloorParams, Responses)
		|| !Character->GetIMGMovementComponent()->IsWalkable(FloorHit))
	{
		return false;
	}
	const FVector LandingCenter = FloorHit.ImpactPoint + FVector::UpVector * (HalfHeight + 3.f);
	if (Character->GetWorld()->OverlapBlockingTestByChannel(LandingCenter, FQuat::Identity,
		Capsule->GetCollisionObjectType(), FCollisionShape::MakeCapsule(Radius, HalfHeight), FloorParams, Responses))
	{
		return false;
	}
	LandingPoint.Z = FloorHit.ImpactPoint.Z;
	Candidate.BackFloor.SetLocation(LandingPoint);
	Candidate.BackFloorComponent = FloorHit.GetComponent();
	Candidate.DropHeight = Candidate.FrontLedge.GetLocation().Z - LandingPoint.Z;
	Candidate.BackLedgeHeight = Candidate.BackLedge.GetLocation().Z - LandingPoint.Z;
	Candidate.LocalBackFloor = Candidate.BackFloorComponent
		? Candidate.BackFloor.GetRelativeTransform(Candidate.BackFloorComponent->GetComponentTransform())
		: FTransform::Identity;
	return true;
}

void UIMGGameplayAbility_Traversal::CheckActiveTarget()
{
	UPrimitiveComponent* Target = IgnoredComponent.Get();
	UIMGTraversalComponent* Component = ActiveComponent.Get();
	const UWorld* World = GetWorld();
	if (!IsValid(Target) || !Component || !World)
	{
		K2_EndAbility();
		return;
	}
	const FIMGTraversalQuery& Settings = Component->GetQuerySettings();
	const FTransform Current = Target->GetComponentTransform();
	const float DeltaTime = FMath::Max(World->GetTimeSeconds() - PreviousTargetCheckTime, KINDA_SMALL_NUMBER);
	auto IsMovingTooFast = [&Settings, DeltaTime](const UPrimitiveComponent& Component,
		const FTransform& CurrentTransform, const FTransform& PreviousTransform)
	{
		const float Speed = FVector::Dist(CurrentTransform.GetLocation(), PreviousTransform.GetLocation()) / DeltaTime;
		const float AngularSpeed = FMath::RadiansToDegrees(
			CurrentTransform.GetRotation().AngularDistance(PreviousTransform.GetRotation())) / DeltaTime;
		return (Component.IsSimulatingPhysics() && !Settings.bAllowSimulatingPhysics)
			|| Speed > Settings.MaxMovingSpeed || AngularSpeed > Settings.MaxAngularSpeedDegrees;
	};
	if (IsMovingTooFast(*Target, Current, PreviousTargetTransform))
	{
		K2_EndAbility();
		return;
	}
	UPrimitiveComponent* BackFloorComponent = ActiveBackFloorComponent.Get();
	if (ActiveAction == EIMGTraversalAction::Hurdle && !IsValid(BackFloorComponent))
	{
		K2_EndAbility();
		return;
	}
	if (BackFloorComponent && BackFloorComponent != Target)
	{
		const FTransform CurrentBackFloor = BackFloorComponent->GetComponentTransform();
		if (IsMovingTooFast(*BackFloorComponent, CurrentBackFloor, PreviousBackFloorTransform))
		{
			K2_EndAbility();
			return;
		}
		PreviousBackFloorTransform = CurrentBackFloor;
	}
	PreviousTargetTransform = Current;
	PreviousTargetCheckTime = World->GetTimeSeconds();
}

void UIMGGameplayAbility_Traversal::OnMontageCompleted()
{
	bMontageCompleted = true;
	K2_EndAbility();
}

void UIMGGameplayAbility_Traversal::OnMontageInterrupted()
{
	K2_EndAbility();
}

void UIMGGameplayAbility_Traversal::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TargetCheckTimer);
	}
	if (ActorInfo && ActorInfo->AbilitySystemComponent.IsValid() && TargetDataDelegateHandle.IsValid())
	{
		UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
		ASC->AbilityTargetDataSetDelegate(Handle, ActivationInfo.GetActivationPredictionKey()).Remove(TargetDataDelegateHandle);
		ASC->ConsumeClientReplicatedTargetData(Handle, ActivationInfo.GetActivationPredictionKey());
		TargetDataDelegateHandle.Reset();
	}
	if (bTraversalStarted)
	{
		if (UIMGTraversalComponent* Component = ActiveComponent.Get())
		{
			Component->EndTraversal();
		}
		if (AIMGCharacter* Character = GetIMGCharacterFromActorInfo())
		{
			if (UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
			{
				if (UPrimitiveComponent* Ignored = IgnoredComponent.Get())
				{
					Capsule->IgnoreComponentWhenMoving(Ignored, false);
				}
			}
			const UIMGHealthComponent* Health = UIMGHealthComponent::FindHealthComponent(Character);
			if ((!Health || !Health->IsDeadOrDying()) && Character->GetIMGMovementComponent())
			{
				UIMGCharacterMovementComponent* Movement = Character->GetIMGMovementComponent();
				if (Movement->MovementMode == MOVE_Flying)
				{
					FFindFloorResult Floor;
					Movement->FindFloor(Character->GetActorLocation(), Floor, false);
					Movement->SetMovementMode(Floor.IsWalkableFloor() && ActiveAction != EIMGTraversalAction::Vault
						? MOVE_Walking : MOVE_Falling);
				}
			}
		}
		OnTraversalEnded(bMontageCompleted && !bWasCancelled);
	}
	bTraversalStarted = false;
	bMontageCompleted = false;
	bLocalRequest = false;
	ActiveAction = EIMGTraversalAction::None;
	ActiveComponent.Reset();
	IgnoredComponent.Reset();
	ActiveBackFloorComponent.Reset();
	LocalCandidate = FIMGTraversalCandidate();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
