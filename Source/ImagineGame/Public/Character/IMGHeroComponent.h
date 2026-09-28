
#pragma once

#include "Components/GameFrameworkInitStateInterface.h"
#include "Components/PawnComponent.h"
#include "GameFeatures/GameFeatureAction_AddInputContextMapping.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameplayTagContainer.h"
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

/** Tuning for world-space movement requests before they reach the movement system. */
USTRUCT(BlueprintType)
struct IMAGINEGAME_API FIMGMovementIntentSettings
{
	GENERATED_BODY()

	/**
	 * Normalized strength used to turn movement intent toward the desired direction.
	 * At 60 Hz, the value is the fraction of the remaining angular error removed
	 * each frame. Zero preserves the current heading; one bypasses smoothing and
	 * responds immediately. Delta-time correction keeps the response frame-rate independent.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement Intent", meta=(ClampMin="0", ClampMax="1", UIMin="0", UIMax="1"))
	float TurningStrength = 1.0f;

	/**
	 * Angle threshold between the currently smoothed movement intent and the desired
	 * movement direction that is treated as a pivot. A pivot bypasses
	 * direction smoothing so downstream systems observe the redirection immediately.
	 *
	 * Set to -1 to disable sharp-turn detection.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement Intent", meta=(ClampMin="-1", ClampMax="180", UIMin="-1", UIMax="180", Units="Degrees"))
	float PivotAngleThreshold = -1.0f;

	/**
	 * Time before another sharp-turn bypass may trigger. Direction changes during
	 * this interval use normal smoothing. Zero permits immediate retriggering.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement Intent", meta=(ClampMin="0", UIMin="0", Units="Seconds", EditCondition="PivotAngleThreshold >= 0"))
	float PivotRetriggerInterval = 0.0f;

	/** Gameplay event sent when a pivot triggers the direction-smoothing bypass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement Intent", meta=(Categories="GameplayEvent", EditCondition="PivotAngleThreshold >= 0"))
	FGameplayTag PivotGameplayEventTag;
};

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

	/**
	 * Shapes and submits a world-space movement request through this hero's
	 * movement-intent pipeline. Player input, AI, and scripted movement should
	 * share this entry point so they receive identical directional response.
	 */
	UFUNCTION(BlueprintCallable, Category="IMG|Movement Intent", meta=(AdvancedDisplay="bForce"))
	UE_API void AddMovementIntent(FVector WorldDirection, float ScaleValue = 1.0f, bool bForce = false);

	/** Clears runtime movement-intent state after a movement request ends. */
	UFUNCTION(BlueprintCallable, Category="IMG|Movement Intent")
	UE_API void ResetMovementIntent();

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
	 * Stateful boundary between world-space movement requests and the movement system.
	 *
	 * The processor owns direction shaping only. AddMovementIntent handles the
	 * engine-facing submission, allowing the same processor to feed
	 * AddMovementInput today or a Mover input command later.
	 */
	struct FMovementIntentProcessor
	{
		FVector UpdateMovementIntent(
			const FVector& DesiredMovementIntent,
			const FVector& ActorFacing,
			float DeltaSeconds,
			const FIMGMovementIntentSettings& Settings,
			bool& bOutTriggeredPivot);
		void Reset();

	private:
		void Initialize(
			const FVector& DesiredDirection,
			const FVector& ActorFacing);
		bool EvaluatePivot(
			float DesiredAngleRadians,
			float DeltaSeconds,
			const FIMGMovementIntentSettings& Settings);
		static float CalculateFrameIndependentAlpha(float TurningStrength, float DeltaSeconds);

		float SmoothedAngleRadians = 0.0f;
		/** Time remaining before another pivot may be triggered. */
		float PivotTimer = 0.0f;
		bool bIsInitialized = false;
	};

	UPROPERTY(EditAnywhere)
	TArray<FInputMappingContextAndPriority> DefaultInputMappings;

	/** Settings applied to world-space movement requests before submission. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement Intent", meta=(ShowOnlyInnerProperties))
	FIMGMovementIntentSettings MovementIntentSettings;

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
