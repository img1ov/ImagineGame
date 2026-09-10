// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystem/Abilities/IMGAbilityCost.h"
#include "ScalableFloat.h"
#include "Templates/SubclassOf.h"

#include "IMGAbilityCost_InventoryItem.generated.h"

struct FGameplayAbilityActivationInfo;
struct FGameplayAbilitySpecHandle;

class UIMGGameplayAbility;
class UIMGInventoryItemDefinition;
class UObject;
struct FGameplayAbilityActorInfo;
struct FGameplayTagContainer;

/**
 * Represents a cost that requires expending a quantity of an inventory item
 */
UCLASS(meta=(DisplayName="Inventory Item"))
class UIMGAbilityCost_InventoryItem : public UIMGAbilityCost
{
	GENERATED_BODY()

public:
	UIMGAbilityCost_InventoryItem();

	//~UIMGAbilityCost interface
	virtual bool CheckCost(const UIMGGameplayAbility* Ability, const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags) const override;
	virtual void ApplyCost(const UIMGGameplayAbility* Ability, const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) override;
	//~End of UIMGAbilityCost interface

protected:
	/** How much of the item to spend (keyed on ability level) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=AbilityCost)
	FScalableFloat Quantity;

	/** Which item to consume */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=AbilityCost)
	TSubclassOf<UIMGInventoryItemDefinition> ItemDefinition;
};
