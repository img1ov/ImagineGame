// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "AbilitySystem/Abilities/IMGGameplayAbility.h"

#include "IMGGameplayAbility_FromEquipment.generated.h"

class UIMGEquipmentInstance;
class UIMGInventoryItemInstance;

/**
 * UIMGGameplayAbility_FromEquipment
 *
 * An ability granted by and associated with an equipment instance
 */
UCLASS()
class UIMGGameplayAbility_FromEquipment : public UIMGGameplayAbility
{
	GENERATED_BODY()

public:

	UIMGGameplayAbility_FromEquipment(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintCallable, Category="IMG|Ability")
	UIMGEquipmentInstance* GetAssociatedEquipment() const;

	UFUNCTION(BlueprintCallable, Category = "IMG|Ability")
	UIMGInventoryItemInstance* GetAssociatedItem() const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

};
