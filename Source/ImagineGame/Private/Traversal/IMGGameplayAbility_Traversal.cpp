#include "Traversal/IMGGameplayAbility_Traversal.h"

#include "AbilitySystem/IMGAbilitySystemComponent.h"
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

#if ENABLE_DRAW_DEBUG
#include "IMGTraversalDebug.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGGameplayAbility_Traversal)

namespace
{
const FName HurdleDistanceFromLedgeCurveName(TEXT("Distance_From_Ledge"));
}

#if ENABLE_DRAW_DEBUG
namespace
{
void DrawAbilityMessage(const AActor* Avatar, const TCHAR* Message, const FColor& Color)
{
	if (Avatar && IMGTraversalDebug::IsEnabled(1))
	{
		IMGTraversalDebug::String(Avatar->GetWorld(), Avatar->GetActorLocation() + FVector::UpVector * 145.f,
			Message, Color, 1);
	}
}
}
#endif

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
	bTargetDataSent = false;
	bTargetDataCancellationSent = false;
	ActiveAction = EIMGTraversalAction::None;
	ActiveObstacleComponents.Reset();
	PreviousObstacleTransforms.Reset();
	LocalCandidate = FIMGTraversalCandidate();
	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	UIMGTraversalComponent* Component = UIMGTraversalComponent::Get(GetAvatarActorFromActorInfo());
	if (!ASC || !Component)
	{
		CancelPendingTargetData();
		return;
	}
	if (ActorInfo->IsNetAuthority())
	{
		TargetDataDelegateHandle = ASC->AbilityTargetDataSetDelegate(Handle, ActivationInfo.GetActivationPredictionKey())
			.AddUObject(this, &ThisClass::OnTargetDataReady);
		TargetDataCancelledDelegateHandle = ASC->AbilityTargetDataCancelledDelegate(
			Handle, ActivationInfo.GetActivationPredictionKey()).AddUObject(this, &ThisClass::OnTargetDataCancelled);
		ASC->CallReplicatedTargetDataDelegatesIfSet(Handle, ActivationInfo.GetActivationPredictionKey());
		if (!IsActive())
		{
			return;
		}
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
		CancelPendingTargetData();
		return;
	}
	PreSelectTraversalAnimation(LocalCandidate);
	if (!IsActive())
	{
		CancelPendingTargetData();
		return;
	}
	const FIMGTraversalAnimationSelection Selection = SelectTraversalAnimation(LocalCandidate);
	if (!IsSelectionValid(Selection, LocalCandidate, false))
	{
#if ENABLE_DRAW_DEBUG
		DrawAbilityMessage(Character, TEXT("Traversal GA: animation selection rejected"), FColor::Red);
#endif
		CancelPendingTargetData();
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

void UIMGGameplayAbility_Traversal::CancelPendingTargetData()
{
	if (!bTargetDataSent && !bTargetDataCancellationSent && CurrentActorInfo
		&& CurrentActorInfo->IsLocallyControlled() && !CurrentActorInfo->IsNetAuthority())
	{
		if (UAbilitySystemComponent* ASC = CurrentActorInfo->AbilitySystemComponent.Get())
		{
			FScopedPredictionWindow PredictionWindow(ASC, true);
			ASC->ServerSetReplicatedTargetDataCancelled(CurrentSpecHandle,
				CurrentActivationInfo.GetActivationPredictionKey(), ASC->ScopedPredictionKey);
			bTargetDataCancellationSent = true;
		}
	}
	if (IsActive())
	{
		K2_EndAbility();
	}
}

void UIMGGameplayAbility_Traversal::OnTargetDataCancelled()
{
	if (!CurrentActorInfo || !CurrentActorInfo->AbilitySystemComponent.IsValid())
	{
		return;
	}
	UAbilitySystemComponent* ASC = CurrentActorInfo->AbilitySystemComponent.Get();
	ASC->ConsumeClientReplicatedTargetData(CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey());
	// Only release the server instance while it is waiting; active traversal remains authority-owned.
	if (IsActive() && !bTraversalStarted)
	{
		K2_EndAbility();
	}
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
		CancelPendingTargetData();
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
			CancelPendingTargetData();
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
#if ENABLE_DRAW_DEBUG
		DrawAbilityMessage(Character, TEXT("Traversal GA: selection or server allow-list rejected"), FColor::Red);
#endif
		CancelPendingTargetData();
		return;
	}
	if (!PrepareWarpTargets(Candidate, Selection))
	{
		CancelPendingTargetData();
		return;
	}
	if (!CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo))
	{
#if ENABLE_DRAW_DEBUG
		DrawAbilityMessage(Character, TEXT("Traversal GA: CommitAbility failed"), FColor::Red);
#endif
		CancelPendingTargetData();
		return;
	}
	ActiveComponent = Component;
	ActiveObstacleComponents.Reset();
	PreviousObstacleTransforms.Reset();
	for (UPrimitiveComponent* ObstacleComponent : Candidate.ObstacleComponents)
	{
		if (IsValid(ObstacleComponent))
		{
			ActiveObstacleComponents.Add(ObstacleComponent);
			PreviousObstacleTransforms.Add(ObstacleComponent->GetComponentTransform());
		}
	}
	if (ActiveObstacleComponents.IsEmpty() && IsValid(Candidate.HitComponent))
	{
		ActiveObstacleComponents.Add(Candidate.HitComponent);
		PreviousObstacleTransforms.Add(Candidate.HitComponent->GetComponentTransform());
	}
	ActiveBackFloorComponent = Candidate.BackFloorComponent;
	PreviousBackFloorTransform = IsValid(Candidate.BackFloorComponent)
		? Candidate.BackFloorComponent->GetComponentTransform() : FTransform::Identity;
	PreviousTargetCheckTime = GetWorld()->GetTimeSeconds();
	bTraversalStarted = true;
	ActiveAction = Selection.Action;
	Component->BeginTraversal(Candidate, Selection);
	for (const TWeakObjectPtr<UPrimitiveComponent>& ObstacleComponent : ActiveObstacleComponents)
	{
		if (ObstacleComponent.IsValid())
		{
			Character->GetCapsuleComponent()->IgnoreComponentWhenMoving(ObstacleComponent.Get(), true);
		}
	}
	Character->GetIMGMovementComponent()->SetMovementMode(MOVE_Flying);
	GetWorld()->GetTimerManager().SetTimer(TargetCheckTimer, this, &ThisClass::CheckActiveTarget, 0.05f, true);
	UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, Selection.Montage, Selection.PlayRate, NAME_None, true, 1.f, Selection.StartTime, true);
	MontageTask->OnCompleted.AddDynamic(this, &ThisClass::OnMontageCompleted);
	MontageTask->OnInterrupted.AddDynamic(this, &ThisClass::OnMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(this, &ThisClass::OnMontageInterrupted);
	MontageTask->ReadyForActivation();
	if (!IsActive())
	{
		CancelPendingTargetData();
		return;
	}
	if (CurrentActorInfo->IsNetAuthority())
	{
		uint8 MontagePlayInstanceId = 0;
		UIMGAbilitySystemComponent* IMGASC = Cast<UIMGAbilitySystemComponent>(ASC);
		if (!IMGASC
			|| !IMGASC->GetActiveReplicatedMontagePlayInstanceId(Selection.Montage, MontagePlayInstanceId)
			|| !Component->PublishTraversalState(MontagePlayInstanceId))
		{
			K2_EndAbility();
			return;
		}
	}
#if ENABLE_DRAW_DEBUG
	IMGTraversalDebug::SuccessfulTraversal(Character->GetWorld(), Candidate, Selection);
#endif
	OnTraversalStarted(Candidate, Selection.Action);
	if (!IsActive())
	{
		CancelPendingTargetData();
		return;
	}
	if (CurrentActorInfo->IsLocallyControlled() && !CurrentActorInfo->IsNetAuthority())
	{
		bTargetDataSent = true;
		ASC->CallServerSetReplicatedTargetData(CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey(),
			Data, ApplicationTag, ASC->ScopedPredictionKey);
	}
	ASC->ConsumeClientReplicatedTargetData(CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey());
}

bool UIMGGameplayAbility_Traversal::IsSelectionValid(const FIMGTraversalAnimationSelection& Selection,
	const FIMGTraversalCandidate& Candidate, bool bRemoteRequest) const
{
	const UIMGTraversalComponent* Traversal = UIMGTraversalComponent::Get(GetAvatarActorFromActorInfo());
	const UWorld* World = GetWorld();
	const double CandidateAge = World ? World->GetTimeSeconds() - Candidate.DetectionTime : BIG_NUMBER;
	const bool bCandidateFresh = Traversal && World && CandidateAge >= 0.0
		&& CandidateAge <= Traversal->GetDetectorSettings().MaxCandidateAge;
#if ENABLE_DRAW_DEBUG
	if (!bCandidateFresh)
	{
		if (const AIMGCharacter* Character = GetIMGCharacterFromActorInfo())
		{
			IMGTraversalDebug::String(Character->GetWorld(), Character->GetActorLocation() + FVector::UpVector * 160.f,
				FString::Printf(TEXT("Traversal Validate: stale candidate %.3fs"), CandidateAge), FColor::Red, 1);
		}
	}
#endif
	const bool bMatchesRemoteWhitelist = AllowedMontages.ContainsByPredicate(
		[&Selection](const FIMGTraversalAllowedMontage& Allowed)
		{
			return Allowed.Action == Selection.Action && Allowed.Montage == Selection.Montage;
		});
	const bool bAllowedRemoteSelection = !bRemoteRequest || AllowedMontages.IsEmpty() || bMatchesRemoteWhitelist;
#if ENABLE_DRAW_DEBUG
	if (bRemoteRequest && !bAllowedRemoteSelection)
	{
		DrawAbilityMessage(GetIMGCharacterFromActorInfo(),
			TEXT("Traversal Validate: remote montage is not in AllowedMontages"), FColor::Red);
	}
#endif
	return bCandidateFresh && Candidate.bHasFrontLedge && IsValid(Candidate.HitComponent)
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
#if ENABLE_DRAW_DEBUG
		DrawAbilityMessage(GetIMGCharacterFromActorInfo(),
			*FString::Printf(TEXT("Traversal Validate: Montage %s  Hit %s"),
				*GetNameSafe(Selection.Montage.Get()), *GetNameSafe(Candidate.HitComponent.Get())), FColor::Red);
#endif
		return false;
	}
	auto GetWindowTimes = [Montage = Selection.Montage.Get()](FName TargetName, float& OutStartTime,
		float& OutEndTime, int32& OutWindowCount)
	{
		TArray<FMotionWarpingWindowData> Windows;
		UMotionWarpingUtilities::GetMotionWarpingWindowsForWarpTargetFromAnimation(Montage, TargetName, Windows);
		OutWindowCount = Windows.Num();
		if (Windows.Num() != 1)
		{
			return false;
		}
		OutStartTime = Windows[0].StartTime;
		OutEndTime = Windows[0].EndTime;
		return OutEndTime > OutStartTime;
	};
	float FrontLedgeWindowStart = 0.f;
	float FrontLedgeWindowEnd = 0.f;
	int32 FrontLedgeWindowCount = 0;
	if (!GetWindowTimes(TEXT("FrontLedge"), FrontLedgeWindowStart, FrontLedgeWindowEnd, FrontLedgeWindowCount)
		|| Selection.StartTime >= FrontLedgeWindowEnd)
	{
#if ENABLE_DRAW_DEBUG
		DrawAbilityMessage(GetIMGCharacterFromActorInfo(),
			*FString::Printf(TEXT("FrontLedge FAIL: %s  Windows %d  Start %.2f  Range %.2f..%.2f"),
				*GetNameSafe(Selection.Montage.Get()), FrontLedgeWindowCount, Selection.StartTime,
				FrontLedgeWindowStart, FrontLedgeWindowEnd), FColor::Red);
#endif
		return false;
	}
	float BackLedgeWindowStart = 0.f;
	float BackLedgeWindowEnd = 0.f;
	int32 BackLedgeWindowCount = 0;
	const bool bUsesBackLedge = Selection.Action == EIMGTraversalAction::Hurdle
		|| Selection.Action == EIMGTraversalAction::Vault;
	const bool bBackLedgeWindowValid = bUsesBackLedge && GetWindowTimes(
		TEXT("BackLedge"), BackLedgeWindowStart, BackLedgeWindowEnd, BackLedgeWindowCount);
	// Traversal assets may use FrontLedge + BackFloor only. An authored BackLedge remains strictly validated.
	const bool bBackLedgeWindowInvalid = BackLedgeWindowCount > 0
		&& (!bBackLedgeWindowValid || Selection.StartTime >= BackLedgeWindowEnd);
	if (bUsesBackLedge
		&& (!Candidate.bHasBackLedge
			|| !IsValid(Candidate.BackLedgeComponent)
			|| bBackLedgeWindowInvalid))
	{
#if ENABLE_DRAW_DEBUG
		DrawAbilityMessage(GetIMGCharacterFromActorInfo(),
			*FString::Printf(TEXT("BackLedge FAIL: %s  Geo %d  Comp %d  Windows %d  PoseStart %.2f  Window %.2f..%.2f"),
				*GetNameSafe(Selection.Montage.Get()), Candidate.bHasBackLedge,
				IsValid(Candidate.BackLedgeComponent), BackLedgeWindowCount,
				Selection.StartTime, BackLedgeWindowStart, BackLedgeWindowEnd), FColor::Red);
#endif
		return false;
	}
	if (Selection.Action != EIMGTraversalAction::Hurdle)
	{
#if ENABLE_DRAW_DEBUG
		if (AIMGCharacter* Character = GetIMGCharacterFromActorInfo(); Character && IMGTraversalDebug::IsEnabled(3))
		{
			IMGTraversalDebug::String(Character->GetWorld(), Candidate.FrontLedge.GetLocation() + FVector::UpVector * 45.f,
				FString::Printf(TEXT("%s  Warp Front %.2f..%.2f  Back %.2f..%.2f  Start %.2f"),
					*GetNameSafe(Selection.Montage.Get()),
					FrontLedgeWindowStart, FrontLedgeWindowEnd, BackLedgeWindowStart, BackLedgeWindowEnd,
					Selection.StartTime), FColor::Yellow, 3);
		}
#endif
		return true;
	}
	if (!Candidate.bHasBackFloor)
	{
#if ENABLE_DRAW_DEBUG
		DrawAbilityMessage(GetIMGCharacterFromActorInfo(), TEXT("Traversal Validate: Hurdle has no back floor"), FColor::Red);
#endif
		return false;
	}
	float BackFloorWindowStart = 0.f;
	float BackFloorWindowEnd = 0.f;
	int32 BackFloorWindowCount = 0;
	if (!GetWindowTimes(TEXT("BackFloor"), BackFloorWindowStart, BackFloorWindowEnd, BackFloorWindowCount)
		|| Selection.StartTime >= BackFloorWindowEnd)
	{
#if ENABLE_DRAW_DEBUG
		DrawAbilityMessage(GetIMGCharacterFromActorInfo(),
			*FString::Printf(TEXT("BackFloor FAIL: %s  Windows %d  Start %.2f  Range %.2f..%.2f"),
				*GetNameSafe(Selection.Montage.Get()), BackFloorWindowCount, Selection.StartTime,
				BackFloorWindowStart, BackFloorWindowEnd), FColor::Red);
#endif
		return false;
	}
	const bool bUseBackLedgeAnchor = bBackLedgeWindowValid;
	const float LandingAnchorTime = bUseBackLedgeAnchor ? BackLedgeWindowEnd : FrontLedgeWindowEnd;
	float LandingAnchorCurveDistance = 0.f;
	float BackFloorDistance = 0.f;
	if (!UAnimationWarpingLibrary::GetCurveValueFromAnimation(
		Selection.Montage, HurdleDistanceFromLedgeCurveName, LandingAnchorTime, LandingAnchorCurveDistance)
		|| !UAnimationWarpingLibrary::GetCurveValueFromAnimation(
			Selection.Montage, HurdleDistanceFromLedgeCurveName, BackFloorWindowEnd, BackFloorDistance))
	{
#if ENABLE_DRAW_DEBUG
		DrawAbilityMessage(GetIMGCharacterFromActorInfo(),
			*FString::Printf(TEXT("Curve FAIL: %s  Missing %s"),
				*GetNameSafe(Selection.Montage.Get()), *HurdleDistanceFromLedgeCurveName.ToString()), FColor::Red);
#endif
		return false;
	}
	FVector LandingDirection = FVector(Candidate.BackLedgeNormal.X, Candidate.BackLedgeNormal.Y, 0.f).GetSafeNormal();
	if (LandingDirection.IsNearlyZero())
	{
		LandingDirection = FVector(Candidate.ApproachDirection.X, Candidate.ApproachDirection.Y, 0.f).GetSafeNormal();
	}
	const float LandingDistance = FMath::Abs(BackFloorDistance - LandingAnchorCurveDistance);
	const FVector LandingAnchor = bUseBackLedgeAnchor
		? Candidate.BackLedge.GetLocation() : Candidate.FrontLedge.GetLocation();
	FVector LandingPoint = LandingAnchor + LandingDirection * LandingDistance;
	AIMGCharacter* Character = GetIMGCharacterFromActorInfo();
	if (!Character || !Character->GetWorld() || !Character->GetCapsuleComponent() || !Character->GetIMGMovementComponent())
	{
#if ENABLE_DRAW_DEBUG
		DrawAbilityMessage(Character, TEXT("Traversal Validate: character movement data unavailable"), FColor::Red);
#endif
		return false;
	}
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const UIMGTraversalComponent* Traversal = UIMGTraversalComponent::Get(Character);
	if (!Traversal)
	{
#if ENABLE_DRAW_DEBUG
		DrawAbilityMessage(Character, TEXT("Traversal Validate: component unavailable"), FColor::Red);
#endif
		return false;
	}
	const FIMGTraversalDetectorSettings& Settings = Traversal->GetDetectorSettings();
	FHitResult FloorHit;
	FCollisionQueryParams FloorParams(SCENE_QUERY_STAT(IMGTraversalSelectedLanding), false, Character);
	const FCollisionResponseParams Responses(Capsule->GetCollisionResponseToChannels());
	const FVector TraceStart(LandingPoint.X, LandingPoint.Y, Candidate.BackLedge.GetLocation().Z + HalfHeight);
	const FVector TraceEnd(LandingPoint.X, LandingPoint.Y,
		Candidate.BackFloor.GetLocation().Z - FMath::Max(Settings.MaxDrop, HalfHeight));
	const bool bFloorHit = Character->GetWorld()->LineTraceSingleByChannel(FloorHit, TraceStart, TraceEnd,
		IMG_TraceChannel_Traversal, FloorParams, Responses);
	const bool bWalkableFloor = bFloorHit && Character->GetIMGMovementComponent()->IsWalkable(FloorHit);
#if ENABLE_DRAW_DEBUG
	IMGTraversalDebug::Line(Character->GetWorld(), TraceStart, bFloorHit ? FloorHit.ImpactPoint : TraceEnd,
		bWalkableFloor ? FColor::Green : FColor::Red, 3, 2.f);
#endif
	UPrimitiveComponent* SelectedFloorComponent = FloorHit.GetComponent();
	const bool bUsableFloorComponent = IsValid(SelectedFloorComponent)
		&& (!SelectedFloorComponent->IsSimulatingPhysics() || Settings.bAllowSimulatingPhysics)
		&& SelectedFloorComponent->GetComponentVelocity().Size() <= Settings.MaxMovingSpeed
		&& SelectedFloorComponent->GetPhysicsAngularVelocityInDegrees().Size() <= Settings.MaxAngularSpeedDegrees;
	if (!bWalkableFloor || !bUsableFloorComponent)
	{
#if ENABLE_DRAW_DEBUG
		DrawAbilityMessage(Character, TEXT("Traversal Validate: adjusted landing floor is unusable"), FColor::Red);
#endif
		return false;
	}
	const FVector LandingCenter = FloorHit.ImpactPoint + FVector::UpVector * (HalfHeight + 3.f);
	const bool bLandingBlocked = Character->GetWorld()->OverlapBlockingTestByChannel(LandingCenter, FQuat::Identity,
		Capsule->GetCollisionObjectType(), FCollisionShape::MakeCapsule(Radius, HalfHeight), FloorParams, Responses);
#if ENABLE_DRAW_DEBUG
	IMGTraversalDebug::Capsule(Character->GetWorld(), LandingCenter, HalfHeight, Radius,
		bLandingBlocked ? FColor::Red : FColor::Green, 3);
#endif
	if (bLandingBlocked)
	{
#if ENABLE_DRAW_DEBUG
		DrawAbilityMessage(Character, TEXT("Traversal Validate: adjusted landing capsule blocked"), FColor::Red);
#endif
		return false;
	}
	FCollisionQueryParams PathParams(SCENE_QUERY_STAT(IMGTraversalSelectedLandingPath), false, Character);
	for (const UPrimitiveComponent* ObstacleComponent : Candidate.ObstacleComponents)
	{
		if (IsValid(ObstacleComponent))
		{
			PathParams.AddIgnoredComponent(ObstacleComponent);
		}
	}
	FHitResult PathHit;
	const FVector BackLedgeCenter = Candidate.BackLedge.GetLocation() + FVector::UpVector * (HalfHeight + 3.f);
	const bool bLandingPathBlocked = Character->GetWorld()->SweepSingleByChannel(PathHit,
		BackLedgeCenter, LandingCenter, FQuat::Identity, Capsule->GetCollisionObjectType(),
		FCollisionShape::MakeCapsule(Radius, HalfHeight), PathParams, Responses);
#if ENABLE_DRAW_DEBUG
	IMGTraversalDebug::Line(Character->GetWorld(), BackLedgeCenter,
		bLandingPathBlocked ? PathHit.Location : LandingCenter,
		bLandingPathBlocked ? FColor::Red : FColor::Green, 3, 2.f);
	IMGTraversalDebug::String(Character->GetWorld(), LandingCenter + FVector::UpVector * 12.f,
		FString::Printf(TEXT("Warp windows F %.2f..%.2f B %.2f..%.2f L %.2f..%.2f  Anchor %s %.1f -> %.1f"),
			FrontLedgeWindowStart, FrontLedgeWindowEnd, BackLedgeWindowStart, BackLedgeWindowEnd,
			BackFloorWindowStart, BackFloorWindowEnd, bUseBackLedgeAnchor ? TEXT("Back") : TEXT("Front"),
			LandingAnchorCurveDistance, BackFloorDistance),
		bLandingPathBlocked ? FColor::Red : FColor::Green, 3);
#endif
	if (bLandingPathBlocked)
	{
#if ENABLE_DRAW_DEBUG
		DrawAbilityMessage(Character, TEXT("Traversal Validate: adjusted landing path blocked"), FColor::Red);
#endif
		return false;
	}
	LandingPoint.Z = FloorHit.ImpactPoint.Z;
	Candidate.BackFloor.SetLocation(LandingPoint);
	Candidate.BackFloorComponent = SelectedFloorComponent;
	Candidate.DropHeight = Candidate.FrontLedge.GetLocation().Z - LandingPoint.Z;
	Candidate.BackLedgeHeight = Candidate.BackLedge.GetLocation().Z - LandingPoint.Z;
	Candidate.LocalBackFloor = Candidate.BackFloorComponent
		? Candidate.BackFloor.GetRelativeTransform(Candidate.BackFloorComponent->GetComponentTransform())
		: FTransform::Identity;
	return true;
}

void UIMGGameplayAbility_Traversal::CheckActiveTarget()
{
	UIMGTraversalComponent* Component = ActiveComponent.Get();
	const UWorld* World = GetWorld();
	const AIMGCharacter* Character = GetIMGCharacterFromActorInfo();
	const UIMGHealthComponent* Health = UIMGHealthComponent::FindHealthComponent(Character);
	if (Health && Health->IsDeadOrDying())
	{
#if ENABLE_DRAW_DEBUG
		DrawAbilityMessage(Character, TEXT("Traversal Abort: character died"), FColor::Red);
#endif
		K2_EndAbility();
		return;
	}
	if (!Component || !World || ActiveObstacleComponents.IsEmpty()
		|| ActiveObstacleComponents.Num() != PreviousObstacleTransforms.Num())
	{
#if ENABLE_DRAW_DEBUG
		DrawAbilityMessage(GetAvatarActorFromActorInfo(), TEXT("Traversal Abort: obstacle set or component lost"), FColor::Red);
#endif
		K2_EndAbility();
		return;
	}
	const FIMGTraversalDetectorSettings& Settings = Component->GetDetectorSettings();
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
	for (int32 Index = 0; Index < ActiveObstacleComponents.Num(); ++Index)
	{
		UPrimitiveComponent* Target = ActiveObstacleComponents[Index].Get();
		if (!IsValid(Target))
		{
#if ENABLE_DRAW_DEBUG
			DrawAbilityMessage(GetAvatarActorFromActorInfo(), TEXT("Traversal Abort: obstacle component lost"), FColor::Red);
#endif
			K2_EndAbility();
			return;
		}
		const FTransform Current = Target->GetComponentTransform();
		if (IsMovingTooFast(*Target, Current, PreviousObstacleTransforms[Index]))
		{
#if ENABLE_DRAW_DEBUG
			DrawAbilityMessage(GetAvatarActorFromActorInfo(), TEXT("Traversal Abort: obstacle moved too fast"), FColor::Red);
			if (IMGTraversalDebug::IsEnabled(3))
			{
				const float Speed = FVector::Dist(Current.GetLocation(), PreviousObstacleTransforms[Index].GetLocation()) / DeltaTime;
				const float AngularSpeed = FMath::RadiansToDegrees(Current.GetRotation().AngularDistance(
					PreviousObstacleTransforms[Index].GetRotation())) / DeltaTime;
				IMGTraversalDebug::String(World, Target->GetComponentLocation() + FVector::UpVector * 18.f,
					FString::Printf(TEXT("Abort %s: V %.0f/%.0f  W %.0f/%.0f"), *Target->GetName(),
						Speed, Settings.MaxMovingSpeed, AngularSpeed, Settings.MaxAngularSpeedDegrees),
					FColor::Red, 3);
			}
#endif
			K2_EndAbility();
			return;
		}
		PreviousObstacleTransforms[Index] = Current;
	}
	UPrimitiveComponent* BackFloorComponent = ActiveBackFloorComponent.Get();
	if (ActiveAction == EIMGTraversalAction::Hurdle && !IsValid(BackFloorComponent))
	{
#if ENABLE_DRAW_DEBUG
		DrawAbilityMessage(GetAvatarActorFromActorInfo(), TEXT("Traversal Abort: back floor lost"), FColor::Red);
#endif
		K2_EndAbility();
		return;
	}
	const bool bBackFloorAlreadyTracked = ActiveObstacleComponents.ContainsByPredicate(
		[BackFloorComponent](const TWeakObjectPtr<UPrimitiveComponent>& Entry)
		{
			return Entry.Get() == BackFloorComponent;
		});
	if (BackFloorComponent && !bBackFloorAlreadyTracked)
	{
		const FTransform CurrentBackFloor = BackFloorComponent->GetComponentTransform();
		if (IsMovingTooFast(*BackFloorComponent, CurrentBackFloor, PreviousBackFloorTransform))
		{
#if ENABLE_DRAW_DEBUG
			DrawAbilityMessage(GetAvatarActorFromActorInfo(), TEXT("Traversal Abort: back floor moved too fast"), FColor::Red);
#endif
			K2_EndAbility();
			return;
		}
		PreviousBackFloorTransform = CurrentBackFloor;
	}
	PreviousTargetCheckTime = World->GetTimeSeconds();
}

void UIMGGameplayAbility_Traversal::OnMontageCompleted()
{
	bMontageCompleted = true;
	K2_EndAbility();
}

void UIMGGameplayAbility_Traversal::OnMontageInterrupted()
{
	if (!bTargetDataSent)
	{
		CancelPendingTargetData();
	}
	else
	{
		K2_EndAbility();
	}
}

void UIMGGameplayAbility_Traversal::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
#if ENABLE_DRAW_DEBUG
	IMGTraversalDebug::DiscardTraversalSnapshot();
#endif
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TargetCheckTimer);
	}
	if (ActorInfo && ActorInfo->AbilitySystemComponent.IsValid())
	{
		UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
		if (TargetDataDelegateHandle.IsValid())
		{
			ASC->AbilityTargetDataSetDelegate(Handle, ActivationInfo.GetActivationPredictionKey())
				.Remove(TargetDataDelegateHandle);
		}
		if (TargetDataCancelledDelegateHandle.IsValid())
		{
			ASC->AbilityTargetDataCancelledDelegate(Handle, ActivationInfo.GetActivationPredictionKey())
				.Remove(TargetDataCancelledDelegateHandle);
		}
		ASC->ConsumeClientReplicatedTargetData(Handle, ActivationInfo.GetActivationPredictionKey());
	}
	TargetDataDelegateHandle.Reset();
	TargetDataCancelledDelegateHandle.Reset();
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
				for (const TWeakObjectPtr<UPrimitiveComponent>& Ignored : ActiveObstacleComponents)
				{
					if (Ignored.IsValid())
					{
						Capsule->IgnoreComponentWhenMoving(Ignored.Get(), false);
					}
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
					Movement->SetMovementMode(Floor.IsWalkableFloor() ? MOVE_Walking : MOVE_Falling);
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
	ActiveObstacleComponents.Reset();
	PreviousObstacleTransforms.Reset();
	ActiveBackFloorComponent.Reset();
	LocalCandidate = FIMGTraversalCandidate();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
