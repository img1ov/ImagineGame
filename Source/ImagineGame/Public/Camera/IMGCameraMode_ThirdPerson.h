
#pragma once

#include "Camera/IMGCameraMode.h"
#include "Camera/IMGCameraDamping.h"
#include "Curves/CurveFloat.h"
#include "IMGCameraMode_ThirdPerson.generated.h"

class APlayerController;
class USkeletalMeshComponent;
class USkeletalMesh;
class UAnimInstance;
class UCurveVector;
class USceneComponent;

/**
 * UIMGCameraMode_ThirdPerson
 *
 *	A basic third person camera mode.
 */
UCLASS(Abstract, Blueprintable)
class IMAGINEGAME_API UIMGCameraMode_ThirdPerson : public UIMGCameraMode
{
	GENERATED_BODY()

public:

	UIMGCameraMode_ThirdPerson();

	/** An optional external focus target. Free look is used when bHasFocus is false. */
	UFUNCTION(BlueprintNativeEvent, Category = "Third Person|Framing")
	void GetFramingFocusPoint(AActor* TargetActor, bool& bHasFocus, FVector& FocusPoint);

protected:

	virtual void UpdateView(float DeltaTime) override;
	virtual void OnActivation() override;
	virtual void OnDeactivation() override;
	virtual void GetFramingFocusPoint_Implementation(AActor* TargetActor, bool& bHasFocus, FVector& FocusPoint);

	void UpdateForTarget();
	void BuildCollisionRequest(const FVector& PivotLocation, const FRotator& PreviousControlRotation);
	FIMGCameraFramingSettings EvaluateFramingSettings(float OrbitPitch) const;

	virtual void DrawDebug(UCanvas* Canvas) const override;

protected:
	/** Full camera-local XYZ offset in cm, sampled using orbit pitch in degrees. */
	UPROPERTY(EditDefaultsOnly, Category = "Third Person", meta = (EditCondition = "!bUseRuntimeFloatCurves"))
	TObjectPtr<const UCurveVector> TargetOffsetCurve = nullptr;

	/** A missing vector asset falls back to the inline XYZ curves. */
	UPROPERTY(EditDefaultsOnly, Category = "Third Person")
	bool bUseRuntimeFloatCurves = false;

	UPROPERTY(EditDefaultsOnly, Category = "Third Person", meta = (EditCondition = "bUseRuntimeFloatCurves"))
	FRuntimeFloatCurve TargetOffsetX;

	UPROPERTY(EditDefaultsOnly, Category = "Third Person", meta = (EditCondition = "bUseRuntimeFloatCurves"))
	FRuntimeFloatCurve TargetOffsetY;

	UPROPERTY(EditDefaultsOnly, Category = "Third Person", meta = (EditCondition = "bUseRuntimeFloatCurves"))
	FRuntimeFloatCurve TargetOffsetZ;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Third Person|Framing", meta = (ClampMin = "0", ClampMax = "1"))
	float PostCollisionLookAtWeight = 0.625f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Third Person|Framing", meta = (ClampMin = "1", Units = "cm"))
	float MinFocusDistance = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Third Person|Framing", meta = (ClampMin = "0", Units = "cm"))
	float OffAxisTolerance = 1.f;
	/** Smooth changes in the standard view offset independently of whole-pawn movement and camera lag. Zero snaps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Viewpoint", meta = (ClampMin = "0", ClampMax = "1000"))
	float ViewpointSpringConstant = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Viewpoint", meta = (ClampMin = "0", ClampMax = "10"))
	float ViewpointDampingRatio = 1.f;

	/** Spring-damped pivot tracking, independently of rotation and pelvis damping. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Camera Lag")
	bool bEnableCameraLag = true;

	/** Spring response rate sqrt(k), not Hz; squared to obtain the unit-mass spring constant. Zero bypasses lag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Camera Lag", meta = (ClampMin = "0.0"))
	float CameraLocationLagSpeed = 10.0f;

	/** One uses the slow exponential mode; lower values allow inertial overshoot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Camera Lag", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float CameraLagDampingRatio = 1.f;

	/** Interpolate the sampled pivot target between frames when advancing the spring in substeps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Camera Lag")
	bool bUseCameraLagSubstepping = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Camera Lag", meta = (ClampMin = "0.005"))
	float CameraLagMaxTimeStep = 1.f / 60.f;

	/** Maximum distance behind the pivot; zero means no limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Camera Lag", meta = (ClampMin = "0.0"))
	float CameraLagMaxDistance = 0.f;

	/** Follow the signed world-height displacement already applied by foot placement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Pelvis Offset")
	bool bEnablePelvisOffset = false;

	/** Final pose curve containing AfterIK - BeforeIK in world cm, including the pose's blend weight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Pelvis Offset", meta = (EditCondition = "bEnablePelvisOffset"))
	FName PelvisOffsetCurveName = TEXT("LegIK_PelvisDeltaZ");

	/** Bone represented by the curve. A missing bone disables sampling; its absolute height is not used. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Pelvis Offset", meta = (EditCondition = "bEnablePelvisOffset"))
	FName PelvisBoneName = TEXT("pelvis");

	/** Unit-mass spring constant k, not Hz. Zero follows the curve immediately. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Pelvis Offset", meta = (EditCondition = "bEnablePelvisOffset", ClampMin = "0", ClampMax = "1000"))
	float PelvisOffsetSpringConstant = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Pelvis Offset", meta = (EditCondition = "bEnablePelvisOffset", ClampMin = "0", ClampMax = "10"))
	float PelvisOffsetDampingRatio = 1.f;

	/** Smooth the rendered orbit; control rotation remains immediate. Camera-based weapon targeting follows the rendered view. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Rotation Damping")
	bool bEnableRotationDamping = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Rotation Damping", meta = (EditCondition = "bEnableRotationDamping"))
	FIMGCameraRotationDampingSettings RotationDampingSettings;

public:
	/** Requests collision on the final blended view, once per camera update. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Collision")
	bool bPreventPenetration = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	FIMGCameraCollisionSettings CollisionSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	bool bIgnoreTargetAttachments = true;

	/** Notify the existing camera assist interface below this remaining distance ratio; zero disables it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision", meta = (ClampMin = "0", ClampMax = "1"))
	float ReportPenetrationPercent = 0.f;

protected:
	FVector UpdateViewpoint(const FVector& DesiredPivotLocation, float DeltaTime);
	void ResetViewpoint();
	FVector UpdateCameraLag(const FVector& DesiredPivotLocation, float DeltaTime);
	double UpdatePelvisOffset(float DeltaTime);
	void ResetPelvisOffset();
	FRotator UpdateRotationDamping(const FRotator& TargetRotation, float DeltaTime);

	FIMGCameraLocationDampingState LocationDampingState;
	FIMGCameraLocationDampingState ViewpointDampingState;
	TWeakObjectPtr<const USceneComponent> CachedViewRoot;
	FVector PreviousViewReferenceOffset = FVector::ZeroVector;
	float PreviousViewCapsuleHalfHeight = 0.f;
	bool bViewpointUsesCapsuleBase = false;

	FIMGCameraRotationDampingState RotationDampingState;
	TWeakObjectPtr<const AActor> CachedViewTarget;
	TWeakObjectPtr<const APlayerController> CachedViewController;

	double PelvisOffset = 0.0;
	double PelvisOffsetVelocity = 0.0;
	bool bHasPelvisOffset = false;
	TWeakObjectPtr<const USkeletalMeshComponent> CachedPelvisMesh;
	TWeakObjectPtr<const USkeletalMesh> CachedPelvisMeshAsset;
	TWeakObjectPtr<const UAnimInstance> CachedPelvisAnimInstance;
	FName CachedPelvisOffsetCurve = NAME_None;
	FName CachedPelvisBone = NAME_None;
};
