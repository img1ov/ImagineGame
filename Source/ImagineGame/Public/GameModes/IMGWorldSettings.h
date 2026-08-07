// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "GameFramework/WorldSettings.h"
#include "IMGWorldSettings.generated.h"

class UIMGExperienceDefinition;
/**
 * 
 */
UCLASS()
class IMAGINEGAME_API AIMGWorldSettings : public AWorldSettings
{
	GENERATED_BODY()

public:
	AIMGWorldSettings(const FObjectInitializer& ObjectInitializer);

public:
	FPrimaryAssetId GetDefaultGameplayExperience() const;

protected:
	UPROPERTY(EditDefaultsOnly, Category = GameMode)
	TSoftClassPtr<UIMGExperienceDefinition> DefaultGameplayExperience;
};
