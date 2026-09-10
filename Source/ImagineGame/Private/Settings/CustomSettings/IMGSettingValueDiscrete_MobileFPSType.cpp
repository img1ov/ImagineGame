// Copyright Epic Games, Inc. All Rights Reserved.

#include "Settings/CustomSettings/IMGSettingValueDiscrete_MobileFPSType.h"

#include "Performance/IMGPerformanceSettings.h"
#include "Settings/IMGSettingsLocal.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGSettingValueDiscrete_MobileFPSType)

#define LOCTEXT_NAMESPACE "IMGSettings"

UIMGSettingValueDiscrete_MobileFPSType::UIMGSettingValueDiscrete_MobileFPSType()
{
}

void UIMGSettingValueDiscrete_MobileFPSType::OnInitialized()
{
	Super::OnInitialized();

	const UIMGPlatformSpecificRenderingSettings* PlatformSettings = UIMGPlatformSpecificRenderingSettings::Get();
	const UIMGSettingsLocal* UserSettings = UIMGSettingsLocal::Get();

	for (int32 TestLimit : PlatformSettings->MobileFrameRateLimits)
	{
		if (UIMGSettingsLocal::IsSupportedMobileFramePace(TestLimit))
		{
			FPSOptions.Add(TestLimit, MakeLimitString(TestLimit));
		}
	}

	const int32 FirstFrameRateWithQualityLimit = UserSettings->GetFirstFrameRateWithQualityLimit();
	if (FirstFrameRateWithQualityLimit > 0)
	{
		SetWarningRichText(FText::Format(LOCTEXT("MobileFPSType_Note", "<strong>Note: Changing the framerate setting to {0} or higher might lower your Quality Presets.</>"), MakeLimitString(FirstFrameRateWithQualityLimit)));
	}
}

int32 UIMGSettingValueDiscrete_MobileFPSType::GetDefaultFPS() const
{
	return UIMGSettingsLocal::GetDefaultMobileFrameRate();
}

FText UIMGSettingValueDiscrete_MobileFPSType::MakeLimitString(int32 Number)
{
	return FText::Format(LOCTEXT("MobileFrameRateOption", "{0} FPS"), FText::AsNumber(Number));
}

void UIMGSettingValueDiscrete_MobileFPSType::StoreInitial()
{
	InitialValue = GetValue();
}

void UIMGSettingValueDiscrete_MobileFPSType::ResetToDefault()
{
	SetValue(GetDefaultFPS(), EGameSettingChangeReason::ResetToDefault);
}

void UIMGSettingValueDiscrete_MobileFPSType::RestoreToInitial()
{
	SetValue(InitialValue, EGameSettingChangeReason::RestoreToInitial);
}

void UIMGSettingValueDiscrete_MobileFPSType::SetDiscreteOptionByIndex(int32 Index)
{
	TArray<int32> FPSOptionsModes;
	FPSOptions.GenerateKeyArray(FPSOptionsModes);

	int32 NewMode = FPSOptionsModes.IsValidIndex(Index) ? FPSOptionsModes[Index] : GetDefaultFPS();

	SetValue(NewMode, EGameSettingChangeReason::Change);
}

int32 UIMGSettingValueDiscrete_MobileFPSType::GetDiscreteOptionIndex() const
{
	TArray<int32> FPSOptionsModes;
	FPSOptions.GenerateKeyArray(FPSOptionsModes);
	return FPSOptionsModes.IndexOfByKey(GetValue());
}

TArray<FText> UIMGSettingValueDiscrete_MobileFPSType::GetDiscreteOptions() const
{
	TArray<FText> Options;
	FPSOptions.GenerateValueArray(Options);

	return Options;
}

int32 UIMGSettingValueDiscrete_MobileFPSType::GetValue() const
{
	return UIMGSettingsLocal::Get()->GetDesiredMobileFrameRateLimit();
}

void UIMGSettingValueDiscrete_MobileFPSType::SetValue(int32 NewLimitFPS, EGameSettingChangeReason InReason)
{
	UIMGSettingsLocal::Get()->SetDesiredMobileFrameRateLimit(NewLimitFPS);

	NotifySettingChanged(InReason);
}

#undef LOCTEXT_NAMESPACE
