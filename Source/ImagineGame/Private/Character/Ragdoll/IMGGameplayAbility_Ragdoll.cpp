#include "Character/Ragdoll/IMGGameplayAbility_Ragdoll.h"

#include "AbilitySystem/IMGAbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimMontage.h"
#include "Character/IMGCharacterMovementComponent.h"
#include "Character/IMGHealthComponent.h"
#include "Character/Ragdoll/IMGRagdollComponent.h"
#include "Engine/PackageMapClient.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameplayPrediction.h"
#include "IMGGameplayTags.h"
#include "IMGLogChannels.h"
#include "Templates/UnrealTemplate.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGGameplayAbility_Ragdoll)

bool FIMGRagdollRecoveryTargetData::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	bOutSuccess = Map != nullptr;
	if (!Map)
	{
		return false;
	}
	Ar << Session << Attempt << ImpactRevision << BodyReferenceSequence << SampleServerTime << StableSeconds;
	bool bLinearSuccess = false;
	bool bAngularSuccess = false;
	LinearVelocity.NetSerialize(Ar, Map, bLinearSuccess);
	AngularVelocityRadians.NetSerialize(Ar, Map, bAngularSuccess);
	UObject* MontageObject = Selection.Montage.Get();
	bOutSuccess &= Map->SerializeObject(Ar, UAnimMontage::StaticClass(), MontageObject);
	if (Ar.IsLoading())
	{
		Selection.Montage = Cast<UAnimMontage>(MontageObject);
	}
	Ar << Selection.StartTime << Selection.PlayRate;
	bOutSuccess &= bLinearSuccess && bAngularSuccess && !Ar.IsError();
	return true;
}

UIMGGameplayAbility_Ragdoll::UIMGGameplayAbility_Ragdoll(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;
	ActivationPolicy = EIMGAbilityActivationPolicy::OnInputTriggered;
	ActivationGroup = EIMGAbilityActivationGroup::Independent;
	bServerRespectsRemoteAbilityCancellation = false;
	SetAssetTags(FGameplayTagContainer(IMGGameplayTags::Ability_Type_Action_Ragdoll));
	ActivationOwnedTags.AddTag(IMGGameplayTags::Status_Ragdoll);
	ActivationOwnedTags.AddTag(TAG_Gameplay_AbilityInputBlocked);
	ActivationBlockedTags.AddTag(IMGGameplayTags::Status_Ragdoll);
	ActivationBlockedTags.AddTag(IMGGameplayTags::Status_Death);
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		FAbilityTriggerData Trigger;
		Trigger.TriggerTag = IMGGameplayTags::GameplayEvent_Ragdoll;
		Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
		AbilityTriggers.Add(Trigger);
	}
}

bool UIMGGameplayAbility_Ragdoll::AreSettingsValid() const
{
	return FMath::IsFinite(MinimumRagdollTime) && MinimumRagdollTime >= 0.f &&
		FMath::IsFinite(RequiredStableTime) && RequiredStableTime > 0.f &&
		FMath::IsFinite(MaxStableLinearSpeed) && MaxStableLinearSpeed >= 0.f &&
		FMath::IsFinite(MaxStableAngularSpeedRadians) && MaxStableAngularSpeedRadians >= 0.f &&
		FMath::IsFinite(CheckInterval) && CheckInterval > 0.f &&
		FMath::IsFinite(SelectionRetryInterval) && SelectionRetryInterval > 0.f &&
		FMath::IsFinite(MaxRagdollTime) && MaxRagdollTime >= MinimumRagdollTime + RequiredStableTime &&
		FMath::IsFinite(RecoveryStartTimeout) && RecoveryStartTimeout > 0.f && !AdditionalVelocity.ContainsNaN();
}

bool UIMGGameplayAbility_Ragdoll::ShouldAbilityRespondToEvent(
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayEventData* Payload) const
{
	return Payload && Payload->EventTag == IMGGameplayTags::GameplayEvent_Ragdoll &&
		Super::ShouldAbilityRespondToEvent(ActorInfo, Payload);
}

bool UIMGGameplayAbility_Ragdoll::CanActivateAbility(
	FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (bProcessingAction || !AreSettingsValid() || !ActorInfo || !ActorInfo->AvatarActor.IsValid() ||
		!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}
	const ACharacter* Character = Cast<ACharacter>(ActorInfo->AvatarActor.Get());
	const UIMGRagdollComponent* Body = UIMGRagdollComponent::FindRagdollComponent(Character);
	const UIMGHealthComponent* Health = UIMGHealthComponent::FindHealthComponent(Character);
	if (!Body || !Body->IsRagdollAvailable() || (Health && (Health->IsDeadOrDying() || Health->GetHealth() <= 0.f)))
	{
		return false;
	}
	return !ActorInfo->IsNetAuthority() ||
		(Body->IsRagdollReady() && Body->GetRagdollPhase() == EIMGRagdollPhase::Inactive);
}

bool UIMGGameplayAbility_Ragdoll::IsActionCurrent(uint16 ExpectedSession) const
{
	return IsActive() && !bIsAbilityEnding && Action.Stage != EActionStage::Ending &&
		Action.Avatar.IsValid() && !Action.Avatar->IsActorBeingDestroyed() &&
		Action.ASC.IsValid() && Action.ASC->GetAvatarActor() == Action.Avatar.Get() &&
		Action.Body.IsValid() && Action.Body->GetOwner() == Action.Avatar.Get() &&
		Action.Body->IsRagdollAvailable() && Action.Movement.IsValid() &&
		(!ExpectedSession || (ExpectedSession == Action.Session &&
			Action.Body->GetRagdollSession() == ExpectedSession &&
			Action.Movement->GetRagdollMovementSession() == ExpectedSession));
}

bool UIMGGameplayAbility_Ragdoll::IsDeathHandoff() const
{
	const UIMGHealthComponent* Health = Action.Health.Get();
	return (Health && (Health->GetHealth() <= 0.f || Health->IsDeadOrDying())) ||
		(Action.ASC.IsValid() && Action.ASC->HasMatchingGameplayTag(IMGGameplayTags::Status_Death)) ||
		(Action.Movement.IsValid() && Action.Movement->MovementMode == MOVE_None);
}

void UIMGGameplayAbility_Ragdoll::ActivateAbility(FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	TGuardValue<bool> Processing(bProcessingAction, true);
	Action.Reset();
	Action.Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	Action.ASC = ActorInfo ? Cast<UIMGAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get()) : nullptr;
	Action.Spec = Handle;
	Action.ActivationKey = ActivationInfo.GetActivationPredictionKey();
	Action.Body = UIMGRagdollComponent::FindRagdollComponent(Action.Avatar.Get());
	Action.Health = UIMGHealthComponent::FindHealthComponent(Action.Avatar.Get());
	const ACharacter* Character = Cast<ACharacter>(Action.Avatar.Get());
	Action.Movement = Character ? Cast<UIMGCharacterMovementComponent>(Character->GetCharacterMovement()) : nullptr;
	Action.ActivatedTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	Action.PreviousCheckTime = Action.ActivatedTime;
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActionCurrent() || !Action.Body->BindRecoveryAbilitySystem(Action.ASC.Get()))
	{
		AbortAction(TEXT("Activation infrastructure unavailable"));
		return;
	}
	Action.Body->OnRagdollEnded.AddUniqueDynamic(this, &ThisClass::HandleBodyEnded);
	Action.Body->OnRagdollBodyUnavailable.AddUniqueDynamic(this, &ThisClass::HandleBodyUnavailable);
	ImpactHandle = Action.ASC->AddGameplayEventTagContainerDelegate(
		FGameplayTagContainer(IMGGameplayTags::GameplayEvent_Ragdoll_Impact),
		FGameplayEventTagMulticastDelegate::FDelegate::CreateUObject(this, &ThisClass::OnImpact));
	if (ActorInfo->IsNetAuthority())
	{
		if (!CommitAbility(Handle, ActorInfo, ActivationInfo) || !IsActionCurrent())
		{
			AbortAction(TEXT("Entry commit failed"));
			return;
		}
		Action.ASC->CancelAbilitiesByFunc(
			[this](const UIMGGameplayAbility* Other, FGameplayAbilitySpecHandle)
			{
				return Other != this && Other->GetActivationPolicy() != EIMGAbilityActivationPolicy::OnSpawn;
			}, true);
		if (!IsActionCurrent())
		{
			AbortAction(TEXT("Avatar changed while cancelling incompatible actions"));
			return;
		}
		const TWeakObjectPtr<UIMGRagdollComponent> StartingBody = Action.Body;
		const TWeakObjectPtr<UIMGCharacterMovementComponent> StartingMovement = Action.Movement;
		const uint16 PreviousSession = StartingBody->GetRagdollSession();
		const bool bStarted = StartingBody->StartRagdoll(AdditionalVelocity);
		if (!IsActive() || bIsAbilityEnding)
		{
			// Entry callbacks can cancel this activation before its new session is assigned to Action.
			if (StartingBody.IsValid() && StartingMovement.IsValid() &&
				StartingBody->GetRagdollSession() != PreviousSession &&
				StartingMovement->GetRagdollMovementPhase() != EIMGRagdollPhase::Inactive &&
				!StartingMovement->GetRagdollMovementState().bExternalTransition)
			{
				const UIMGHealthComponent* Health = UIMGHealthComponent::FindHealthComponent(StartingBody->GetOwner());
				if ((Health && (Health->GetHealth() <= 0.f || Health->IsDeadOrDying())) ||
					StartingMovement->MovementMode == MOVE_None)
				{
					StartingBody->HandOffRagdollBody(StartingBody->GetRagdollSession());
					StartingMovement->SetMovementMode(MOVE_None);
				}
				StartingBody->AbortRagdoll(StartingBody->GetRagdollSession(), true);
			}
			return;
		}
		if (StartingBody.IsValid() && StartingBody->GetRagdollSession() != PreviousSession)
		{
			Action.Session = StartingBody->GetRagdollSession();
		}
		if (!bStarted || !IsActionCurrent(Action.Session) || !Action.Session || Action.bBodyFailedDuringEntry)
		{
			AbortAction(TEXT("Body entry failed"));
			return;
		}
		Action.Body->BeginRecoverySession(
			Action.Session, Action.ActivationKey.Current, Action.Body->GetRagdollServerTime() + MinimumRagdollTime);
		Action.Stage = EActionStage::Settling;
		UE_LOG(LogIMGAbilitySystem, Verbose, TEXT("[RagdollGA] Started Avatar=%s Session=%u Key=%d"),
			*GetNameSafe(Action.Avatar.Get()), Action.Session, Action.ActivationKey.Current);
		TargetDataHandle = Action.ASC->AbilityTargetDataSetDelegate(Handle, Action.ActivationKey)
			.AddUObject(this, &ThisClass::OnRecoveryTargetData);
		TargetCancelledHandle = Action.ASC->AbilityTargetDataCancelledDelegate(Handle, Action.ActivationKey)
			.AddUObject(this, &ThisClass::OnRecoveryTargetDataCancelled);
		Action.ASC->CallReplicatedTargetDataDelegatesIfSet(Handle, Action.ActivationKey);
	}
	if (!IsActionCurrent())
	{
		return;
	}
	GetWorld()->GetTimerManager().SetTimer(CheckTimer, this, &ThisClass::CheckRagdoll, CheckInterval, true);
}

void UIMGGameplayAbility_Ragdoll::CheckRagdoll()
{
	if (bProcessingAction)
	{
		return;
	}
	TGuardValue<bool> Processing(bProcessingAction, true);
	if (!IsActionCurrent() || IsDeathHandoff())
	{
		AbortAction(TEXT("Action invalidated or death took control"));
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	const double SampleInterval = Now - Action.PreviousCheckTime;
	const bool bSampleGap = SampleInterval > static_cast<double>(CheckInterval) * 2.0;
	if (bSampleGap)
	{
		Action.StableStartTime = -1.0;
	}
	Action.PreviousCheckTime = Now;
	const FIMGRagdollRecoveryRepState& Approval = Action.Body->GetRecoveryApproval();
	if (Action.Stage == EActionStage::Entering)
	{
		if (Approval.Session != 0 && Approval.ActivationKey == Action.ActivationKey.Current &&
			!Approval.bEnded && Approval.Session == Action.Body->GetRagdollSession())
		{
			Action.Session = Approval.Session;
			Action.ImpactRevision = Approval.ImpactRevision;
			Action.Stage = Approval.bApproved ? EActionStage::Recovering : EActionStage::Settling;
		}
		else if (Now - Action.ActivatedTime > RecoveryStartTimeout)
		{
			AbortAction(TEXT("Authority session did not arrive"));
		}
		return;
	}
	if (Approval.Session != Action.Session || Approval.bEnded)
	{
		if (!CurrentActorInfo->IsNetAuthority())
		{
			if (Action.Body->GetRagdollSession() == Action.Session &&
				Action.Body->GetRagdollPhase() != EIMGRagdollPhase::Inactive)
			{
				return;
			}
			Action.bCompleted = Action.Movement->GetRagdollMovementState().EndReason == EIMGRagdollEndReason::Recovered;
			EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, false, !Action.bCompleted);
		}
		else
		{
			AbortAction(TEXT("Recovery approval retired"));
		}
		return;
	}
	if (Approval.ImpactRevision != Action.ImpactRevision)
	{
		Action.ImpactRevision = Approval.ImpactRevision;
		Action.StableStartTime = -1.0;
		Action.NextSelectionTime = 0.0;
	}
	if (Approval.bApproved)
	{
		Action.Stage = EActionStage::Recovering;
		CheckRecovery();
	}
	else if (Now - Action.ActivatedTime > MaxRagdollTime)
	{
		AbortAction(TEXT("Ragdoll maximum wait exceeded"));
	}
	else if (CurrentActorInfo->IsLocallyControlled() ||
		(CurrentActorInfo->IsNetAuthority() && Cast<ACharacter>(Action.Avatar.Get()) &&
			!Cast<ACharacter>(Action.Avatar.Get())->IsPlayerControlled()))
	{
		CheckSettling();
	}
}

void UIMGGameplayAbility_Ragdoll::OnImpact(FGameplayTag Tag, const FGameplayEventData*)
{
	if (Tag != IMGGameplayTags::GameplayEvent_Ragdoll_Impact || !IsActionCurrent() ||
		!CurrentActorInfo->IsNetAuthority() || Action.Stage == EActionStage::Recovering)
	{
		return;
	}
	Action.StableStartTime = -1.0;
	Action.Body->DelayRecoverySession(
		Action.Session, Action.Body->GetRagdollServerTime() + MinimumRagdollTime);
	Action.ImpactRevision = Action.Body->GetRecoveryApproval().ImpactRevision;
}

void UIMGGameplayAbility_Ragdoll::HandleBodyEnded(int32 Session, EIMGRagdollEndReason Reason)
{
	if (!Action.Session || Session != Action.Session || bIsAbilityEnding)
	{
		return;
	}
	Action.bTransferred = Reason == EIMGRagdollEndReason::Transferred;
	if (Action.bTransferred)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
	}
	else if (Reason == EIMGRagdollEndReason::Recovered && CurrentActorInfo && !CurrentActorInfo->IsNetAuthority())
	{
		Action.bCompleted = true;
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, false, false);
	}
	else if (!Action.bCompleted)
	{
		AbortAction(TEXT("Body session ended"));
	}
}

void UIMGGameplayAbility_Ragdoll::HandleBodyUnavailable(int32 Session, EIMGRagdollBodyIssue Issue)
{
	if (bIsAbilityEnding || IsWaitingRagdollBodyIssue(Issue) || Issue == EIMGRagdollBodyIssue::None)
	{
		return;
	}
	if (Action.Stage == EActionStage::Entering && Action.Session == 0)
	{
		Action.bBodyFailedDuringEntry = true;
		return;
	}
	if (Session == Action.Session)
	{
		AbortAction(TEXT("Body execution resource unavailable"));
	}
}

void UIMGGameplayAbility_Ragdoll::AbortAction(const TCHAR* Reason)
{
	UE_LOG(LogIMGAbilitySystem, Verbose, TEXT("[RagdollGA] Abort Avatar=%s Session=%u Reason=%s"),
		*GetNameSafe(Action.Avatar.Get()), Action.Session, Reason);
	if (IsActive() && !bIsAbilityEnding)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo,
			CurrentActorInfo && CurrentActorInfo->IsNetAuthority(), true);
	}
}

void UIMGGameplayAbility_Ragdoll::RemoveActionBindings()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CheckTimer);
	}
	if (UIMGRagdollComponent* Body = Action.Body.Get())
	{
		Body->OnRagdollEnded.RemoveDynamic(this, &ThisClass::HandleBodyEnded);
		Body->OnRagdollBodyUnavailable.RemoveDynamic(this, &ThisClass::HandleBodyUnavailable);
	}
	if (UIMGAbilitySystemComponent* ASC = Action.ASC.Get())
	{
		ASC->RemoveGameplayEventTagContainerDelegate(
			FGameplayTagContainer(IMGGameplayTags::GameplayEvent_Ragdoll_Impact), ImpactHandle);
		ASC->AbilityTargetDataSetDelegate(Action.Spec, Action.ActivationKey).Remove(TargetDataHandle);
		ASC->AbilityTargetDataCancelledDelegate(Action.Spec, Action.ActivationKey).Remove(TargetCancelledHandle);
		ASC->ConsumeClientReplicatedTargetData(Action.Spec, Action.ActivationKey);
	}
	ImpactHandle.Reset();
	TargetDataHandle.Reset();
	TargetCancelledHandle.Reset();
	if (IsValid(MontageTask))
	{
		MontageTask->OnCompleted.RemoveAll(this);
		MontageTask->OnInterrupted.RemoveAll(this);
		MontageTask->OnCancelled.RemoveAll(this);
	}
	MontageTask = nullptr;
}

void UIMGGameplayAbility_Ragdoll::EndAbility(FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	if (Action.Spec.IsValid() && (Action.Spec != Handle ||
		Action.ActivationKey != ActivationInfo.GetActivationPredictionKey()))
	{
		return;
	}
	if (!IsEndAbilityValid(Handle, ActorInfo))
	{
		return;
	}
	if (ScopeLockCount > 0)
	{
		WaitingToExecute.Add(FPostLockDelegate::CreateUObject(
			this, &ThisClass::EndAbility, Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled));
		return;
	}
	TGuardValue<bool> Processing(bProcessingAction, true);
	{
		TGuardValue<bool> Ending(bIsAbilityEnding, true);
		if (ActorInfo && ActorInfo->IsNetAuthority())
		{
			bWasCancelled |= !Action.bCompleted && !Action.bTransferred;
		}
		UE_LOG(LogIMGAbilitySystem, Verbose, TEXT("[RagdollGA] End Avatar=%s Session=%u Completed=%d Cancelled=%d Transferred=%d"),
			*GetNameSafe(Action.Avatar.Get()), Action.Session, Action.bCompleted, bWasCancelled, Action.bTransferred);
		if (bWasCancelled && !RemoteInstanceEnded && ActorInfo && !ActorInfo->IsNetAuthority() &&
			Action.Stage == EActionStage::Recovering && Action.ASC.IsValid() && Action.Body.IsValid() &&
			!Action.Body->GetRecoveryApproval().bEnded)
		{
			FScopedPredictionWindow Prediction(Action.ASC.Get(), true);
			Action.ASC->ServerSetReplicatedTargetDataCancelled(
				Action.Spec, Action.ActivationKey, Action.ASC->ScopedPredictionKey);
		}
		Action.Stage = EActionStage::Ending;
		RemoveActionBindings();
		const bool bAuthority = Action.Avatar.IsValid() && Action.Avatar->HasAuthority();
		UIMGRagdollComponent* Body = Action.Body.Get();
		if (bAuthority && Body && Action.Session != 0)
		{
			Body->EndRecoverySession(Action.Session);
		}
		if (bAuthority && !Action.bTransferred && Action.Session != 0)
		{
			// Death GA cancels other abilities before StartDeath disables movement.
			if (IsDeathHandoff())
			{
				if (Body)
				{
					Body->HandOffRagdollBody(Action.Session);
				}
				if (UIMGCharacterMovementComponent* Movement = Action.Movement.Get();
					Movement && Movement->GetRagdollMovementSession() == Action.Session)
				{
					Movement->SetMovementMode(MOVE_None);
				}
			}
			if (Body && Body->GetRagdollSession() == Action.Session)
			{
				if (Body->GetRagdollPhase() == EIMGRagdollPhase::Recovering)
				{
					Body->CompleteRecovery(Action.Session, bWasCancelled || !Action.bCompleted);
				}
				else if (Body->GetRagdollPhase() == EIMGRagdollPhase::Simulating)
				{
					Body->AbortRagdoll(Action.Session, true);
				}
			}
			else if (UIMGCharacterMovementComponent* Movement = Action.Movement.Get();
				Movement && Movement->GetRagdollMovementSession() == Action.Session)
			{
				Movement->StopRagdollMovement(EIMGRagdollEndReason::Unavailable, Action.Session);
			}
		}
		Action.Reset();
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UIMGGameplayAbility_Ragdoll::OnPawnAvatarSet()
{
	if (IsActive())
	{
		AbortAction(TEXT("Avatar changed"));
	}
	Super::OnPawnAvatarSet();
}

void UIMGGameplayAbility_Ragdoll::OnRemoveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	if (IsActive())
	{
		AbortAction(TEXT("Ability removed"));
	}
	Super::OnRemoveAbility(ActorInfo, Spec);
}
