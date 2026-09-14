

#include "AbilitySystem/IMGAbilitySystemGlobals.h"

#include "AbilitySystem/IMGGameplayEffectContext.h"

struct FGameplayEffectContext;

UIMGAbilitySystemGlobals::UIMGAbilitySystemGlobals(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FGameplayEffectContext* UIMGAbilitySystemGlobals::AllocGameplayEffectContext() const
{
	return new FIMGGameplayEffectContext();
}
