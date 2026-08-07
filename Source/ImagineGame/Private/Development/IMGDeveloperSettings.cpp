

#include "Development/IMGDeveloperSettings.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"

#if WITH_EDITOR
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGDeveloperSettings)

#define LOCTEXT_NAMESPACE "IMGCheats"

namespace Act::CVars
{
	static constexpr const TCHAR* ShouldAlwaysPlayForceFeedback = TEXT("IMGPC.ShouldAlwaysPlayForceFeedback");
}

static void EnsureActDeveloperSettingsCVarsExist()
{
	IConsoleManager& ConsoleManager = IConsoleManager::Get();
	if (ConsoleManager.FindConsoleVariable(Act::CVars::ShouldAlwaysPlayForceFeedback) == nullptr)
	{
		// UDeveloperSettingsBackedByCVars will fatal if a ConsoleVariable-backed property references a missing CVar.
		// Register it here to avoid relying on static init order across translation units.
		ConsoleManager.RegisterConsoleVariable(
			Act::CVars::ShouldAlwaysPlayForceFeedback,
			0,
			TEXT("Should force feedback effects be played, even if the last input device was not a gamepad?"),
			ECVF_Default);
	}
}

UIMGDeveloperSettings::UIMGDeveloperSettings()
{
}

FName UIMGDeveloperSettings::GetCategoryName() const
{
	return FApp::GetProjectName();
}

void UIMGDeveloperSettings::PostInitProperties()
{
	EnsureActDeveloperSettingsCVarsExist();
	Super::PostInitProperties();

#if WITH_EDITOR
	ApplySettings();
#endif
}

#if WITH_EDITOR
void UIMGDeveloperSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	ApplySettings();
}

void UIMGDeveloperSettings::PostReloadConfig(FProperty* PropertyThatWasLoaded)
{
	Super::PostReloadConfig(PropertyThatWasLoaded);

	ApplySettings();
}

void UIMGDeveloperSettings::ApplySettings()
{
}

void UIMGDeveloperSettings::OnPlayInEditorStarted() const
{
	// Show a notification toast to remind the user that there's an experience override set
	if (ExperienceOverride.IsValid())
	{
		FNotificationInfo Info(FText::Format(
			LOCTEXT("ExperienceOverrideActive", "Developer Settings Override\nExperience {0}"),
			FText::FromName(ExperienceOverride.PrimaryAssetName)
		));
		Info.ExpireDuration = 2.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}
}
#endif

#undef LOCTEXT_NAMESPACE

