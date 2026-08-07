// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Components/ActorComponent.h"
#include "Net/Serialization/FastArraySerializer.h"

#include "IMGInventoryManagerComponent.generated.h"

#define UE_API IMAGINEGAME_API

class UIMGInventoryItemDefinition;
class UIMGInventoryItemInstance;
class UIMGInventoryManagerComponent;
class UObject;
struct FFrame;
struct FIMGInventoryList;
struct FNetDeltaSerializeInfo;
struct FReplicationFlags;

/** A message when an item is added to the inventory */
USTRUCT(BlueprintType)
struct FIMGInventoryChangeMessage
{
	GENERATED_BODY()

	//@TODO: Tag based names+owning actors for inventories instead of directly exposing the component?
	UPROPERTY(BlueprintReadOnly, Category=Inventory)
	TObjectPtr<UActorComponent> InventoryOwner = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = Inventory)
	TObjectPtr<UIMGInventoryItemInstance> Instance = nullptr;

	UPROPERTY(BlueprintReadOnly, Category=Inventory)
	int32 NewCount = 0;

	UPROPERTY(BlueprintReadOnly, Category=Inventory)
	int32 Delta = 0;
};

/** A single entry in an inventory */
USTRUCT(BlueprintType)
struct FIMGInventoryEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	FIMGInventoryEntry()
	{}

	FString GetDebugString() const;

private:
	friend FIMGInventoryList;
	friend UIMGInventoryManagerComponent;

	UPROPERTY()
	TObjectPtr<UIMGInventoryItemInstance> Instance = nullptr;

	UPROPERTY()
	int32 StackCount = 0;

	UPROPERTY(NotReplicated)
	int32 LastObservedCount = INDEX_NONE;
};

/** List of inventory items */
USTRUCT(BlueprintType)
struct FIMGInventoryList : public FFastArraySerializer
{
	GENERATED_BODY()

	FIMGInventoryList()
		: OwnerComponent(nullptr)
	{
	}

	FIMGInventoryList(UActorComponent* InOwnerComponent)
		: OwnerComponent(InOwnerComponent)
	{
	}

	TArray<UIMGInventoryItemInstance*> GetAllItems() const;

public:
	//~FFastArraySerializer contract
	void PreReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize);
	void PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize);
	void PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize);
	//~End of FFastArraySerializer contract

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FIMGInventoryEntry, FIMGInventoryList>(Entries, DeltaParms, *this);
	}

	UIMGInventoryItemInstance* AddEntry(TSubclassOf<UIMGInventoryItemDefinition> ItemClass, int32 StackCount);
	void AddEntry(UIMGInventoryItemInstance* Instance);

	void RemoveEntry(UIMGInventoryItemInstance* Instance);

private:
	void BroadcastChangeMessage(FIMGInventoryEntry& Entry, int32 OldCount, int32 NewCount);

private:
	friend UIMGInventoryManagerComponent;

private:
	// Replicated list of items
	UPROPERTY()
	TArray<FIMGInventoryEntry> Entries;

	UPROPERTY(NotReplicated)
	TObjectPtr<UActorComponent> OwnerComponent;
};

template<>
struct TStructOpsTypeTraits<FIMGInventoryList> : public TStructOpsTypeTraitsBase2<FIMGInventoryList>
{
	enum { WithNetDeltaSerializer = true };
};

//////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Manages an inventory
 */
UCLASS(MinimalAPI, BlueprintType)
class UIMGInventoryManagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	UE_API UIMGInventoryManagerComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category=Inventory)
	UE_API bool CanAddItemDefinition(TSubclassOf<UIMGInventoryItemDefinition> ItemDef, int32 StackCount = 1);
	
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category=Inventory)
	UE_API UIMGInventoryItemInstance* AddItemDefinition(TSubclassOf<UIMGInventoryItemDefinition> ItemDef, int32 StackCount = 1);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category=Inventory)
	UE_API void AddItemInstance(UIMGInventoryItemInstance* ItemInstance);
	
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category=Inventory)
	UE_API void RemoveItemInstance(UIMGInventoryItemInstance* ItemInstance);
	
	UFUNCTION(BlueprintCallable, Category=Inventory, BlueprintPure=false)
	UE_API TArray<UIMGInventoryItemInstance*> GetAllItems() const;
	
	UFUNCTION(BlueprintCallable, Category=Inventory, BlueprintPure)
	UE_API UIMGInventoryItemInstance* FindFirstItemStackByDefinition(TSubclassOf<UIMGInventoryItemDefinition> ItemDef) const;

	UE_API int32 GetTotalItemCountByDefinition(TSubclassOf<UIMGInventoryItemDefinition> ItemDef) const;
	UE_API bool ConsumeItemsByDefinition(TSubclassOf<UIMGInventoryItemDefinition> ItemDef, int32 NumToConsume);

	//~UObject interface
	UE_API virtual bool ReplicateSubobjects(class UActorChannel* Channel, class FOutBunch* Bunch, FReplicationFlags* RepFlags) override;
	UE_API virtual void ReadyForReplication() override;
	//~End of UObject interface
	
private:
	UPROPERTY(Replicated)
	FIMGInventoryList InventoryList;
};

#undef UE_API