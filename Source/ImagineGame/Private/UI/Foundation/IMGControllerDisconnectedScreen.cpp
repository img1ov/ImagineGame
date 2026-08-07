#include "UI/Foundation/IMGControllerDisconnectedScreen.h"

#include "Components/HorizontalBox.h"
#include "CommonButtonBase.h"
#include "CommonUISettings.h"
#include "GameFramework/InputDeviceSubsystem.h"
#include "GenericPlatform/GenericPlatformApplicationMisc.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "ICommonUIModule.h"
#include "NativeGameplayTags.h"
#include "IMGLogChannels.h"

#if WITH_EDITOR
#include "CommonUIVisibilitySubsystem.h"
#endif	// WITH_EDITOR

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGControllerDisconnectedScreen)

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Platform_Trait_Input_HasStrictControllerPairing, "Platform.Trait.Input.HasStrictControllerPairing");

UIMGControllerDisconnectedScreen::UIMGControllerDisconnectedScreen(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// By default, only strict pairing platforms will need this button.
	PlatformSupportsUserChangeTags.AddTag(TAG_Platform_Trait_Input_HasStrictControllerPairing);
}

void UIMGControllerDisconnectedScreen::NativeOnActivated()
{
	Super::NativeOnActivated();

	if (!HBox_SwitchUser)
	{
		UE_LOG(LogIMG, Error, TEXT("Unable to find HBox_SwitchUser on Widget %s"), *GetNameSafe(this));
		return;
	}

	if (!Button_ChangeUser)
	{
		UE_LOG(LogIMG, Error, TEXT("Unable to find Button_ChangeUser on Widget %s"), *GetNameSafe(this));
		return;
	}

	HBox_SwitchUser->SetVisibility(ESlateVisibility::Collapsed);
	Button_ChangeUser->SetVisibility(ESlateVisibility::Hidden);

	if (ShouldDisplayChangeUserButton())
	{
		// This is the platform user for "unpaired" input devices. Not every platform supports this, so
		// only set this to visible if the unpaired user is valid.
		const FPlatformUserId UnpairedUserId = IPlatformInputDeviceMapper::Get().GetUserForUnpairedInputDevices();
		if (UnpairedUserId.IsValid())
		{
			HBox_SwitchUser->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			Button_ChangeUser->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}		
	}

	Button_ChangeUser->OnClicked().AddUObject(this, &ThisClass::HandleChangeUserClicked);
}

bool UIMGControllerDisconnectedScreen::ShouldDisplayChangeUserButton() const
{
	bool bRequiresChangeUserButton = ICommonUIModule::GetSettings().GetPlatformTraits().HasAll(PlatformSupportsUserChangeTags);

	// Check the tags that we may be emulating in the editor too
#if WITH_EDITOR
	const FGameplayTagContainer& PlatformEmulationTags = UCommonUIVisibilitySubsystem::Get(GetOwningLocalPlayer())->GetVisibilityTags();
	bRequiresChangeUserButton |= PlatformEmulationTags.HasAll(PlatformSupportsUserChangeTags);
#endif	// WITH_EDITOR

	return bRequiresChangeUserButton;
}

void UIMGControllerDisconnectedScreen::HandleChangeUserClicked()
{
	ensure(ShouldDisplayChangeUserButton());

	UE_LOG(LogIMG, Log, TEXT("[%hs] Change user requested!"), __func__);

	const FPlatformUserId OwningPlayerId = GetOwningLocalPlayer()->GetPlatformUserId();
	const FInputDeviceId DeviceId = IPlatformInputDeviceMapper::Get().GetPrimaryInputDeviceForUser(OwningPlayerId);

	FGenericPlatformApplicationMisc::ShowPlatformUserSelector(
		DeviceId,
		EPlatformUserSelectorFlags::Default,
		[this](const FPlatformUserSelectionCompleteParams& Params)
		{
			HandleChangeUserCompleted(Params);
		});
}

void UIMGControllerDisconnectedScreen::HandleChangeUserCompleted(const FPlatformUserSelectionCompleteParams& Params)
{
	UE_LOG(LogIMG, Log, TEXT("[%hs] User change complete!"), __func__);

	// TODO: Handle any user changing logic in your game here
}