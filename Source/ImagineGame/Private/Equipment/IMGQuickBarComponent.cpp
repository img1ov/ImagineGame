

#include "Equipment/IMGQuickBarComponent.h"

#include "Equipment/IMGEquipmentDefinition.h"
#include "Equipment/IMGEquipmentInstance.h"
#include "Equipment/IMGEquipmentManagerComponent.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Inventory/InventoryFragment_EquippableItem.h"
#include "NativeGameplayTags.h"
#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGQuickBarComponent)

class FLifetimeProperty;
class UIMGEquipmentDefinition;


UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_IMG_QuickBar_Message_SlotsChanged, "IMG.QuickBar.Message.SlotsChanged");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_IMG_QuickBar_Message_ActiveIndexChanged, "IMG.QuickBar.Message.ActiveIndexChanged");

UIMGQuickBarComponent::UIMGQuickBarComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsReplicatedByDefault(true);
}

void UIMGQuickBarComponent::GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ThisClass, Slots);
	DOREPLIFETIME(ThisClass, ActiveSlotIndex);
}

void UIMGQuickBarComponent::BeginPlay()
{
	bEndingPlay = false;
	if (Slots.Num() < NumSlots)
	{
		Slots.AddDefaulted(NumSlots - Slots.Num());
	}
	if (HasAuthority())
	{
		if (AController* Controller = GetController<AController>())
		{
			Controller->OnPossessedPawnChanged.AddUniqueDynamic(this, &ThisClass::HandlePossessedPawnChanged);
		}
	}

	Super::BeginPlay();
}

void UIMGQuickBarComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndingPlay = true;
	++SelectionRevision;
	if (AController* Controller = GetController<AController>())
	{
		Controller->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::HandlePossessedPawnChanged);
	}
	if (HasAuthority())
	{
		ActiveSlotIndex = -1;
		ActiveSlotPawn.Reset();
		UnequipItemInSlot();
	}
	Super::EndPlay(EndPlayReason);
}

void UIMGQuickBarComponent::HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	if (HasAuthority() && !bEndingPlay && OldPawn != NewPawn && NewPawn == GetPawn<APawn>() && ActiveSlotIndex != INDEX_NONE &&
		(!NewPawn || ActiveSlotPawn.Get() != NewPawn))
	{
		const uint32 Revision = ++SelectionRevision;
		ActiveSlotIndex = -1;
		ActiveSlotPawn.Reset();
		UnequipItemInSlot();
		if (Revision == SelectionRevision)
		{
			OnRep_ActiveSlotIndex();
		}
	}
}

void UIMGQuickBarComponent::CycleActiveSlotForward()
{
	if (Slots.Num() < 2)
	{
		return;
	}

	const int32 OldIndex = (ActiveSlotIndex < 0 ? Slots.Num()-1 : ActiveSlotIndex);
	int32 NewIndex = ActiveSlotIndex;
	do
	{
		NewIndex = (NewIndex + 1) % Slots.Num();
		if (Slots[NewIndex] != nullptr)
		{
			SetActiveSlotIndex(NewIndex);
			return;
		}
	} while (NewIndex != OldIndex);
}

void UIMGQuickBarComponent::CycleActiveSlotBackward()
{
	if (Slots.Num() < 2)
	{
		return;
	}

	const int32 OldIndex = (ActiveSlotIndex < 0 ? Slots.Num()-1 : ActiveSlotIndex);
	int32 NewIndex = ActiveSlotIndex;
	do
	{
		NewIndex = (NewIndex - 1 + Slots.Num()) % Slots.Num();
		if (Slots[NewIndex] != nullptr)
		{
			SetActiveSlotIndex(NewIndex);
			return;
		}
	} while (NewIndex != OldIndex);
}

void UIMGQuickBarComponent::EquipItemInSlot()
{
	check(Slots.IsValidIndex(ActiveSlotIndex));
	check(EquippedItem == nullptr);

	if (UIMGInventoryItemInstance* SlotItem = Slots[ActiveSlotIndex])
	{
		if (const UInventoryFragment_EquippableItem* EquipInfo = SlotItem->FindFragmentByClass<UInventoryFragment_EquippableItem>())
		{
			TSubclassOf<UIMGEquipmentDefinition> EquipDef = EquipInfo->EquipmentDefinition;
			if (EquipDef != nullptr)
			{
				if (UIMGEquipmentManagerComponent* EquipmentManager = FindEquipmentManager())
				{
					const uint32 Revision = SelectionRevision;
					UIMGEquipmentInstance* NewEquipment = EquipmentManager->EquipItem(EquipDef);
					if (IsValid(NewEquipment))
					{
						// Actor construction or OnEquipped can change the selection or possession synchronously.
						if (Revision == SelectionRevision && ActiveSlotPawn.Get() == NewEquipment->GetPawn() &&
							ActiveSlotPawn.Get() == GetPawn<APawn>())
						{
							EquippedItem = NewEquipment;
							EquippedItem->SetInstigator(SlotItem);
						}
						else if (IsValid(EquipmentManager))
						{
							EquipmentManager->UnequipItem(NewEquipment);
						}
					}
				}
			}
		}
	}
}

void UIMGQuickBarComponent::UnequipItemInSlot()
{
	UIMGEquipmentInstance* PreviousItem = EquippedItem;
	EquippedItem = nullptr;
	if (IsValid(PreviousItem))
	{
		// The Controller may already possess a different Pawn when this callback runs.
		if (APawn* PreviousPawn = PreviousItem->GetPawn(); IsValid(PreviousPawn))
		{
			if (UIMGEquipmentManagerComponent* EquipmentManager = PreviousPawn->FindComponentByClass<UIMGEquipmentManagerComponent>())
			{
				EquipmentManager->UnequipItem(PreviousItem);
			}
		}
	}
}

UIMGEquipmentManagerComponent* UIMGQuickBarComponent::FindEquipmentManager() const
{
	if (AController* OwnerController = Cast<AController>(GetOwner()))
	{
		if (APawn* Pawn = OwnerController->GetPawn())
		{
			return Pawn->FindComponentByClass<UIMGEquipmentManagerComponent>();
		}
	}
	return nullptr;
}

void UIMGQuickBarComponent::SetActiveSlotIndex_Implementation(int32 NewIndex)
{
	if (bEndingPlay || !Slots.IsValidIndex(NewIndex))
	{
		return;
	}
	APawn* Pawn = GetPawn<APawn>();
	if (ActiveSlotIndex == NewIndex && ActiveSlotPawn.Get() == Pawn)
	{
		return;
	}
	const uint32 Revision = ++SelectionRevision;
	UnequipItemInSlot();
	if (Revision != SelectionRevision || Pawn != GetPawn<APawn>())
	{
		return;
	}
	ActiveSlotIndex = NewIndex;
	ActiveSlotPawn = Pawn;
	EquipItemInSlot();
	if (Revision == SelectionRevision)
	{
		OnRep_ActiveSlotIndex();
	}
}

UIMGInventoryItemInstance* UIMGQuickBarComponent::GetActiveSlotItem() const
{
	return Slots.IsValidIndex(ActiveSlotIndex) ? Slots[ActiveSlotIndex] : nullptr;
}

int32 UIMGQuickBarComponent::GetNextFreeItemSlot() const
{
	int32 SlotIndex = 0;
	for (const TObjectPtr<UIMGInventoryItemInstance>& ItemPtr : Slots)
	{
		if (ItemPtr == nullptr)
		{
			return SlotIndex;
		}
		++SlotIndex;
	}

	return INDEX_NONE;
}

void UIMGQuickBarComponent::AddItemToSlot(int32 SlotIndex, UIMGInventoryItemInstance* Item)
{
	if (Slots.IsValidIndex(SlotIndex) && (Item != nullptr))
	{
		if (Slots[SlotIndex] == nullptr)
		{
			Slots[SlotIndex] = Item;
			OnRep_Slots();
		}
	}
}

UIMGInventoryItemInstance* UIMGQuickBarComponent::RemoveItemFromSlot(int32 SlotIndex)
{
	if (!Slots.IsValidIndex(SlotIndex))
	{
		return nullptr;
	}
	UIMGInventoryItemInstance* Result = Slots[SlotIndex];
	Slots[SlotIndex] = nullptr;

	if (ActiveSlotIndex == SlotIndex)
	{
		const uint32 Revision = ++SelectionRevision;
		ActiveSlotIndex = -1;
		ActiveSlotPawn.Reset();
		UnequipItemInSlot();
		if (Revision == SelectionRevision)
		{
			OnRep_ActiveSlotIndex();
		}
	}
	if (Result)
	{
		OnRep_Slots();
	}

	return Result;
}

void UIMGQuickBarComponent::OnRep_Slots()
{
	FIMGQuickBarSlotsChangedMessage Message;
	Message.Owner = GetOwner();
	Message.Slots = Slots;

	UGameplayMessageSubsystem& MessageSystem = UGameplayMessageSubsystem::Get(this);
	MessageSystem.BroadcastMessage(TAG_IMG_QuickBar_Message_SlotsChanged, Message);
}

void UIMGQuickBarComponent::OnRep_ActiveSlotIndex()
{
	FIMGQuickBarActiveIndexChangedMessage Message;
	Message.Owner = GetOwner();
	Message.ActiveIndex = ActiveSlotIndex;

	UGameplayMessageSubsystem& MessageSystem = UGameplayMessageSubsystem::Get(this);
	MessageSystem.BroadcastMessage(TAG_IMG_QuickBar_Message_ActiveIndexChanged, Message);
}
