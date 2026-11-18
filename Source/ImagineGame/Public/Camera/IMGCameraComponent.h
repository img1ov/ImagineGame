
#pragma once

#include "Camera/CameraComponent.h"
#include "GameFramework/Actor.h"
#include "Camera/IMGCameraCollision.h"
#include "UObject/ObjectKey.h"

#include "IMGCameraComponent.generated.h"

class UCanvas;
class UIMGCameraMode;
class UIMGCameraModeStack;
class UIMGCameraShakeInstance;
class UIMGCameraShakeDefinition;
class UObject;
struct FFrame;
struct FGameplayTag;
struct FMinimalViewInfo;
struct FIMGCameraModeView;
template <class TClass> class TSubclassOf;

DECLARE_DELEGATE_RetVal(TSubclassOf<UIMGCameraMode>, FIMGCameraModeDelegate);


/**
 * UIMGCameraComponent
 *
 *	The base camera component class used by this project.
 */
UCLASS()
class IMAGINEGAME_API UIMGCameraComponent : public UCameraComponent
{
	GENERATED_BODY()

public:

	UIMGCameraComponent(const FObjectInitializer& ObjectInitializer);

	// Returns the camera component if one exists on the specified actor.
	UFUNCTION(BlueprintPure, Category = "IMG|Camera")
	static UIMGCameraComponent* FindCameraComponent(const AActor* Actor) { return (IsValid(Actor) ? Actor->FindComponentByClass<UIMGCameraComponent>() : nullptr); }

	// Returns the target actor that the camera is looking at.
	virtual AActor* GetTargetActor() const { return GetOwner(); }

	// Delegate used to query for the best camera mode.
	FIMGCameraModeDelegate DetermineCameraModeDelegate;

	// Add an offset to the field of view.  The offset is only for one frame, it gets cleared once it is applied.
	void AddFieldOfViewOffset(float FovOffset);

	/** Plays a finite effect once. Its lifetime is independent of the requesting ability or mode. */
	UFUNCTION(BlueprintCallable, Category = "IMG|Camera|Shake")
	UIMGCameraShakeInstance* PlayCameraShake(const UIMGCameraShakeDefinition* Definition, float AmplitudeScale = 1.f, float FrequencyScale = 1.f);

	/** Starts or resumes one continuous instance for this source and definition. The source must release it. */
	UFUNCTION(BlueprintCallable, Category = "IMG|Camera|Shake")
	UIMGCameraShakeInstance* StartCameraShake(const UIMGCameraShakeDefinition* Definition, UObject* SourceOwner, float AmplitudeScale = 1.f, float FrequencyScale = 1.f);

	/** Fixed spatial source; distance attenuation is recomputed from the unshaken view on every update. */
	UFUNCTION(BlueprintCallable, Category = "IMG|Camera|Shake")
	UIMGCameraShakeInstance* PlayCameraShakeAtLocation(const UIMGCameraShakeDefinition* Definition, FVector SourceLocation, float AmplitudeScale = 1.f, float FrequencyScale = 1.f);

	/** Updates scales; continuous effects resume automatically. AutoStart also refreshes a finite overall hold. */
	UFUNCTION(BlueprintCallable, Category = "IMG|Camera|Shake")
	bool UpdateCameraShake(UIMGCameraShakeInstance* Shake, float AmplitudeScale, float FrequencyScale = 1.f, bool bAutoStart = false);

	UFUNCTION(BlueprintCallable, Category = "IMG|Camera|Shake")
	void StopCameraShake(UIMGCameraShakeInstance* Shake, bool bImmediately = false);

	UFUNCTION(BlueprintCallable, Category = "IMG|Camera|Shake")
	void StopCameraShakesFromSource(UObject* SourceOwner, bool bImmediately = false);

	UFUNCTION(BlueprintCallable, Category = "IMG|Camera|Shake")
	void StopAllCameraShakes(bool bImmediately = false);

	virtual void DrawDebug(UCanvas* Canvas) const;

	/** Uses the same local-player projection rules for rotation damping and the final near-clip scan. */
	FVector2D GetPerspectiveHalfFovTangents(const FMinimalViewInfo& View) const;

	/** Clears output collision history without changing the active camera modes. */
	void ResetCollision();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision|Debug")
	bool bDrawCollision = false;

	// Gets the tag associated with the top layer and the blend weight of it
	void GetBlendInfo(float& OutWeightOfTopLayer, FGameplayTag& OutTagOfTopLayer) const;

	/** Reset active modes after a teleport or another discontinuous change to the camera target. */
	UFUNCTION(BlueprintCallable, Category = "IMG|Camera")
	void ResetInterpolation();

	virtual void NotifyCameraCut() override;
	virtual void Activate(bool bReset = false) override;
	virtual void Deactivate() override;

protected:

	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetCameraView(float DeltaTime, FMinimalViewInfo& DesiredView) override;

	virtual void UpdateCameraModes();
	void ResolveCollision(float DeltaTime, const FIMGCameraModeView& CameraModeView, FMinimalViewInfo& DesiredView);
	void UpdateCameraShakes(float DeltaTime, const FIMGCameraModeView& CameraModeView, FMinimalViewInfo& DesiredView);
	void SynchronizeCameraShakeContext();
	void DiscardCameraShakesBeforeCurrentFrame();
	bool CanAcceptCameraShakeRequests() const;
	void RemoveCameraShake(UIMGCameraShakeInstance* Shake);
	UIMGCameraShakeInstance* CreateCameraShake(const UIMGCameraShakeDefinition* Definition, UObject* SourceOwner,
		TOptional<FVector> SourceLocation, bool bContinuous, float AmplitudeScale, float FrequencyScale);

protected:

	// Stack used to blend the camera modes.
	UPROPERTY()
	TObjectPtr<UIMGCameraModeStack> CameraModeStack;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UIMGCameraShakeInstance>> CameraShakes;
	// Cooldowns outlive finished instances without retaining their configuration assets.
	TMap<TObjectKey<const UIMGCameraShakeDefinition>, double> LastShakeStartTimes;

	TWeakObjectPtr<const AActor> ShakeTarget;
	TWeakObjectPtr<const AActor> ShakeController;
	TOptional<FVector> ShakeReferenceLocation;
	uint64 LastShakeCutFrame = MAX_uint64;

	// Offset applied to the field of view.  The offset is only for one frame, it gets cleared once it is applied.
	float FieldOfViewOffset;
	uint64 FieldOfViewOffsetFrame = MAX_uint64;

	FIMGCameraCollisionState CollisionState;
	FIMGCameraCollisionResult CollisionDiagnostics;
	TWeakObjectPtr<const AActor> CollisionTarget;
	TWeakObjectPtr<const AActor> CollisionAnchor;
	TWeakObjectPtr<const AActor> CollisionController;
	ECollisionChannel CollisionTraceChannel = ECC_Camera;
	bool bCollisionTraceComplex = false;
	mutable float LastCollisionDebugTime = -MAX_flt;
	uint64 LastCameraUpdateFrame = MAX_uint64;
	bool bEndingPlay = false;
	bool bUnregistering = false;

};
