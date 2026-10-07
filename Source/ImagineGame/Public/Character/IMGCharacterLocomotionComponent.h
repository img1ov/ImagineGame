#pragma once

#include "AttributeSet.h"
#include "Components/PawnComponent.h"

#include "IMGCharacterLocomotionComponent.generated.h"

#define UE_API IMAGINEGAME_API

class UCharacterMovementComponent;
class UGameFrameworkComponentManager;
class UIMGAbilitySystemComponent;
class UIMGLocomotionSet;
class UIMGPawnExtensionComponent;
struct FActorInitStateChangedParams;
struct FOnAttributeChangeData;

/** Connects GAS locomotion values to a Blueprint decision graph before character movement ticks. */
UCLASS(MinimalAPI, Blueprintable, meta = (BlueprintSpawnableComponent))
class UIMGCharacterLocomotionComponent : public UPawnComponent
{
	GENERATED_BODY()

public:
	UE_API UIMGCharacterLocomotionComponent(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintPure, Category = "IMG|Locomotion")
	UIMGAbilitySystemComponent* GetAbilitySystemComponent() const { return AbilitySystemComponent; }

	UFUNCTION(BlueprintPure, Category = "IMG|Locomotion")
	const UIMGLocomotionSet* GetLocomotionSet() const { return LocomotionSet; }

	UFUNCTION(BlueprintPure, Category = "IMG|Locomotion")
	UCharacterMovementComponent* GetCharacterMovementComponent() const { return MovementComponent; }

	UFUNCTION(BlueprintPure, Category = "IMG|Locomotion")
	UE_API bool IsLocomotionReady() const;

	UE_API virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	UE_API virtual void OnRegister() override;
	UE_API virtual void OnUnregister() override;
	UE_API virtual void BeginPlay() override;
	UE_API virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Called after binding the current ASC/LocomotionSet. Read attributes' CurrentValue here. */
	UFUNCTION(BlueprintImplementableEvent, Category = "IMG|Locomotion", meta = (DisplayName = "On Locomotion Ready"))
	void OnLocomotionReady();

	/** Called after the current pairing is removed. The CMC remains accessible for cleanup. */
	UFUNCTION(BlueprintImplementableEvent, Category = "IMG|Locomotion", meta = (DisplayName = "On Locomotion Unavailable"))
	void OnLocomotionUnavailable();

	/** Notification only; apply final CMC values in Update Locomotion Pre CMC. */
	UFUNCTION(BlueprintImplementableEvent, Category = "IMG|Locomotion", meta = (DisplayName = "On Locomotion Attribute Changed"))
	void OnLocomotionAttributeChanged(FGameplayAttribute Attribute, float OldValue, float NewValue);

	/** Runs before the CMC tick. The Blueprint graph decides and writes the CMC parameters. */
	UFUNCTION(BlueprintImplementableEvent, Category = "IMG|Locomotion", meta = (DisplayName = "Update Locomotion Pre CMC"))
	void UpdateLocomotionPreCMC(float DeltaTime);

private:
	void InitializeConnections();
	void ClearConnections();
	void RefreshLocomotionSet();
	void UnbindLocomotionSet();
	void HandleGameplayReady(const FActorInitStateChangedParams& Params);
	void HandleAbilitySystemInitialized();
	void HandleAbilitySystemUninitialized();
	void HandleAttributeChanged(const FOnAttributeChangeData& ChangeData);

	UPROPERTY(Transient)
	TObjectPtr<UIMGAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(Transient)
	TObjectPtr<const UIMGLocomotionSet> LocomotionSet;

	UPROPERTY(Transient)
	TObjectPtr<UCharacterMovementComponent> MovementComponent;

	TWeakObjectPtr<UGameFrameworkComponentManager> ComponentManager;
	TWeakObjectPtr<UIMGPawnExtensionComponent> PawnExtensionComponent;
	FDelegateHandle GameplayReadyHandle;
	TMap<FGameplayAttribute, FDelegateHandle> AttributeChangeHandles;
	bool bGameplayReady = false;
	bool bRefreshingLocomotionSet = false;
};

#undef UE_API
