// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Templates/SubclassOf.h"

#include "IMGEquipmentDefinition.generated.h"

class AActor;
class UIMGAbilitySet;
class UIMGEquipmentInstance;

USTRUCT()
struct FIMGEquipmentActorToSpawn
{
	GENERATED_BODY()

	FIMGEquipmentActorToSpawn()
	{}

	UPROPERTY(EditAnywhere, Category=Equipment)
	TSubclassOf<AActor> ActorToSpawn;

	UPROPERTY(EditAnywhere, Category=Equipment)
	FName AttachSocket;

	UPROPERTY(EditAnywhere, Category=Equipment)
	FTransform AttachTransform;
};


/**
 * UIMGEquipmentDefinition
 *
 * Definition of a piece of equipment that can be applied to a pawn
 */
UCLASS(Blueprintable, Const, Abstract, BlueprintType)
class UIMGEquipmentDefinition : public UObject
{
	GENERATED_BODY()

public:
	UIMGEquipmentDefinition(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// Class to spawn
	UPROPERTY(EditDefaultsOnly, Category=Equipment)
	TSubclassOf<UIMGEquipmentInstance> InstanceType;

	// Gameplay ability sets to grant when this is equipped
	UPROPERTY(EditDefaultsOnly, Category=Equipment)
	TArray<TObjectPtr<const UIMGAbilitySet>> AbilitySetsToGrant;

	// Actors to spawn on the pawn when this is equipped
	UPROPERTY(EditDefaultsOnly, Category=Equipment)
	TArray<FIMGEquipmentActorToSpawn> ActorsToSpawn;
};
