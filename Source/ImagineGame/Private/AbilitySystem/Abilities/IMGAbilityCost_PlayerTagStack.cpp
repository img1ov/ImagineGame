// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/IMGAbilityCost_PlayerTagStack.h"

#include "GameFramework/Controller.h"
#include "AbilitySystem/Abilities/IMGGameplayAbility.h"
#include "Player/IMGPlayerState.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGAbilityCost_PlayerTagStack)

UIMGAbilityCost_PlayerTagStack::UIMGAbilityCost_PlayerTagStack()
{
	Quantity.SetValue(1.0f);
}

bool UIMGAbilityCost_PlayerTagStack::CheckCost(const UIMGGameplayAbility* Ability, const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (AController* PC = Ability->GetControllerFromActorInfo())
	{
		if (AIMGPlayerState* PS = Cast<AIMGPlayerState>(PC->PlayerState))
		{
			const int32 AbilityLevel = Ability->GetAbilityLevel(Handle, ActorInfo);

			const float NumStacksReal = Quantity.GetValueAtLevel(AbilityLevel);
			const int32 NumStacks = FMath::TruncToInt(NumStacksReal);

			return PS->GetStatTagStackCount(Tag) >= NumStacks;
		}
	}
	return false;
}

void UIMGAbilityCost_PlayerTagStack::ApplyCost(const UIMGGameplayAbility* Ability, const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo)
{
	if (ActorInfo->IsNetAuthority())
	{
		if (AController* PC = Ability->GetControllerFromActorInfo())
		{
			if (AIMGPlayerState* PS = Cast<AIMGPlayerState>(PC->PlayerState))
			{
				const int32 AbilityLevel = Ability->GetAbilityLevel(Handle, ActorInfo);

				const float NumStacksReal = Quantity.GetValueAtLevel(AbilityLevel);
				const int32 NumStacks = FMath::TruncToInt(NumStacksReal);

				PS->RemoveStatTagStack(Tag, NumStacks);
			}
		}
	}
}
