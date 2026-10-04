#include "Weapons/IMGShooterWeaponSpawner.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Equipment/IMGPickupDefinition.h"
#include "Equipment/IMGQuickBarComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Inventory/IMGInventoryItemInstance.h"
#include "Inventory/IMGInventoryItemDefinition.h"
#include "Inventory/IMGInventoryManagerComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGShooterWeaponSpawner)

void AIMGShooterWeaponSpawner::AttemptPickUpWeapon_Implementation(APawn* Pawn)
{
	if (!HasAuthority() || !bIsWeaponAvailable || !Pawn || !UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Pawn))
	{
		return;
	}
	const TSubclassOf<UIMGInventoryItemDefinition> ItemDefinition = WeaponDefinition ? WeaponDefinition->InventoryItemDefinition : nullptr;
	if (ItemDefinition && GrantWeapon(ItemDefinition, Pawn))
	{
		bIsWeaponAvailable = false;
		SetWeaponPickupVisibility(false);
		PlayPickupEffects();
		StartCoolDown();
	}
}

bool AIMGShooterWeaponSpawner::GrantWeapon(TSubclassOf<UIMGInventoryItemDefinition> WeaponItemClass, APawn* ReceivingPawn)
{
	if (!HasAuthority() || !WeaponItemClass || !ReceivingPawn)
	{
		return false;
	}

	AController* Controller = ReceivingPawn->GetController();
	UIMGInventoryManagerComponent* Inventory = Controller ? Controller->FindComponentByClass<UIMGInventoryManagerComponent>() : nullptr;
	UIMGQuickBarComponent* QuickBar = Controller ? Controller->FindComponentByClass<UIMGQuickBarComponent>() : nullptr;
	if (!Inventory || !QuickBar)
	{
		return false;
	}

	if (bRefillDuplicateWeaponAmmo)
	{
		if (UIMGInventoryItemInstance* ExistingItem = Inventory->FindFirstItemStackByDefinition(WeaponItemClass))
		{
			if (!SpareAmmoTag.IsValid())
			{
			return false;
			}
			const int32 AmmoToAdd = GetDefaultStatFromItemDef(WeaponItemClass, SpareAmmoTag) - ExistingItem->GetStatTagStackCount(SpareAmmoTag);
			if (AmmoToAdd <= 0)
			{
			return false;
			}
			ExistingItem->AddStatTagStack(SpareAmmoTag, AmmoToAdd);
			return true;
		}
	}

	const int32 SlotIndex = QuickBar->GetNextFreeItemSlot();
	if (SlotIndex == INDEX_NONE || !Inventory->CanAddItemDefinition(WeaponItemClass, 1))
	{
		return false;
	}

	UIMGInventoryItemInstance* Item = Inventory->AddItemDefinition(WeaponItemClass, 1);
	if (!Item)
	{
		return false;
	}
	QuickBar->AddItemToSlot(SlotIndex, Item);
	if (QuickBar->GetSlots()[SlotIndex] != Item)
	{
		Inventory->RemoveItemInstance(Item);
		return false;
	}

	const UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(ReceivingPawn);
	if (!BlockAutoEquipTag.IsValid() || !ASC || !ASC->HasMatchingGameplayTag(BlockAutoEquipTag))
	{
		QuickBar->SetActiveSlotIndex(SlotIndex);
	}
	return true;
}
