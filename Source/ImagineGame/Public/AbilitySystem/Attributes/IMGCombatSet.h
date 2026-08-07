// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/IMGAttributeSet.h"

#include "IMGCombatSet.generated.h"

class UObject;
struct FFrame;

/**
 * UIMGCombatSet
 *
 *  Class that defines attributes that are necessary for applying damage or healing.
 *	Attribute examples include: damage, healing, attack power, and shield penetrations.
 */
UCLASS(BlueprintType)
class IMAGINEGAME_API UIMGCombatSet : public UIMGAttributeSet
{
	GENERATED_BODY()
	
public:
	
	UIMGCombatSet();
	
	ATTRIBUTE_ACCESSORS(UIMGCombatSet, BaseDamage);
	ATTRIBUTE_ACCESSORS(UIMGCombatSet, BaseHeal);
	
protected:
	
	UFUNCTION()
	void OnRep_BaseDamage(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_BaseHeal(const FGameplayAttributeData& OldValue);
	
private:
	
	// The base amount of damage to apply in the damage execution.
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_BaseDamage, Category = "IMG|Combat", Meta = (AllowPrivateAccess = true))
	FGameplayAttributeData BaseDamage;

	// The base amount of healing to apply in the heal execution.
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_BaseHeal, Category = "IMG|Combat", Meta = (AllowPrivateAccess = true))
	FGameplayAttributeData BaseHeal;
};
