// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/IMGGameplayAbility_Reset.h"

#include "AbilitySystem/IMGAbilitySystemComponent.h"
#include "Character/IMGCharacter.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "IMGGameplayTags.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGGameplayAbility_Reset)

UIMGGameplayAbility_Reset::UIMGGameplayAbility_Reset(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		// Add the ability trigger tag as default to the CDO.
		FAbilityTriggerData TriggerData;
		TriggerData.TriggerTag = IMGGameplayTags::GameplayEvent_RequestReset;
		TriggerData.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
		AbilityTriggers.Add(TriggerData);
	}
}

void UIMGGameplayAbility_Reset::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	check(ActorInfo);

	UIMGAbilitySystemComponent* IMGASC = CastChecked<UIMGAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get());

	FGameplayTagContainer AbilityTypesToIgnore;
	AbilityTypesToIgnore.AddTag(IMGGameplayTags::Ability_Behavior_SurvivesDeath);

	// Cancel all abilities and block others from starting.
	IMGASC->CancelAbilities(nullptr, &AbilityTypesToIgnore, this);

	SetCanBeCanceled(false);

	// Execute the reset from the character
	if (AIMGCharacter* IMGChar = Cast<AIMGCharacter>(CurrentActorInfo->AvatarActor.Get()))
	{
		IMGChar->Reset();
	}

	// Let others know a reset has occurred
	FIMGPlayerResetMessage Message;
	Message.OwnerPlayerState = CurrentActorInfo->OwnerActor.Get();
	UGameplayMessageSubsystem& MessageSystem = UGameplayMessageSubsystem::Get(this);
	MessageSystem.BroadcastMessage(IMGGameplayTags::GameplayEvent_Reset, Message);

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	const bool bReplicateEndAbility = true;
	const bool bWasCanceled = false;
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, bReplicateEndAbility, bWasCanceled);
}
