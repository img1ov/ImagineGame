// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Camera/CameraComponent.h"
#include "IMGCameraComponent.generated.h"

/**
 * 
 */
UCLASS()
class IMAGINEGAME_API UIMGCameraComponent : public UCameraComponent
{
	GENERATED_BODY()
	
public:

	static const UIMGCameraComponent* FindCameraComponent(APawn* Pawn) {return nullptr;}
	void GetBlendInfo(float TopCameraWeight, FGameplayTag GameplayTag) const;
};
