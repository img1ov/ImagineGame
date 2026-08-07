

#include "UI/IMGSettingScreen.h"

#include "Input/CommonUIInputTypes.h"
#include "Player/IMGLocalPlayer.h"
#include "Settings/IMGGameSettingRegistry.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGSettingScreen)

class UGameSettingRegistry;

void UIMGSettingScreen::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	BackHandle = RegisterUIActionBinding(FBindUIActionArgs(BackInputActionData, true, FSimpleDelegate::CreateUObject(this, &ThisClass::HandleBackAction)));
	ApplyHandle = RegisterUIActionBinding(FBindUIActionArgs(ApplyInputActionData, true, FSimpleDelegate::CreateUObject(this, &ThisClass::HandleApplyAction)));
	CancelChangesHandle = RegisterUIActionBinding(FBindUIActionArgs(CancelChangesInputActionData, true, FSimpleDelegate::CreateUObject(this, &ThisClass::HandleCancelChangesAction)));
}

UGameSettingRegistry* UIMGSettingScreen::CreateRegistry()
{
	UIMGGameSettingRegistry* NewRegistry = NewObject<UIMGGameSettingRegistry>();

	if (UIMGLocalPlayer* LocalPlayer = CastChecked<UIMGLocalPlayer>(GetOwningLocalPlayer()))
	{
		NewRegistry->Initialize(LocalPlayer);
	}

	return NewRegistry;
}

void UIMGSettingScreen::HandleBackAction()
{
	if (AttemptToPopNavigation())
	{
		return;
	}

	ApplyChanges();

	DeactivateWidget();
}

void UIMGSettingScreen::HandleApplyAction()
{
	ApplyChanges();
}

void UIMGSettingScreen::HandleCancelChangesAction()
{
	CancelChanges();
}

void UIMGSettingScreen::OnSettingsDirtyStateChanged_Implementation(bool bSettingsDirty)
{
	if (bSettingsDirty)
	{
		if (!GetActionBindings().Contains(ApplyHandle))
		{
			AddActionBinding(ApplyHandle);
		}
		if (!GetActionBindings().Contains(CancelChangesHandle))
		{
			AddActionBinding(CancelChangesHandle);
		}
	}
	else
	{
		RemoveActionBinding(ApplyHandle);
		RemoveActionBinding(CancelChangesHandle);
	}
}
