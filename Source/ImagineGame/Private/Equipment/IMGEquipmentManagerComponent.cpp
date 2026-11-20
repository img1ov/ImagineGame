
#include "Equipment/IMGEquipmentManagerComponent.h"

#include "AbilitySystem/IMGAbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Engine/ActorChannel.h"
#include "Equipment/IMGEquipmentDefinition.h"
#include "Equipment/IMGEquipmentInstance.h"
#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGEquipmentManagerComponent)

class FLifetimeProperty;
struct FReplicationFlags;

//////////////////////////////////////////////////////////////////////
// FIMGAppliedEquipmentEntry

FString FIMGAppliedEquipmentEntry::GetDebugString() const
{
	return FString::Printf(TEXT("%s of %s"), *GetNameSafe(Instance), *GetNameSafe(EquipmentDefinition.Get()));
}

//////////////////////////////////////////////////////////////////////
// FIMGEquipmentList

void FIMGEquipmentList::PreReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize)
{
 	for (int32 Index : RemovedIndices)
 	{
		FIMGAppliedEquipmentEntry& Entry = Entries[Index];
		UIMGEquipmentInstance* Instance = Entry.NotifiedInstance.Get();
		Entry.NotifiedInstance.Reset();
		if (Instance)
		{
			Instance->NotifyUnequipped();
		}
 	}
}

void FIMGEquipmentList::PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize)
{
	for (int32 Index : AddedIndices)
	{
		NotifyEntryEquipped(Index);
	}
}

void FIMGEquipmentList::PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize)
{
	for (int32 Index : ChangedIndices)
	{
		NotifyEntryEquipped(Index);
	}
}

void FIMGEquipmentList::NotifyEntryEquipped(int32 Index)
{
	const UIMGEquipmentManagerComponent* Manager = Cast<UIMGEquipmentManagerComponent>(OwnerComponent);
	if (!Entries.IsValidIndex(Index) || !IsValid(Manager) || Manager->bEndingPlay || !Manager->HasBegunPlay() || Manager->IsBeingDestroyed() ||
		!IsValid(Manager->GetOwner()) || Manager->GetOwner()->IsActorBeingDestroyed())
	{
		return;
	}
	FIMGAppliedEquipmentEntry& Entry = Entries[Index];
	UIMGEquipmentInstance* Previous = Entry.NotifiedInstance.Get();
	UIMGEquipmentInstance* Current = IsValid(Entry.Instance) && Entry.EquipmentDefinition ? Entry.Instance.Get() : nullptr;
	if (Previous == Current)
	{
		return;
	}
	const TSubclassOf<UIMGEquipmentDefinition> Definition = Entry.EquipmentDefinition;
	Entry.NotifiedInstance = Current;
	if (Previous)
	{
		Previous->NotifyUnequipped();
	}
	if (IsValid(Current) && IsValid(Manager) && !Manager->bEndingPlay &&
		Entries.IsValidIndex(Index) && Entries[Index].NotifiedInstance.Get() == Current)
	{
		Current->NotifyEquipped(Definition);
	}
}

UIMGAbilitySystemComponent* FIMGEquipmentList::GetAbilitySystemComponent() const
{
	check(OwnerComponent);
	AActor* OwningActor = OwnerComponent->GetOwner();
	return Cast<UIMGAbilitySystemComponent>(UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OwningActor));
}

UIMGEquipmentInstance* FIMGEquipmentList::AddEntry(TSubclassOf<UIMGEquipmentDefinition> EquipmentDefinition)
{
	UIMGEquipmentInstance* Result = nullptr;

	check(EquipmentDefinition != nullptr);
 	check(OwnerComponent);
	check(OwnerComponent->GetOwner()->HasAuthority());
	
	const UIMGEquipmentDefinition* EquipmentCDO = GetDefault<UIMGEquipmentDefinition>(EquipmentDefinition);

	TSubclassOf<UIMGEquipmentInstance> InstanceType = EquipmentCDO->InstanceType;
	if (InstanceType == nullptr)
	{
		InstanceType = UIMGEquipmentInstance::StaticClass();
	}
	
	FIMGAppliedEquipmentEntry& NewEntry = Entries.AddDefaulted_GetRef();
	NewEntry.EquipmentDefinition = EquipmentDefinition;
	NewEntry.Instance = NewObject<UIMGEquipmentInstance>(OwnerComponent->GetOwner(), InstanceType);  //@TODO: Using the actor instead of component as the outer due to UE-127172
	Result = NewEntry.Instance;

	// Ability grants and actor construction may synchronously remove this entry or resize the list.
	FIMGAbilitySet_GrantedHandles GrantedHandles;
	if (UIMGAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		NewEntry.GrantingAbilitySystem = ASC;
		for (const TObjectPtr<const UIMGAbilitySet>& AbilitySet : EquipmentCDO->AbilitySetsToGrant)
		{
			if (AbilitySet)
			{
				AbilitySet->GiveToAbilitySystem(ASC, &GrantedHandles, Result);
			}
			if (!Entries.ContainsByPredicate([Result](const FIMGAppliedEquipmentEntry& Entry) { return Entry.Instance == Result; }))
			{
				GrantedHandles.TakeFromAbilitySystem(ASC);
				return nullptr;
			}
		}
	}
	else
	{
		//@TODO: Warning logging?
	}

	int32 Index = Entries.IndexOfByPredicate([Result](const FIMGAppliedEquipmentEntry& Entry) { return Entry.Instance == Result; });
	if (Index == INDEX_NONE)
	{
		return nullptr;
	}
	Entries[Index].GrantedHandles = MoveTemp(GrantedHandles);
	Result->SpawnEquipmentActors(EquipmentCDO->ActorsToSpawn);
	Index = Entries.IndexOfByPredicate([Result](const FIMGAppliedEquipmentEntry& Entry) { return Entry.Instance == Result; });
	if (Index == INDEX_NONE)
	{
		return nullptr;
	}
	MarkItemDirty(Entries[Index]);

	return Result;
}

void FIMGEquipmentList::RemoveEntry(UIMGEquipmentInstance* Instance)
{
	const int32 Index = Entries.IndexOfByPredicate([Instance](const FIMGAppliedEquipmentEntry& Entry) { return Entry.Instance == Instance; });
	if (Index != INDEX_NONE && !Entries[Index].bRemoving)
	{
		// Keep the entry visible to the unequip event while rejecting recursive removal.
		Entries[Index].bRemoving = true;
		UIMGAbilitySystemComponent* ASC = Entries[Index].GrantingAbilitySystem.Get();
		FIMGAbilitySet_GrantedHandles Handles = MoveTemp(Entries[Index].GrantedHandles);
		if (OwnerComponent->IsUsingRegisteredSubObjectList() && OwnerComponent->IsReplicatedSubObjectRegistered(Instance))
		{
			OwnerComponent->RemoveReplicatedSubObject(Instance);
		}
		Instance->NotifyUnequipped();
		if (ASC)
		{
			Handles.TakeFromAbilitySystem(ASC);
		}
		Instance->DestroyEquipmentActors();
		const int32 RemovedIndex = Entries.IndexOfByPredicate([Instance](const FIMGAppliedEquipmentEntry& Entry) { return Entry.Instance == Instance; });
		if (RemovedIndex != INDEX_NONE)
		{
			Entries.RemoveAt(RemovedIndex);
			MarkArrayDirty();
		}
	}
}

//////////////////////////////////////////////////////////////////////
// UIMGEquipmentManagerComponent

UIMGEquipmentManagerComponent::UIMGEquipmentManagerComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, EquipmentList(this)
{
	SetIsReplicatedByDefault(true);
	bWantsInitializeComponent = true;
}

void UIMGEquipmentManagerComponent::GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ThisClass, EquipmentList);
}

UIMGEquipmentInstance* UIMGEquipmentManagerComponent::EquipItem(TSubclassOf<UIMGEquipmentDefinition> EquipmentClass)
{
	UIMGEquipmentInstance* Result = nullptr;
	if (!bEndingPlay && IsValid(GetOwner()) && !GetOwner()->IsActorBeingDestroyed() && GetOwner()->HasAuthority() && EquipmentClass != nullptr)
	{
		Result = EquipmentList.AddEntry(EquipmentClass);
		if (Result != nullptr)
		{
			Result->NotifyEquipped(EquipmentClass);
			if (!Result->IsEquipmentActive())
			{
				EquipmentList.RemoveEntry(Result);
				return nullptr;
			}
			if (!EquipmentList.Entries.ContainsByPredicate([Result](const FIMGAppliedEquipmentEntry& Entry) { return Entry.Instance == Result; }))
			{
				return nullptr;
			}

			if (IsUsingRegisteredSubObjectList() && IsReadyForReplication())
			{
				AddReplicatedSubObject(Result);
			}
		}
	}
	return Result;
}

void UIMGEquipmentManagerComponent::UnequipItem(UIMGEquipmentInstance* ItemInstance)
{
	if (IsValid(ItemInstance) && GetOwner() && GetOwner()->HasAuthority())
	{
		EquipmentList.RemoveEntry(ItemInstance);
	}
}

bool UIMGEquipmentManagerComponent::ReplicateSubobjects(UActorChannel* Channel, class FOutBunch* Bunch, FReplicationFlags* RepFlags)
{
	bool WroteSomething = Super::ReplicateSubobjects(Channel, Bunch, RepFlags);

	for (FIMGAppliedEquipmentEntry& Entry : EquipmentList.Entries)
	{
		UIMGEquipmentInstance* Instance = Entry.Instance;

		if (IsValid(Instance))
		{
			WroteSomething |= Channel->ReplicateSubobject(Instance, *Bunch, *RepFlags);
		}
	}

	return WroteSomething;
}

void UIMGEquipmentManagerComponent::InitializeComponent()
{
	Super::InitializeComponent();
}

void UIMGEquipmentManagerComponent::BeginPlay()
{
	bEndingPlay = false;
	Super::BeginPlay();
	if (!GetOwner()->HasAuthority())
	{
		for (int32 Index = 0; Index < EquipmentList.Entries.Num(); ++Index)
		{
			EquipmentList.NotifyEntryEquipped(Index);
		}
	}
}

void UIMGEquipmentManagerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearEquipment();
	Super::EndPlay(EndPlayReason);
}

void UIMGEquipmentManagerComponent::UninitializeComponent()
{
	ClearEquipment();
	Super::UninitializeComponent();
}

void UIMGEquipmentManagerComponent::ClearEquipment()
{
	if (bEndingPlay)
	{
		return;
	}
	bEndingPlay = true;
	const bool bAuthority = GetOwner() && GetOwner()->HasAuthority();
	// End presentation while the world and its local cosmetic actors are still available.
	TArray<UIMGEquipmentInstance*> Instances;
	for (FIMGAppliedEquipmentEntry& Entry : EquipmentList.Entries)
	{
		if (UIMGEquipmentInstance* Notified = Entry.NotifiedInstance.Get())
		{
			Instances.AddUnique(Notified);
		}
		Entry.NotifiedInstance.Reset();
		if (IsValid(Entry.Instance))
		{
			Instances.AddUnique(Entry.Instance);
		}
	}
	for (UIMGEquipmentInstance* Instance : Instances)
	{
		if (!IsValid(Instance))
		{
			continue;
		}
		if (bAuthority)
		{
			UnequipItem(Instance);
		}
		else
		{
			Instance->NotifyUnequipped();
		}
	}
}

void UIMGEquipmentManagerComponent::ReadyForReplication()
{
	Super::ReadyForReplication();

	// Register existing IMGEquipmentInstances
	if (IsUsingRegisteredSubObjectList())
	{
		for (const FIMGAppliedEquipmentEntry& Entry : EquipmentList.Entries)
		{
			UIMGEquipmentInstance* Instance = Entry.Instance;

			if (IsValid(Instance))
			{
				AddReplicatedSubObject(Instance);
			}
		}
	}
}

UIMGEquipmentInstance* UIMGEquipmentManagerComponent::GetFirstInstanceOfType(TSubclassOf<UIMGEquipmentInstance> InstanceType)
{
	for (FIMGAppliedEquipmentEntry& Entry : EquipmentList.Entries)
	{
		if (UIMGEquipmentInstance* Instance = Entry.Instance)
		{
			if (Instance->IsA(InstanceType))
			{
				return Instance;
			}
		}
	}

	return nullptr;
}

TArray<UIMGEquipmentInstance*> UIMGEquipmentManagerComponent::GetEquipmentInstancesOfType(TSubclassOf<UIMGEquipmentInstance> InstanceType) const
{
	TArray<UIMGEquipmentInstance*> Results;
	for (const FIMGAppliedEquipmentEntry& Entry : EquipmentList.Entries)
	{
		if (UIMGEquipmentInstance* Instance = Entry.Instance)
		{
			if (Instance->IsA(InstanceType))
			{
				Results.Add(Instance);
			}
		}
	}
	return Results;
}
