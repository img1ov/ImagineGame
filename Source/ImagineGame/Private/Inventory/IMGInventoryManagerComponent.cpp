// Fill out your copyright notice in the Description page of Project Settings.

#include "Inventory/IMGInventoryManagerComponent.h"

#include "Engine/ActorChannel.h"
#include "Engine/World.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "Inventory/IMGInventoryItemDefinition.h"
#include "Inventory/IMGInventoryItemInstance.h"
#include "NativeGameplayTags.h"
#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGInventoryManagerComponent)

class FLifetimeProperty;
struct FReplicationFlags;


UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_IMG_Inventory_Message_StackChanged, "IMG.Inventory.Message.StackChanged");

//////////////////////////////////////////////////////////////////////
// FIMGInventoryEntry

FString FIMGInventoryEntry::GetDebugString() const
{
	TSubclassOf<UIMGInventoryItemDefinition> ItemDef;
	if (Instance != nullptr)
	{
		ItemDef = Instance->GetItemDef();
	}

	return FString::Printf(TEXT("%s (%d x %s)"), *GetNameSafe(Instance), StackCount, *GetNameSafe(ItemDef));
}

//////////////////////////////////////////////////////////////////////
// FIMGInventoryList

void FIMGInventoryList::PreReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize)
{
	for (int32 Index : RemovedIndices)
	{
		FIMGInventoryEntry& Stack = Entries[Index];
		BroadcastChangeMessage(Stack, /*OldCount=*/ Stack.StackCount, /*NewCount=*/ 0);
		Stack.LastObservedCount = 0;
	}
}

void FIMGInventoryList::PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize)
{
	for (int32 Index : AddedIndices)
	{
		FIMGInventoryEntry& Stack = Entries[Index];
		BroadcastChangeMessage(Stack, /*OldCount=*/ 0, /*NewCount=*/ Stack.StackCount);
		Stack.LastObservedCount = Stack.StackCount;
	}
}

void FIMGInventoryList::PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize)
{
	for (int32 Index : ChangedIndices)
	{
		FIMGInventoryEntry& Stack = Entries[Index];
		check(Stack.LastObservedCount != INDEX_NONE);
		BroadcastChangeMessage(Stack, /*OldCount=*/ Stack.LastObservedCount, /*NewCount=*/ Stack.StackCount);
		Stack.LastObservedCount = Stack.StackCount;
	}
}

void FIMGInventoryList::BroadcastChangeMessage(FIMGInventoryEntry& Entry, int32 OldCount, int32 NewCount)
{
	FIMGInventoryChangeMessage Message;
	Message.InventoryOwner = OwnerComponent;
	Message.Instance = Entry.Instance;
	Message.NewCount = NewCount;
	Message.Delta = NewCount - OldCount;

	UGameplayMessageSubsystem& MessageSystem = UGameplayMessageSubsystem::Get(OwnerComponent->GetWorld());
	MessageSystem.BroadcastMessage(TAG_IMG_Inventory_Message_StackChanged, Message);
}

UIMGInventoryItemInstance* FIMGInventoryList::AddEntry(TSubclassOf<UIMGInventoryItemDefinition> ItemDef, int32 StackCount)
{
	UIMGInventoryItemInstance* Result = nullptr;

	check(ItemDef != nullptr);
 	check(OwnerComponent);

	AActor* OwningActor = OwnerComponent->GetOwner();
	check(OwningActor->HasAuthority());


	FIMGInventoryEntry& NewEntry = Entries.AddDefaulted_GetRef();
	NewEntry.Instance = NewObject<UIMGInventoryItemInstance>(OwnerComponent->GetOwner());  //@TODO: Using the actor instead of component as the outer due to UE-127172
	NewEntry.Instance->SetItemDef(ItemDef);
	for (UIMGInventoryItemFragment* Fragment : GetDefault<UIMGInventoryItemDefinition>(ItemDef)->Fragments)
	{
		if (Fragment != nullptr)
		{
			Fragment->OnInstanceCreated(NewEntry.Instance);
		}
	}
	NewEntry.StackCount = StackCount;
	Result = NewEntry.Instance;

	//const UIMGInventoryItemDefinition* ItemCDO = GetDefault<UIMGInventoryItemDefinition>(ItemDef);
	MarkItemDirty(NewEntry);

	return Result;
}

void FIMGInventoryList::AddEntry(UIMGInventoryItemInstance* Instance)
{
	unimplemented();
}

void FIMGInventoryList::RemoveEntry(UIMGInventoryItemInstance* Instance)
{
	for (auto EntryIt = Entries.CreateIterator(); EntryIt; ++EntryIt)
	{
		FIMGInventoryEntry& Entry = *EntryIt;
		if (Entry.Instance == Instance)
		{
			EntryIt.RemoveCurrent();
			MarkArrayDirty();
		}
	}
}

TArray<UIMGInventoryItemInstance*> FIMGInventoryList::GetAllItems() const
{
	TArray<UIMGInventoryItemInstance*> Results;
	Results.Reserve(Entries.Num());
	for (const FIMGInventoryEntry& Entry : Entries)
	{
		if (Entry.Instance != nullptr) //@TODO: Would prefer to not deal with this here and hide it further?
		{
			Results.Add(Entry.Instance);
		}
	}
	return Results;
}

//////////////////////////////////////////////////////////////////////
// UIMGInventoryManagerComponent

UIMGInventoryManagerComponent::UIMGInventoryManagerComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, InventoryList(this)
{
	SetIsReplicatedByDefault(true);
}

void UIMGInventoryManagerComponent::GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ThisClass, InventoryList);
}

bool UIMGInventoryManagerComponent::CanAddItemDefinition(TSubclassOf<UIMGInventoryItemDefinition> ItemDef, int32 StackCount)
{
	//@TODO: Add support for stack limit / uniqueness checks / etc...
	return true;
}

UIMGInventoryItemInstance* UIMGInventoryManagerComponent::AddItemDefinition(TSubclassOf<UIMGInventoryItemDefinition> ItemDef, int32 StackCount)
{
	UIMGInventoryItemInstance* Result = nullptr;
	if (ItemDef != nullptr)
	{
		Result = InventoryList.AddEntry(ItemDef, StackCount);
		
		if (IsUsingRegisteredSubObjectList() && IsReadyForReplication() && Result)
		{
			AddReplicatedSubObject(Result);
		}
	}
	return Result;
}

void UIMGInventoryManagerComponent::AddItemInstance(UIMGInventoryItemInstance* ItemInstance)
{
	InventoryList.AddEntry(ItemInstance);
	if (IsUsingRegisteredSubObjectList() && IsReadyForReplication() && ItemInstance)
	{
		AddReplicatedSubObject(ItemInstance);
	}
}

void UIMGInventoryManagerComponent::RemoveItemInstance(UIMGInventoryItemInstance* ItemInstance)
{
	InventoryList.RemoveEntry(ItemInstance);

	if (ItemInstance && IsUsingRegisteredSubObjectList())
	{
		RemoveReplicatedSubObject(ItemInstance);
	}
}

TArray<UIMGInventoryItemInstance*> UIMGInventoryManagerComponent::GetAllItems() const
{
	return InventoryList.GetAllItems();
}

UIMGInventoryItemInstance* UIMGInventoryManagerComponent::FindFirstItemStackByDefinition(TSubclassOf<UIMGInventoryItemDefinition> ItemDef) const
{
	for (const FIMGInventoryEntry& Entry : InventoryList.Entries)
	{
		UIMGInventoryItemInstance* Instance = Entry.Instance;

		if (IsValid(Instance))
		{
			if (Instance->GetItemDef() == ItemDef)
			{
				return Instance;
			}
		}
	}

	return nullptr;
}

int32 UIMGInventoryManagerComponent::GetTotalItemCountByDefinition(TSubclassOf<UIMGInventoryItemDefinition> ItemDef) const
{
	int32 TotalCount = 0;
	for (const FIMGInventoryEntry& Entry : InventoryList.Entries)
	{
		UIMGInventoryItemInstance* Instance = Entry.Instance;

		if (IsValid(Instance))
		{
			if (Instance->GetItemDef() == ItemDef)
			{
				++TotalCount;
			}
		}
	}

	return TotalCount;
}

bool UIMGInventoryManagerComponent::ConsumeItemsByDefinition(TSubclassOf<UIMGInventoryItemDefinition> ItemDef, int32 NumToConsume)
{
	AActor* OwningActor = GetOwner();
	if (!OwningActor || !OwningActor->HasAuthority())
	{
		return false;
	}

	//@TODO: N squared right now as there's no acceleration structure
	int32 TotalConsumed = 0;
	while (TotalConsumed < NumToConsume)
	{
		if (UIMGInventoryItemInstance* Instance = UIMGInventoryManagerComponent::FindFirstItemStackByDefinition(ItemDef))
		{
			InventoryList.RemoveEntry(Instance);
			++TotalConsumed;
		}
		else
		{
			return false;
		}
	}

	return TotalConsumed == NumToConsume;
}

void UIMGInventoryManagerComponent::ReadyForReplication()
{
	Super::ReadyForReplication();

	// Register existing UIMGInventoryItemInstance
	if (IsUsingRegisteredSubObjectList())
	{
		for (const FIMGInventoryEntry& Entry : InventoryList.Entries)
		{
			UIMGInventoryItemInstance* Instance = Entry.Instance;

			if (IsValid(Instance))
			{
				AddReplicatedSubObject(Instance);
			}
		}
	}
}

bool UIMGInventoryManagerComponent::ReplicateSubobjects(UActorChannel* Channel, class FOutBunch* Bunch, FReplicationFlags* RepFlags)
{
	bool WroteSomething = Super::ReplicateSubobjects(Channel, Bunch, RepFlags);

	for (FIMGInventoryEntry& Entry : InventoryList.Entries)
	{
		UIMGInventoryItemInstance* Instance = Entry.Instance;

		if (Instance && IsValid(Instance))
		{
			WroteSomething |= Channel->ReplicateSubobject(Instance, *Bunch, *RepFlags);
		}
	}

	return WroteSomething;
}

//////////////////////////////////////////////////////////////////////
//

// UCLASS(Abstract)
// class UIMGInventoryFilter : public UObject
// {
// public:
// 	virtual bool PassesFilter(UIMGInventoryItemInstance* Instance) const { return true; }
// };

// UCLASS()
// class UIMGInventoryFilter_HasTag : public UIMGInventoryFilter
// {
// public:
// 	virtual bool PassesFilter(UIMGInventoryItemInstance* Instance) const { return true; }
// };