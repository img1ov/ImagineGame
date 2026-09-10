// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/IMGAbilityCost_ItemTagStack.h"

#include "Equipment/IMGGameplayAbility_FromEquipment.h"
#include "Inventory/IMGInventoryItemInstance.h"
#include "NativeGameplayTags.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGAbilityCost_ItemTagStack)

UE_DEFINE_GAMEPLAY_TAG(TAG_ABILITY_FAIL_COST, "Ability.ActivateFail.Cost");

UIMGAbilityCost_ItemTagStack::UIMGAbilityCost_ItemTagStack()
{
	Quantity.SetValue(1.0f);
	FailureTag = TAG_ABILITY_FAIL_COST;
}

bool UIMGAbilityCost_ItemTagStack::CheckCost(const UIMGGameplayAbility* Ability, const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (const UIMGGameplayAbility_FromEquipment* EquipmentAbility = Cast<const UIMGGameplayAbility_FromEquipment>(Ability))
	{
		if (UIMGInventoryItemInstance* ItemInstance = EquipmentAbility->GetAssociatedItem())
		{
			const int32 AbilityLevel = Ability->GetAbilityLevel(Handle, ActorInfo);

			const float NumStacksReal = Quantity.GetValueAtLevel(AbilityLevel);
			const int32 NumStacks = FMath::TruncToInt(NumStacksReal);
			const bool bCanApplyCost = ItemInstance->GetStatTagStackCount(Tag) >= NumStacks;

			// Inform other abilities why this cost cannot be applied
			if (!bCanApplyCost && OptionalRelevantTags && FailureTag.IsValid())
			{
				OptionalRelevantTags->AddTag(FailureTag);
			}
			return bCanApplyCost;
		}
	}
	return false;
}

void UIMGAbilityCost_ItemTagStack::ApplyCost(const UIMGGameplayAbility* Ability, const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo)
{
	if (ActorInfo->IsNetAuthority())
	{
		if (const UIMGGameplayAbility_FromEquipment* EquipmentAbility = Cast<const UIMGGameplayAbility_FromEquipment>(Ability))
		{
			if (UIMGInventoryItemInstance* ItemInstance = EquipmentAbility->GetAssociatedItem())
			{
				const int32 AbilityLevel = Ability->GetAbilityLevel(Handle, ActorInfo);

				const float NumStacksReal = Quantity.GetValueAtLevel(AbilityLevel);
				const int32 NumStacks = FMath::TruncToInt(NumStacksReal);

				ItemInstance->RemoveStatTagStack(Tag, NumStacks);
			}
		}
	}
}
