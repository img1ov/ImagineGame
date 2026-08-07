// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "AbilitySystem/Abilities/IMGGameplayAbility.h"

#include "IMGGameplayAbility_Jump.generated.h"

class UObject;
struct FFrame;
struct FGameplayAbilityActorInfo;
struct FGameplayTagContainer;

/**
 * UIMGGameplayAbility_Jump
 *
 *	Gameplay ability used for character jumping.
 */
UCLASS()
class IMAGINEGAME_API UIMGGameplayAbility_Jump : public UIMGGameplayAbility
{
	GENERATED_BODY()

public:

	UIMGGameplayAbility_Jump(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:

	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr, FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	UFUNCTION(BlueprintCallable, Category="Act|Ability")
	void CharacterJumpStart();

	UFUNCTION(BlueprintCallable, Category="Act|Ability")
	void CharacterJumpStop();
};
