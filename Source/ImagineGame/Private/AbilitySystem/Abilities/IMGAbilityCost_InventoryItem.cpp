// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/IMGAbilityCost_InventoryItem.h"

#include "AbilitySystem/Abilities/IMGGameplayAbility.h"
#include "Inventory/IMGInventoryManagerComponent.h"
#include "GameFramework/Controller.h"
#include "GameplayAbilitySpec.h"
#include "GameplayAbilitySpecHandle.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGAbilityCost_InventoryItem)

UIMGAbilityCost_InventoryItem::UIMGAbilityCost_InventoryItem()
{
	Quantity.SetValue(1.0f);
}

bool UIMGAbilityCost_InventoryItem::CheckCost(const UIMGGameplayAbility* Ability, const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (AController* PC = Ability->GetControllerFromActorInfo())
	{
		if (UIMGInventoryManagerComponent* InventoryComponent = PC->GetComponentByClass<UIMGInventoryManagerComponent>())
		{
			const int32 AbilityLevel = Ability->GetAbilityLevel(Handle, ActorInfo);

			const float NumItemsToConsumeReal = Quantity.GetValueAtLevel(AbilityLevel);
			const int32 NumItemsToConsume = FMath::TruncToInt(NumItemsToConsumeReal);

			return InventoryComponent->GetTotalItemCountByDefinition(ItemDefinition) >= NumItemsToConsume;
		}
	}
	return false;
}

void UIMGAbilityCost_InventoryItem::ApplyCost(const UIMGGameplayAbility* Ability, const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo)
{
	if (ActorInfo->IsNetAuthority())
	{
		if (AController* PC = Ability->GetControllerFromActorInfo())
		{
			if (UIMGInventoryManagerComponent* InventoryComponent = PC->GetComponentByClass<UIMGInventoryManagerComponent>())
			{
				const int32 AbilityLevel = Ability->GetAbilityLevel(Handle, ActorInfo);

				const float NumItemsToConsumeReal = Quantity.GetValueAtLevel(AbilityLevel);
				const int32 NumItemsToConsume = FMath::TruncToInt(NumItemsToConsumeReal);

				InventoryComponent->ConsumeItemsByDefinition(ItemDefinition, NumItemsToConsume);
			}
		}
	}
}
