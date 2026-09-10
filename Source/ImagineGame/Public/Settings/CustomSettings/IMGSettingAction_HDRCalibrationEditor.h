// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameSettingAction.h"
#include "GameSettingValueScalarDynamic.h"

#include "IMGSettingAction_HDRCalibrationEditor.generated.h"

class UGameSetting;

UCLASS()
class UIMGSettingAction_HDRCalibrationEditor : public UGameSettingAction
{
	GENERATED_BODY()

public:
	UIMGSettingAction_HDRCalibrationEditor();
	virtual TArray<UGameSetting*> GetChildSettings() override;

private:
	UPROPERTY()
	TObjectPtr<UGameSettingValueScalarDynamic> HDRCalibrationValueSetting;
};
