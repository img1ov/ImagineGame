// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "GameplayTagContainer.h"
#include "Engine/DataAsset.h"
#include "IMGPawnData.generated.h"

class APawn;
class UIMGCameraMode;
class UIMGInputConfig;
class UIMGAbilityTagRelationshipMapping;
class UIMGAbilitySet;

UCLASS()
class IMAGINEGAME_API UIMGPawnData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UIMGPawnData(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Pawn")
	TSubclassOf<APawn> PawnClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Abilities")
	TArray<TObjectPtr<UIMGAbilitySet>> AbilitySets;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Abilities")
	TObjectPtr<UIMGAbilityTagRelationshipMapping> TagRelationshipMapping;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Input")
	TObjectPtr<UIMGInputConfig> InputConfig;

	/** Default camera mode used by player controlled pawns. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Camera")
	TSubclassOf<UIMGCameraMode> DefaultCameraMode;
};
