#pragma once

#include "AttributeSet.h"
#include "Components/GameFrameworkComponentDelegates.h"
#include "Character/IMGMovementPrerequisiteComponent.h"

#include "IMGLocomotionComponent.generated.h"

#define UE_API IMAGINEGAME_API

class AIMGCharacter;
class UIMGAbilitySystemComponent;
class UIMGCharacterMovementComponent;
class UIMGLocomotionSet;
class UIMGPawnExtensionComponent;
struct FActorInitStateChangedParams;
struct FOnAttributeChangeData;

/** Connects GAS locomotion values to a Blueprint decision graph before character movement ticks. */
UCLASS(MinimalAPI, Blueprintable, meta = (BlueprintSpawnableComponent))
class UIMGLocomotionComponent : public UIMGMovementPrerequisiteComponent
{
	GENERATED_BODY()

public:
	UE_API UIMGLocomotionComponent(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintPure, Category = "IMG|Locomotion", meta = (DisplayName = "Get IMG Character"))
	UE_API AIMGCharacter* GetIMGCharacter() const;

	UFUNCTION(BlueprintPure, Category = "IMG|Locomotion", meta = (DisplayName = "Get IMG Ability System Component"))
	UIMGAbilitySystemComponent* GetIMGAbilitySystemComponent() const { return AbilitySystemComponent; }

	UFUNCTION(BlueprintPure, Category = "IMG|Locomotion")
	const UIMGLocomotionSet* GetLocomotionSet() const { return LocomotionSet; }

	UFUNCTION(BlueprintPure, Category = "IMG|Locomotion", meta = (DisplayName = "Get IMG Character Movement Component"))
	UE_API UIMGCharacterMovementComponent* GetIMGCharacterMovementComponent() const;

	UFUNCTION(BlueprintPure, Category = "IMG|Locomotion")
	UE_API bool IsLocomotionReady() const;

protected:
	UE_API virtual void OnRegister() override;
	UE_API virtual void OnUnregister() override;
	UE_API virtual void BeginPlay() override;
	UE_API virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	UE_API virtual bool CanUpdateLocomotion() const override;

	/** Called after binding the current ASC and CMC. The optional LocomotionSet may be null. */
	UFUNCTION(BlueprintImplementableEvent, Category = "IMG|Locomotion", meta = (DisplayName = "On Locomotion Ready"))
	void OnLocomotionReady();

	/** Called after the current ASC pairing is removed. The CMC remains accessible for cleanup. */
	UFUNCTION(BlueprintImplementableEvent, Category = "IMG|Locomotion", meta = (DisplayName = "On Locomotion Unavailable"))
	void OnLocomotionUnavailable();

	/** Notification only; apply final CMC values in UpdateLocomotionPreCMC. */
	UFUNCTION(BlueprintImplementableEvent, Category = "IMG|Locomotion", meta = (DisplayName = "On Locomotion Attribute Changed"))
	void OnLocomotionAttributeChanged(FGameplayAttribute Attribute, float OldValue, float NewValue);

private:
	void RegisterForGameplayReady();
	void InitializeGameplayReadyConnections();
	void ClearConnections();
	void RefreshAbilitySystemConnection();
	void ClearAbilitySystemConnection();
	void RefreshLocomotionSet();
	void UnbindLocomotionSet();
	UFUNCTION()
	void HandleGameplayReady(const FActorInitStateChangedParams& Params);
	void HandleAbilitySystemInitialized();
	void HandleAbilitySystemUninitialized();
	void HandleAttributeChanged(const FOnAttributeChangeData& ChangeData);

	UPROPERTY(Transient)
	TObjectPtr<UIMGAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(Transient)
	TObjectPtr<const UIMGLocomotionSet> LocomotionSet;

	TWeakObjectPtr<UIMGPawnExtensionComponent> PawnExtensionComponent;
	FActorInitStateChangedBPDelegate GameplayReadyDelegate;
	TMap<FGameplayAttribute, FDelegateHandle> AttributeChangeHandles;
	bool bRefreshingConnections = false;
};

#undef UE_API
