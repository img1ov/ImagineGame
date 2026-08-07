// Fill out your copyright notice in the Description page of Project Settings.


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
