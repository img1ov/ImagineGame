// Copyright Epic Games, Inc. All Rights Reserved.

#include "Settings/CustomSettings/IMGSettingValueDiscrete_PerfStat.h"

#include "CommonUIVisibilitySubsystem.h"
#include "Performance/IMGPerformanceSettings.h"
#include "Performance/IMGPerformanceStatTypes.h"
#include "Settings/IMGSettingsLocal.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGSettingValueDiscrete_PerfStat)

class ULocalPlayer;

#define LOCTEXT_NAMESPACE "IMGSettings"

//////////////////////////////////////////////////////////////////////

class FGameSettingEditCondition_PerfStatAllowed : public FGameSettingEditCondition
{
public:
	static TSharedRef<FGameSettingEditCondition_PerfStatAllowed> Get(EIMGDisplayablePerformanceStat Stat)
	{
		return MakeShared<FGameSettingEditCondition_PerfStatAllowed>(Stat);
	}

	FGameSettingEditCondition_PerfStatAllowed(EIMGDisplayablePerformanceStat Stat)
		: AssociatedStat(Stat)
	{
	}

	//~FGameSettingEditCondition interface
	virtual void GatherEditState(const ULocalPlayer* InLocalPlayer, FGameSettingEditableState& InOutEditState) const override
	{
		const FGameplayTagContainer& VisibilityTags = UCommonUIVisibilitySubsystem::GetChecked(InLocalPlayer)->GetVisibilityTags();

		bool bCanShowStat = false;
		for (const FIMGPerformanceStatGroup& Group : GetDefault<UIMGPerformanceSettings>()->UserFacingPerformanceStats) //@TODO: Move this stuff to per-platform instead of doing vis queries too?
		{
			if (Group.AllowedStats.Contains(AssociatedStat))
			{
				const bool bShowGroup = (Group.VisibilityQuery.IsEmpty() || Group.VisibilityQuery.Matches(VisibilityTags));
				if (bShowGroup)
				{
					bCanShowStat = true;
					break;
				}
			}
		}

		if (!bCanShowStat)
		{
			InOutEditState.Hide(TEXT("Stat is not listed in UIMGPerformanceSettings or is suppressed by current platform traits"));
		}
	}
	//~End of FGameSettingEditCondition interface

private:
	EIMGDisplayablePerformanceStat AssociatedStat;
};

//////////////////////////////////////////////////////////////////////

UIMGSettingValueDiscrete_PerfStat::UIMGSettingValueDiscrete_PerfStat()
{
}

void UIMGSettingValueDiscrete_PerfStat::SetStat(EIMGDisplayablePerformanceStat InStat)
{
	StatToDisplay = InStat;
	SetDevName(FName(*FString::Printf(TEXT("PerfStat_%d"), (int32)StatToDisplay)));
	AddEditCondition(FGameSettingEditCondition_PerfStatAllowed::Get(StatToDisplay));
}

void UIMGSettingValueDiscrete_PerfStat::AddMode(FText&& Label, EIMGStatDisplayMode Mode)
{
	Options.Emplace(MoveTemp(Label));
	DisplayModes.Add(Mode);
}

void UIMGSettingValueDiscrete_PerfStat::OnInitialized()
{
	Super::OnInitialized();

	AddMode(LOCTEXT("PerfStatDisplayMode_None", "None"), EIMGStatDisplayMode::Hidden);
	AddMode(LOCTEXT("PerfStatDisplayMode_TextOnly", "Text Only"), EIMGStatDisplayMode::TextOnly);
	AddMode(LOCTEXT("PerfStatDisplayMode_GraphOnly", "Graph Only"), EIMGStatDisplayMode::GraphOnly);
	AddMode(LOCTEXT("PerfStatDisplayMode_TextAndGraph", "Text and Graph"), EIMGStatDisplayMode::TextAndGraph);
}

void UIMGSettingValueDiscrete_PerfStat::StoreInitial()
{
	const UIMGSettingsLocal* Settings = UIMGSettingsLocal::Get();
	InitialMode = Settings->GetPerfStatDisplayState(StatToDisplay);
}

void UIMGSettingValueDiscrete_PerfStat::ResetToDefault()
{
	UIMGSettingsLocal* Settings = UIMGSettingsLocal::Get();
	Settings->SetPerfStatDisplayState(StatToDisplay, EIMGStatDisplayMode::Hidden);
	NotifySettingChanged(EGameSettingChangeReason::ResetToDefault);
}

void UIMGSettingValueDiscrete_PerfStat::RestoreToInitial()
{
	UIMGSettingsLocal* Settings = UIMGSettingsLocal::Get();
	Settings->SetPerfStatDisplayState(StatToDisplay, InitialMode);
	NotifySettingChanged(EGameSettingChangeReason::RestoreToInitial);
}

void UIMGSettingValueDiscrete_PerfStat::SetDiscreteOptionByIndex(int32 Index)
{
	if (DisplayModes.IsValidIndex(Index))
	{
		const EIMGStatDisplayMode DisplayMode = DisplayModes[Index];

		UIMGSettingsLocal* Settings = UIMGSettingsLocal::Get();
		Settings->SetPerfStatDisplayState(StatToDisplay, DisplayMode);
	}
	NotifySettingChanged(EGameSettingChangeReason::Change);
}

int32 UIMGSettingValueDiscrete_PerfStat::GetDiscreteOptionIndex() const
{
	const UIMGSettingsLocal* Settings = UIMGSettingsLocal::Get();
	return DisplayModes.Find(Settings->GetPerfStatDisplayState(StatToDisplay));
}

TArray<FText> UIMGSettingValueDiscrete_PerfStat::GetDiscreteOptions() const
{
	return Options;
}

#undef LOCTEXT_NAMESPACE
