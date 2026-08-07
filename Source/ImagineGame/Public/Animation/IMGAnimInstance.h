// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "GameplayEffectTypes.h"
#include "Animation/AnimInstance.h"
#include "IMGAnimInstance.generated.h"

class UIMGCharacterMovementComponent;
class UAbilitySystemComponent;

/**
 * 
 */
UCLASS()
class IMAGINEGAME_API UIMGAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void InitializeWithAbilitySystem(UAbilitySystemComponent* ASC);

protected:
	virtual void NativeInitializeAnimation() override;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "GameplayTags")
	FGameplayTagBlueprintPropertyMap GameplayTagPropertyMap;
};
