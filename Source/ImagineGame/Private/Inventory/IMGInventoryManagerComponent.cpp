
#include "Inventory/IMGInventoryManagerComponent.h"

#include "Engine/ActorChannel.h"
#include "Engine/World.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "Inventory/IMGInventoryItemDefinition.h"
#include "Inventory/IMGInventoryItemInstance.h"
#include "NativeGameplayTags.h"
#include "Net/UnrealNetwork.h"
#include "UObject/StrongObjectPtr.h"

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


	Result = NewObject<UIMGInventoryItemInstance>(OwningActor);
	const TStrongObjectPtr<UIMGInventoryItemInstance> InstanceGuard(Result);
	Result->SetItemDef(ItemDef);
	for (UIMGInventoryItemFragment* Fragment : GetDefault<UIMGInventoryItemDefinition>(ItemDef)->Fragments)
	{
		if (Fragment != nullptr)
		{
			Fragment->OnInstanceCreated(Result);
		}
	}
	FIMGInventoryEntry& NewEntry = Entries.AddDefaulted_GetRef();
	NewEntry.Instance = Result;
	NewEntry.StackCount = StackCount;

	MarkItemDirty(NewEntry);

	return Result;
}

UIMGInventoryItemInstance* FIMGInventoryList::AddEntry(UIMGInventoryItemInstance* Instance)
{
	check(OwnerComponent && OwnerComponent->GetOwner()->HasAuthority());
	// Replicated objects keep their network outer. Copy foreign pickups instead of renaming them.
	UIMGInventoryItemInstance* OwnedInstance = Instance->GetOuter() == OwnerComponent->GetOwner()
		? Instance : DuplicateObject<UIMGInventoryItemInstance>(Instance, OwnerComponent->GetOwner());
	if (!OwnedInstance)
	{
		return nullptr;
	}
	FIMGInventoryEntry& NewEntry = Entries.AddDefaulted_GetRef();
	NewEntry.Instance = OwnedInstance;
	NewEntry.StackCount = 1;
	MarkItemDirty(NewEntry);
	return OwnedInstance;
}

void FIMGInventoryList::RemoveEntry(int32 Index)
{
	Entries.RemoveAt(Index);
	MarkArrayDirty();
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
	return GetOwner() && GetOwner()->HasAuthority() && ItemDef && StackCount > 0;
}

UIMGInventoryItemInstance* UIMGInventoryManagerComponent::AddItemDefinition(TSubclassOf<UIMGInventoryItemDefinition> ItemDef, int32 StackCount)
{
	UIMGInventoryItemInstance* Result = nullptr;
	if (CanAddItemDefinition(ItemDef, StackCount))
	{
		Result = InventoryList.AddEntry(ItemDef, StackCount);
		
		if (IsUsingRegisteredSubObjectList() && IsReadyForReplication() && Result)
		{
			AddReplicatedSubObject(Result);
		}
	}
	return Result;
}

UIMGInventoryItemInstance* UIMGInventoryManagerComponent::AddItemInstance(UIMGInventoryItemInstance* ItemInstance)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !IsValid(ItemInstance) || !ItemInstance->GetItemDef())
	{
		return nullptr;
	}
	if (InventoryList.Entries.ContainsByPredicate([ItemInstance](const FIMGInventoryEntry& Entry) { return Entry.Instance == ItemInstance; }))
	{
		return ItemInstance;
	}
	UIMGInventoryItemInstance* Result = InventoryList.AddEntry(ItemInstance);
	if (IsUsingRegisteredSubObjectList() && IsReadyForReplication() && Result)
	{
		AddReplicatedSubObject(Result);
	}
	return Result;
}

void UIMGInventoryManagerComponent::RemoveItemInstance(UIMGInventoryItemInstance* ItemInstance)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !ItemInstance)
	{
		return;
	}
	const int32 Index = InventoryList.Entries.IndexOfByPredicate([ItemInstance](const FIMGInventoryEntry& Entry) { return Entry.Instance == ItemInstance; });
	if (Index != INDEX_NONE)
	{
		RemoveItemAt(Index);
	}
}

void UIMGInventoryManagerComponent::RemoveItemAt(int32 Index)
{
	UIMGInventoryItemInstance* Instance = InventoryList.Entries[Index].Instance;
	if (Instance && IsUsingRegisteredSubObjectList())
	{
		RemoveReplicatedSubObject(Instance);
	}
	InventoryList.RemoveEntry(Index);
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
			if (Instance->GetItemDef() == ItemDef && Entry.StackCount > 0)
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
			if (Instance->GetItemDef() == ItemDef && Entry.StackCount > 0)
			{
				TotalCount = static_cast<int32>(FMath::Min<int64>(static_cast<int64>(TotalCount) + Entry.StackCount, MAX_int32));
			}
		}
	}

	return TotalCount;
}

bool UIMGInventoryManagerComponent::ConsumeItemsByDefinition(TSubclassOf<UIMGInventoryItemDefinition> ItemDef, int32 NumToConsume)
{
	AActor* OwningActor = GetOwner();
	if (!OwningActor || !OwningActor->HasAuthority() || !ItemDef || NumToConsume < 0 ||
		GetTotalItemCountByDefinition(ItemDef) < NumToConsume)
	{
		return false;
	}

	int32 Remaining = NumToConsume;
	for (int32 Index = InventoryList.Entries.Num() - 1; Index >= 0 && Remaining > 0; --Index)
	{
		FIMGInventoryEntry& Entry = InventoryList.Entries[Index];
		if (!IsValid(Entry.Instance) || Entry.Instance->GetItemDef() != ItemDef || Entry.StackCount <= 0)
		{
			continue;
		}
		const int32 Count = FMath::Min(Entry.StackCount, Remaining);
		Remaining -= Count;
		if (Count == Entry.StackCount)
		{
			RemoveItemAt(Index);
		}
		else
		{
			Entry.StackCount -= Count;
			InventoryList.MarkItemDirty(Entry);
		}
	}

	return Remaining == 0;
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
