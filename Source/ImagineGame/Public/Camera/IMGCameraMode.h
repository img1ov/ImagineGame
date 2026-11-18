
#pragma once

#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "Camera/IMGCameraCollision.h"
#include "Camera/IMGCameraFraming.h"

#include "IMGCameraMode.generated.h"



class AActor;
class UCanvas;
class UIMGCameraComponent;

/**
 * EIMGCameraModeBlendFunction
 *
 *	Blend function used for transitioning between camera modes.
 */
UENUM(BlueprintType)
enum class EIMGCameraModeBlendFunction : uint8
{
	// Does a simple linear interpolation.
	Linear,

	// Smoothly accelerates into the target. Ease amount controlled by the exponent.
	EaseIn,

	// Smoothly decelerates into the target. Ease amount controlled by the exponent.
	EaseOut,

	// Smoothly accelerates and decelerates.  Ease amount controlled by the exponent.
	EaseInOut,

	COUNT	UMETA(Hidden)
};


/**
 * FIMGCameraModeView
 *
 *	View data produced by the camera mode that is used to blend camera modes.
 */
struct IMAGINEGAME_API FIMGCameraModeView
{
public:

	FIMGCameraModeView();

	void Blend(const FIMGCameraModeView& Other, float OtherWeight);

public:

	FVector Location;
	FRotator Rotation;
	FRotator ControlRotation;
	float FieldOfView;
	float CameraShakeScale = 1.f;
	// Values already include this mode's global scale; absent categories use CameraShakeScale.
	TMap<FGameplayTag, float> CameraShakeCategoryScales;
	FIMGCameraFramingResult Framing;
	TOptional<FIMGCameraCollisionRequest> Collision;
};


/**
 * UIMGCameraMode
 *
 *	Base class for all camera modes.
 */
UCLASS(Abstract, NotBlueprintable)
class IMAGINEGAME_API UIMGCameraMode : public UObject
{
	GENERATED_BODY()

public:

	UIMGCameraMode();

	UIMGCameraComponent* GetIMGCameraComponent() const;

	virtual UWorld* GetWorld() const override;

	AActor* GetTargetActor() const;

	const FIMGCameraModeView& GetCameraModeView() const { return View; }

	// Called when this camera mode is activated on the camera mode stack.
	virtual void OnActivation() {};

	// Called when this camera mode is deactivated on the camera mode stack.
	virtual void OnDeactivation() {};

	bool UpdateCameraMode(float DeltaTime);

	/** Snap interpolation on this mode's next update, including while it is being blended. */
	UFUNCTION(BlueprintCallable, Category = "IMG|Camera")
	void ResetInterpolation();

	float GetBlendTime() const { return BlendTime; }
	float GetBlendWeight() const { return BlendWeight; }
	void SetBlendWeight(float Weight);

	FGameplayTag GetCameraTypeTag() const
	{
		return CameraTypeTag;
	}

	virtual void DrawDebug(UCanvas* Canvas) const;

protected:

	virtual FVector GetPivotLocation() const;
	virtual FRotator GetPivotRotation() const;

	virtual void UpdateView(float DeltaTime);
	virtual void UpdateBlending(float DeltaTime);

protected:
	// A tag that can be queried by gameplay code that cares when a kind of camera mode is active
	// without having to ask about a specific mode (e.g., when aiming downsights to get more accuracy)
	UPROPERTY(EditDefaultsOnly, Category = "Blending")
	FGameplayTag CameraTypeTag;

	// View output produced by the camera mode.
	FIMGCameraModeView View;

	// The horizontal field of view (in degrees).
	UPROPERTY(EditDefaultsOnly, Category = "View", Meta = (UIMin = "5.0", UIMax = "170", ClampMin = "5.0", ClampMax = "170.0"))
	float FieldOfView;

	/** Changes reception strength without restarting or freezing shared shake instances. */
	UPROPERTY(EditDefaultsOnly, Category = "Shake", meta = (ClampMin = "0"))
	float CameraShakeScale = 1.f;

	/** Exact category overrides, multiplied by CameraShakeScale. Missing categories use one. */
	UPROPERTY(EditDefaultsOnly, Category = "Shake")
	TMap<FGameplayTag, float> CameraShakeCategoryScales;

	// Minimum view pitch (in degrees).
	UPROPERTY(EditDefaultsOnly, Category = "View", Meta = (UIMin = "-89.9", UIMax = "89.9", ClampMin = "-89.9", ClampMax = "89.9"))
	float ViewPitchMin;

	// Maximum view pitch (in degrees).
	UPROPERTY(EditDefaultsOnly, Category = "View", Meta = (UIMin = "-89.9", UIMax = "89.9", ClampMin = "-89.9", ClampMax = "89.9"))
	float ViewPitchMax;

	// How long it takes to blend in this mode.
	UPROPERTY(EditDefaultsOnly, Category = "Blending")
	float BlendTime;

	// Function used for blending.
	UPROPERTY(EditDefaultsOnly, Category = "Blending")
	EIMGCameraModeBlendFunction BlendFunction;

	// Exponent used by blend functions to control the shape of the curve.
	UPROPERTY(EditDefaultsOnly, Category = "Blending")
	float BlendExponent;

	// Linear blend alpha used to determine the blend weight.
	float BlendAlpha;

	// Blend weight calculated using the blend alpha and function.
	float BlendWeight;

protected:
	/** Snap this mode's interpolation during its next view update. */
	UPROPERTY(transient)
	uint32 bResetInterpolation:1;
	uint32 InterpolationResetRevision = 0;
};


/**
 * UIMGCameraModeStack
 *
 *	Stack used for blending camera modes.
 */
UCLASS()
class IMAGINEGAME_API UIMGCameraModeStack : public UObject
{
	GENERATED_BODY()

public:

	UIMGCameraModeStack();

	void ActivateStack();
	void DeactivateStack();

	bool IsStackActivate() const { return bIsActive; }

	void PushCameraMode(TSubclassOf<UIMGCameraMode> CameraModeClass);

	bool EvaluateStack(float DeltaTime, FIMGCameraModeView& OutCameraModeView);

	void ResetInterpolation();

	void DrawDebug(UCanvas* Canvas) const;

	// Gets the tag associated with the top layer and the blend weight of it
	void GetBlendInfo(float& OutWeightOfTopLayer, FGameplayTag& OutTagOfTopLayer) const;

protected:

	UIMGCameraMode* GetCameraModeInstance(TSubclassOf<UIMGCameraMode> CameraModeClass);

	bool UpdateStack(float DeltaTime);
	void BlendStack(FIMGCameraModeView& OutCameraModeView) const;

protected:

	bool bIsActive;
	uint32 StackRevision = 0;

	UPROPERTY()
	TArray<TObjectPtr<UIMGCameraMode>> CameraModeInstances;

	UPROPERTY()
	TArray<TObjectPtr<UIMGCameraMode>> CameraModeStack;
};
