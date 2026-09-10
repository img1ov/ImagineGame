// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystem/Abilities/IMGGameplayAbility.h"

#include "IMGGameplayAbility_Death.generated.h"

class UObject;
struct FFrame;
struct FGameplayAbilityActorInfo;
struct FGameplayEventData;


/**
 * UIMGGameplayAbility_Death
 *
 *	Gameplay ability used for handling death.
 *	Ability is activated automatically via the "GameplayEvent.Death" ability trigger tag.
 */
UCLASS(Abstract)
class UIMGGameplayAbility_Death : public UIMGGameplayAbility
{
	GENERATED_BODY()

public:

	UIMGGameplayAbility_Death(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	// Starts the death sequence.
	UFUNCTION(BlueprintCallable, Category = "IMG|Ability")
	void StartDeath();

	// Finishes the death sequence.
	UFUNCTION(BlueprintCallable, Category = "IMG|Ability")
	void FinishDeath();

protected:

	// If enabled, the ability will automatically call StartDeath.  FinishDeath is always called when the ability ends if the death was started.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Death")
	bool bAutoStartDeath;
};
