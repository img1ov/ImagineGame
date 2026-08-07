// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Inventory/IMGInventoryItemDefinition.h"
#include "Templates/SubclassOf.h"

#include "InventoryFragment_EquippableItem.generated.h"

class UIMGEquipmentDefinition;
class UObject;

UCLASS()
class UInventoryFragment_EquippableItem : public UIMGInventoryItemFragment
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category=IMG)
	TSubclassOf<UIMGEquipmentDefinition> EquipmentDefinition;
};
