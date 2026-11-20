
#pragma once

#include "Engine/World.h"
#include "Engine/TimerHandle.h"
#include "Equipment/IMGEquipmentDefinition.h"

#include "IMGEquipmentInstance.generated.h"

class AActor;
class APawn;
class UIMGPawnComponent_CharacterParts;
class USceneComponent;
class USkeletalMeshComponent;
class UIMGHealthComponent;
struct FComponentRequestHandle;
struct FFrame;
struct FIMGEquipmentActorToSpawn;

/**
 * UIMGEquipmentInstance
 *
 * A piece of equipment spawned and applied to a pawn
 */
UCLASS(BlueprintType, Blueprintable)
class UIMGEquipmentInstance : public UObject
{
	GENERATED_BODY()

public:
	UIMGEquipmentInstance(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~UObject interface
	virtual bool IsSupportedForNetworking() const override { return true; }
	virtual UWorld* GetWorld() const override final;
	//~End of UObject interface

	UFUNCTION(BlueprintPure, Category=Equipment)
	UObject* GetInstigator() const { return Instigator; }

	void SetInstigator(UObject* InInstigator) { Instigator = InInstigator; }

	UFUNCTION(BlueprintPure, Category=Equipment)
	APawn* GetPawn() const;

	UFUNCTION(BlueprintPure, Category=Equipment, meta=(DeterminesOutputType=PawnType))
	APawn* GetTypedPawn(TSubclassOf<APawn> PawnType) const;

	UFUNCTION(BlueprintPure, Category=Equipment)
	TArray<AActor*> GetSpawnedActors() const;

	// Called by the manager, including when a replicated instance reference resolves late.
	void NotifyEquipped(TSubclassOf<UIMGEquipmentDefinition> Definition);
	void NotifyUnequipped();
	bool IsEquipmentActive() const { return bEquipmentActive; }

	// Also call after custom code changes components/tags without a Parts or animation notification.
	UFUNCTION(BlueprintCallable, Category=Equipment)
	void RefreshEquipmentActorAttachments();

	virtual void SpawnEquipmentActors(const TArray<FIMGEquipmentActorToSpawn>& ActorsToSpawn);
	virtual void DestroyEquipmentActors();

	virtual void OnEquipped();
	virtual void OnUnequipped();

protected:
	/** Register all replication fragments */
	virtual void RegisterReplicationFragments(UE::Net::FFragmentRegistrationContext& Context, UE::Net::EFragmentRegistrationFlags RegistrationFlags) override;

	UFUNCTION(BlueprintImplementableEvent, Category=Equipment, meta=(DisplayName="OnEquipped"))
	void K2_OnEquipped();

	UFUNCTION(BlueprintImplementableEvent, Category=Equipment, meta=(DisplayName="OnUnequipped"))
	void K2_OnUnequipped();

	// Notifies about actor references and local attachment changes after OnEquipped.
	UFUNCTION(BlueprintImplementableEvent, Category=Equipment, meta=(DisplayName="OnEquipmentActorsChanged"))
	void K2_OnEquipmentActorsChanged();

private:
	UFUNCTION()
	void OnRep_Instigator();

	UFUNCTION()
	void OnRep_SpawnedActors();

	void StopAttachmentObservation();
	void DestroyLocalActors();
	void RefreshLocalActors();
	void HandleAttachmentExtension(AActor* Actor, FName EventName);
	void SetLocalActorWaiting(AActor* Actor, bool bWaiting);

	UFUNCTION()
	void HandleAttachmentPartsChanged(UIMGPawnComponent_CharacterParts* Component);
	UFUNCTION()
	void HandleAttachmentAnimInitialized();
	UFUNCTION()
	void HandleAttachmentPartEndPlay(AActor* Actor, EEndPlayReason::Type Reason);
	UFUNCTION()
	void HandleAttachmentPawnEndPlay(AActor* Actor, EEndPlayReason::Type Reason);
	UFUNCTION()
	void HandleAttachmentDeath(AActor* Actor);

private:
	UPROPERTY(ReplicatedUsing=OnRep_Instigator)
	TObjectPtr<UObject> Instigator;

	UPROPERTY(ReplicatedUsing=OnRep_SpawnedActors)
	TArray<TObjectPtr<AActor>> SpawnedActors;

	UPROPERTY(Transient)
	TArray<FIMGEquipmentActorToSpawn> LocalActorConfigurations;
	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> LocalSpawnedActors;

	TSharedPtr<FComponentRequestHandle> AttachmentExtensionHandle;
	TWeakObjectPtr<UIMGPawnComponent_CharacterParts> AttachmentParts;
	TWeakObjectPtr<UIMGHealthComponent> AttachmentHealth;
	TArray<TWeakObjectPtr<AActor>> ObservedAttachmentParts;
	TArray<TWeakObjectPtr<USkeletalMeshComponent>> ObservedAttachmentMeshes;
	TMap<TWeakObjectPtr<AActor>, TWeakObjectPtr<USceneComponent>> AttachmentTickTargets;
	TSet<TWeakObjectPtr<AActor>> ActorsHiddenForAttachment;
	TSet<int32> WarnedAttachmentEntries;
	FTimerHandle AttachmentRefreshTimer;
	uint32 AttachmentGeneration = 0;
	uint32 AttachmentRevision = 0;
	bool bEquipmentActive = false;
	bool bEquipEventDelivered = false;
	bool bEndingEquipment = false;
	bool bObservingAttachments = false;
	bool bRefreshingAttachments = false;
	bool bLocalActorsChanged = false;
};
