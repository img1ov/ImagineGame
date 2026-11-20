

#include "Equipment/IMGEquipmentInstance.h"


#include "Components/SkeletalMeshComponent.h"
#include "Components/GameFrameworkComponentManager.h"
#include "Cosmetics/IMGPawnComponent_CharacterParts.h"
#include "Character/IMGHealthComponent.h"
#include "GameplayTagAssetInterface.h"
#include "GameFramework/Character.h"
#include "Equipment/IMGEquipmentDefinition.h"
#include "IMGLogChannels.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

#include "Iris/ReplicationSystem/ReplicationFragmentUtil.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGEquipmentInstance)

class FLifetimeProperty;
class UClass;
class USceneComponent;

UIMGEquipmentInstance::UIMGEquipmentInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UWorld* UIMGEquipmentInstance::GetWorld() const
{
	if (APawn* OwningPawn = GetPawn())
	{
		return OwningPawn->GetWorld();
	}
	else
	{
		return nullptr;
	}
}

void UIMGEquipmentInstance::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ThisClass, Instigator);
	DOREPLIFETIME(ThisClass, SpawnedActors);
}

void UIMGEquipmentInstance::RegisterReplicationFragments(UE::Net::FFragmentRegistrationContext& Context, UE::Net::EFragmentRegistrationFlags RegistrationFlags)
{
	using namespace UE::Net;

	// Build descriptors and allocate PropertyReplicationFragments for this object
	FReplicationFragmentUtil::CreateAndRegisterFragmentsForObject(this, Context, RegistrationFlags);
}

APawn* UIMGEquipmentInstance::GetPawn() const
{
	return Cast<APawn>(GetOuter());
}

APawn* UIMGEquipmentInstance::GetTypedPawn(TSubclassOf<APawn> PawnType) const
{
	APawn* Result = nullptr;
	if (UClass* ActualPawnType = PawnType)
	{
		if (GetOuter()->IsA(ActualPawnType))
		{
			Result = Cast<APawn>(GetOuter());
		}
	}
	return Result;
}

void UIMGEquipmentInstance::SpawnEquipmentActors(const TArray<FIMGEquipmentActorToSpawn>& ActorsToSpawn)
{
	const uint32 Generation = AttachmentGeneration;
	if (APawn* OwningPawn = GetPawn(); IsValid(OwningPawn) && OwningPawn->HasAuthority())
	{
		TWeakObjectPtr<USceneComponent> AttachTarget = OwningPawn->GetRootComponent();
		if (ACharacter* Char = Cast<ACharacter>(OwningPawn))
		{
			AttachTarget = Char->GetMesh();
		}

		for (const FIMGEquipmentActorToSpawn& SpawnInfo : ActorsToSpawn)
		{
			if (SpawnInfo.bUsePartAttachment || !SpawnInfo.ActorToSpawn)
			{
				continue;
			}
			AActor* NewActor = GetWorld()->SpawnActorDeferred<AActor>(SpawnInfo.ActorToSpawn, FTransform::Identity, OwningPawn);
			if (!IsValid(NewActor))
			{
				UE_LOG(LogIMG, Warning, TEXT("Equipment %s: failed to spawn %s."), *GetPathName(), *GetNameSafe(SpawnInfo.ActorToSpawn));
				continue;
			}
			const auto CanUseActor = [this, Generation, NewActor]()
			{
				return AttachmentGeneration == Generation && IsValid(NewActor) && !NewActor->IsActorBeingDestroyed();
			};
			if (CanUseActor())
			{
				NewActor->FinishSpawning(FTransform::Identity, /*bIsDefaultTransform=*/ true);
			}
			if (CanUseActor())
			{
				NewActor->SetActorRelativeTransform(SpawnInfo.AttachTransform);
			}
			if (CanUseActor())
			{
				NewActor->AttachToComponent(AttachTarget.Get(), FAttachmentTransformRules::KeepRelativeTransform, SpawnInfo.AttachSocket);
			}
			if (AttachmentGeneration != Generation)
			{
				if (IsValid(NewActor) && !NewActor->IsActorBeingDestroyed())
				{
					NewActor->Destroy();
				}
				return;
			}
			if (IsValid(NewActor) && !NewActor->IsActorBeingDestroyed())
			{
				SpawnedActors.Add(NewActor);
			}
		}
	}
}

void UIMGEquipmentInstance::DestroyEquipmentActors()
{
	++AttachmentGeneration;
	StopAttachmentObservation();
	DestroyLocalActors();
	APawn* Pawn = GetPawn();
	if (!Pawn || !Pawn->HasAuthority())
	{
		return;
	}
	TArray<TObjectPtr<AActor>> ActorsToDestroy = MoveTemp(SpawnedActors);
	for (AActor* Actor : ActorsToDestroy)
	{
		if (Actor)
		{
			Actor->Destroy();
		}
	}
}

void UIMGEquipmentInstance::OnEquipped()
{
	K2_OnEquipped();
}

void UIMGEquipmentInstance::OnUnequipped()
{
	K2_OnUnequipped();
}

void UIMGEquipmentInstance::OnRep_Instigator()
{
}

TArray<AActor*> UIMGEquipmentInstance::GetSpawnedActors() const
{
	if (LocalActorConfigurations.IsEmpty())
	{
		return SpawnedActors;
	}
	TArray<AActor*> Result;
	for (AActor* Actor : SpawnedActors)
	{
		if (IsValid(Actor) && !Actor->IsActorBeingDestroyed())
		{
			Result.Add(Actor);
		}
	}
	for (AActor* Actor : LocalSpawnedActors)
	{
		if (IsValid(Actor) && !Actor->IsActorBeingDestroyed())
		{
			Result.Add(Actor);
		}
	}
	return Result;
}

void UIMGEquipmentInstance::NotifyEquipped(TSubclassOf<UIMGEquipmentDefinition> Definition)
{
	APawn* Pawn = GetPawn();
	if (bEquipmentActive || bEndingEquipment || !IsValid(Pawn) || Pawn->IsActorBeingDestroyed() || !Definition)
	{
		return;
	}
	bEquipmentActive = true;
	const uint32 Generation = ++AttachmentGeneration;

	if (!Pawn->IsNetMode(NM_DedicatedServer))
	{
		for (const FIMGEquipmentActorToSpawn& Info : GetDefault<UIMGEquipmentDefinition>(Definition)->ActorsToSpawn)
		{
			if (Info.bUsePartAttachment)
			{
				LocalActorConfigurations.Add(Info);
			}
		}
		LocalSpawnedActors.SetNum(LocalActorConfigurations.Num());
		bObservingAttachments = !LocalActorConfigurations.IsEmpty();
		if (bObservingAttachments)
		{
			Pawn->OnEndPlay.AddUniqueDynamic(this, &ThisClass::HandleAttachmentPawnEndPlay);
			if (UGameFrameworkComponentManager* Manager = UGameFrameworkComponentManager::GetForActor(Pawn))
			{
				TSharedPtr<FComponentRequestHandle> Handle = Manager->AddExtensionHandler(Pawn->GetClass(),
					UGameFrameworkComponentManager::FExtensionHandlerDelegate::CreateUObject(this, &ThisClass::HandleAttachmentExtension));
				if (CanRefreshAttachments(Generation))
				{
					AttachmentExtensionHandle = MoveTemp(Handle);
				}
			}
			if (CanRefreshAttachments(Generation))
			{
				RefreshLocalActors();
			}
		}
	}
	if (bEquipmentActive && AttachmentGeneration == Generation)
	{
		bEquipEventDelivered = true;
		OnEquipped();
	}
}

void UIMGEquipmentInstance::NotifyUnequipped()
{
	if (!bEquipmentActive || bEndingEquipment)
	{
		return;
	}
	TGuardValue<bool> Ending(bEndingEquipment, true);
	bEquipmentActive = false;
	++AttachmentGeneration;
	StopAttachmentObservation();
	if (APawn* Pawn = GetPawn())
	{
		Pawn->OnEndPlay.RemoveDynamic(this, &ThisClass::HandleAttachmentPawnEndPlay);
	}
	const bool bDeliverEvent = bEquipEventDelivered;
	bEquipEventDelivered = false;
	if (bDeliverEvent)
	{
		OnUnequipped();
	}
	DestroyLocalActors();
	LocalActorConfigurations.Reset();
}

void UIMGEquipmentInstance::RefreshEquipmentActorAttachments()
{
	++AttachmentRevision;
	if (bEquipmentActive && bObservingAttachments && !AttachmentRefreshTimer.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			AttachmentRefreshTimer = World->GetTimerManager().SetTimerForNextTick(this, &ThisClass::RefreshLocalActors);
		}
	}
}

void UIMGEquipmentInstance::StopAttachmentObservation()
{
	++AttachmentRevision;
	bObservingAttachments = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AttachmentRefreshTimer);
	}
	AttachmentExtensionHandle.Reset();
	if (UIMGPawnComponent_CharacterParts* Parts = AttachmentParts.Get())
	{
		Parts->OnCharacterPartsChanged.RemoveDynamic(this, &ThisClass::HandleAttachmentPartsChanged);
	}
	AttachmentParts.Reset();
	if (UIMGHealthComponent* Health = AttachmentHealth.Get())
	{
		Health->OnDeathStarted.RemoveDynamic(this, &ThisClass::HandleAttachmentDeath);
	}
	AttachmentHealth.Reset();
	ClearAttachmentTargetObservers();
	WarnedAttachmentEntries.Reset();
}

void UIMGEquipmentInstance::ClearAttachmentTargetObservers()
{
	for (const TWeakObjectPtr<AActor>& Part : ObservedAttachmentParts)
	{
		if (AActor* Actor = Part.Get())
		{
			Actor->OnEndPlay.RemoveDynamic(this, &ThisClass::HandleAttachmentPartEndPlay);
		}
	}
	ObservedAttachmentParts.Reset();
	for (const TWeakObjectPtr<USkeletalMeshComponent>& Mesh : ObservedAttachmentMeshes)
	{
		if (USkeletalMeshComponent* Component = Mesh.Get())
		{
			Component->OnAnimInitialized.RemoveDynamic(this, &ThisClass::HandleAttachmentAnimInitialized);
		}
	}
	ObservedAttachmentMeshes.Reset();
}

void UIMGEquipmentInstance::DestroyLocalActors()
{
	TArray<TObjectPtr<AActor>> ActorsToDestroy = MoveTemp(LocalSpawnedActors);
	const auto TickTargets = MoveTemp(AttachmentTickTargets);
	ActorsHiddenForAttachment.Reset();
	bLocalActorsChanged = false;
	for (AActor* Actor : ActorsToDestroy)
	{
		if (IsValid(Actor))
		{
			if (const TWeakObjectPtr<USceneComponent>* Target = TickTargets.Find(Actor); Target && Target->IsValid())
			{
				Actor->RemoveTickPrerequisiteComponent(Target->Get());
			}
			Actor->Destroy();
		}
	}
}

void UIMGEquipmentInstance::SetLocalActorWaiting(AActor* Actor, bool bWaiting)
{
	if (bWaiting)
	{
		if (const TWeakObjectPtr<USceneComponent>* Target = AttachmentTickTargets.Find(Actor); Target && Target->IsValid())
		{
			Actor->RemoveTickPrerequisiteComponent(Target->Get());
		}
		bLocalActorsChanged |= AttachmentTickTargets.Remove(Actor) > 0;
		if (!Actor->IsHidden())
		{
			const uint32 Generation = AttachmentGeneration;
			ActorsHiddenForAttachment.Add(Actor);
			Actor->SetActorHiddenInGame(true);
			if (AttachmentGeneration != Generation || !IsValid(Actor) || Actor->IsActorBeingDestroyed())
			{
				return;
			}
		}
		if (USceneComponent* Root = Actor->GetRootComponent(); Root && Root->GetAttachParent())
		{
			bLocalActorsChanged = true;
			Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		}
	}
	else if (ActorsHiddenForAttachment.Remove(Actor))
	{
		Actor->SetActorHiddenInGame(false);
	}
}

bool UIMGEquipmentInstance::CanRefreshAttachments(uint32 Generation) const
{
	return bEquipmentActive && bObservingAttachments && AttachmentGeneration == Generation;
}

USkeletalMeshComponent* UIMGEquipmentInstance::ResolveLocalAttachmentTarget(const FIMGEquipmentPartAttachment& PartAttachment,
	const ACharacter* Character, const TArray<AActor*>& PartActors, int32& MatchingParts, int32& MeshCount, int32& TaggedMeshCount)
{
	const bool bHasPartTag = PartAttachment.PartTag.IsValid();
	const bool bUseComponentTag = !PartAttachment.ComponentTag.IsNone();
	USkeletalMeshComponent* Target = nullptr;
	USkeletalMeshComponent* FirstMesh = nullptr;
	for (AActor* Part : PartActors)
	{
		if (!bHasPartTag || !IsValid(Part) || Part->IsActorBeingDestroyed())
		{
			continue;
		}
		const IGameplayTagAssetInterface* Tags = Cast<IGameplayTagAssetInterface>(Part);
		if (!Tags || !Tags->HasMatchingGameplayTag(PartAttachment.PartTag))
		{
			continue;
		}
		++MatchingParts;
		if (!Part->HasActorBegunPlay())
		{
			continue;
		}
		Part->OnEndPlay.AddUniqueDynamic(this, &ThisClass::HandleAttachmentPartEndPlay);
		ObservedAttachmentParts.AddUnique(Part);
		TArray<USkeletalMeshComponent*> Meshes;
		Part->GetComponents(Meshes);
		for (USkeletalMeshComponent* Mesh : Meshes)
		{
			if (!IsValid(Mesh) || Mesh->IsBeingDestroyed())
			{
				continue;
			}
			++MeshCount;
			if (!FirstMesh)
			{
				FirstMesh = Mesh;
			}
			const bool bHasComponentTag = bUseComponentTag && Mesh->ComponentHasTag(PartAttachment.ComponentTag);
			if (bHasComponentTag)
			{
				++TaggedMeshCount;
				Target = Mesh;
			}
			if (!bUseComponentTag || bHasComponentTag || Mesh == FirstMesh)
			{
				Mesh->OnAnimInitialized.AddUniqueDynamic(this, &ThisClass::HandleAttachmentAnimInitialized);
				ObservedAttachmentMeshes.AddUnique(Mesh);
			}
		}
	}
	if (TaggedMeshCount == 0)
	{
		Target = FirstMesh;
	}
	if (MatchingParts == 0 && Character)
	{
		if (USkeletalMeshComponent* CharacterMesh = Character->GetMesh(); IsValid(CharacterMesh) && !CharacterMesh->IsBeingDestroyed())
		{
			CharacterMesh->OnAnimInitialized.AddUniqueDynamic(this, &ThisClass::HandleAttachmentAnimInitialized);
			ObservedAttachmentMeshes.AddUnique(CharacterMesh);
			Target = CharacterMesh->IsVisible() ? CharacterMesh : nullptr;
		}
	}
	else if (MatchingParts != 1 || TaggedMeshCount > 1)
	{
		Target = nullptr;
	}
	return Target;
}

bool UIMGEquipmentInstance::TryAttachLocalActor(AActor* Actor, TWeakObjectPtr<USkeletalMeshComponent> SelectedTarget,
	const FIMGEquipmentActorToSpawn& Info, uint32 Generation, uint32 Revision)
{
	USkeletalMeshComponent* Target = SelectedTarget.Get();
	if (Revision != AttachmentRevision || !Target || !Target->IsRegistered() ||
		!IsValid(Target->GetOwner()) || Target->GetOwner()->IsActorBeingDestroyed())
	{
		return false;
	}

	const USceneComponent* Root = Actor->GetRootComponent();
	if (Root && Root->GetAttachParent() == Target && Root->GetAttachSocketName() == Info.AttachSocket)
	{
		return true;
	}

	if (const TWeakObjectPtr<USceneComponent>* Previous = AttachmentTickTargets.Find(Actor); Previous && Previous->IsValid())
	{
		Actor->RemoveTickPrerequisiteComponent(Previous->Get());
	}
	bLocalActorsChanged |= AttachmentTickTargets.Remove(Actor) > 0;

	const auto CanCompleteAttachment = [this, Generation, Revision, Actor, SelectedTarget]()
	{
		return CanRefreshAttachments(Generation) && Revision == AttachmentRevision &&
			IsValid(Actor) && !Actor->IsActorBeingDestroyed() && SelectedTarget.IsValid() && SelectedTarget->IsRegistered();
	};
	if (!Actor->AttachToComponent(Target, FAttachmentTransformRules::KeepRelativeTransform, Info.AttachSocket) || !CanCompleteAttachment())
	{
		return false;
	}

	Actor->SetActorRelativeTransform(Info.AttachTransform);
	if (!CanCompleteAttachment())
	{
		return false;
	}

	Root = Actor->GetRootComponent();
	if (!Root || Root->GetAttachParent() != Target || Root->GetAttachSocketName() != Info.AttachSocket)
	{
		return false;
	}

	Actor->AddTickPrerequisiteComponent(Target);
	AttachmentTickTargets.Add(Actor, Target);
	bLocalActorsChanged = true;
	return true;
}

void UIMGEquipmentInstance::RefreshLocalActors()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AttachmentRefreshTimer);
	}
	APawn* Pawn = GetPawn();
	if (!bEquipmentActive || !bObservingAttachments || !IsValid(Pawn) || Pawn->IsActorBeingDestroyed())
	{
		return;
	}
	if (bRefreshingAttachments)
	{
		RefreshEquipmentActorAttachments();
		return;
	}
	TGuardValue<bool> Refreshing(bRefreshingAttachments, true);
	const uint32 Generation = AttachmentGeneration;
	const uint32 Revision = AttachmentRevision;
	UIMGHealthComponent* Health = UIMGHealthComponent::FindHealthComponent(Pawn);
	if (AttachmentHealth.Get() != Health)
	{
		if (UIMGHealthComponent* OldHealth = AttachmentHealth.Get())
		{
			OldHealth->OnDeathStarted.RemoveDynamic(this, &ThisClass::HandleAttachmentDeath);
		}
		AttachmentHealth = Health;
		if (Health)
		{
			Health->OnDeathStarted.AddUniqueDynamic(this, &ThisClass::HandleAttachmentDeath);
		}
	}
	if (Health && Health->IsDeadOrDying())
	{
		StopAttachmentObservation();
		return;
	}

	UIMGPawnComponent_CharacterParts* Parts = Pawn->FindComponentByClass<UIMGPawnComponent_CharacterParts>();
	if (!IsValid(Parts) || !Parts->IsRegistered() || !Parts->HasBegunPlay() || Parts->IsBeingDestroyed())
	{
		Parts = nullptr;
	}
	if (AttachmentParts.Get() != Parts)
	{
		if (UIMGPawnComponent_CharacterParts* OldParts = AttachmentParts.Get())
		{
			OldParts->OnCharacterPartsChanged.RemoveDynamic(this, &ThisClass::HandleAttachmentPartsChanged);
		}
		AttachmentParts = Parts;
		if (Parts)
		{
			Parts->OnCharacterPartsChanged.AddUniqueDynamic(this, &ThisClass::HandleAttachmentPartsChanged);
		}
	}
	// Subscribe before reading the snapshot. Parts notifications do not replay history.
	ClearAttachmentTargetObservers();
	const ACharacter* Character = Cast<ACharacter>(Pawn);
	const TArray<AActor*> PartActors = Parts ? Parts->GetCharacterPartActors() : TArray<AActor*>();
	for (int32 Index = 0; Index < LocalActorConfigurations.Num(); ++Index)
	{
		const FIMGEquipmentActorToSpawn Info = LocalActorConfigurations[Index];
		const FIMGEquipmentPartAttachment& PartAttachment = Info.PartAttachment;
		int32 MatchingParts = 0;
		int32 MeshCount = 0;
		int32 TaggedMeshCount = 0;
		USkeletalMeshComponent* Target = ResolveLocalAttachmentTarget(PartAttachment, Character, PartActors, MatchingParts, MeshCount, TaggedMeshCount);
		if (!CanRefreshAttachments(Generation))
		{
			return;
		}
		const TWeakObjectPtr<USkeletalMeshComponent> ChosenMesh = Target;
		if (Target && (!Target->IsRegistered() || !Target->GetSkinnedAsset() ||
			(!Info.AttachSocket.IsNone() && !Target->DoesSocketExist(Info.AttachSocket))))
		{
			Target = nullptr;
		}
		AActor* Actor = LocalSpawnedActors[Index];
		const TWeakObjectPtr<USkeletalMeshComponent> SelectedTarget = Target;
		if ((!IsValid(Actor) || Actor->IsActorBeingDestroyed()) && Info.ActorToSpawn)
		{
			Actor = GetWorld()->SpawnActorDeferred<AActor>(Info.ActorToSpawn, Pawn->GetActorTransform(), Pawn, Pawn,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (IsValid(Actor) && !Actor->IsActorBeingDestroyed())
			{
				if (CanRefreshAttachments(Generation))
				{
					Actor->SetReplicates(false);
					Actor->SetActorEnableCollision(false);
				}
				if (!CanRefreshAttachments(Generation))
				{
					if (IsValid(Actor) && !Actor->IsActorBeingDestroyed())
					{
						Actor->Destroy();
					}
					return;
				}
				if (!IsValid(Actor) || Actor->IsActorBeingDestroyed())
				{
					LocalSpawnedActors[Index] = nullptr;
					continue;
				}
				LocalSpawnedActors[Index] = Actor;
				Actor->FinishSpawning(Pawn->GetActorTransform());
				if (!bEquipmentActive || AttachmentGeneration != Generation)
				{
					return;
				}
				if (!IsValid(Actor) || Actor->IsActorBeingDestroyed())
				{
					LocalSpawnedActors[Index] = nullptr;
					continue;
				}
				// Construction scripts must not turn a local representation into a network/gameplay actor.
				Actor->SetReplicates(false);
				Actor->SetActorEnableCollision(false);
				if (!bEquipmentActive || AttachmentGeneration != Generation)
				{
					return;
				}
				if (!IsValid(Actor) || Actor->IsActorBeingDestroyed())
				{
					LocalSpawnedActors[Index] = nullptr;
					continue;
				}
				if (!bObservingAttachments)
				{
					SetLocalActorWaiting(Actor, true);
					return;
				}
				bLocalActorsChanged = true;
			}
		}
		bool bAttached = false;
		if (IsValid(Actor) && !Actor->IsActorBeingDestroyed())
		{
			bAttached = TryAttachLocalActor(Actor, SelectedTarget, Info, Generation, Revision);
			if (!CanRefreshAttachments(Generation))
			{
				return;
			}
			if (IsValid(Actor) && !Actor->IsActorBeingDestroyed())
			{
				SetLocalActorWaiting(Actor, !bAttached);
				if (!CanRefreshAttachments(Generation))
				{
					return;
				}
			}
			if (!IsValid(Actor) || Actor->IsActorBeingDestroyed())
			{
				LocalSpawnedActors[Index] = nullptr;
				bLocalActorsChanged = true;
				continue;
			}
		}
		if (!bAttached && !WarnedAttachmentEntries.Contains(Index))
		{
			WarnedAttachmentEntries.Add(Index);
			UE_LOG(LogIMG, Warning, TEXT("Equipment %s: Part presentation is waiting or misconfigured (Class=%s Tag=%s ComponentTag=%s Socket=%s Parts=%d Meshes=%d TaggedMeshes=%d ChosenMesh=%s Actor=%s). Check Part GameplayTags, mesh/socket availability, fallback mesh visibility and actor attachment."),
				*GetPathName(), *GetNameSafe(Info.ActorToSpawn), *PartAttachment.PartTag.ToString(), *PartAttachment.ComponentTag.ToString(),
				*Info.AttachSocket.ToString(), MatchingParts, MeshCount, TaggedMeshCount, *GetNameSafe(ChosenMesh.Get()), *GetNameSafe(Actor));
		}
		else if (bAttached)
		{
			WarnedAttachmentEntries.Remove(Index);
		}
	}
	const bool bNotifyActorsChanged = bLocalActorsChanged && bEquipEventDelivered;
	bLocalActorsChanged = false;
	if (bNotifyActorsChanged)
	{
		K2_OnEquipmentActorsChanged();
	}
}

void UIMGEquipmentInstance::HandleAttachmentExtension(AActor* Actor, FName EventName)
{
	if (!bObservingAttachments || Actor != GetPawn())
	{
		return;
	}
	if (EventName == UGameFrameworkComponentManager::NAME_ReceiverRemoved)
	{
		NotifyUnequipped();
	}
	else if (EventName == UIMGPawnComponent_CharacterParts::NAME_CharacterPartsRemoved)
	{
		if (UIMGPawnComponent_CharacterParts* Parts = AttachmentParts.Get())
		{
			Parts->OnCharacterPartsChanged.RemoveDynamic(this, &ThisClass::HandleAttachmentPartsChanged);
		}
		AttachmentParts.Reset();
		RefreshEquipmentActorAttachments();
		const uint32 Generation = AttachmentGeneration;
		const TArray<TObjectPtr<AActor>> Actors = LocalSpawnedActors;
		for (AActor* LocalActor : Actors)
		{
			if (IsValid(LocalActor) && !LocalActor->IsActorBeingDestroyed() && LocalActor->GetAttachParentActor() != Actor)
			{
				SetLocalActorWaiting(LocalActor, true);
				if (!CanRefreshAttachments(Generation))
				{
					return;
				}
			}
		}
	}
	else if (EventName == UIMGPawnComponent_CharacterParts::NAME_CharacterPartsReady)
	{
		RefreshEquipmentActorAttachments();
	}
}

void UIMGEquipmentInstance::HandleAttachmentPartsChanged(UIMGPawnComponent_CharacterParts* Component)
{
	if (Component == AttachmentParts.Get())
	{
		RefreshEquipmentActorAttachments();
	}
}

void UIMGEquipmentInstance::HandleAttachmentAnimInitialized()
{
	RefreshEquipmentActorAttachments();
}

void UIMGEquipmentInstance::HandleAttachmentPartEndPlay(AActor* Actor, EEndPlayReason::Type Reason)
{
	if (!bObservingAttachments)
	{
		return;
	}
	RefreshEquipmentActorAttachments();
	const uint32 Generation = AttachmentGeneration;
	const TArray<TObjectPtr<AActor>> Actors = LocalSpawnedActors;
	for (AActor* LocalActor : Actors)
	{
		if (IsValid(LocalActor) && !LocalActor->IsActorBeingDestroyed() && LocalActor->GetAttachParentActor() == Actor)
		{
			SetLocalActorWaiting(LocalActor, true);
			if (!CanRefreshAttachments(Generation))
			{
				return;
			}
		}
	}
}

void UIMGEquipmentInstance::HandleAttachmentPawnEndPlay(AActor* Actor, EEndPlayReason::Type Reason)
{
	NotifyUnequipped();
}

void UIMGEquipmentInstance::HandleAttachmentDeath(AActor* Actor)
{
	// Keep the current death presentation, but never reattach or respawn after death starts.
	StopAttachmentObservation();
}

void UIMGEquipmentInstance::OnRep_SpawnedActors()
{
	if (bEquipmentActive && bEquipEventDelivered)
	{
		K2_OnEquipmentActorsChanged();
	}
}
