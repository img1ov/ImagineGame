#pragma once

#include "Components/SceneComponent.h"
#include "Cosmetics/IMGCosmeticAnimationTypes.h"
#include "Equipment/IMGEquipmentInstance.h"
#include "GameFramework/InputDevicePropertyHandle.h"
#include "Engine/TimerHandle.h"

#include "IMGWeaponInstance.generated.h"

#define UE_API IMAGINEGAME_API

class UAnimInstance;
class UMeshComponent;
class UObject;
struct FFrame;
struct FGameplayTagContainer;
class UInputDeviceProperty;
class USkeletalMeshComponent;
class UIMGHealthComponent;
class UIMGPawnComponent_CharacterParts;
struct FComponentRequestHandle;

/**
 * UIMGWeaponInstance
 *
 * A piece of equipment representing a weapon spawned and applied to a pawn
 */
UCLASS(MinimalAPI)
class UIMGWeaponInstance : public UIMGEquipmentInstance
{
	GENERATED_BODY()

public:
	UE_API UIMGWeaponInstance(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Finds the single weapon Actor attached to the character's equipment hierarchy. */
	UFUNCTION(BlueprintPure, Category="IMG|Weapon")
	static UE_API AActor* FindEquippedWeaponActor(const AActor* CharacterActor);

	UFUNCTION(BlueprintPure, Category="IMG|Weapon")
	static UE_API bool TryGetEquippedWeaponActor(const AActor* CharacterActor, AActor*& WeaponActor);

	/** Finds the first mesh on the equipped weapon that matches the optional component tag. */
	UFUNCTION(BlueprintPure, Category="IMG|Weapon", meta=(AdvancedDisplay="ComponentTag"))
	static UE_API bool TryGetEquippedWeaponMesh(const AActor* CharacterActor, UMeshComponent*& WeaponMesh, FName ComponentTag = NAME_None);

	/** Reads a socket on the first weapon mesh matching ComponentTag, or the first mesh when the tag is empty. */
	UFUNCTION(BlueprintPure, Category="IMG|Weapon", meta=(AdvancedDisplay="ComponentTag"))
	static UE_API bool TryGetEquippedWeaponSocketTransform(const AActor* CharacterActor, FName SocketName, FTransform& SocketTransform,
		ERelativeTransformSpace TransformSpace = RTS_World, FName ComponentTag = NAME_None);

	//~UIMGEquipmentInstance interface
	UE_API virtual void OnEquipped() override;
	UE_API virtual void OnUnequipped() override;
	//~End of UIMGEquipmentInstance interface

	UFUNCTION(BlueprintCallable)
	UE_API void UpdateFiringTime();

	// Returns how long it's been since the weapon was interacted with (fired or equipped)
	UFUNCTION(BlueprintPure)
	UE_API float GetTimeSinceLastInteractedWith() const;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Animation)
	FIMGAnimLayerSelectionSet EquippedAnimSet;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Animation)
	FIMGAnimLayerSelectionSet UneuippedAnimSet;

	/**
	 * Device properties that should be applied while this weapon is equipped.
	 * These properties will be played in with the "Looping" flag enabled, so they will
	 * play continuously until this weapon is unequipped! 
	 */
	UPROPERTY(EditDefaultsOnly, Instanced, BlueprintReadOnly, Category = "Input Devices")
	TArray<TObjectPtr<UInputDeviceProperty>> ApplicableDeviceProperties;
	
	// Choose the best layer from EquippedAnimSet or UneuippedAnimSet based on the specified gameplay tags
	UFUNCTION(BlueprintCallable, BlueprintPure=false, Category=Animation)
	UE_API TSubclassOf<UAnimInstance> PickBestAnimLayer(bool bEquipped, const FGameplayTagContainer& CosmeticTags) const;

	/** Returns the owning Pawn's Platform User ID */
	UFUNCTION(BlueprintCallable)
	UE_API const FPlatformUserId GetOwningUserId() const;

	/** Callback for when the owning pawn of this weapon dies. Removes all spawned device properties. */
	UFUNCTION()
	UE_API void OnDeathStarted(AActor* OwningActor);

	/**
	 * Apply the ApplicableDeviceProperties to the owning pawn of this weapon.
	 * Populate the DevicePropertyHandles so that they can be removed later. This will
	 * Play the device properties in Looping mode so that they will share the lifetime of the
	 * weapon being Equipped.
	 */
	UE_API void ApplyDeviceProperties();

	/** Remove any device proeprties that were activated in ApplyDeviceProperties. */
	UE_API void RemoveDeviceProperties();

private:
	// Blueprint equip/unequip events own presentation. This observer only repairs missing equipped layers.
	void StartAnimLayerObservation();
	void StopAnimLayerObservation();
	void HandlePawnExtension(AActor* Actor, FName EventName);
	void QueueAnimLayerRefresh();
	void RefreshEquippedAnimLayer();

	UFUNCTION()
	void HandleCharacterPartsChanged(UIMGPawnComponent_CharacterParts* Component);

	UFUNCTION()
	void HandleAnimInitialized();

	TSharedPtr<FComponentRequestHandle> PawnExtensionHandle;
	TWeakObjectPtr<UIMGPawnComponent_CharacterParts> ObservedPawnCustomizer;
	TWeakObjectPtr<USkeletalMeshComponent> ObservedMesh;
	// Attribution only; the mesh and Blueprint retain ownership of linked layers and their transitions.
	TWeakObjectPtr<USkeletalMeshComponent> FallbackLinkedMesh;
	TWeakObjectPtr<UAnimInstance> FallbackLinkedInstance;
	FTimerHandle AnimLayerRefreshTimer;
	bool bObservingAnimLayer = false;
	bool bWarnedMissingAnimationDependency = false;

	/** Set of device properties activated by this weapon. Populated by ApplyDeviceProperties */
	UPROPERTY(Transient)
	TSet<FInputDevicePropertyHandle> DevicePropertyHandles;
	TWeakObjectPtr<UIMGHealthComponent> DeviceHealthComponent;

	double TimeLastEquipped = 0.0;
	double TimeLastFired = 0.0;
};

#undef UE_API
