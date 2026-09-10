// Copyright Epic Games, Inc. All Rights Reserved.

#include "Settings/CustomSettings/IMGSettingAction_SafeZoneEditor.h"

#include "DataSource/GameSettingDataSource.h"
#include "Player/IMGLocalPlayer.h"
#include "Settings/IMGGameSettingRegistry.h"
#include "Widgets/Layout/SSafeZone.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGSettingAction_SafeZoneEditor)

class UGameSetting;

#define LOCTEXT_NAMESPACE "IMGSettings"

UIMGSettingAction_SafeZoneEditor::UIMGSettingAction_SafeZoneEditor()
{
	SafeZoneValueSetting = NewObject<UIMGSettingValueScalarDynamic_SafeZoneValue>();
	SafeZoneValueSetting->SetDevName(TEXT("SafeZoneValue"));
	SafeZoneValueSetting->SetDisplayName(LOCTEXT("SafeZoneValue_Name", "Safe Zone Value"));
	SafeZoneValueSetting->SetDescriptionRichText(LOCTEXT("SafeZoneValue_Description", "The safezone area percentage."));
	SafeZoneValueSetting->SetDefaultValue(0.0f);
	SafeZoneValueSetting->SetDynamicGetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(GetSafeZone));
	SafeZoneValueSetting->SetDynamicSetter(GET_LOCAL_SETTINGS_FUNCTION_PATH(SetSafeZone));
	SafeZoneValueSetting->SetDisplayFormat([](double SourceValue, double NormalizedValue){ return FText::AsNumber(SourceValue); });
	SafeZoneValueSetting->SetSettingParent(this);
}

TArray<UGameSetting*> UIMGSettingAction_SafeZoneEditor::GetChildSettings()
{
	return { SafeZoneValueSetting };
}

void UIMGSettingValueScalarDynamic_SafeZoneValue::ResetToDefault()
{
	Super::ResetToDefault();
	SSafeZone::SetGlobalSafeZoneScale(TOptional<float>(DefaultValue.Get(0.0f)));
}

void UIMGSettingValueScalarDynamic_SafeZoneValue::RestoreToInitial()
{
	Super::RestoreToInitial();
	SSafeZone::SetGlobalSafeZoneScale(TOptional<float>(InitialValue));
}

#undef LOCTEXT_NAMESPACE
