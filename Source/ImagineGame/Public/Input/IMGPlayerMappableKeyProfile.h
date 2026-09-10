// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UserSettings/EnhancedInputUserSettings.h"

#include "IMGPlayerMappableKeyProfile.generated.h"

#define UE_API IMAGINEGAME_API

UCLASS(MinimalAPI)
class UIMGPlayerMappableKeyProfile : public UEnhancedPlayerMappableKeyProfile
{
	GENERATED_BODY()

protected:

	//~ Begin UEnhancedPlayerMappableKeyProfile interface
	UE_API virtual void EquipProfile() override;
	UE_API virtual void UnEquipProfile() override;
	//~ End UEnhancedPlayerMappableKeyProfile interface
};

#undef UE_API
