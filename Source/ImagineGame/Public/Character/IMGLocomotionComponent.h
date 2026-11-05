#pragma once

#include "AttributeSet.h"
#include "Components/GameFrameworkComponentDelegates.h"
#include "Character/IMGMovementPrerequisiteComponent.h"
#include "Character/IMGMovementSimulationContext.h"

#include "IMGLocomotionComponent.generated.h"

#define UE_API IMAGINEGAME_API

class AIMGCharacter;
class UIMGAbilitySystemComponent;
class UIMGCharacterMovementComponent;
class UIMGLocomotionSet;
class UIMGPawnExtensionComponent;
struct FActorInitStateChangedParams;
struct FOnAttributeChangeData;

/** Aggregates locomotion inputs and owns their single CMC parameter application path. */
UCLASS(MinimalAPI, Blueprintable, meta = (BlueprintSpawnableComponent))
class UIMGLocomotionComponent : public UIMGMovementPrerequisiteComponent
{
	GENERATED_BODY()

public:
	UE_API UIMGLocomotionComponent(const FObjectInitializer& ObjectInitializer);
	UE_API virtual void Activate(bool bReset = false) override;
	UE_API virtual void Deactivate() override;

	UFUNCTION(BlueprintPure, Category = "IMG|Locomotion", meta = (DisplayName = "Get IMG Character"))
	UE_API AIMGCharacter* GetIMGCharacter() const;

	UFUNCTION(BlueprintPure, Category = "IMG|Locomotion", meta = (DisplayName = "Get IMG Ability System Component"))
	UIMGAbilitySystemComponent* GetIMGAbilitySystemComponent() const { return AbilitySystemComponent; }

	UFUNCTION(BlueprintPure, Category = "IMG|Locomotion")
	const UIMGLocomotionSet* GetLocomotionSet() const { return IsLocomotionReady() ? LocomotionSet.Get() : nullptr; }

	UFUNCTION(BlueprintPure, Category = "IMG|Locomotion", meta = (DisplayName = "Get IMG Character Movement Component"))
	UE_API UIMGCharacterMovementComponent* GetIMGCharacterMovementComponent() const;

	UFUNCTION(BlueprintPure, Category = "IMG|Locomotion")
	UE_API bool IsLocomotionReady() const;

protected:
	UE_API virtual void OnRegister() override;
	UE_API virtual void OnUnregister() override;
	UE_API virtual void BeginPlay() override;
	UE_API virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	UE_API virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	UE_API virtual bool CanUpdateLocomotion() const override;
	virtual void OnLocomotionReadyNative() {}
	virtual void OnLocomotionUnavailableNative() {}
	UE_API virtual void UpdateLocomotionPreCMC_Implementation(float DeltaTime) override;
	virtual uint8 GetMovementRequest() const { return 0; }
	virtual void ExtendMovementContext(FMovementSimulationContext& Context) const {}
	UE_API virtual bool CalculateMovementParameters(uint8 Request, const FMovementSimulationContext& Context,
		const FVector& Input, bool bReplay, FCharacterMovementParameters& OutParameters);
	UE_API UIMGCharacterMovementComponent* GetBoundMovement() const;

	enum ELocomotionParameterIndex : uint8
	{
		WalkSpeedParameter, RunSpeedParameter, SprintSpeedParameter, CrouchSpeedParameter, LocomotionParameterCount
	};

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Locomotion", meta = (ClampMin = "0"))
	float BaseMaxAcceleration = 800.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Locomotion", meta = (ClampMin = "0"))
	float BaseGroundFriction = 2.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Locomotion", meta = (ClampMin = "0"))
	float BaseBrakingDecelerationWalking = 800.f;

	/** Called after binding the current LocomotionSet and CMC provider. */
	UFUNCTION(BlueprintImplementableEvent, Category = "IMG|Locomotion", meta = (DisplayName = "On Locomotion Ready"))
	void OnLocomotionReady();

	/** Called after releasing the current Set and CMC provider. The CMC remains accessible for cleanup. */
	UFUNCTION(BlueprintImplementableEvent, Category = "IMG|Locomotion", meta = (DisplayName = "On Locomotion Unavailable"))
	void OnLocomotionUnavailable();

	/** Notification after updating pending inputs; an in-progress move retains its captured context. */
	UFUNCTION(BlueprintImplementableEvent, Category = "IMG|Locomotion", meta = (DisplayName = "On Locomotion Attribute Changed"))
	void OnLocomotionAttributeChanged(FGameplayAttribute Attribute, float OldValue, float NewValue);

private:
	void RegisterForGameplayReady();
	void InitializeGameplayReadyConnections();
	void ClearConnections();
	UIMGAbilitySystemComponent* GetReadyAbilitySystem() const;
	void RefreshAbilitySystemConnection();
	void RefreshLocomotionSet();
	void ClearLocomotionConnection(bool bReleaseAbilitySystem);
	void InitializeMovementConnection();
	void ReleaseMovementConnection();
	void InitializeMovementContext();
	void AdvanceMovementContextRevision();
	bool CaptureMovementRequest(uint8& OutRequest, FMovementSimulationContext& OutContext);
	bool ApplyMovementRequest(uint8 Request, const FMovementSimulationContext& Context, const FVector& Input, bool bReplay);
	UFUNCTION()
	void HandleGameplayReady(const FActorInitStateChangedParams& Params);
	UFUNCTION()
	void HandleMovementUpdated(float DeltaSeconds, FVector OldLocation, FVector OldVelocity);
	void HandleAbilitySystemInitialized();
	void HandleAbilitySystemUninitialized();
	void HandleAttributeSetsUpdated();
	void HandleAttributeChanged(const FOnAttributeChangeData& ChangeData);

	UPROPERTY(Transient)
	TObjectPtr<UIMGAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(Transient)
	TObjectPtr<const UIMGLocomotionSet> LocomotionSet;

	TWeakObjectPtr<UIMGPawnExtensionComponent> PawnExtensionComponent;
	FActorInitStateChangedBPDelegate GameplayReadyDelegate;
	FDelegateHandle AttributeSetsUpdatedHandle;
	TMap<FGameplayAttribute, FDelegateHandle> AttributeChangeHandles;
	TWeakObjectPtr<UIMGCharacterMovementComponent> BoundMovement;
	FCharacterMovementParameters OriginalMovementParameters;
	FMovementSimulationContext PendingMovementContext;
	uint32 MovementContextRevision = 0;
	bool bLocomotionDisabled = false;
	bool bRefreshingConnections = false;
	bool bLocomotionRefreshPending = false;
	bool bLocomotionReadyNotified = false;
	bool bWarnedInvalidMovementContext = false;
	bool bWarnedInvalidMovementParameters = false;
};

#undef UE_API
