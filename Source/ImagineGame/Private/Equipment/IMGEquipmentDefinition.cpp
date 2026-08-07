// Fill out your copyright notice in the Description page of Project Settings.


#include "Equipment/IMGEquipmentDefinition.h"
#include "Equipment/IMGEquipmentInstance.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGEquipmentDefinition)

UIMGEquipmentDefinition::UIMGEquipmentDefinition(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstanceType = UIMGEquipmentInstance::StaticClass();
}
