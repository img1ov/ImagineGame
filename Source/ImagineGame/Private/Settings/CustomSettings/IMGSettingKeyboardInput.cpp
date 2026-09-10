// Copyright Epic Games, Inc. All Rights Reserved.

#include "Settings/CustomSettings/IMGSettingKeyboardInput.h"

#include "EnhancedInputSubsystems.h"
#include "Settings/IMGSettingsLocal.h"
#include "Player/IMGLocalPlayer.h"
#include "PlayerMappableInputConfig.h"
#include "EnhancedInputSubsystems.h"
#include "UserSettings/EnhancedInputUserSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGSettingKeyboardInput)

class ULocalPlayer;

#define LOCTEXT_NAMESPACE "IMGSettings"

namespace IMG::ErrorMessages
{
	static const FText UnknownMappingName = LOCTEXT("IMGErrors_UnknownMappingName", "Unknown Mapping");
}


UIMGSettingKeyboardInput::UIMGSettingKeyboardInput()
{
	bReportAnalytics = false;
}

FText UIMGSettingKeyboardInput::GetSettingDisplayName() const
{
	if (const FKeyMappingRow* Row = FindKeyMappingRow())
	{
		if (Row->HasAnyMappings())
		{
			return Row->Mappings.begin()->GetDisplayName();
		}
	}

	return IMG::ErrorMessages::UnknownMappingName;
}

FText UIMGSettingKeyboardInput::GetSettingDisplayCategory() const
{
	if (const FKeyMappingRow* Row = FindKeyMappingRow())
	{
		if (Row->HasAnyMappings())
		{
			return Row->Mappings.begin()->GetDisplayCategory();
		}
	}

	return IMG::ErrorMessages::UnknownMappingName;
}

const FKeyMappingRow* UIMGSettingKeyboardInput::FindKeyMappingRow() const
{
	if (const UEnhancedPlayerMappableKeyProfile* Profile = FindMappableKeyProfile())
	{
		return Profile->FindKeyMappingRow(ActionMappingName);
	}

	ensure(false);
	return nullptr;
}

UEnhancedPlayerMappableKeyProfile* UIMGSettingKeyboardInput::FindMappableKeyProfile() const
{
	if (UEnhancedInputUserSettings* Settings = GetUserSettings())
	{
		return Settings->GetKeyProfileWithId(ProfileIdentifier);
	}

	ensure(false);
	return nullptr;
}

UEnhancedInputUserSettings* UIMGSettingKeyboardInput::GetUserSettings() const
{
	if(UIMGLocalPlayer* IMGLocalPlayer = Cast<UIMGLocalPlayer>(LocalPlayer))
	{
		// Map the key to the player key profile
		if (UEnhancedInputLocalPlayerSubsystem* System = IMGLocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			return System->GetUserSettings();
		}
	}

	return nullptr;
}

void UIMGSettingKeyboardInput::OnInitialized()
{
	DynamicDetails = FGetGameSettingsDetails::CreateLambda([this](ULocalPlayer&)
	{
		if (const FKeyMappingRow* Row = FindKeyMappingRow())
		{
			if (Row->HasAnyMappings())
			{
				return FText::Format(LOCTEXT("DynamicDetails_KeyboardInputAction", "Bindings for {0}"), Row->Mappings.begin()->GetDisplayName());
			}
		}
		return FText::GetEmpty();
	});

	Super::OnInitialized();
}

void UIMGSettingKeyboardInput::InitializeInputData(const UEnhancedPlayerMappableKeyProfile* KeyProfile, const FKeyMappingRow& MappingData, const FPlayerMappableKeyQueryOptions& InQueryOptions)
{
	check(KeyProfile);

	ProfileIdentifier = KeyProfile->GetProfileIdString();
	QueryOptions = InQueryOptions;

	for (const FPlayerKeyMapping& Mapping : MappingData.Mappings)
	{
		// Only add mappings that pass the query filters that have been provided upon creation
		if (!KeyProfile->DoesMappingPassQueryOptions(Mapping, QueryOptions))
		{
			continue;
		}

		ActionMappingName = Mapping.GetMappingName();
		InitialKeyMappings.Add(Mapping.GetSlot(), Mapping.GetCurrentKey());
		const FText& MappingDisplayName =Mapping.GetDisplayName();

		if (!MappingDisplayName.IsEmpty())
		{
			SetDisplayName(MappingDisplayName);
		}
	}

	const FString NameString = TEXT("KBM_Input_") + ActionMappingName.ToString();
	SetDevName(*NameString);
}

FText UIMGSettingKeyboardInput::GetKeyTextFromSlot(const EPlayerMappableKeySlot InSlot) const
{
	if (const UEnhancedPlayerMappableKeyProfile* Profile = FindMappableKeyProfile())
	{
		FPlayerMappableKeyQueryOptions QueryOptionsForSlot = QueryOptions;
		QueryOptionsForSlot.SlotToMatch = InSlot;

		if (const FKeyMappingRow* Row = FindKeyMappingRow())
		{
			for (const FPlayerKeyMapping& Mapping : Row->Mappings)
			{
				if (Profile->DoesMappingPassQueryOptions(Mapping, QueryOptionsForSlot))
				{
					return Mapping.GetCurrentKey().GetDisplayName();
				}
			}
		}
	}

	return EKeys::Invalid.GetDisplayName();
}

void UIMGSettingKeyboardInput::ResetToDefault()
{
	if (UEnhancedInputUserSettings* Settings = GetUserSettings())
	{
		FMapPlayerKeyArgs Args = {};
		Args.MappingName = ActionMappingName;

		FGameplayTagContainer FailureReason;
		Settings->ResetAllPlayerKeysInRow(Args, FailureReason);

		NotifySettingChanged(EGameSettingChangeReason::Change);
	}
}

void UIMGSettingKeyboardInput::StoreInitial()
{
	if (const UEnhancedPlayerMappableKeyProfile* Profile = FindMappableKeyProfile())
	{
		if(const FKeyMappingRow* Row = FindKeyMappingRow())
		{
			for (const FPlayerKeyMapping& Mapping : Row->Mappings)
			{
				if (Profile->DoesMappingPassQueryOptions(Mapping, QueryOptions))
				{
					ActionMappingName = Mapping.GetMappingName();
					InitialKeyMappings.Add(Mapping.GetSlot(), Mapping.GetCurrentKey());
				}
			}
		}
	}
}

void UIMGSettingKeyboardInput::RestoreToInitial()
{
	for (TPair<EPlayerMappableKeySlot, FKey> Pair : InitialKeyMappings)
	{
		ChangeBinding((int32)Pair.Key, Pair.Value);
	}
}

bool UIMGSettingKeyboardInput::ChangeBinding(int32 InKeyBindSlot, FKey NewKey)
{
	if (!NewKey.IsGamepadKey())
	{
		FMapPlayerKeyArgs Args = {};
		Args.MappingName = ActionMappingName;
		Args.Slot = (EPlayerMappableKeySlot) (static_cast<uint8>(InKeyBindSlot));
		Args.NewKey = NewKey;
		// If you want to, you can additionally specify this mapping to only be applied to a certain hardware device or key profile
		//Args.ProfileId =
		//Args.HardwareDeviceId =

		if (UEnhancedInputUserSettings* Settings = GetUserSettings())
		{
			FGameplayTagContainer FailureReason;
			Settings->MapPlayerKey(Args, FailureReason);
			NotifySettingChanged(EGameSettingChangeReason::Change);
		}

		return true;
	}

	return false;
}

void UIMGSettingKeyboardInput::GetAllMappedActionsFromKey(int32 InKeyBindSlot, FKey Key, TArray<FName>& OutActionNames) const
{
	if (const UEnhancedPlayerMappableKeyProfile* Profile = FindMappableKeyProfile())
	{
		Profile->GetMappingNamesForKey(Key, OutActionNames);
	}
}

bool UIMGSettingKeyboardInput::IsMappingCustomized() const
{
	bool bResult = false;

	if (const UEnhancedPlayerMappableKeyProfile* Profile = FindMappableKeyProfile())
	{
		FPlayerMappableKeyQueryOptions QueryOptionsForSlot = QueryOptions;

		if (const FKeyMappingRow* Row = FindKeyMappingRow())
		{
			for (const FPlayerKeyMapping& Mapping : Row->Mappings)
			{
				if (Profile->DoesMappingPassQueryOptions(Mapping, QueryOptionsForSlot))
				{
					bResult |= Mapping.IsCustomized();
				}
			}
		}
	}

	return bResult;
}

#undef LOCTEXT_NAMESPACE
