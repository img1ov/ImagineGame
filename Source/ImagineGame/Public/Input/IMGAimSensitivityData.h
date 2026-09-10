// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"

#include "IMGAimSensitivityData.generated.h"

#define UE_API IMAGINEGAME_API

enum class EIMGGamepadSensitivity : uint8;

class UObject;

/** Defines a set of gamepad sensitivity to a float value. */
UCLASS(MinimalAPI, BlueprintType, Const, Meta = (DisplayName = "IMG Aim Sensitivity Data", ShortTooltip = "Data asset used to define a map of Gamepad Sensitivty to a float value."))
class UIMGAimSensitivityData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UE_API UIMGAimSensitivityData(const FObjectInitializer& ObjectInitializer);

	UE_API const float SensitivtyEnumToFloat(const EIMGGamepadSensitivity InSensitivity) const;

protected:
	/** Map of SensitivityMap settings to their corresponding float */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TMap<EIMGGamepadSensitivity, float> SensitivityMap;
};

#undef UE_API
