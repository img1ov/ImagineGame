#include "AbilitySystem/Tasks/AbilityTask_PlayMontageAndWaitForEvent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystemLog.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AbilityTask_PlayMontageAndWaitForEvent)

UAbilityTask_PlayMontageAndWaitForEvent::UAbilityTask_PlayMontageAndWaitForEvent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Rate = 1.f;
	AnimRootMotionTranslationScale = 1.f;
	bStopWhenAbilityEnds = true;
}

FAnimMontageInstance* UAbilityTask_PlayMontageAndWaitForEvent::GetMontageInstance() const
{
	UAnimInstance* AnimInstance = MontageAnimInstance.Get();
	return AnimInstance && MontageInstanceId != INDEX_NONE
		? AnimInstance->GetMontageInstanceForID(MontageInstanceId) : nullptr;
}

bool UAbilityTask_PlayMontageAndWaitForEvent::OwnsPlayingMontage() const
{
	const UAbilitySystemComponent* ASC = AbilitySystemComponent.Get();
	const FGameplayAbilityActorInfo* ActorInfo = Ability ? Ability->GetCurrentActorInfo() : nullptr;
	UAnimInstance* AnimInstance = MontageAnimInstance.Get();
	const FAnimMontageInstance* Active = AnimInstance ? AnimInstance->GetActiveInstanceForMontage(MontageToPlay) : nullptr;
	return ASC && Ability && ActorInfo && AnimInstance && MontageInstanceId != INDEX_NONE
		&& ActorInfo->GetAnimInstance() == AnimInstance
		&& ASC->GetAnimatingAbility() == Ability && Ability->GetCurrentMontage() == MontageToPlay
		&& (!Active || Active->GetInstanceID() == MontageInstanceId);
}

void UAbilityTask_PlayMontageAndWaitForEvent::RestoreRootMotion(bool bReleaseDisabledRootMotion)
{
	if (bReleaseDisabledRootMotion && bPushedDisableRootMotion)
	{
		if (FAnimMontageInstance* Instance = GetMontageInstance())
		{
			Instance->PopDisableRootMotion();
		}
		bPushedDisableRootMotion = false;
	}
	if (bAppliedRootMotionScale)
	{
		if (ACharacter* Character = RootMotionCharacter.Get())
		{
			UAnimInstance* CurrentAnimInstance = Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
			const FAnimMontageInstance* Active = CurrentAnimInstance ? CurrentAnimInstance->GetRootMotionMontageInstance() : nullptr;
			const bool bReplaced = Active && (CurrentAnimInstance != MontageAnimInstance.Get() ||
				Active->GetInstanceID() != MontageInstanceId);
			// Restore the original character after avatar loss, but never overwrite a newer playback's scale.
			if (!bReplaced && Character->GetAnimRootMotionTranslationScale() == AnimRootMotionTranslationScale)
			{
				Character->SetAnimRootMotionTranslationScale(PreviousRootMotionTranslationScale);
			}
		}
		bAppliedRootMotionScale = false;
	}
}

void UAbilityTask_PlayMontageAndWaitForEvent::UnbindMontageDelegates()
{
	if (FAnimMontageInstance* Instance = GetMontageInstance())
	{
		if (Instance->OnMontageBlendingOutStarted.IsBoundToObject(this))
		{
			Instance->OnMontageBlendingOutStarted.Unbind();
		}
		if (Instance->OnMontageEnded.IsBoundToObject(this))
		{
			Instance->OnMontageEnded.Unbind();
		}
	}
}

void UAbilityTask_PlayMontageAndWaitForEvent::OnMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted)
{
	if (bFinishing || Montage != MontageToPlay)
	{
		return;
	}
	RestoreRootMotion(bInterrupted);
	if (OwnsPlayingMontage())
	{
		AbilitySystemComponent->ClearAnimatingAbility(Ability);
	}
	if (bInterrupted)
	{
		bFinishing = true;
		if (ShouldBroadcastAbilityTaskDelegates())
		{
			OnInterrupted.Broadcast(FGameplayTag(), FGameplayEventData());
		}
		EndTask();
	}
	else if (ShouldBroadcastAbilityTaskDelegates())
	{
		OnBlendOut.Broadcast(FGameplayTag(), FGameplayEventData());
	}
}

void UAbilityTask_PlayMontageAndWaitForEvent::OnAbilityCancelled()
{
	if (bFinishing || bDestroying)
	{
		return;
	}
	bFinishing = true;
	StopPlayingMontage();
	if (ShouldBroadcastAbilityTaskDelegates())
	{
		OnCancelled.Broadcast(FGameplayTag(), FGameplayEventData());
	}
	EndTask();
}

void UAbilityTask_PlayMontageAndWaitForEvent::OnMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (bFinishing || Montage != MontageToPlay)
	{
		return;
	}
	bFinishing = true;
	RestoreRootMotion();
	if (OwnsPlayingMontage())
	{
		AbilitySystemComponent->ClearAnimatingAbility(Ability);
	}
	if (ShouldBroadcastAbilityTaskDelegates())
	{
		if (bInterrupted)
		{
			OnInterrupted.Broadcast(FGameplayTag(), FGameplayEventData());
		}
		else
		{
			OnCompleted.Broadcast(FGameplayTag(), FGameplayEventData());
		}
	}
	EndTask();
}

void UAbilityTask_PlayMontageAndWaitForEvent::OnGameplayEvent(FGameplayTag EventTag, const FGameplayEventData* Payload)
{
	if (!bFinishing && ShouldBroadcastAbilityTaskDelegates())
	{
		FGameplayEventData Data = Payload ? *Payload : FGameplayEventData();
		Data.EventTag = EventTag;
		EventReceived.Broadcast(EventTag, Data);
	}
}

UAbilityTask_PlayMontageAndWaitForEvent* UAbilityTask_PlayMontageAndWaitForEvent::PlayMontageAndWaitForEvent(
	UGameplayAbility* OwningAbility, FName TaskInstanceName, UAnimMontage* MontageToPlay,
	FGameplayTagContainer EventTags, float Rate, FName StartSection, bool bStopWhenAbilityEnds,
	float AnimRootMotionTranslationScale, bool bApplyRootMotion, float StartTimeSeconds)
{
	UAbilitySystemGlobals::NonShipping_ApplyGlobalAbilityScaler_Rate(Rate);
	UAbilityTask_PlayMontageAndWaitForEvent* Task = NewAbilityTask<UAbilityTask_PlayMontageAndWaitForEvent>(OwningAbility, TaskInstanceName);
	Task->MontageToPlay = MontageToPlay;
	Task->EventTags = EventTags;
	Task->Rate = Rate;
	Task->StartSection = StartSection;
	Task->bStopWhenAbilityEnds = bStopWhenAbilityEnds;
	Task->AnimRootMotionTranslationScale = AnimRootMotionTranslationScale;
	Task->bApplyRootMotion = bApplyRootMotion;
	Task->StartTimeSeconds = StartTimeSeconds;
	return Task;
}

void UAbilityTask_PlayMontageAndWaitForEvent::Activate()
{
	UAbilitySystemComponent* ASC = AbilitySystemComponent.Get();
	const FGameplayAbilityActorInfo* ActorInfo = Ability ? Ability->GetCurrentActorInfo() : nullptr;
	UAnimInstance* AnimInstance = ActorInfo ? ActorInfo->GetAnimInstance() : nullptr;
	if (!Ability || !ASC || !AnimInstance || !MontageToPlay || !FMath::IsFinite(Rate) ||
		!FMath::IsFinite(AnimRootMotionTranslationScale) || !FMath::IsFinite(StartTimeSeconds) ||
		StartTimeSeconds < 0.f || StartTimeSeconds >= MontageToPlay->GetPlayLength())
	{
		OnAbilityCancelled();
		return;
	}

	MontageAnimInstance = AnimInstance;
	EventHandle = ASC->AddGameplayEventTagContainerDelegate(EventTags,
		FGameplayEventTagMulticastDelegate::FDelegate::CreateUObject(this, &ThisClass::OnGameplayEvent));
	const float Duration = ASC->PlayMontage(
		Ability, Ability->GetCurrentActivationInfo(), MontageToPlay, Rate, StartSection, StartTimeSeconds);
	// PlayMontage can synchronously end this task through an interrupted ability or gameplay event.
	if (bFinishing || !ShouldBroadcastAbilityTaskDelegates())
	{
		EndTask();
		return;
	}
	const FGameplayAbilityActorInfo* CurrentActorInfo = Ability->GetCurrentActorInfo();
	FAnimMontageInstance* Instance = MontageAnimInstance.IsValid()
		? MontageAnimInstance->GetActiveInstanceForMontage(MontageToPlay) : nullptr;
	if (Duration <= 0.f || !Instance || !CurrentActorInfo || CurrentActorInfo->GetAnimInstance() != AnimInstance ||
		AbilitySystemComponent.Get() != ASC ||
		ASC->GetAnimatingAbility() != Ability || ASC->GetCurrentMontage() != MontageToPlay)
	{
		ABILITY_LOG(Warning, TEXT("PlayMontageAndWaitForEvent failed: Ability=%s Montage=%s Task=%s"),
			*GetNameSafe(Ability), *GetNameSafe(MontageToPlay), *InstanceName.ToString());
		OnAbilityCancelled();
		return;
	}
	MontageInstanceId = Instance->GetInstanceID();
	CancelledHandle = Ability->OnGameplayAbilityCancelled.AddUObject(this, &ThisClass::OnAbilityCancelled);
	BlendingOutDelegate.BindUObject(this, &ThisClass::OnMontageBlendingOut);
	MontageEndedDelegate.BindUObject(this, &ThisClass::OnMontageEnded);
	Instance->OnMontageBlendingOutStarted = BlendingOutDelegate;
	Instance->OnMontageEnded = MontageEndedDelegate;

	if (!bApplyRootMotion)
	{
		Instance->PushDisableRootMotion();
		bPushedDisableRootMotion = true;
	}
	else if (ACharacter* Character = Cast<ACharacter>(GetAvatarActor()); Character && MontageToPlay->HasRootMotion() &&
		(Character->GetLocalRole() == ROLE_Authority ||
			(Character->GetLocalRole() == ROLE_AutonomousProxy &&
				Ability->GetNetExecutionPolicy() == EGameplayAbilityNetExecutionPolicy::LocalPredicted)))
	{
		RootMotionCharacter = Character;
		PreviousRootMotionTranslationScale = Character->GetAnimRootMotionTranslationScale();
		Character->SetAnimRootMotionTranslationScale(AnimRootMotionTranslationScale);
		bAppliedRootMotionScale = true;
	}
	SetWaitingOnAvatar();
}

void UAbilityTask_PlayMontageAndWaitForEvent::ExternalCancel()
{
	OnAbilityCancelled();
}

void UAbilityTask_PlayMontageAndWaitForEvent::OnDestroy(bool AbilityEnded)
{
	if (bDestroying)
	{
		return;
	}
	bDestroying = true;
	bFinishing = true;
	if (Ability)
	{
		Ability->OnGameplayAbilityCancelled.Remove(CancelledHandle);
	}
	CancelledHandle.Reset();
	if (UAbilitySystemComponent* ASC = AbilitySystemComponent.Get())
	{
		ASC->RemoveGameplayEventTagContainerDelegate(EventTags, EventHandle);
	}
	EventHandle.Reset();
	if (AbilityEnded && bStopWhenAbilityEnds)
	{
		StopPlayingMontage();
	}
	RestoreRootMotion();
	UnbindMontageDelegates();
	MontageAnimInstance.Reset();
	RootMotionCharacter.Reset();
	MontageInstanceId = INDEX_NONE;
	Super::OnDestroy(AbilityEnded);
}

bool UAbilityTask_PlayMontageAndWaitForEvent::StopPlayingMontage()
{
	FAnimMontageInstance* Instance = GetMontageInstance();
	if (!Instance || !OwnsPlayingMontage())
	{
		return false;
	}
	RestoreRootMotion();
	UnbindMontageDelegates();
	AbilitySystemComponent->CurrentMontageStop();
	return true;
}

FString UAbilityTask_PlayMontageAndWaitForEvent::GetDebugString() const
{
	const FGameplayAbilityActorInfo* ActorInfo = Ability ? Ability->GetCurrentActorInfo() : nullptr;
	UAnimInstance* AnimInstance = ActorInfo ? ActorInfo->GetAnimInstance() : nullptr;
	return FString::Printf(TEXT("PlayMontageAndWaitForEvent. Montage=%s Instance=%d Current=%s"),
		*GetNameSafe(MontageToPlay), MontageInstanceId,
		*GetNameSafe(AnimInstance ? AnimInstance->GetCurrentActiveMontage() : nullptr));
}
