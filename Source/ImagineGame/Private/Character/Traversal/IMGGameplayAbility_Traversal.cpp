#include "Character/Traversal/IMGGameplayAbility_Traversal.h"

#include "AbilitySystem/IMGAbilitySystemComponent.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/IMGCharacter.h"
#include "Character/IMGCharacterMovementComponent.h"
#include "Character/IMGHealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "MotionWarpingComponent.h"
#include "Character/Traversal/IMGTraversalComponent.h"
#include "Character/Traversal/IMGTraversalDetector.h"
#include "Character/Traversal/IMGTraversalTargetData.h"
#include "IMGTraversalPlacement.h"

#if ENABLE_DRAW_DEBUG
#include "IMGTraversalDebug.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGGameplayAbility_Traversal)

DEFINE_LOG_CATEGORY_STATIC(LogIMGTraversal, Log, All);

#if ENABLE_DRAW_DEBUG
namespace
{
void DrawAbilityMessage(const AActor* Avatar, const TCHAR* Message, const FColor& Color)
{
	if (IsValid(Avatar) && IMGTraversalDebug::IsEnabled(1))
	{
		IMGTraversalDebug::String(
		    Avatar->GetWorld(), Avatar->GetActorLocation() + FVector::UpVector * 145.f, Message, Color, 1);
	}
}
} // namespace
#endif

UIMGGameplayAbility_Traversal::UIMGGameplayAbility_Traversal(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	ActivationGroup = EIMGAbilityActivationGroup::Exclusive_Replaceable;
	bServerRespectsRemoteAbilityCancellation = false;
}

void UIMGGameplayAbility_Traversal::LogTraversalEvent(
    const TCHAR* Event, const FString& Detail, bool bWarning) const
{
	if (!bWarning && !UE_LOG_ACTIVE(LogIMGTraversal, Verbose))
	{
		return;
	}
	const AActor* Avatar = CurrentActorInfo ? CurrentActorInfo->AvatarActor.Get() : nullptr;
	const AIMGCharacter* Character = Cast<AIMGCharacter>(Avatar);
	const UIMGCharacterMovementComponent* Movement = Character ? Character->GetIMGMovementComponent() : nullptr;
	const FString Context = FString::Printf(
	    TEXT("[Traversal] %s Ability=%s Avatar=%s Role=%d NetMode=%d Local=%d Auth=%d Spec=%s PredKey=%d ActivationMode=%d Started=%d Sent=%d Loc=%s Velocity=%s MoveMode=%u/%u | %s"),
	    Event, *GetNameSafe(this), *GetNameSafe(Avatar), Avatar ? static_cast<int32>(Avatar->GetLocalRole()) : -1,
	    Avatar ? static_cast<int32>(Avatar->GetNetMode()) : -1,
	    CurrentActorInfo ? CurrentActorInfo->IsLocallyControlled() : false,
	    CurrentActorInfo ? CurrentActorInfo->IsNetAuthority() : false, *CurrentSpecHandle.ToString(),
	    CurrentActivationInfo.GetActivationPredictionKey().Current, static_cast<int32>(CurrentActivationInfo.ActivationMode),
	    bTraversalStarted, bTargetDataSent, Avatar ? *Avatar->GetActorLocation().ToCompactString() : TEXT("None"),
	    Avatar ? *Avatar->GetVelocity().ToCompactString() : TEXT("None"),
	    Movement ? static_cast<uint8>(Movement->MovementMode) : static_cast<uint8>(MOVE_None),
	    Movement ? Movement->CustomMovementMode : 0, *Detail);
	if (bWarning)
	{
		UE_LOG(LogIMGTraversal, Warning, TEXT("%s"), *Context);
	}
	else
	{
		UE_LOG(LogIMGTraversal, Verbose, TEXT("%s"), *Context);
	}
}

bool UIMGGameplayAbility_Traversal::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
    const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (bProcessingTraversalRequest || !ActorInfo || !ActorInfo->AvatarActor.IsValid())
	{
		return false;
	}
	const AIMGCharacter* Character = Cast<AIMGCharacter>(ActorInfo->AvatarActor.Get());
	if (!Character) { return false; }
	if (ActorInfo->IsLocallyControlled())
	{
		const UWorld* World = Character->GetWorld();
		if (!World || (NetworkRequestAvatar == Character &&
		    (PendingAuthorityPredictionKey != 0 || World->GetTimeSeconds() < NextLocalRetryTime)))
		{
			return false;
		}
	}
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags)) { return false; }
	const UIMGTraversalComponent* Component = UIMGTraversalComponent::Get(Character);
	const UIMGHealthComponent* Health = UIMGHealthComponent::FindHealthComponent(Character);
	const UIMGCharacterMovementComponent* Movement = Character ? Character->GetIMGMovementComponent() : nullptr;
	if (!Component || !Component->IsTraversalAvailable() || Component->IsTraversalActive() ||
	    !IsValid(Component->GetMotionWarpingComponent()) || !Component->GetMotionWarpingComponent()->IsRegistered() ||
	    !Movement || !Movement->IsRegistered() || !Movement->IsActive() || Character->GetStance() != EIMGStance::Stand ||
	    !(Movement->IsMovingOnGround() || Movement->IsFalling()) || (Health && Health->IsDeadOrDying()))
	{
		return false;
	}
	if (ActorInfo->IsLocallyControlled())
	{
		// Reject empty attempts before GAS sends its activation RPC; animation selection remains in ActivateAbility.
		FIMGTraversalCandidate Candidate;
		EIMGTraversalFailure Failure;
		const bool bAI = ActorInfo->IsNetAuthority() && Character->GetController() && !Character->IsPlayerControlled();
		const EIMGTraversalRequestSource Source = bAI ? EIMGTraversalRequestSource::AI : RequestSource;
		const FVector Intent = Component->GetMovementIntent(Source);
		const FVector SearchDirection = Source == EIMGTraversalRequestSource::AI ? Intent : FVector::ZeroVector;
		if (!Component->Detect(Candidate, Failure, SearchDirection, Source) ||
		    !Component->CanInitiate(Candidate, Intent))
		{
			return false;
		}
	}
	return true;
}

void UIMGGameplayAbility_Traversal::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
    const FGameplayEventData* TriggerEventData)
{
	TGuardValue<bool> ProcessingRequest(bProcessingTraversalRequest, true);
	bLocalRequest = false;
	bTraversalStarted = false;
	bMontageCompleted = false;
	bTargetDataSent = false;
	bTargetDataCancellationSent = false;
	ActiveObstacleComponents.Reset();
	PreviousObstacleTransforms.Reset();
	LocalCandidate = FIMGTraversalCandidate();
	ClearNetworkTracking();
	NetworkRequestAvatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	if (ActorInfo && ActorInfo->IsLocallyControlled() && !ActorInfo->IsNetAuthority())
	{
		if (UIMGAbilitySystemComponent* IMGASC = Cast<UIMGAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get()))
		{
			NetworkASC = IMGASC;
			PendingAuthoritySpec = Handle;
			PendingAuthorityPredictionKey = ActivationInfo.GetActivationPredictionKey().Current;
			NetworkTerminationHandle = IMGASC->AbilityNetworkTermination.AddUObject(this, &ThisClass::OnNetworkTermination);
		}
	}
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	LogTraversalEvent(TEXT("Activate"), TEXT("LocalPredicted traversal activation"));
	if (!IsActive() || bIsAbilityEnding)
	{
		LogTraversalEvent(TEXT("ActivationAborted"), TEXT("Ability ended during base activation"));
		CancelPendingTargetData();
		return;
	}
	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	UIMGTraversalComponent* Component = UIMGTraversalComponent::Get(GetAvatarActorFromActorInfo());
	if (!ASC || !Component)
	{
		LogTraversalEvent(TEXT("ActivationAborted"), TEXT("Missing ASC or traversal component"), true);
		CancelPendingTargetData();
		return;
	}
	if (ActorInfo->IsNetAuthority())
	{
		TargetDataDelegateHandle =
		    ASC->AbilityTargetDataSetDelegate(Handle, ActivationInfo.GetActivationPredictionKey())
		        .AddUObject(this, &ThisClass::OnTargetDataReady);
		TargetDataCancelledDelegateHandle =
		    ASC->AbilityTargetDataCancelledDelegate(Handle, ActivationInfo.GetActivationPredictionKey())
		        .AddUObject(this, &ThisClass::OnTargetDataCancelled);
		ASC->CallReplicatedTargetDataDelegatesIfSet(Handle, ActivationInfo.GetActivationPredictionKey());
		if (!IsActive() || bIsAbilityEnding || bTraversalStarted)
		{
			return;
		}
	}
	AIMGCharacter* Character = GetIMGCharacterFromActorInfo();
	const bool bServerControlledAI =
	    ActorInfo->IsNetAuthority() && Character && Character->GetController() && !Character->IsPlayerControlled();
	const bool bShouldBuildLocalRequest = ActorInfo->IsLocallyControlled() || bServerControlledAI;
	if (!bShouldBuildLocalRequest)
	{
		LogTraversalEvent(TEXT("AwaitTargetData"), TEXT("Authority is waiting for the remote selection"));
		GetWorld()->GetTimerManager().SetTimer(TargetCheckTimer, this, &ThisClass::OnTargetDataTimedOut,
		    FMath::IsFinite(TargetDataTimeout) ? FMath::Max(TargetDataTimeout, 0.1f) : 2.f, false);
		return;
	}
	bLocalRequest = true;
	EIMGTraversalFailure Failure;
	const EIMGTraversalRequestSource Source = bServerControlledAI ? EIMGTraversalRequestSource::AI : RequestSource;
	const FVector Intent = Component->GetMovementIntent(Source);
	const FVector SearchDirection = Source == EIMGTraversalRequestSource::AI ? Intent : FVector::ZeroVector;
	if (!Component->Detect(LocalCandidate, Failure, SearchDirection, Source))
	{
		LogTraversalEvent(TEXT("LocalDetectionRejected"), UEnum::GetValueAsString(Failure));
		CancelPendingTargetData();
		return;
	}
	if (!Component->CanInitiate(LocalCandidate, Intent))
	{
		LogTraversalEvent(TEXT("LocalInitiationRejected"), FString::Printf(TEXT("Source=%d DistanceXY=%.2f"),
		    static_cast<int32>(Source), LocalCandidate.PlanarDistanceToLedge));
		CancelPendingTargetData();
		return;
	}
	PreSelectTraversalAnimation(LocalCandidate);
	auto IsRequestContextValid = [this, ASC, Component, Character]()
	{
		return IsActive() && !bIsAbilityEnding && CurrentActorInfo && IsValid(ASC) &&
		    CurrentActorInfo->AbilitySystemComponent.Get() == ASC && ASC->GetAvatarActor() == Character &&
		    GetAvatarActorFromActorInfo() == Character && IsValid(Character) && IsValid(Component) &&
		    Component->GetOwner() == Character;
	};
	if (!IsRequestContextValid())
	{
		LogTraversalEvent(TEXT("PreSelectionAborted"), TEXT("Ability ended during PreSelectTraversalAnimation"));
		CancelPendingTargetData();
		return;
	}
	const FIMGTraversalAnimationSelection Selection = SelectTraversalAnimation(LocalCandidate);
	if (!IsRequestContextValid() || !IsSelectionValid(Selection, LocalCandidate, Intent, false))
	{
		LogTraversalEvent(TEXT("LocalSelectionRejected"), FString::Printf(TEXT("Action=%d Montage=%s Start=%.3f Rate=%.3f"),
		    static_cast<int32>(Selection.Action), *GetNameSafe(Selection.Montage.Get()), Selection.StartTime, Selection.PlayRate));
#if ENABLE_DRAW_DEBUG
		DrawAbilityMessage(Character, TEXT("Traversal GA: animation selection rejected"), FColor::Red);
#endif
		CancelPendingTargetData();
		return;
	}
	auto* Request = new FIMGTraversalTargetData();
	Request->Direction = LocalCandidate.ApproachDirection;
	Request->MovementIntent = Intent;
	Request->Source = LocalCandidate.RequestSource;
	Request->Action = Selection.Action;
	Request->Montage = Selection.Montage;
	Request->StartTime = Selection.StartTime;
	Request->PlayRate = Selection.PlayRate;
	if (UE_LOG_ACTIVE(LogIMGTraversal, Verbose))
	{
		LogTraversalEvent(TEXT("LocalRequestPrepared"), FString::Printf(
		    TEXT("Source=%d Action=%d Montage=%s Start=%.3f Rate=%.3f Front=%s Direction=%s Intent=%s"),
		    static_cast<int32>(Source), static_cast<int32>(Selection.Action), *GetNameSafe(Selection.Montage.Get()),
		    Selection.StartTime, Selection.PlayRate, *LocalCandidate.FrontLedge.GetLocation().ToCompactString(),
		    *LocalCandidate.ApproachDirection.ToCompactString(), *Intent.ToCompactString()));
	}
	FGameplayAbilityTargetDataHandle Data;
	Data.Add(Request);
	OnTargetDataReady(Data, FGameplayTag());
}

void UIMGGameplayAbility_Traversal::SendPendingTargetDataCancellation()
{
	if (!bTargetDataSent && !bTargetDataCancellationSent && CurrentActorInfo &&
	    CurrentActorInfo->IsLocallyControlled() && !CurrentActorInfo->IsNetAuthority())
	{
		if (UAbilitySystemComponent* ASC = CurrentActorInfo->AbilitySystemComponent.Get())
		{
			FScopedPredictionWindow PredictionWindow(ASC, true);
			ASC->ServerSetReplicatedTargetDataCancelled(
			    CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey(), ASC->ScopedPredictionKey);
			bTargetDataCancellationSent = true;
			LogTraversalEvent(TEXT("TargetDataCancellationSent"), TEXT("Predicted attempt ended before request submission"));
		}
	}
}

void UIMGGameplayAbility_Traversal::CancelPendingTargetData()
{
	SendPendingTargetDataCancellation();
	if (IsActive())
	{
		K2_EndAbility();
	}
}

void UIMGGameplayAbility_Traversal::OnTargetDataCancelled()
{
	if (!CurrentActorInfo || !CurrentActorInfo->AbilitySystemComponent.IsValid())
	{
		LogTraversalEvent(TEXT("TargetDataCancellationIgnored"), TEXT("ActorInfo or ASC is unavailable"));
		return;
	}
	UAbilitySystemComponent* ASC = CurrentActorInfo->AbilitySystemComponent.Get();
	ASC->ConsumeClientReplicatedTargetData(CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey());
	LogTraversalEvent(TEXT("TargetDataCancellationReceived"),
	    bTraversalStarted ? TEXT("Active authority traversal retained") : TEXT("Release authority waiting for target data"));
	// Only release the server instance while it is waiting; active traversal remains authority-owned.
	if (IsActive() && !bTraversalStarted)
	{
		K2_EndAbility();
	}
}

void UIMGGameplayAbility_Traversal::OnTargetDataReady(
    const FGameplayAbilityTargetDataHandle& Data, FGameplayTag ApplicationTag)
{
	if (!IsActive() || bIsAbilityEnding || bTraversalStarted || !CurrentActorInfo ||
	    !CurrentActorInfo->AbilitySystemComponent.IsValid())
	{
		LogTraversalEvent(TEXT("TargetDataIgnored"), TEXT("Ability inactive/ending/already started, or ActorInfo/ASC unavailable"));
		return;
	}
	TGuardValue<bool> ProcessingRequest(bProcessingTraversalRequest, true);
	UAbilitySystemComponent* ASC = CurrentActorInfo->AbilitySystemComponent.Get();
	FScopedPredictionWindow PredictionWindow(ASC);
	// Keep the request alive if a synchronous callback consumes GAS's replicated-data cache.
	const FGameplayAbilityTargetDataHandle RequestData = Data;
	const FIMGTraversalTargetData* Request =
	    RequestData.Num() == 1 && RequestData.Get(0) && RequestData.Get(0)->GetScriptStruct() == FIMGTraversalTargetData::StaticStruct()
	    ? static_cast<const FIMGTraversalTargetData*>(RequestData.Get(0))
	    : nullptr;
	if (!Request)
	{
		LogTraversalEvent(TEXT("TargetDataRejected"), TEXT("Expected one traversal request"), true);
		CancelPendingTargetData();
		return;
	}
	FIMGTraversalCandidate Candidate;
	FIMGTraversalAnimationSelection Selection;
	const bool bRemoteRequest = CurrentActorInfo->IsNetAuthority() && !bLocalRequest;
	if (!ResolveTraversalRequest(*Request, bRemoteRequest, Candidate, Selection) ||
	    !StartTraversalExecution(Candidate, Selection, Request->MovementIntent, bRemoteRequest))
	{
		CancelPendingTargetData();
		return;
	}
	if (CurrentActorInfo->IsLocallyControlled() && !CurrentActorInfo->IsNetAuthority())
	{
		bTargetDataSent = true;
		ASC->CallServerSetReplicatedTargetData(CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey(),
		    RequestData, ApplicationTag, ASC->ScopedPredictionKey);
		LogTraversalEvent(TEXT("TargetDataSent"), TEXT("Predicted montage started; authority must revalidate the request"));
	}
	ASC->ConsumeClientReplicatedTargetData(CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey());
}

bool UIMGGameplayAbility_Traversal::ResolveTraversalRequest(const FIMGTraversalTargetData& Request, bool bRemoteRequest,
    FIMGTraversalCandidate& OutCandidate, FIMGTraversalAnimationSelection& OutSelection) const
{
	const UIMGTraversalComponent* Component = UIMGTraversalComponent::Get(GetAvatarActorFromActorInfo());
	const AIMGCharacter* Character = GetIMGCharacterFromActorInfo();
	if (!Component || !Character)
	{
		LogTraversalEvent(TEXT("TargetDataRejected"), TEXT("Missing traversal character/component"), true);
		return false;
	}
	LogTraversalEvent(TEXT("TargetDataReady"), FString::Printf(
	    TEXT("Remote=%d Source=%d Action=%d Montage=%s Start=%.3f Rate=%.3f Direction=%s Intent=%s"), bRemoteRequest,
	    static_cast<int32>(Request.Source), static_cast<int32>(Request.Action), *GetNameSafe(Request.Montage.Get()),
	    Request.StartTime, Request.PlayRate, *Request.Direction.ToCompactString(), *Request.MovementIntent.ToCompactString()));
	if (bRemoteRequest)
	{
		EIMGTraversalFailure Failure;
		if (Request.Source != RequestSource)
		{
			LogTraversalEvent(TEXT("ServerRequestRejected"), FString::Printf(TEXT("Source=%d ExpectedSource=%d"),
			    static_cast<int32>(Request.Source), static_cast<int32>(RequestSource)), true);
			return false;
		}
		if (Request.Direction.ContainsNaN() || Request.MovementIntent.ContainsNaN() ||
		    !Request.Direction.IsNormalized() || !FMath::IsNearlyZero(Request.Direction.Z) ||
		    (!Request.MovementIntent.IsZero() &&
		        (!Request.MovementIntent.IsNormalized() || !FMath::IsNearlyZero(Request.MovementIntent.Z))))
		{
			LogTraversalEvent(TEXT("ServerRequestRejected"), TEXT("Invalid planar request direction or movement intent"), true);
			return false;
		}
		if (!Component->Detect(OutCandidate, Failure, Request.Direction, RequestSource))
		{
			LogTraversalEvent(TEXT("ServerDetectionRejected"), FString::Printf(TEXT("%s Direction=%s Intent=%s"),
			    *UEnum::GetValueAsString(Failure), *Request.Direction.ToCompactString(), *Request.MovementIntent.ToCompactString()), true);
			return false;
		}
	}
	else
	{
		OutCandidate = LocalCandidate;
	}
	OutSelection.Action = Request.Action;
	OutSelection.Montage = Request.Montage;
	OutSelection.StartTime = Request.StartTime;
	OutSelection.PlayRate = Request.PlayRate;
	if (!IsSelectionValid(OutSelection, OutCandidate, Request.MovementIntent, bRemoteRequest))
	{
#if ENABLE_DRAW_DEBUG
		DrawAbilityMessage(Character, TEXT("Traversal GA: selection or server allow-list rejected"), FColor::Red);
#endif
		return false;
	}
	return true;
}

bool UIMGGameplayAbility_Traversal::StartTraversalExecution(FIMGTraversalCandidate& Candidate,
    const FIMGTraversalAnimationSelection& Selection, const FVector& MovementIntent, bool bRemoteRequest)
{
	UIMGTraversalComponent* Component = UIMGTraversalComponent::Get(GetAvatarActorFromActorInfo());
	AIMGCharacter* Character = GetIMGCharacterFromActorInfo();
	UAbilitySystemComponent* ASC = CurrentActorInfo->AbilitySystemComponent.Get();
	TArray<FIMGTraversalWarpTarget> WarpTargets;
	if (!PrepareWarpTargets(Candidate, Selection, WarpTargets))
	{
		return false;
	}
	const FTransform PreparedActorTransform = Character->GetActorTransform();
	const UCapsuleComponent* PreparedCapsule = Character->GetCapsuleComponent();
	const float PreparedHalfHeight = PreparedCapsule->GetScaledCapsuleHalfHeight();
	const float PreparedRadius = PreparedCapsule->GetScaledCapsuleRadius();
	const USkeletalMeshComponent* PreparedMesh = Character->GetMesh();
	const FTransform PreparedMeshTransform = PreparedMesh ? PreparedMesh->GetRelativeTransform() : FTransform::Identity;
	const USkeletalMesh* PreparedMeshAsset = PreparedMesh ? PreparedMesh->GetSkeletalMeshAsset() : nullptr;
	const UAnimInstance* PreparedAnimInstance = PreparedMesh ? PreparedMesh->GetAnimInstance() : nullptr;
	const FVector PreparedBaseTranslation = Character->GetBaseTranslationOffset();
	const FQuat PreparedBaseRotation = Character->GetBaseRotationOffset();
	TrackActiveTargets(Candidate, WarpTargets);
	if (!CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo))
	{
		LogTraversalEvent(TEXT("CommitRejected"), TEXT("CommitAbility failed after geometry and animation validation"), true);
#if ENABLE_DRAW_DEBUG
		DrawAbilityMessage(Character, TEXT("Traversal GA: CommitAbility failed"), FColor::Red);
#endif
		return false;
	}
	if (!IsActive() || bIsAbilityEnding || !CurrentActorInfo || CurrentActorInfo->AbilitySystemComponent.Get() != ASC ||
	    !IsValid(ASC) || ASC->GetAvatarActor() != Character || GetAvatarActorFromActorInfo() != Character ||
	    !IsValid(Component) || !IsSelectionValid(Selection, Candidate, MovementIntent, bRemoteRequest))
	{
		LogTraversalEvent(TEXT("PostCommitRejected"), TEXT("Ability, avatar, component or selection changed during commit"), true);
		return false;
	}
	const UCapsuleComponent* CapsuleAfterCommit = Character->GetCapsuleComponent();
	const USkeletalMeshComponent* MeshAfterCommit = Character->GetMesh();
	if (!Character->GetActorTransform().Equals(PreparedActorTransform) || CapsuleAfterCommit != PreparedCapsule ||
	    !IsValid(CapsuleAfterCommit) || CapsuleAfterCommit->GetScaledCapsuleHalfHeight() != PreparedHalfHeight ||
	    CapsuleAfterCommit->GetScaledCapsuleRadius() != PreparedRadius || MeshAfterCommit != PreparedMesh ||
	    (MeshAfterCommit && (!IsValid(MeshAfterCommit) ||
	        !MeshAfterCommit->GetRelativeTransform().Equals(PreparedMeshTransform) ||
	        MeshAfterCommit->GetSkeletalMeshAsset() != PreparedMeshAsset ||
	        MeshAfterCommit->GetAnimInstance() != PreparedAnimInstance)) ||
	    !Character->GetBaseTranslationOffset().Equals(PreparedBaseTranslation) ||
	    !Character->GetBaseRotationOffset().Equals(PreparedBaseRotation))
	{
		LogTraversalEvent(TEXT("PostCommitPlacementChanged"), TEXT("Commit changed the placement entry pose or capsule/mesh"), true);
		return false;
	}
	for (int32 Index = 0; Index < ActiveObstacleComponents.Num(); ++Index)
	{
		const UPrimitiveComponent* Surface = ActiveObstacleComponents[Index].Get();
		if (!FIMGTraversalDetector::CanUseSurface(Surface, Component->GetDetectorSettings(), Candidate.RequestSource))
		{
			LogTraversalEvent(TEXT("PostCommitSurfaceRejected"), GetNameSafe(Surface), true);
			return false;
		}
		if (!Surface->GetComponentTransform().Equals(PreviousObstacleTransforms[Index]))
		{
			LogTraversalEvent(TEXT("PostCommitPlacementChanged"), TEXT("Commit moved a placement surface"), true);
			return false;
		}
	}
	LocalCandidate = Candidate;
	ActiveComponent = Component;
	bTraversalStarted = true;
	RegisterCorrectionFilter();
	if (!Component->BeginTraversal(Candidate, Selection, WarpTargets, CurrentSpecHandle,
	        CurrentActivationInfo.GetActivationPredictionKey().Current) || !IsTraversalExecutionValid() ||
	    CurrentActorInfo->AbilitySystemComponent.Get() != ASC)
	{
		LogTraversalEvent(TEXT("TraversalStartAborted"), TEXT("Ability/component invalidated or Flying was not retained"), true);
		return false;
	}
	TrackedTargetsRevision = Component->GetTraversalTargetsRevision();
	GetWorld()->GetTimerManager().SetTimer(TargetCheckTimer, this, &ThisClass::CheckActiveTarget,
	    FMath::Max(Component->GetTargetValidationInterval(), 0.01f), true);
	UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
	    this, NAME_None, Selection.Montage, Selection.PlayRate, NAME_None, true, 1.f, Selection.StartTime, true);
	MontageTask->OnCompleted.AddDynamic(this, &ThisClass::OnMontageCompleted);
	MontageTask->OnInterrupted.AddDynamic(this, &ThisClass::OnMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(this, &ThisClass::OnMontageInterrupted);
	MontageTask->ReadyForActivation();
	if (!IsTraversalExecutionValid() || CurrentActorInfo->AbilitySystemComponent.Get() != ASC)
	{
		LogTraversalEvent(TEXT("MontageStartAborted"), TEXT("Montage task ended synchronously or traversal was invalidated"));
		return false;
	}
	if (CurrentActorInfo->IsNetAuthority())
	{
		uint8 MontagePlayInstanceId = 0;
		UIMGAbilitySystemComponent* IMGASC = Cast<UIMGAbilitySystemComponent>(ASC);
		if (!IMGASC || !IMGASC->GetActiveReplicatedMontagePlayInstanceId(Selection.Montage, MontagePlayInstanceId) ||
		    !Component->PublishTraversalState(MontagePlayInstanceId))
		{
			LogTraversalEvent(TEXT("SnapshotPublishFailed"), TEXT("No active replicated montage instance or snapshot publication failed"), true);
			return false;
		}
	}
	LogTraversalEvent(TEXT("TraversalStarted"), FString::Printf(TEXT("Action=%d Montage=%s Start=%.3f Rate=%.3f Targets=%d"),
	    static_cast<int32>(Selection.Action), *GetNameSafe(Selection.Montage.Get()), Selection.StartTime, Selection.PlayRate,
	    WarpTargets.Num()));
#if ENABLE_DRAW_DEBUG
	IMGTraversalDebug::SuccessfulTraversal(Character, Candidate, Selection);
#endif
	OnTraversalStarted(Candidate, Selection.Action);
	if (!IsTraversalExecutionValid() || CurrentActorInfo->AbilitySystemComponent.Get() != ASC)
	{
		LogTraversalEvent(TEXT("StartedHookAborted"), TEXT("Traversal invalidated during OnTraversalStarted"));
		return false;
	}
	return true;
}

bool UIMGGameplayAbility_Traversal::IsTraversalExecutionValid() const
{
	const UIMGTraversalComponent* Component = ActiveComponent.Get();
	const AIMGCharacter* Character = GetIMGCharacterFromActorInfo();
	const UAbilitySystemComponent* ASC = CurrentActorInfo ? CurrentActorInfo->AbilitySystemComponent.Get() : nullptr;
	const UIMGCharacterMovementComponent* Movement = Character ? Character->GetIMGMovementComponent() : nullptr;
	const UMotionWarpingComponent* Warping = Component ? Component->GetMotionWarpingComponent() : nullptr;
	const UIMGHealthComponent* Health = UIMGHealthComponent::FindHealthComponent(Character);
	return IsActive() && !bIsAbilityEnding && bTraversalStarted && Component && Component->IsTraversalAvailable() &&
	    Component->IsTraversalActive() && IsValid(Character) && !Character->IsActorBeingDestroyed() &&
	    Component->GetOwner() == Character && IsValid(ASC) && ASC->GetAvatarActor() == Character &&
	    IsValid(Movement) && Movement->IsRegistered() && Movement->IsActive() && Movement->IsFlying() &&
	    IsValid(Warping) && Warping->IsRegistered() &&
	    (!Health || !Health->IsDeadOrDying());
}

void UIMGGameplayAbility_Traversal::TrackActiveTargets(
    const FIMGTraversalCandidate& Candidate, const TArray<FIMGTraversalWarpTarget>& WarpTargets)
{
	ActiveObstacleComponents.Reset();
	PreviousObstacleTransforms.Reset();
	for (UPrimitiveComponent* ObstacleComponent : Candidate.ObstacleComponents)
	{
		if (!ActiveObstacleComponents.Contains(ObstacleComponent))
		{
			ActiveObstacleComponents.Add(ObstacleComponent);
			PreviousObstacleTransforms.Add(IsValid(ObstacleComponent)
			    ? ObstacleComponent->GetComponentTransform() : FTransform::Identity);
		}
	}
	if (ActiveObstacleComponents.IsEmpty() && IsValid(Candidate.HitComponent))
	{
		ActiveObstacleComponents.Add(Candidate.HitComponent);
		PreviousObstacleTransforms.Add(Candidate.HitComponent->GetComponentTransform());
	}
	for (const FIMGTraversalWarpTarget& Target : WarpTargets)
	{
		if (!ActiveObstacleComponents.Contains(Target.Component.Get()))
		{
			ActiveObstacleComponents.Add(Target.Component.Get());
			PreviousObstacleTransforms.Add(IsValid(Target.Component)
			    ? Target.Component->GetComponentTransform() : FTransform::Identity);
		}
	}
	PreviousTargetCheckTime = GetWorld()->GetTimeSeconds();
}

void UIMGGameplayAbility_Traversal::RegisterCorrectionFilter()
{
	ClearCorrectionFilter();
	if (CurrentActorInfo->IsLocallyControlled() && !CurrentActorInfo->IsNetAuthority())
	{
		UIMGCharacterMovementComponent* Movement = GetIMGCharacterFromActorInfo()->GetIMGMovementComponent();
		if (const FNetworkPredictionData_Client_Character* ClientData = Movement->GetPredictionData_Client_Character())
		{
			for (const FSavedMovePtr& Move : ClientData->SavedMoves)
			{
				PreTraversalMoveTimeStamps.Add(Move->TimeStamp);
			}
		}
		FilteredMovement = Movement;
		CorrectionFilterStartTime = GetWorld()->GetTimeSeconds();
		CorrectionFilterHandle = Movement->ClientCorrectionFilter.AddUObject(this, &ThisClass::FilterClientCorrection);
	}
}

bool UIMGGameplayAbility_Traversal::IsSelectionValid(const FIMGTraversalAnimationSelection& Selection,
    const FIMGTraversalCandidate& Candidate, const FVector& MovementIntent, bool bRemoteRequest) const
{
	const UIMGTraversalComponent* Traversal = UIMGTraversalComponent::Get(GetAvatarActorFromActorInfo());
	const AIMGCharacter* Character = GetIMGCharacterFromActorInfo();
	const UIMGCharacterMovementComponent* Movement = Character ? Character->GetIMGMovementComponent() : nullptr;
	const UIMGHealthComponent* Health = UIMGHealthComponent::FindHealthComponent(Character);
	auto Reject = [this, bRemoteRequest, &Selection](const FString& Reason)
	{
		LogTraversalEvent(TEXT("SelectionRejected"), FString::Printf(TEXT("Remote=%d Action=%d Montage=%s Start=%.3f Rate=%.3f | %s"),
		    bRemoteRequest, static_cast<int32>(Selection.Action), *GetNameSafe(Selection.Montage.Get()),
		    Selection.StartTime, Selection.PlayRate, *Reason), bRemoteRequest);
		return false;
	};
	if (!Traversal || !Traversal->IsTraversalAvailable() || Traversal->IsTraversalActive() ||
	    !IsValid(Traversal->GetMotionWarpingComponent()) || !Traversal->GetMotionWarpingComponent()->IsRegistered() ||
	    !Movement || !Movement->IsRegistered() || !Movement->IsActive() || Character->GetStance() != EIMGStance::Stand ||
	    !(Movement->IsMovingOnGround() || Movement->IsFalling()) || (Health && Health->IsDeadOrDying()))
	{
		return Reject(TEXT("Traversal/warping not ready, traversal already active, stance/movement disallowed or character dying"));
	}
	for (const UPrimitiveComponent* Obstacle : Candidate.ObstacleComponents)
	{
		if (!FIMGTraversalDetector::CanUseSurface(Obstacle, Traversal->GetDetectorSettings(), Candidate.RequestSource))
		{
			return Reject(FString::Printf(TEXT("Obstacle surface is no longer usable: %s"), *GetNameSafe(Obstacle)));
		}
	}
	const UWorld* World = GetWorld();
	const double CandidateAge = World ? World->GetTimeSeconds() - Candidate.DetectionTime : BIG_NUMBER;
	const bool bCandidateFresh =
	    Traversal && World && CandidateAge >= 0.0 && CandidateAge <= Traversal->GetDetectorSettings().MaxCandidateAge;
#if ENABLE_DRAW_DEBUG
	if (!bCandidateFresh && Character && IMGTraversalDebug::IsEnabled(1) &&
	    IMGTraversalDebug::IsStageEnabled(IMGTraversalDebug::EStage::Execution))
	{
		IMGTraversalDebug::String(Character->GetWorld(), Character->GetActorLocation() + FVector::UpVector * 160.f,
		    FString::Printf(TEXT("Traversal Validate: stale candidate %.3fs"), CandidateAge), FColor::Red, 1);
	}
#endif
	const bool bMatchesRemoteWhitelist =
	    AllowedMontages.ContainsByPredicate([&Selection](const FIMGTraversalAllowedMontage& Allowed)
	        { return Allowed.Action == Selection.Action && Allowed.Montage == Selection.Montage; });
	const bool bAllowedRemoteSelection = !bRemoteRequest || AllowedMontages.IsEmpty() || bMatchesRemoteWhitelist;
#if ENABLE_DRAW_DEBUG
	if (bRemoteRequest && !bAllowedRemoteSelection)
	{
		DrawAbilityMessage(GetIMGCharacterFromActorInfo(),
		    TEXT("Traversal Validate: remote montage is not in AllowedMontages"), FColor::Red);
	}
#endif
	const FIMGTraversalAnimationSettings* Settings =
	    Traversal ? Traversal->FindAnimationSettings(Selection.Action, Selection.Montage) : nullptr;
	if (!Settings)
	{
		return Reject(TEXT("No execution settings for the selected action/montage"));
	}
	if (!Traversal->CanInitiate(Candidate, MovementIntent, bRemoteRequest))
	{
		return Reject(FString::Printf(TEXT("Initiation constraints failed: Source=%d DistanceXY=%.2f Grounded=%d Direction=%s Intent=%s"),
		    static_cast<int32>(Candidate.RequestSource), Candidate.PlanarDistanceToLedge, Movement->IsMovingOnGround(),
		    *Candidate.ApproachDirection.ToCompactString(), *MovementIntent.ToCompactString()));
	}
	if (!FMath::IsFinite(Traversal->GetTargetValidationInterval()) || Traversal->GetTargetValidationInterval() < 0.01f ||
	    !FMath::IsFinite(Settings->MinPlayRate) || !FMath::IsFinite(Settings->MaxPlayRate) || Settings->MinPlayRate <= 0.f ||
	    Settings->MaxPlayRate < Settings->MinPlayRate || !FMath::IsFinite(Settings->MinVerticalClearance) ||
	    Settings->MinVerticalClearance < 0.f || !FMath::IsFinite(Settings->MinHorizontalClearance) ||
	    Settings->MinHorizontalClearance < 0.f)
	{
		return Reject(TEXT("Invalid validation interval, play-rate range or clearance configuration"));
	}
	if (!(Candidate.VerticalClearance >= Settings->MinVerticalClearance) ||
	    !(Candidate.HorizontalClearance >= Settings->MinHorizontalClearance))
	{
		return Reject(FString::Printf(TEXT("Clearance failed: Vertical=%.2f Min=%.2f Horizontal=%.2f Min=%.2f"),
		    Candidate.VerticalClearance, Settings->MinVerticalClearance, Candidate.HorizontalClearance, Settings->MinHorizontalClearance));
	}
	if (!bCandidateFresh)
	{
		return Reject(FString::Printf(TEXT("Candidate age=%.3f Max=%.3f"), CandidateAge, Traversal->GetDetectorSettings().MaxCandidateAge));
	}
	if (!Candidate.bHasFrontLedge || !IsValid(Candidate.HitComponent) || !Candidate.Supports(Selection.Action))
	{
		return Reject(TEXT("Candidate has no valid front ledge/hit component or does not support the selected action"));
	}
	if (!IsValid(Selection.Montage) || !FMath::IsFinite(Selection.StartTime) || Selection.StartTime < 0.f ||
	    !(Selection.StartTime < Selection.Montage->GetPlayLength()) || !FMath::IsFinite(Selection.PlayRate) ||
	    Selection.PlayRate < Settings->MinPlayRate || Selection.PlayRate > Settings->MaxPlayRate ||
	    !FMath::IsFinite(Selection.Montage->RateScale) || Selection.Montage->RateScale <= 0.f ||
	    !Selection.Montage->HasRootMotion())
	{
		return Reject(FString::Printf(TEXT("Invalid montage/timing: PlayRateMin=%.3f Max=%.3f"), Settings->MinPlayRate, Settings->MaxPlayRate));
	}
	if (!bAllowedRemoteSelection)
	{
		return Reject(TEXT("Remote action/montage pair is not in AllowedMontages"));
	}
	return true;
}

bool UIMGGameplayAbility_Traversal::PrepareWarpTargets(FIMGTraversalCandidate& Candidate,
    const FIMGTraversalAnimationSelection& Selection, TArray<FIMGTraversalWarpTarget>& OutTargets) const
{
	OutTargets.Reset();
	AIMGCharacter* Character = GetIMGCharacterFromActorInfo();
	const UIMGTraversalComponent* Traversal = UIMGTraversalComponent::Get(Character);
	const FIMGTraversalAnimationSettings* Settings =
	    Traversal ? Traversal->FindAnimationSettings(Selection.Action, Selection.Montage) : nullptr;
	if (!Character || !Character->GetWorld() || !Character->GetCapsuleComponent() ||
	    !Character->GetIMGMovementComponent() || !Settings)
	{
		LogTraversalEvent(TEXT("WarpTargetsRejected"), TEXT("Missing character, world, capsule or execution settings"), true);
		return false;
	}
	TArray<FIMGTraversalWarpWindow> Windows;
	Traversal->GetWarpWindows(Selection.Montage, Windows);
	return IMGTraversalPlacement::PrepareTargets(*Character, *Settings, Traversal->GetDetectorSettings(), Windows,
	    Candidate, Selection, OutTargets, UE_LOG_ACTIVE(LogIMGTraversal, Verbose),
	    [this](const TCHAR* Event, const FString& Detail, bool bWarning)
	    {
		    LogTraversalEvent(Event, Detail, bWarning);
	    });
}

void UIMGGameplayAbility_Traversal::OnTargetDataTimedOut()
{
	if (IsActive() && !bIsAbilityEnding && !bTraversalStarted)
	{
		LogTraversalEvent(TEXT("TargetDataTimedOut"), TEXT("Authority released an attempt still waiting for target data"), true);
		K2_EndAbility();
	}
}

void UIMGGameplayAbility_Traversal::FilterClientCorrection(
    const FCharacterMoveResponseDataContainer& Response, bool& bDiscardCorrection)
{
	const UIMGCharacterMovementComponent* Movement = FilteredMovement.Get();
	const UWorld* World = GetWorld();
	if (!IsTraversalExecutionValid() || !CurrentActorInfo->IsLocallyControlled() || CurrentActorInfo->IsNetAuthority() ||
	    !Movement || Movement != GetIMGCharacterFromActorInfo()->GetIMGMovementComponent() || !World)
	{
		return;
	}
	// A recorded timestamp cannot identify a move after another timestamp-reset cycle. Fail open then.
	const double Age = World->GetTimeSeconds() - CorrectionFilterStartTime;
	if (!FMath::IsFinite(Movement->MinTimeBetweenTimeStampResets) || Age < 0.0 ||
	    Age >= Movement->MinTimeBetweenTimeStampResets * 0.5 ||
	    !PreTraversalMoveTimeStamps.Contains(Response.ClientAdjustment.TimeStamp))
	{
		return;
	}
	bDiscardCorrection = true;
	LogTraversalEvent(TEXT("PreTraversalCorrectionDiscarded"), FString::Printf(
	    TEXT("Move=%.6f ServerMode=%u; native ACK retained, current traversal execution retained"),
	    Response.ClientAdjustment.TimeStamp, Response.ClientAdjustment.MovementMode));
}

void UIMGGameplayAbility_Traversal::ClearCorrectionFilter()
{
	if (UIMGCharacterMovementComponent* Movement = FilteredMovement.Get())
	{
		Movement->ClientCorrectionFilter.Remove(CorrectionFilterHandle);
	}
	CorrectionFilterHandle.Reset();
	FilteredMovement.Reset();
	PreTraversalMoveTimeStamps.Reset();
}

void UIMGGameplayAbility_Traversal::DelayLocalRetry()
{
	if (CurrentActorInfo && CurrentActorInfo->IsLocallyControlled())
	{
		if (const UWorld* World = GetWorld())
		{
			const float Delay = FMath::IsFinite(RetryInterval) ? FMath::Max(RetryInterval, 0.01f) : 0.25f;
			NextLocalRetryTime = World->GetTimeSeconds() + Delay;
		}
	}
}

void UIMGGameplayAbility_Traversal::OnNetworkTermination(
    FGameplayAbilitySpecHandle Handle, int16 PredictionKey, bool bRejected)
{
	if (!NetworkASC.IsValid() || NetworkASC->GetAvatarActor() != NetworkRequestAvatar.Get() ||
	    Handle != PendingAuthoritySpec || PredictionKey != PendingAuthorityPredictionKey || PredictionKey == 0)
	{
		return;
	}
	if (UIMGTraversalComponent* Component = UIMGTraversalComponent::Get(NetworkRequestAvatar.Get()))
	{
		Component->RetirePredictedReplay(Handle, PredictionKey);
	}
	PendingAuthorityPredictionKey = 0;
	PendingAuthoritySpec = FGameplayAbilitySpecHandle();
	NetworkASC->AbilityNetworkTermination.Remove(NetworkTerminationHandle);
	NetworkTerminationHandle.Reset();
	NetworkASC.Reset();
	// Local EndAbility starts the retry interval; a late authority result only releases its gate.
	LogTraversalEvent(TEXT("AuthorityAttemptReleased"), bRejected ? TEXT("Activation rejected") : TEXT("Authority instance ended"));
}

void UIMGGameplayAbility_Traversal::ClearNetworkTracking()
{
	if (PendingAuthorityPredictionKey != 0)
	{
		if (UIMGTraversalComponent* Component = UIMGTraversalComponent::Get(NetworkRequestAvatar.Get()))
		{
			Component->RetirePredictedReplay(PendingAuthoritySpec, PendingAuthorityPredictionKey);
		}
	}
	ClearCorrectionFilter();
	if (UIMGAbilitySystemComponent* ASC = NetworkASC.Get())
	{
		ASC->AbilityNetworkTermination.Remove(NetworkTerminationHandle);
	}
	NetworkTerminationHandle.Reset();
	NetworkASC.Reset();
	NetworkRequestAvatar.Reset();
	PendingAuthoritySpec = FGameplayAbilitySpecHandle();
	PendingAuthorityPredictionKey = 0;
	NextLocalRetryTime = 0.0;
}

void UIMGGameplayAbility_Traversal::OnRemoveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	if (IsActive()) { EndAbility(CurrentSpecHandle, ActorInfo, CurrentActivationInfo, true, true); }
	ClearNetworkTracking();
	Super::OnRemoveAbility(ActorInfo, Spec);
}

void UIMGGameplayAbility_Traversal::OnPawnAvatarSet()
{
	const AIMGCharacter* PreviousCharacter = ActiveComponent.IsValid()
	    ? Cast<AIMGCharacter>(ActiveComponent->GetOwner()) : nullptr;
	const TWeakObjectPtr<UAnimMontage> PreviousMontage = ActiveComponent.IsValid()
	    ? ActiveComponent->GetActiveTraversalState().Montage.Get() : nullptr;
	const TWeakObjectPtr<UAnimInstance> PreviousAnim = PreviousCharacter && PreviousCharacter->GetMesh()
	    ? PreviousCharacter->GetMesh()->GetAnimInstance() : nullptr;
	const FAnimMontageInstance* PreviousInstance = PreviousAnim.IsValid()
	    ? PreviousAnim->GetActiveInstanceForMontage(PreviousMontage.Get()) : nullptr;
	const int32 PreviousInstanceId = PreviousInstance ? PreviousInstance->GetInstanceID() : INDEX_NONE;
	if (IsActive() && CurrentActorInfo)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
	}
	// ActorInfo already points at the new avatar, so the ability task may no longer find the old mesh.
	if (PreviousCharacter != GetAvatarActorFromActorInfo() && PreviousAnim.IsValid() && PreviousMontage.IsValid())
	{
		const FAnimMontageInstance* Instance = PreviousAnim->GetActiveInstanceForMontage(PreviousMontage.Get());
		if (Instance && Instance->GetInstanceID() == PreviousInstanceId)
		{
			PreviousAnim->Montage_Stop(0.f, PreviousMontage.Get());
		}
	}
	ClearNetworkTracking();
	Super::OnPawnAvatarSet();
}

void UIMGGameplayAbility_Traversal::CheckActiveTarget()
{
	UIMGTraversalComponent* Component = ActiveComponent.Get();
	const UWorld* World = GetWorld();
	const AIMGCharacter* Character = GetIMGCharacterFromActorInfo();
	const UIMGHealthComponent* Health = UIMGHealthComponent::FindHealthComponent(Character);
	if (Health && Health->IsDeadOrDying())
	{
		LogTraversalEvent(TEXT("TraversalAborted"), TEXT("Character died"), true);
#if ENABLE_DRAW_DEBUG
		DrawAbilityMessage(Character, TEXT("Traversal Abort: character died"), FColor::Red);
#endif
		K2_EndAbility();
		return;
	}
	if (IsTraversalExecutionValid() && Component &&
	    TrackedTargetsRevision != Component->GetTraversalTargetsRevision())
	{
		const FIMGTraversalRepState& State = Component->GetActiveTraversalState();
		LocalCandidate.ObstacleComponents = State.ObstacleComponents;
		TrackActiveTargets(LocalCandidate, State.WarpTargets);
		TrackedTargetsRevision = Component->GetTraversalTargetsRevision();
	}
	if (!IsTraversalExecutionValid() || !World || ActiveObstacleComponents.IsEmpty() ||
	    ActiveObstacleComponents.Num() != PreviousObstacleTransforms.Num())
	{
		const UIMGCharacterMovementComponent* Movement = Character ? Character->GetIMGMovementComponent() : nullptr;
		LogTraversalEvent(TEXT("TraversalAborted"), FString::Printf(
		    TEXT("Execution binding, Flying or obstacle set lost: Mode=%u Custom=%u"),
		    Movement ? static_cast<uint8>(Movement->MovementMode) : static_cast<uint8>(MOVE_None),
		    Movement ? Movement->CustomMovementMode : 0), true);
#if ENABLE_DRAW_DEBUG
		DrawAbilityMessage(
		    GetAvatarActorFromActorInfo(), TEXT("Traversal Abort: execution binding or obstacle set lost"), FColor::Red);
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
		const float AngularSpeed =
		    FMath::RadiansToDegrees(CurrentTransform.GetRotation().AngularDistance(PreviousTransform.GetRotation())) /
		    DeltaTime;
		return CurrentTransform.ContainsNaN() || !FMath::IsFinite(Speed) || !FMath::IsFinite(AngularSpeed) ||
		    (Component.IsSimulatingPhysics() && !Settings.bAllowSimulatingPhysics) || Speed > Settings.MaxMovingSpeed ||
		    AngularSpeed > Settings.MaxAngularSpeedDegrees;
	};
	for (int32 Index = 0; Index < ActiveObstacleComponents.Num(); ++Index)
	{
		UPrimitiveComponent* Target = ActiveObstacleComponents[Index].Get();
		if (!FIMGTraversalDetector::CanUseSurface(Target, Settings, LocalCandidate.RequestSource))
		{
			LogTraversalEvent(TEXT("TraversalAborted"), FString::Printf(TEXT("Obstacle lost or surface policy changed: %s"),
			    *GetNameSafe(Target)), true);
#if ENABLE_DRAW_DEBUG
			DrawAbilityMessage(
			    GetAvatarActorFromActorInfo(), TEXT("Traversal Abort: obstacle component lost"), FColor::Red);
#endif
			K2_EndAbility();
			return;
		}
		const FTransform Current = Target->GetComponentTransform();
		if (IsMovingTooFast(*Target, Current, PreviousObstacleTransforms[Index]))
		{
			const float Speed = FVector::Dist(Current.GetLocation(), PreviousObstacleTransforms[Index].GetLocation()) / DeltaTime;
			const float AngularSpeed = FMath::RadiansToDegrees(
			    Current.GetRotation().AngularDistance(PreviousObstacleTransforms[Index].GetRotation())) / DeltaTime;
			LogTraversalEvent(TEXT("TraversalAborted"), FString::Printf(
			    TEXT("Obstacle=%s invalid transform, forbidden physics or excessive motion: Speed=%.2f Max=%.2f AngularSpeed=%.2f Max=%.2f SimPhysics=%d AllowPhysics=%d"),
			    *GetNameSafe(Target), Speed, Settings.MaxMovingSpeed, AngularSpeed, Settings.MaxAngularSpeedDegrees,
			    Target->IsSimulatingPhysics(), Settings.bAllowSimulatingPhysics), true);
#if ENABLE_DRAW_DEBUG
			DrawAbilityMessage(
			    GetAvatarActorFromActorInfo(), TEXT("Traversal Abort: obstacle moved too fast"), FColor::Red);
			if (IMGTraversalDebug::IsEnabled(3) && IMGTraversalDebug::IsStageEnabled(IMGTraversalDebug::EStage::Execution))
			{
				IMGTraversalDebug::String(World, Target->GetComponentLocation() + FVector::UpVector * 18.f,
				    FString::Printf(TEXT("Abort %s: V %.0f/%.0f  W %.0f/%.0f"), *Target->GetName(), Speed,
				        Settings.MaxMovingSpeed, AngularSpeed, Settings.MaxAngularSpeedDegrees),
				    FColor::Red, 3);
			}
#endif
			K2_EndAbility();
			return;
		}
		PreviousObstacleTransforms[Index] = Current;
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
	LogTraversalEvent(TEXT("MontageInterruptedOrCancelled"),
	    TEXT("Montage interrupted, task cancelled or montage startup failed"));
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
	if (!IsEndAbilityValid(Handle, ActorInfo))
	{
		return;
	}
	if (ScopeLockCount > 0)
	{
		WaitingToExecute.Add(FPostLockDelegate::CreateUObject(this, &ThisClass::EndAbility,
		    Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled));
		return;
	}
	// The owner may already have ended locally and still needs this terminal result to release its next attempt.
	bReplicateEndAbility |= ActorInfo && ActorInfo->IsNetAuthority() && !ActorInfo->IsLocallyControlled() &&
	    ActivationInfo.GetActivationPredictionKey().IsValidKey();
	LogTraversalEvent(TEXT("End"), FString::Printf(TEXT("Completed=%d Cancelled=%d RemoteEnded=%d ReplicateEnd=%d"),
	    bMontageCompleted && !bWasCancelled, bWasCancelled, RemoteInstanceEnded, bReplicateEndAbility));
	if (ActorInfo && !ActorInfo->IsNetAuthority() && bTargetDataSent && RemoteInstanceEnded && !bMontageCompleted)
	{
		LogTraversalEvent(TEXT("AuthorityEndedPredictedTraversal"),
		    TEXT("Remote instance ended before local completion; correlate authority logs by Spec and PredKey"), true);
	}
	// Cleanup and Blueprint notifications can synchronously request another end.
	{
		TGuardValue<bool> EndingGuard(bIsAbilityEnding, true);
		ClearCorrectionFilter();
		DelayLocalRetry();
		SendPendingTargetDataCancellation();
#if ENABLE_DRAW_DEBUG
		IMGTraversalDebug::DiscardTraversalSnapshot(GetAvatarActorFromActorInfo());
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
		const bool bNotifyTraversalEnded = bTraversalStarted;
		const bool bNotifyEndedHook = bNotifyTraversalEnded && ActiveComponent.IsValid() &&
		    ActiveComponent->GetOwner() == GetAvatarActorFromActorInfo();
		const bool bCompleted = bMontageCompleted && !bWasCancelled;
		bTraversalStarted = false;
		if (bNotifyTraversalEnded)
		{
			if (UIMGTraversalComponent* Component = ActiveComponent.Get())
			{
				Component->EndTraversal();
			}
		}
		bMontageCompleted = false;
		bLocalRequest = false;
		ActiveComponent.Reset();
		ActiveObstacleComponents.Reset();
		PreviousObstacleTransforms.Reset();
		TrackedTargetsRevision = 0;
		LocalCandidate = FIMGTraversalCandidate();
		if (bNotifyEndedHook)
		{
			OnTraversalEnded(bCompleted);
		}
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
