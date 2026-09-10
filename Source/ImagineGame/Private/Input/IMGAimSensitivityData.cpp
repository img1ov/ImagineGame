// Copyright Epic Games, Inc. All Rights Reserved.

#include "Input/IMGAimSensitivityData.h"

#include "Settings/IMGSettingsShared.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGAimSensitivityData)

UIMGAimSensitivityData::UIMGAimSensitivityData(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SensitivityMap =
	{
		{ EIMGGamepadSensitivity::Slow,			0.5f },
		{ EIMGGamepadSensitivity::SlowPlus,		0.75f },
		{ EIMGGamepadSensitivity::SlowPlusPlus,	0.9f },
		{ EIMGGamepadSensitivity::Normal,		1.0f },
		{ EIMGGamepadSensitivity::NormalPlus,	1.1f },
		{ EIMGGamepadSensitivity::NormalPlusPlus,1.25f },
		{ EIMGGamepadSensitivity::Fast,			1.5f },
		{ EIMGGamepadSensitivity::FastPlus,		1.75f },
		{ EIMGGamepadSensitivity::FastPlusPlus,	2.0f },
		{ EIMGGamepadSensitivity::Insane,		2.5f },
	};
}

const float UIMGAimSensitivityData::SensitivtyEnumToFloat(const EIMGGamepadSensitivity InSensitivity) const
{
	if (const float* Sens = SensitivityMap.Find(InSensitivity))
	{
		return *Sens;
	}

	return 1.0f;
}
