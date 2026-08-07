#include "Settings/IMGGameSettingRegistry.h"

#include "GameSettingCollection.h"
#include "Settings/IMGSettingsLocal.h"
#include "Settings/IMGSettingsShared.h"
#include "Player/IMGLocalPlayer.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGGameSettingRegistry)

DEFINE_LOG_CATEGORY(LogIMGGameSettingRegistry);

#define LOCTEXT_NAMESPACE "IMG"

//--------------------------------------
// UIMGGameSettingRegistry
//--------------------------------------

UIMGGameSettingRegistry::UIMGGameSettingRegistry()
{
}

UIMGGameSettingRegistry* UIMGGameSettingRegistry::Get(UIMGLocalPlayer* InLocalPlayer)
{
	UIMGGameSettingRegistry* Registry = FindObject<UIMGGameSettingRegistry>(InLocalPlayer, TEXT("IMGGameSettingRegistry"), EFindObjectFlags::ExactClass);
	if (Registry == nullptr)
	{
		Registry = NewObject<UIMGGameSettingRegistry>(InLocalPlayer, TEXT("IMGGameSettingRegistry"));
		Registry->Initialize(InLocalPlayer);
	}

	return Registry;
}

bool UIMGGameSettingRegistry::IsFinishedInitializing() const
{
	if (Super::IsFinishedInitializing())
	{
		if (UIMGLocalPlayer* LocalPlayer = Cast<UIMGLocalPlayer>(OwningLocalPlayer))
		{
			if (LocalPlayer->GetSharedSettings() == nullptr)
			{
				return false;
			}
		}

		return true;
	}

	return false;
}

void UIMGGameSettingRegistry::OnInitialize(ULocalPlayer* InLocalPlayer)
{
	UIMGLocalPlayer* IMGLocalPlayer = Cast<UIMGLocalPlayer>(InLocalPlayer);

	VideoSettings = InitializeVideoSettings(IMGLocalPlayer);
	InitializeVideoSettings_FrameRates(VideoSettings, IMGLocalPlayer);
	RegisterSetting(VideoSettings);

	AudioSettings = InitializeAudioSettings(IMGLocalPlayer);
	RegisterSetting(AudioSettings);

	GameplaySettings = InitializeGameplaySettings(IMGLocalPlayer);
	RegisterSetting(GameplaySettings);

	MouseAndKeyboardSettings = InitializeMouseAndKeyboardSettings(IMGLocalPlayer);
	RegisterSetting(MouseAndKeyboardSettings);

	GamepadSettings = InitializeGamepadSettings(IMGLocalPlayer);
	RegisterSetting(GamepadSettings);
}

void UIMGGameSettingRegistry::SaveChanges()
{
	Super::SaveChanges();
	
	if (UIMGLocalPlayer* LocalPlayer = Cast<UIMGLocalPlayer>(OwningLocalPlayer))
	{
		// Game user settings need to be applied to handle things like resolution, this saves indirectly
		LocalPlayer->GetLocalSettings()->ApplySettings(false);
		
		LocalPlayer->GetSharedSettings()->ApplySettings();
		LocalPlayer->GetSharedSettings()->SaveSettings();
	}
}

#undef LOCTEXT_NAMESPACE

