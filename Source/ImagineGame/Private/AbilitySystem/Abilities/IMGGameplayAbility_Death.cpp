// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/IMGGameplayAbility_Death.h"

#include "AbilitySystem/Abilities/IMGGameplayAbility.h"
#include "AbilitySystem/IMGAbilitySystemComponent.h"
#include "Character/IMGHealthComponent.h"
#include "IMGGameplayTags.h"
#include "IMGLogChannels.h"
#include "Trace/Trace.inl"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGGameplayAbility_Death)

UIMGGameplayAbility_Death::UIMGGameplayAbility_Death(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;

	bAutoStartDeath = true;

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		// Add the ability trigger tag as default to the CDO.
		FAbilityTriggerData TriggerData;
		TriggerData.TriggerTag = IMGGameplayTags::GameplayEvent_Death;
		TriggerData.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
		AbilityTriggers.Add(TriggerData);
	}
}

void UIMGGameplayAbility_Death::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	check(ActorInfo);

	UIMGAbilitySystemComponent* IMGASC = CastChecked<UIMGAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get());

	FGameplayTagContainer AbilityTypesToIgnore;
	AbilityTypesToIgnore.AddTag(IMGGameplayTags::Ability_Behavior_SurvivesDeath);

	// Cancel all abilities and block others from starting.
	IMGASC->CancelAbilities(nullptr, &AbilityTypesToIgnore, this);

	SetCanBeCanceled(false);

	if (!ChangeActivationGroup(EIMGAbilityActivationGroup::Exclusive_Blocking))
	{
		UE_LOG(LogIMGAbilitySystem, Error, TEXT("UIMGGameplayAbility_Death::ActivateAbility: Ability [%s] failed to change activation group to blocking."), *GetName());
	}

	if (bAutoStartDeath)
	{
		StartDeath();
	}

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

void UIMGGameplayAbility_Death::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	check(ActorInfo);

	// Always try to finish the death when the ability ends in case the ability doesn't.
	// This won't do anything if the death hasn't been started.
	FinishDeath();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UIMGGameplayAbility_Death::StartDeath()
{
	if (UIMGHealthComponent* HealthComponent = UIMGHealthComponent::FindHealthComponent(GetAvatarActorFromActorInfo()))
	{
		if (HealthComponent->GetDeathState() == EIMGDeathState::NotDead)
		{
			HealthComponent->StartDeath();
		}
	}
}

void UIMGGameplayAbility_Death::FinishDeath()
{
	if (UIMGHealthComponent* HealthComponent = UIMGHealthComponent::FindHealthComponent(GetAvatarActorFromActorInfo()))
	{
		if (HealthComponent->GetDeathState() == EIMGDeathState::DeathStarted)
		{
			HealthComponent->FinishDeath();
		}
	}
}
