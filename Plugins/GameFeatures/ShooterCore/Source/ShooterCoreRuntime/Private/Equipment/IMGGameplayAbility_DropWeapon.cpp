#include "Equipment/IMGGameplayAbility_DropWeapon.h"

#include "Equipment/IMGQuickBarComponent.h"
#include "GameFramework/Controller.h"
#include "Inventory/IMGInventoryItemInstance.h"
#include "Inventory/IMGInventoryManagerComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGGameplayAbility_DropWeapon)

UIMGGameplayAbility_DropWeapon::UIMGGameplayAbility_DropWeapon(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

void UIMGGameplayAbility_DropWeapon::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	AController* Controller = GetControllerFromActorInfo();
	UIMGQuickBarComponent* QuickBar = Controller ? Controller->FindComponentByClass<UIMGQuickBarComponent>() : nullptr;
	UIMGInventoryManagerComponent* Inventory = Controller ? Controller->FindComponentByClass<UIMGInventoryManagerComponent>() : nullptr;
	if (!QuickBar || !Inventory || !QuickBar->GetActiveSlotItem() || !CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		K2_EndAbility();
		return;
	}
	if (UIMGInventoryItemInstance* Item = QuickBar->RemoveItemFromSlot(QuickBar->GetActiveSlotIndex()))
	{
		// TODO: Spawn a world pickup that preserves the item's runtime state before removing it.
		Inventory->RemoveItemInstance(Item);
		QuickBar->CycleActiveSlotForward();
	}
	K2_EndAbility();
}
