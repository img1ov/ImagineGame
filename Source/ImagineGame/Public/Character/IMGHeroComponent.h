
#pragma once

#include "Components/GameFrameworkInitStateInterface.h"
#include "Components/PawnComponent.h"
#include "GameFeatures/GameFeatureAction_AddInputContextMapping.h"
#include "GameplayAbilitySpecHandle.h"
#include "Input/IMGInputConfig.h"

#include "IMGHeroComponent.generated.h"

#define UE_API IMAGINEGAME_API

namespace EEndPlayReason { enum Type : int; }
struct FLoadedMappableConfigPair;
struct FMappableConfigPair;

class UGameFrameworkComponentManager;
class UInputComponent;
class UIMGCameraMode;
class UIMGInputConfig;
class UObject;
struct FActorInitStateChangedParams;
struct FFrame;
struct FGameplayTag;
struct FInputActionValue;

/**
 * Component that sets up input and camera handling for player controlled pawns (or bots that simulate players).
 * This depends on a PawnExtensionComponent to coordinate initialization.
 */
UCLASS(MinimalAPI, Blueprintable, Meta=(BlueprintSpawnableComponent))
class UIMGHeroComponent : public UPawnComponent, public IGameFrameworkInitStateInterface
{
	GENERATED_BODY()

public:

	UE_API UIMGHeroComponent(const FObjectInitializer& ObjectInitializer);

	/** Returns the hero component if one exists on the specified actor. */
	UFUNCTION(BlueprintPure, Category="IMG")
	static UIMGHeroComponent* FindHeroComponent(const AActor* Actor){ return (Actor ? Actor->FindComponentByClass<UIMGHeroComponent>() : nullptr);}

	/** Overrides the camera from an active gameplay ability. */
	UE_API void SetAbilityCameraMode(TSubclassOf<UIMGCameraMode> CameraMode, const FGameplayAbilitySpecHandle& OwningSpecHandle);

	/** Clears the camera override if owned by this ability. */
	UE_API void ClearAbilityCameraMode(const FGameplayAbilitySpecHandle& OwningSpecHandle);

	/** Adds mode-specific input config */
	UE_API void AddAdditionalInputConfig(const UIMGInputConfig* InputConfig);

	/** Removes a mode-specific input config if it has been added */
	UE_API void RemoveAdditionalInputConfig(const UIMGInputConfig* InputConfig);

	/** True if this is controlled by a real player and has progressed far enough in initialization where additional input bindings can be added */
	UE_API bool IsReadyToBindInputs() const;

	/** The name of the extension event sent via UGameFrameworkComponentManager when ability inputs are ready to bind */
	static UE_API const FName NAME_BindInputsNow;

	/** The name of this component-implemented feature */
	static UE_API const FName NAME_ActorFeatureName;

	//~ Begin IGameFrameworkInitStateInterface interface
	virtual FName GetFeatureName() const override { return NAME_ActorFeatureName; }
	UE_API virtual bool CanChangeInitState(UGameFrameworkComponentManager* Manager, FGameplayTag CurrentState, FGameplayTag DesiredState) const override;
	UE_API virtual void HandleChangeInitState(UGameFrameworkComponentManager* Manager, FGameplayTag CurrentState, FGameplayTag DesiredState) override;
	UE_API virtual void OnActorInitStateChanged(const FActorInitStateChangedParams& Params) override;
	UE_API virtual void CheckDefaultInitialization() override;
	//~ End IGameFrameworkInitStateInterface interface

protected:

	UE_API virtual void OnRegister() override;
	UE_API virtual void BeginPlay() override;
	UE_API virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UE_API void InitializePlayerInput(UInputComponent* PlayerInputComponent);

	UE_API void Input_AbilityInputTagPressed(FGameplayTag InputTag);
	UE_API void Input_AbilityInputTagReleased(FGameplayTag InputTag);

	UE_API void Input_AutoRun(const FInputActionValue& InputActionValue);
	void Input_Move(const FInputActionValue& InputActionValue);
	void Input_MoveCompleted(const FInputActionValue&);
	UE_API void Input_LookMouse(const FInputActionValue& InputActionValue);
	UE_API void Input_LookStick(const FInputActionValue& InputActionValue);
	UE_API void Input_Crouch(const FInputActionValue& InputActionValue);
	UE_API void Input_Crawl(const FInputActionValue& InputActionValue);

	UE_API TSubclassOf<UIMGCameraMode> DetermineCameraMode() const;

protected:
	/**
	 * Stateful boundary between raw player input and the movement system.
	 *
	 * Keeping this state separate from Input_Move makes the latter an adapter:
	 * it builds a world-space desired intent, delegates shaping here, then hands
	 * the result to the active movement implementation. The same processor can
	 * therefore feed AddMovementInput today or a Mover input command later.
	 */
	struct FMovementIntentProcessor
	{
		FVector Update(const FVector& DesiredMovementIntent, float DeltaSeconds, float TurningStrength);
		void Reset();

	private:
		float MovementIntentAngleRadians = 0.0f;
		bool bHasMovementIntent = false;
	};

	UPROPERTY(EditAnywhere)
	TArray<FInputMappingContextAndPriority> DefaultInputMappings;

	/**
	 * Controls how quickly movement intent turns toward the desired world-space direction.
	 * This follows Mover's smoothing-strength convention rather than representing degrees per second:
	 * larger values respond faster, smaller non-negative values feel heavier, and a negative value
	 * bypasses smoothing to preserve the legacy instant response.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement Intent", meta=(ClampMin="-1", UIMin="-1", UIMax="100"))
	float TurningStrength = 60.0f;

	/** Camera mode set by an ability. */
	UPROPERTY()
	TSubclassOf<UIMGCameraMode> AbilityCameraMode;

	/** Spec handle for the last ability to set a camera mode. */
	FGameplayAbilitySpecHandle AbilityCameraModeOwningSpecHandle;

	/** True when player input bindings have been applied, will never be true for non - players */
	bool bReadyToBindInputs;

	/** Runtime-only state for movement intent shaping. */
	FMovementIntentProcessor MovementIntentProcessor;
};

#undef UE_API
