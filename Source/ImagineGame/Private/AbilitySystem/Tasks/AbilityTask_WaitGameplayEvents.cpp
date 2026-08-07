

#include "AbilitySystem/Tasks/AbilityTask_WaitGameplayEvents.h"

#include "AbilitySystem/IMGAbilitySystemComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemLog.h"

UAbilityTask_WaitGameplayEvents::UAbilityTask_WaitGameplayEvents(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UIMGAbilitySystemComponent* UAbilityTask_WaitGameplayEvents::GetTargetASC()
{
	return Cast<UIMGAbilitySystemComponent>(AbilitySystemComponent);
}

UAbilityTask_WaitGameplayEvents* UAbilityTask_WaitGameplayEvents::WaitGameplayEvents(UGameplayAbility* OwningAbility, FName TaskInstanceName, FGameplayTagContainer InEventTags)
{
	UAbilityTask_WaitGameplayEvents* MyObj = NewAbilityTask<UAbilityTask_WaitGameplayEvents>(OwningAbility, TaskInstanceName);
	MyObj->EventTags = InEventTags;
	return MyObj;
}

void UAbilityTask_WaitGameplayEvents::Activate()
{
	if (!Ability)
	{
		return;
	}

	UIMGAbilitySystemComponent* ActAbilitySystemComponent = GetTargetASC();
	if (ActAbilitySystemComponent)
	{
		EventHandle = ActAbilitySystemComponent->AddGameplayEventTagContainerDelegate(
			EventTags,
			FGameplayEventTagMulticastDelegate::FDelegate::CreateUObject(this, &UAbilityTask_WaitGameplayEvents::OnGameplayEvent));
	}
	else
	{
		ABILITY_LOG(Warning, TEXT("UAbilityTask_WaitGameplayEvents called on invalid AbilitySystemComponent"));
	}

	SetWaitingOnAvatar();
}

void UAbilityTask_WaitGameplayEvents::ExternalCancel()
{
	Super::ExternalCancel();
}

void UAbilityTask_WaitGameplayEvents::OnDestroy(bool AbilityEnded)
{
	UIMGAbilitySystemComponent* ActAbilitySystemComponent = GetTargetASC();
	if (ActAbilitySystemComponent)
	{
		ActAbilitySystemComponent->RemoveGameplayEventTagContainerDelegate(EventTags, EventHandle);
	}

	Super::OnDestroy(AbilityEnded);
}

void UAbilityTask_WaitGameplayEvents::OnGameplayEvent(FGameplayTag EventTag, const FGameplayEventData* Payload)
{
	if (!ShouldBroadcastAbilityTaskDelegates())
	{
		return;
	}

	FGameplayEventData TempData = Payload ? *Payload : FGameplayEventData();
	TempData.EventTag = EventTag;

	EventReceived.Broadcast(EventTag, TempData);
}

FString UAbilityTask_WaitGameplayEvents::GetDebugString() const
{
	return FString::Printf(TEXT("WaitGameplayEvents. EventTags: %s"), *EventTags.ToString());
}
