// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/Attributes/IMGCombatSet.h"

#include "Net/UnrealNetwork.h"

UIMGCombatSet::UIMGCombatSet()
	: BaseDamage(0.0f)
	, BaseHeal(0.0f)
{
}

void UIMGCombatSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UIMGCombatSet, BaseDamage, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UIMGCombatSet, BaseHeal, COND_OwnerOnly, REPNOTIFY_Always);
}

void UIMGCombatSet::OnRep_BaseDamage(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UIMGCombatSet, BaseDamage, OldValue);
}

void UIMGCombatSet::OnRep_BaseHeal(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UIMGCombatSet, BaseHeal, OldValue);
}
