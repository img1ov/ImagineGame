// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "EnhancedInputComponent.h"
#include "IMGInputConfig.h"

#include "IMGInputComponent.generated.h"

class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;

/**
 * ActInputComponent
 *
 * Component used to manage input mappings and bindings using an input config data asset.
 */
UCLASS()
class IMAGINEGAME_API UIMGInputComponent : public UEnhancedInputComponent
{
	GENERATED_BODY()

public:
	UIMGInputComponent(const FObjectInitializer& ObjectInitializer);

	void AddInputMappings(const UIMGInputConfig* InputConfig, UEnhancedInputLocalPlayerSubsystem* InputSubsystem) const;
	void RemoveInputMappings(const UIMGInputConfig* InputConfig, UEnhancedInputLocalPlayerSubsystem* InputSubsystem) const;

	template<class UserClass, typename FuncType>
	void BindNativeAction(const UIMGInputConfig* InputConfig, const FGameplayTag& InputTag, ETriggerEvent TriggerEvent, UserClass* Object, FuncType Func, bool bLogIfNotFound);

	template<class UserClass, typename PressedFuncType, typename ReleasedFuncType>
	void BindAbilityActions(const UIMGInputConfig* InputConfig, UserClass* Object, PressedFuncType PressedFunc, ReleasedFuncType ReleasedFunc, TArray<uint32>& BindHandles);

	void RemoveBinds(TArray<uint32>& BindHandles);
};

template <class UserClass, typename FuncType>
void UIMGInputComponent::BindNativeAction(const UIMGInputConfig* InputConfig, const FGameplayTag& InputTag, ETriggerEvent TriggerEvent, UserClass* Object, FuncType Func, bool bLogIfNotFound)
{
	check(InputConfig);
	if (const UInputAction* IA = InputConfig->FindNativeInputActionForTag(InputTag, bLogIfNotFound))
	{
		// Native actions are explicitly bound by gameplay code using the selected trigger event.
		BindAction(IA, TriggerEvent, Object, Func);
	}
}

template <class UserClass, typename PressedFuncType, typename ReleasedFuncType>
void UIMGInputComponent::BindAbilityActions(const UIMGInputConfig* InputConfig, UserClass* Object, PressedFuncType PressedFunc, ReleasedFuncType ReleasedFunc, TArray<uint32>& BindHandles)
{
	check(InputConfig);

	for (const FActInputAction& Action : InputConfig->AbilityInputActions)
	{
		if (Action.InputAction)
		{
			if (PressedFunc)
			{
				// Keep binding handles so they can be removed cleanly on pawn swap/rebind.
				BindHandles.Add(BindAction(Action.InputAction, Action.PressedTriggerEvent, Object, PressedFunc, Action.InputTag).GetHandle());
			}

			if (ReleasedFunc && Action.bBindReleaseEvent)
			{
				BindHandles.Add(BindAction(Action.InputAction, ETriggerEvent::Completed, Object, ReleasedFunc, Action.InputTag).GetHandle());
			}
		}
	}
}
