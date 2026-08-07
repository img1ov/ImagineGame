// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/Abilities/IMGGameplayAbility_Jump.h"

#include "Character/IMGCharacter.h"

UIMGGameplayAbility_Jump::UIMGGameplayAbility_Jump(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

bool UIMGGameplayAbility_Jump::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!ActorInfo || !ActorInfo->AvatarActor.IsValid())
	{
		return false;
	}

	const AIMGCharacter* IMGCharacter = Cast<AIMGCharacter>(ActorInfo->AvatarActor.Get());
	if (!IMGCharacter || !IMGCharacter->CanJump())
	{
		return false;
	}

	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags))
	{
		return false;
	}

	return true;
}

void UIMGGameplayAbility_Jump::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// Stop jumping in case the ability blueprint doesn't call it.
	CharacterJumpStop();
	
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UIMGGameplayAbility_Jump::CharacterJumpStart()
{
	if (AIMGCharacter* IMGCharacter = GetIMGCharacterFromActorInfo())
	{
		if (IMGCharacter->IsLocallyControlled() && !IMGCharacter->bPressedJump)
		{
			IMGCharacter->UnCrouch();
			IMGCharacter->Jump();
		}
	}
}

void UIMGGameplayAbility_Jump::CharacterJumpStop()
{
	if (AIMGCharacter* IMGCharacter = GetIMGCharacterFromActorInfo())
	{
		if (IMGCharacter->IsLocallyControlled() && !IMGCharacter->bPressedJump)
		{
			IMGCharacter->StopJumping();
		}
	}
}
