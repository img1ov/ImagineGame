// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameSettingAction.h"
#include "GameSettingValueScalarDynamic.h"

#include "IMGSettingAction_SafeZoneEditor.generated.h"

class UGameSetting;
class UObject;


UCLASS()
class UIMGSettingValueScalarDynamic_SafeZoneValue : public UGameSettingValueScalarDynamic
{
	GENERATED_BODY()

public:
	virtual void ResetToDefault() override;
	virtual void RestoreToInitial() override;
};

UCLASS()
class UIMGSettingAction_SafeZoneEditor : public UGameSettingAction
{
	GENERATED_BODY()

public:
	UIMGSettingAction_SafeZoneEditor();
	virtual TArray<UGameSetting*> GetChildSettings() override;

private:
	UPROPERTY()
	TObjectPtr<UIMGSettingValueScalarDynamic_SafeZoneValue> SafeZoneValueSetting;
};
