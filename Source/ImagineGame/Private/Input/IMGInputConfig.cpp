// Fill out your copyright notice in the Description page of Project Settings.

#include "Input/IMGInputConfig.h"

#include "IMGLogChannels.h"

UIMGInputConfig::UIMGInputConfig(const FObjectInitializer& ObjectInitializer)
{
}

const UInputAction* UIMGInputConfig::FindNativeInputActionForTag(const FGameplayTag& InputTag, bool bLogNotFound) const
{
	// Native actions are manually bound by gameplay code (e.g. move/look).
	for (const FActInputAction& TagAction : NativeInputTagActions)
	{
		if (TagAction.InputAction && (TagAction.InputTag == InputTag))
		{
			return TagAction.InputAction;
		}
	}

	if (bLogNotFound)
	{
		UE_LOG(LogIMG, Error, TEXT("Can't find NativeInputAction for InputTag [%s] on InputConfig [%s]."), *InputTag.ToString(), *GetNameSafe(this));
	}

	return nullptr;
}

const UInputAction* UIMGInputConfig::FindAbilityInputActionForTag(const FGameplayTag& InputTag, bool bLogNotFound) const
{
	// Ability actions are auto-bound to gameplay ability input tags.
	for (const FActInputAction& Action : AbilityInputActions)
	{
		if (Action.InputAction && (Action.InputTag == InputTag))
		{
			return Action.InputAction;
		}
	}

	if (bLogNotFound)
	{
		UE_LOG(LogIMG, Error, TEXT("Can't find AbilityInputAction for InputTag [%s] on InputConfig [%s]."), *InputTag.ToString(), *GetNameSafe(this));
	}

	return nullptr;
}
