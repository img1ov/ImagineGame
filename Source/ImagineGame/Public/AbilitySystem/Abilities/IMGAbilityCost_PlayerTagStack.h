// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayTagContainer.h"
#include "AbilitySystem/Abilities/IMGAbilityCost.h"
#include "ScalableFloat.h"

#include "IMGAbilityCost_PlayerTagStack.generated.h"

struct FGameplayAbilityActivationInfo;
struct FGameplayAbilitySpecHandle;

class UIMGGameplayAbility;
class UObject;
struct FGameplayAbilityActorInfo;

/**
 * Represents a cost that requires expending a quantity of a tag stack on the player state
 */
UCLASS(meta=(DisplayName="Player Tag Stack"))
class UIMGAbilityCost_PlayerTagStack : public UIMGAbilityCost
{
	GENERATED_BODY()

public:
	UIMGAbilityCost_PlayerTagStack();

	//~UIMGAbilityCost interface
	virtual bool CheckCost(const UIMGGameplayAbility* Ability, const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags) const override;
	virtual void ApplyCost(const UIMGGameplayAbility* Ability, const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) override;
	//~End of UIMGAbilityCost interface

protected:
	/** How much of the tag to spend (keyed on ability level) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Costs)
	FScalableFloat Quantity;

	/** Which tag to spend some of */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Costs)
	FGameplayTag Tag;
};
