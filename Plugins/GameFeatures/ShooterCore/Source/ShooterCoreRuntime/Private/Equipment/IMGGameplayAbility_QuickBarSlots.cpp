#include "Equipment/IMGGameplayAbility_QuickBarSlots.h"

#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Equipment/IMGQuickBarComponent.h"
#include "GameFramework/Controller.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGGameplayAbility_QuickBarSlots)

UIMGGameplayAbility_QuickBarSlots::UIMGGameplayAbility_QuickBarSlots(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ActivationPolicy = EIMGAbilityActivationPolicy::OnSpawn;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalOnly;
}

void UIMGGameplayAbility_QuickBarSlots::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!GetControllerFromActorInfo() || !SelectSlotEventTag.IsValid() || !CycleForwardEventTag.IsValid() || !CycleBackwardEventTag.IsValid())
	{
		K2_EndAbility();
		return;
	}

	UAbilityTask_WaitGameplayEvent* SelectTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, SelectSlotEventTag);
	SelectTask->EventReceived.AddDynamic(this, &ThisClass::SelectSlot);
	SelectTask->ReadyForActivation();

	UAbilityTask_WaitGameplayEvent* ForwardTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, CycleForwardEventTag);
	ForwardTask->EventReceived.AddDynamic(this, &ThisClass::CycleForward);
	ForwardTask->ReadyForActivation();

	UAbilityTask_WaitGameplayEvent* BackwardTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, CycleBackwardEventTag);
	BackwardTask->EventReceived.AddDynamic(this, &ThisClass::CycleBackward);
	BackwardTask->ReadyForActivation();
}

void UIMGGameplayAbility_QuickBarSlots::SelectSlot(FGameplayEventData Payload)
{
	if (AController* Controller = GetControllerFromActorInfo())
	{
		if (UIMGQuickBarComponent* QuickBar = Controller->FindComponentByClass<UIMGQuickBarComponent>())
		{
			QuickBar->SetActiveSlotIndex(FMath::TruncToInt(Payload.EventMagnitude));
		}
	}
}

void UIMGGameplayAbility_QuickBarSlots::CycleForward(FGameplayEventData Payload)
{
	if (AController* Controller = GetControllerFromActorInfo())
	{
		if (UIMGQuickBarComponent* QuickBar = Controller->FindComponentByClass<UIMGQuickBarComponent>())
		{
			QuickBar->CycleActiveSlotForward();
		}
	}
}

void UIMGGameplayAbility_QuickBarSlots::CycleBackward(FGameplayEventData Payload)
{
	if (AController* Controller = GetControllerFromActorInfo())
	{
		if (UIMGQuickBarComponent* QuickBar = Controller->FindComponentByClass<UIMGQuickBarComponent>())
		{
			QuickBar->CycleActiveSlotBackward();
		}
	}
}
