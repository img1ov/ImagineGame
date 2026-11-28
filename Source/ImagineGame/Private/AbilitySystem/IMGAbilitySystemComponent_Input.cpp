#include "AbilitySystem/IMGAbilitySystemComponent.h"
#include "GameFramework/Actor.h"
#include "Templates/UnrealTemplate.h"

UE_DEFINE_GAMEPLAY_TAG(TAG_Gameplay_AbilityInputBlocked, "Gameplay.AbilityInputBlocked");

void UIMGAbilitySystemComponent::AbilityInputTagPressed(const FGameplayTag InputTag)
{
	if (InputTag.IsValid())
	{
		for (const FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
		{
			if (AbilitySpec.Ability && AbilitySpec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
			{
				InputPressedSpecHandles.AddUnique(AbilitySpec.Handle);
				InputHeldSpecHandles.AddUnique(AbilitySpec.Handle);
			}
		}
	}
}

void UIMGAbilitySystemComponent::AbilityInputTagTriggered(const FGameplayTag InputTag)
{
	if (InputTag.IsValid())
	{
		for (const FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
		{
			if (AbilitySpec.Ability && AbilitySpec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
			{
				// One-shot trigger for command outputs: do not add to held handles.
				InputPressedSpecHandles.AddUnique(AbilitySpec.Handle);
			}
		}
	}
}

void UIMGAbilitySystemComponent::AbilityInputTagReleased(const FGameplayTag InputTag)
{
	if (InputTag.IsValid())
	{
		for (const FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
		{
			if (AbilitySpec.Ability && AbilitySpec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
			{
				InputReleasedSpecHandles.AddUnique(AbilitySpec.Handle);
				InputHeldSpecHandles.Remove(AbilitySpec.Handle);
			}
		}
	}
}

void UIMGAbilitySystemComponent::ProcessAbilityInput(float DeltaTime, bool bGamePaused)
{
	if (bProcessingAbilityInput)
	{
		return;
	}
	TGuardValue<bool> ProcessingInput(bProcessingAbilityInput, true);
	if (HasMatchingGameplayTag(TAG_Gameplay_AbilityInputBlocked))
	{
		ClearAbilityInput();
		return;
	}

	const uint32 InputGeneration = AbilityInputGeneration;
	const AActor* InputAvatar = GetAvatarActor();
	const TArray<FGameplayAbilitySpecHandle, TInlineAllocator<8>> PressedHandles(InputPressedSpecHandles);
	const TArray<FGameplayAbilitySpecHandle, TInlineAllocator<8>> ReleasedHandles(InputReleasedSpecHandles);
	const TArray<FGameplayAbilitySpecHandle, TInlineAllocator<8>> HeldHandles(InputHeldSpecHandles);
	// Inputs queued by ability callbacks belong to the next processing pass.
	InputPressedSpecHandles.Reset();
	InputReleasedSpecHandles.Reset();
	TArray<FGameplayAbilitySpecHandle, TInlineAllocator<8>> AbilitiesToActive;
	const auto IsCurrentInput = [this, InputGeneration, InputAvatar]()
	{
		return AbilityInputGeneration == InputGeneration && GetAvatarActor() == InputAvatar;
	};


	//
	// Process all abilities that activate when the input is held.
	//
	for (const FGameplayAbilitySpecHandle& SpecHandle : HeldHandles)
	{
		if (const FGameplayAbilitySpec* AbilitySpec = FindAbilitySpecFromHandle(SpecHandle))
		{
			if (AbilitySpec->Ability && !AbilitySpec->IsActive())
			{
				const UIMGGameplayAbility* IMGAbilityCDO = Cast<UIMGGameplayAbility>(AbilitySpec->Ability);
				if (IMGAbilityCDO &&
					IMGAbilityCDO->GetActivationPolicy() == EIMGAbilityActivationPolicy::WhileInputActive)
				{
					AbilitiesToActive.AddUnique(AbilitySpec->Handle);
				}
			}
		}
	}

	//
	// Process all abilities that had their input pressed this frame.
	//
	for (const FGameplayAbilitySpecHandle& SpecHandle : PressedHandles)
	{
		if (FGameplayAbilitySpec* AbilitySpec = FindAbilitySpecFromHandle(SpecHandle))
		{
			if (AbilitySpec->Ability)
			{
				AbilitySpec->InputPressed = true;

				if (AbilitySpec->IsActive())
				{
					// Ability is active so pass along the input event.
					AbilitySpecInputPressed(*AbilitySpec);
					if (!IsCurrentInput())
					{
						return;
					}
				}
				else
				{
					const UIMGGameplayAbility* IMGAbilityCDO = Cast<UIMGGameplayAbility>(AbilitySpec->Ability);
					if (IMGAbilityCDO &&
						IMGAbilityCDO->GetActivationPolicy() == EIMGAbilityActivationPolicy::OnInputTriggered)
					{
						AbilitiesToActive.AddUnique(AbilitySpec->Handle);
					}
				}
			}
		}
	}

	//
	// Try to activate all the abilities that are from presses and holds.
	// We do it all at once so that held inputs don't activate the ability
	// and then also send an input event to the ability because of the press.
	//
	if (!AbilitiesToActive.IsEmpty())
	{
		for (const FGameplayAbilitySpecHandle& AbilitySpecHandle : AbilitiesToActive)
		{
			TryActivateAbility(AbilitySpecHandle);
			if (!IsCurrentInput())
			{
				return;
			}
		}
	}

	//
	// Process all abilities that had their input released this frame.
	//
	for (const FGameplayAbilitySpecHandle& SpecHandle : ReleasedHandles)
	{
		if (FGameplayAbilitySpec* AbilitySpec = FindAbilitySpecFromHandle(SpecHandle))
		{
			if (AbilitySpec->Ability)
			{
				AbilitySpec->InputPressed = false;

				if (AbilitySpec->IsActive())
				{
					// Ability is active so pass along the input event.
					AbilitySpecInputReleased(*AbilitySpec);
					if (!IsCurrentInput())
					{
						return;
					}
				}
			}
		}
	}
}

void UIMGAbilitySystemComponent::ClearAbilityInput()
{
	++AbilityInputGeneration;
	InputPressedSpecHandles.Reset();
	InputReleasedSpecHandles.Reset();
	InputHeldSpecHandles.Reset();
}

void UIMGAbilitySystemComponent::AbilitySpecInputPressed(FGameplayAbilitySpec& Spec)
{
	HandleAbilityInputEvent(Spec, true);
}

void UIMGAbilitySystemComponent::AbilitySpecInputReleased(FGameplayAbilitySpec& Spec)
{
	HandleAbilityInputEvent(Spec, false);
}

void UIMGAbilitySystemComponent::HandleAbilityInputEvent(FGameplayAbilitySpec& Spec, bool bPressed)
{
	ABILITYLIST_SCOPE_LOCK();
	const FGameplayAbilitySpecHandle Handle = Spec.Handle;
	const uint32 Generation = AbilityInputGeneration;
	const TWeakObjectPtr<AActor> Avatar = GetAvatarActor();
	using FInputRecipient = TPair<TWeakObjectPtr<UGameplayAbility>, FPredictionKey>;
	TArray<FInputRecipient, TInlineAllocator<4>> Recipients;
	const auto AddRecipient = [&Recipients](UGameplayAbility* Instance)
	{
		if (IsValid(Instance) && Instance->IsActive())
		{
			Recipients.Emplace(Instance, Instance->GetCurrentActivationInfo().GetActivationPredictionKey());
		}
	};
	if (UGameplayAbility* Primary = Spec.GetPrimaryInstance())
	{
		AddRecipient(Primary);
	}
	else if (Spec.IsActive())
	{
		for (UGameplayAbility* Instance : Spec.GetAbilityInstances())
		{
			AddRecipient(Instance);
		}
	}

	if (bPressed)
	{
		Super::AbilitySpecInputPressed(Spec);
	}
	else
	{
		Super::AbilitySpecInputReleased(Spec);
	}

	// Input callbacks can retire the avatar, spec, or activation before generic events are dispatched.
	TArray<FPredictionKey, TInlineAllocator<4>> DispatchedKeys;
	for (const FInputRecipient& Recipient : Recipients)
	{
		if (Generation != AbilityInputGeneration || Avatar.Get() != GetAvatarActor())
		{
			return;
		}
		const FGameplayAbilitySpec* CurrentSpec = FindAbilitySpecFromHandle(Handle);
		if (!CurrentSpec || CurrentSpec->PendingRemove)
		{
			return;
		}
		const UGameplayAbility* Instance = Recipient.Key.Get();
		if (Instance && Instance->IsActive() &&
			Instance->GetCurrentActivationInfo().GetActivationPredictionKey() == Recipient.Value &&
			!DispatchedKeys.Contains(Recipient.Value))
		{
			// GAS routes generic events by spec and prediction key, which some instances may share.
			DispatchedKeys.Add(Recipient.Value);
			InvokeReplicatedEvent(bPressed ? EAbilityGenericReplicatedEvent::InputPressed :
				EAbilityGenericReplicatedEvent::InputReleased, Handle, Recipient.Value);
		}
	}
}
