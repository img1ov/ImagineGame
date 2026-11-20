
#pragma once

#include "AbilitySystem/IMGAbilitySet.h"
#include "Components/PawnComponent.h"
#include "Net/Serialization/FastArraySerializer.h"

#include "IMGEquipmentManagerComponent.generated.h"

#define UE_API IMAGINEGAME_API

class UActorComponent;
class UIMGAbilitySystemComponent;
class UIMGEquipmentDefinition;
class UIMGEquipmentInstance;
class UIMGEquipmentManagerComponent;
class UObject;
struct FFrame;
struct FIMGEquipmentList;
struct FNetDeltaSerializeInfo;
struct FReplicationFlags;

/** A single piece of applied equipment */
USTRUCT(BlueprintType)
struct FIMGAppliedEquipmentEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	FIMGAppliedEquipmentEntry()
	{}

	FString GetDebugString() const;

private:
	friend FIMGEquipmentList;
	friend UIMGEquipmentManagerComponent;

	// The equipment class that got equipped
	UPROPERTY()
	TSubclassOf<UIMGEquipmentDefinition> EquipmentDefinition;

	UPROPERTY()
	TObjectPtr<UIMGEquipmentInstance> Instance = nullptr;

	// Local delivery state; FastArray references may be unmapped on the first add callback.
	UPROPERTY(NotReplicated)
	TWeakObjectPtr<UIMGEquipmentInstance> NotifiedInstance;

	// Revoke on the ASC that received the grants, even after the Pawn's ASC binding changes.
	UPROPERTY(NotReplicated)
	TWeakObjectPtr<UIMGAbilitySystemComponent> GrantingAbilitySystem;

	// Authority-only list of granted handles
	UPROPERTY(NotReplicated)
	FIMGAbilitySet_GrantedHandles GrantedHandles;

	bool bRemoving = false;
};

/** List of applied equipment */
USTRUCT(BlueprintType)
struct FIMGEquipmentList : public FFastArraySerializer
{
	GENERATED_BODY()

	FIMGEquipmentList()
		: OwnerComponent(nullptr)
	{
	}

	FIMGEquipmentList(UActorComponent* InOwnerComponent)
		: OwnerComponent(InOwnerComponent)
	{
	}

public:
	//~FFastArraySerializer contract
	void PreReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize);
	void PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize);
	void PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize);
	//~End of FFastArraySerializer contract

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FIMGAppliedEquipmentEntry, FIMGEquipmentList>(Entries, DeltaParms, *this);
	}

	UIMGEquipmentInstance* AddEntry(TSubclassOf<UIMGEquipmentDefinition> EquipmentDefinition);
	void RemoveEntry(UIMGEquipmentInstance* Instance);

private:
	UIMGAbilitySystemComponent* GetAbilitySystemComponent() const;
	void NotifyEntryEquipped(int32 Index);

	friend UIMGEquipmentManagerComponent;

private:
	// Replicated list of equipment entries
	UPROPERTY()
	TArray<FIMGAppliedEquipmentEntry> Entries;

	UPROPERTY(NotReplicated)
	TObjectPtr<UActorComponent> OwnerComponent;
};

template<>
struct TStructOpsTypeTraits<FIMGEquipmentList> : public TStructOpsTypeTraitsBase2<FIMGEquipmentList>
{
	enum { WithNetDeltaSerializer = true };
};

///////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Manages equipment applied to a pawn
 */
UCLASS(MinimalAPI, BlueprintType, Const)
class UIMGEquipmentManagerComponent : public UPawnComponent
{
	GENERATED_BODY()

public:
	UE_API UIMGEquipmentManagerComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly)
	UE_API UIMGEquipmentInstance* EquipItem(TSubclassOf<UIMGEquipmentDefinition> EquipmentDefinition);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly)
	UE_API void UnequipItem(UIMGEquipmentInstance* ItemInstance);

	//~UObject interface
	UE_API virtual bool ReplicateSubobjects(class UActorChannel* Channel, class FOutBunch* Bunch, FReplicationFlags* RepFlags) override;
	//~End of UObject interface

	//~UActorComponent interface
	UE_API virtual void InitializeComponent() override;
	UE_API virtual void BeginPlay() override;
	UE_API virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	UE_API virtual void UninitializeComponent() override;
	UE_API virtual void ReadyForReplication() override;
	//~End of UActorComponent interface

	/** Returns the first equipped instance of a given type, or nullptr if none are found */
	UFUNCTION(BlueprintCallable, BlueprintPure)
	UE_API UIMGEquipmentInstance* GetFirstInstanceOfType(TSubclassOf<UIMGEquipmentInstance> InstanceType);

 	/** Returns all equipped instances of a given type, or an empty array if none are found */
 	UFUNCTION(BlueprintCallable, BlueprintPure)
	UE_API TArray<UIMGEquipmentInstance*> GetEquipmentInstancesOfType(TSubclassOf<UIMGEquipmentInstance> InstanceType) const;

	template <typename T>
	T* GetFirstInstanceOfType()
	{
		return (T*)GetFirstInstanceOfType(T::StaticClass());
	}

private:
	friend FIMGEquipmentList;
	void ClearEquipment();
	bool bEndingPlay = false;

	UPROPERTY(Replicated)
	FIMGEquipmentList EquipmentList;
};

#undef UE_API
