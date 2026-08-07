

#include "Cosmetics/IMGPawnComponent_CharacterParts.h"

#include "Components/SkeletalMeshComponent.h"
#include "Cosmetics/IMGCharacterPartTypes.h"
#include "GameFramework/Character.h"
#include "GameplayTagAssetInterface.h"
#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGPawnComponent_CharacterParts)

class FLifetimeProperty;
class UPhysicsAsset;
class USkeletalMesh;
class UWorld;

//////////////////////////////////////////////////////////////////////

FString FIMGAppliedCharacterPartEntry::GetDebugString() const
{
	return FString::Printf(TEXT("(PartClass: %s, Socket: %s, Instance: %s)"), *GetPathNameSafe(Part.PartClass), *Part.SocketName.ToString(), *GetPathNameSafe(SpawnedComponent));
}

//////////////////////////////////////////////////////////////////////

void FIMGCharacterPartList::PreReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize)
{
	bool bDestroyedAnyActors = false;
	for (int32 Index : RemovedIndices)
	{
		FIMGAppliedCharacterPartEntry& Entry = Entries[Index];
		bDestroyedAnyActors |= DestroyActorForEntry(Entry);
	}

	if (bDestroyedAnyActors && ensure(OwnerComponent))
	{
		OwnerComponent->BroadcastChanged();
	}
}

void FIMGCharacterPartList::PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize)
{
	bool bCreatedAnyActors = false;
	for (int32 Index : AddedIndices)
	{
		FIMGAppliedCharacterPartEntry& Entry = Entries[Index];
		bCreatedAnyActors |= SpawnActorForEntry(Entry);
	}

	if (bCreatedAnyActors && ensure(OwnerComponent))
	{
		OwnerComponent->BroadcastChanged();
	}
}

void FIMGCharacterPartList::PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize)
{
	bool bChangedAnyActors = false;

	// We don't support dealing with propagating changes, just destroy and recreate
	for (int32 Index : ChangedIndices)
	{
		FIMGAppliedCharacterPartEntry& Entry = Entries[Index];

		bChangedAnyActors |= DestroyActorForEntry(Entry);
		bChangedAnyActors |= SpawnActorForEntry(Entry);
	}

	if (bChangedAnyActors && ensure(OwnerComponent))
	{
		OwnerComponent->BroadcastChanged();
	}
}

FIMGCharacterPartHandle FIMGCharacterPartList::AddEntry(FIMGCharacterPart NewPart)
{
	FIMGCharacterPartHandle Result;
	Result.PartHandle = PartHandleCounter++;

	if (ensure(OwnerComponent && OwnerComponent->GetOwner() && OwnerComponent->GetOwner()->HasAuthority()))
	{
		FIMGAppliedCharacterPartEntry& NewEntry = Entries.AddDefaulted_GetRef();
		NewEntry.Part = NewPart;
		NewEntry.PartHandle = Result.PartHandle;
	
		if (SpawnActorForEntry(NewEntry))
		{
			OwnerComponent->BroadcastChanged();
		}

		MarkItemDirty(NewEntry);
	}

	return Result;
}

void FIMGCharacterPartList::RemoveEntry(FIMGCharacterPartHandle Handle)
{
	for (auto EntryIt = Entries.CreateIterator(); EntryIt; ++EntryIt)
	{
		FIMGAppliedCharacterPartEntry& Entry = *EntryIt;
		if (Entry.PartHandle == Handle.PartHandle)
		{
			const bool bDestroyedActor = DestroyActorForEntry(Entry);
			EntryIt.RemoveCurrent();
			MarkArrayDirty();

			if (bDestroyedActor && ensure(OwnerComponent))
			{
				OwnerComponent->BroadcastChanged();
			}

			break;
		}
	}
}

void FIMGCharacterPartList::ClearAllEntries(bool bBroadcastChangeDelegate)
{
	bool bDestroyedAnyActors = false;
	for (FIMGAppliedCharacterPartEntry& Entry : Entries)
	{
		bDestroyedAnyActors |= DestroyActorForEntry(Entry);
	}
	Entries.Reset();
	MarkArrayDirty();

	if (bDestroyedAnyActors && bBroadcastChangeDelegate && ensure(OwnerComponent))
	{
		OwnerComponent->BroadcastChanged();
	}
}

FGameplayTagContainer FIMGCharacterPartList::CollectCombinedTags() const
{
	FGameplayTagContainer Result;

	for (const FIMGAppliedCharacterPartEntry& Entry : Entries)
	{
		if (Entry.SpawnedComponent != nullptr)
		{
			if (IGameplayTagAssetInterface* TagInterface = Cast<IGameplayTagAssetInterface>(Entry.SpawnedComponent->GetChildActor()))
			{
				TagInterface->GetOwnedGameplayTags(/*inout*/ Result);
			}
		}
	}

	return Result;
}

bool FIMGCharacterPartList::SpawnActorForEntry(FIMGAppliedCharacterPartEntry& Entry)
{
	bool bCreatedAnyActors = false;

	if (ensure(OwnerComponent) && !OwnerComponent->IsNetMode(NM_DedicatedServer))
	{
		if (Entry.Part.PartClass != nullptr)
		{
			UWorld* World = OwnerComponent->GetWorld();

			if (USceneComponent* ComponentToAttachTo = OwnerComponent->GetSceneComponentToAttachTo())
			{
				const FTransform SpawnTransform = ComponentToAttachTo->GetSocketTransform(Entry.Part.SocketName);

				UChildActorComponent* PartComponent = NewObject<UChildActorComponent>(OwnerComponent->GetOwner());

				PartComponent->SetupAttachment(ComponentToAttachTo, Entry.Part.SocketName);
				PartComponent->SetChildActorClass(Entry.Part.PartClass);
				PartComponent->RegisterComponent();

				if (AActor* SpawnedActor = PartComponent->GetChildActor())
				{
					switch (Entry.Part.CollisionMode)
					{
					case ECharacterCustomizationCollisionMode::UseCollisionFromCharacterPart:
						// Do nothing
						break;

					case ECharacterCustomizationCollisionMode::NoCollision:
						SpawnedActor->SetActorEnableCollision(false);
						break;
					}

					// Set up a direct tick dependency to work around the child actor component not providing one
					if (USceneComponent* SpawnedRootComponent = SpawnedActor->GetRootComponent())
					{
						SpawnedRootComponent->AddTickPrerequisiteComponent(ComponentToAttachTo);
					}
				}

				Entry.SpawnedComponent = PartComponent;
				bCreatedAnyActors = true;
			}
		}
	}

	return bCreatedAnyActors;
}

bool FIMGCharacterPartList::DestroyActorForEntry(FIMGAppliedCharacterPartEntry& Entry)
{
	bool bDestroyedAnyActors = false;

	if (Entry.SpawnedComponent != nullptr)
	{
		Entry.SpawnedComponent->DestroyComponent();
		Entry.SpawnedComponent = nullptr;
		bDestroyedAnyActors = true;
	}

	return bDestroyedAnyActors;
}

//////////////////////////////////////////////////////////////////////

UIMGPawnComponent_CharacterParts::UIMGPawnComponent_CharacterParts(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsReplicatedByDefault(true);
}

void UIMGPawnComponent_CharacterParts::GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ThisClass, CharacterPartList);
}

FIMGCharacterPartHandle UIMGPawnComponent_CharacterParts::AddCharacterPart(const FIMGCharacterPart& NewPart)
{
	return CharacterPartList.AddEntry(NewPart);
}

void UIMGPawnComponent_CharacterParts::RemoveCharacterPart(FIMGCharacterPartHandle Handle)
{
	CharacterPartList.RemoveEntry(Handle);
}

void UIMGPawnComponent_CharacterParts::RemoveAllCharacterParts()
{
	CharacterPartList.ClearAllEntries(/*bBroadcastChangeDelegate=*/ true);
}

void UIMGPawnComponent_CharacterParts::BeginPlay()
{
	Super::BeginPlay();
}

void UIMGPawnComponent_CharacterParts::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CharacterPartList.ClearAllEntries(/*bBroadcastChangeDelegate=*/ false);

	Super::EndPlay(EndPlayReason);
}

void UIMGPawnComponent_CharacterParts::OnRegister()
{
	Super::OnRegister();
	
	if (!IsTemplate())
	{
		CharacterPartList.SetOwnerComponent(this);
	}
}

TArray<AActor*> UIMGPawnComponent_CharacterParts::GetCharacterPartActors() const
{
	TArray<AActor*> Result;
	Result.Reserve(CharacterPartList.Entries.Num());

	for (const FIMGAppliedCharacterPartEntry& Entry : CharacterPartList.Entries)
	{
		if (UChildActorComponent* PartComponent = Entry.SpawnedComponent)
		{
			if (AActor* SpawnedActor = PartComponent->GetChildActor())
			{
				Result.Add(SpawnedActor);
			}
		}
	}

	return Result;
}

USkeletalMeshComponent* UIMGPawnComponent_CharacterParts::GetParentMeshComponent() const
{
	if (AActor* OwnerActor = GetOwner())
	{
		if (ACharacter* OwningCharacter = Cast<ACharacter>(OwnerActor))
		{
			if (USkeletalMeshComponent* MeshComponent = OwningCharacter->GetMesh())
			{
				return MeshComponent;
			}
		}
	}

	return nullptr;
}

USceneComponent* UIMGPawnComponent_CharacterParts::GetSceneComponentToAttachTo() const
{
	if (USkeletalMeshComponent* MeshComponent = GetParentMeshComponent())
	{
		return MeshComponent;
	}
	else if (AActor* OwnerActor = GetOwner())
	{
		return OwnerActor->GetRootComponent();
	}
	else
	{
		return nullptr;
	}
}

FGameplayTagContainer UIMGPawnComponent_CharacterParts::GetCombinedTags(FGameplayTag RequiredPrefix) const
{
	FGameplayTagContainer Result = CharacterPartList.CollectCombinedTags();
	if (RequiredPrefix.IsValid())
	{
		return Result.Filter(FGameplayTagContainer(RequiredPrefix));
	}
	else
	{
		return Result;
	}
}

void UIMGPawnComponent_CharacterParts::BroadcastChanged()
{
	const bool bReinitPose = true;

	// Check to see if the body type has changed
	if (USkeletalMeshComponent* MeshComponent = GetParentMeshComponent())
	{
		// Determine the mesh to use based on cosmetic part tags
		const FGameplayTagContainer MergedTags = GetCombinedTags(FGameplayTag());
		USkeletalMesh* DesiredMesh = BodyMeshes.SelectBestBodyStyle(MergedTags);

		// Apply the desired mesh (this call is a no-op if the mesh hasn't changed)
		MeshComponent->SetSkeletalMesh(DesiredMesh, /*bReinitPose=*/ bReinitPose);

		// Apply the desired physics asset if there's a forced override independent of the one from the mesh
		if (UPhysicsAsset* PhysicsAsset = BodyMeshes.ForcedPhysicsAsset)
		{
			MeshComponent->SetPhysicsAsset(PhysicsAsset, /*bForceReInit=*/ bReinitPose);
		}
	}

	// Let observers know, e.g., if they need to apply team coloring or similar
	OnCharacterPartsChanged.Broadcast(this);
}


