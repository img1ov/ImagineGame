
#pragma once

#include "Camera/IMGCameraMode.h"
#include "Camera/IMGCameraRotationDamping.h"
#include "Camera/IMGCameraShake.h"
#include "Curves/CurveFloat.h"
#include "Camera/IMGPenetrationAvoidanceFeeler.h"
#include "DrawDebugHelpers.h"
#include "IMGCameraMode_ThirdPerson.generated.h"

class UCurveVector;
class APlayerController;
class USkeletalMesh;
class USkeletalMeshComponent;

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

	/** Queried once per view update while Shake is enabled. Gameplay/curves own activation and non-negative scales. */
	UFUNCTION(BlueprintNativeEvent, Category = "Third Person|Shake")
	void GetShakeInput(AActor* TargetActor, float DeltaTime, bool& bActive, float& AmplitudeScale, float& FrequencyScale);

protected:

	virtual void UpdateView(float DeltaTime) override;
	virtual void OnActivation() override;
	virtual void OnDeactivation() override;
	virtual void GetShakeInput_Implementation(AActor* TargetActor, float DeltaTime, bool& bActive, float& AmplitudeScale, float& FrequencyScale);

	void UpdateForTarget();
	void UpdatePreventPenetration(float DeltaTime);
	void PreventCameraPenetration(class AActor const& ViewTarget, FVector const& SafeLoc, FVector& CameraLoc, float const& DeltaTime, float& DistBlockedPct, bool bSingleRayOnly);

	virtual void DrawDebug(UCanvas* Canvas) const override;

protected:

	// Curve that defines local-space offsets from the target using the view pitch to evaluate the curve.
	UPROPERTY(EditDefaultsOnly, Category = "Third Person", Meta = (EditCondition = "!bUseRuntimeFloatCurves"))
	TObjectPtr<const UCurveVector> TargetOffsetCurve;

	// UE-103986: Live editing of RuntimeFloatCurves during PIE does not work (unlike curve assets).
	// Once that is resolved this will become the default and TargetOffsetCurve will be removed.
	UPROPERTY(EditDefaultsOnly, Category = "Third Person")
	bool bUseRuntimeFloatCurves;

	UPROPERTY(EditDefaultsOnly, Category = "Third Person", Meta = (EditCondition = "bUseRuntimeFloatCurves"))
	FRuntimeFloatCurve TargetOffsetX;

	UPROPERTY(EditDefaultsOnly, Category = "Third Person", Meta = (EditCondition = "bUseRuntimeFloatCurves"))
	FRuntimeFloatCurve TargetOffsetY;

	UPROPERTY(EditDefaultsOnly, Category = "Third Person", Meta = (EditCondition = "bUseRuntimeFloatCurves"))
	FRuntimeFloatCurve TargetOffsetZ;

	// Controls how quickly crouch and crawl eye-height offsets blend in or out.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person")
	float CrouchOffsetBlendMultiplier = 5.0f;

	/** Lag the camera pivot like a spring arm, independently of rotation damping. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Camera Lag")
	bool bEnableCameraLag = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Camera Lag", meta = (ClampMin = "0.0"))
	float CameraLocationLagSpeed = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Camera Lag")
	bool bUseCameraLagSubstepping = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Camera Lag", meta = (ClampMin = "0.005"))
	float CameraLagMaxTimeStep = 1.f / 60.f;

	/** Maximum distance behind the pivot; zero means no limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Camera Lag", meta = (ClampMin = "0.0"))
	float CameraLagMaxDistance = 0.f;

	/** Smooth the rendered orbit; control rotation remains immediate. Camera-based weapon targeting follows the rendered view. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Rotation Damping")
	bool bEnableRotationDamping = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Rotation Damping", meta = (EditCondition = "bEnableRotationDamping"))
	FIMGCameraRotationDampingSettings RotationDampingSettings;

	/** Camera-local visual offsets after the orbit and before collision; control rotation remains immediate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Shake")
	bool bEnableShake = false;

	/** All axis amplitudes default to zero. Configure the pattern and override GetShakeInput to drive it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Shake", meta = (EditCondition = "bEnableShake"))
	FIMGCameraShakeSettings ShakeSettings;

	/** Follow the rendered body's height without requiring the AnimInstance to publish IK state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Pelvis Offset")
	bool bApplyPelvisOffset = false;

	/** Socket or bone on the target Character's Mesh. None or a missing name uses the original pivot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Pelvis Offset", meta = (EditCondition = "bApplyPelvisOffset"))
	FName PelvisOffsetSocket = NAME_None;

	/** Desired pivot height above the sampled socket, in world centimeters. Configure for this model and camera mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Pelvis Offset", meta = (ForceUnits = "cm", EditCondition = "bApplyPelvisOffset"))
	float PelvisOffsetHeight = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Pelvis Offset", meta = (ClampMin = "0", ClampMax = "1", EditCondition = "bApplyPelvisOffset"))
	float PelvisOffsetWeight = 1.f;

	/** Remove this much absolute height error before applying the weight, continuously around zero. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Pelvis Offset", meta = (ClampMin = "0", ForceUnits = "cm", EditCondition = "bApplyPelvisOffset"))
	float PelvisOffsetDeadZone = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Pelvis Offset", meta = (ClampMin = "0", ForceUnits = "cm", EditCondition = "bApplyPelvisOffset"))
	float PelvisOffsetMaxDown = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Pelvis Offset", meta = (ClampMin = "0", ForceUnits = "cm", EditCondition = "bApplyPelvisOffset"))
	float PelvisOffsetMaxUp = 40.f;

	/** Zero follows the target height immediately. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Pelvis Offset", meta = (ClampMin = "0", ClampMax = "30", ForceUnits = "Hz", EditCondition = "bApplyPelvisOffset"))
	float PelvisOffsetFrequency = 2.f;

	/** One is critical damping; lower values allow overshoot, still bounded by MaxDown/MaxUp. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Pelvis Offset", meta = (ClampMin = "0", ClampMax = "10", EditCondition = "bApplyPelvisOffset"))
	float PelvisOffsetDampingRatio = 1.f;

	/** Draw the original pivot, compensated pivot and applied height each frame; requires ENABLE_DRAW_DEBUG. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Pelvis Offset", meta = (EditCondition = "bApplyPelvisOffset"))
	bool bDrawPelvisOffset = false;

	// Penetration prevention
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Collision")
	float PenetrationBlendInTime = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Collision")
	float PenetrationBlendOutTime = 0.15f;

	/** If true, does collision checks to keep the camera out of the world. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Collision")
	bool bPreventPenetration = true;

	/** If true, try to detect nearby walls and move the camera in anticipation.  Helps prevent popping. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Collision")
	bool bDoPredictiveAvoidance = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	float CollisionPushOutDistance = 2.f;

	/** When the camera's distance is pushed into this percentage of its full distance due to penetration */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	float ReportPenetrationPercent = 0.f;

	/**
	 * These are the feeler rays that are used to find where to place the camera.
	 * Index: 0  : This is the normal feeler we use to prevent collisions.
	 * Index: 1+ : These feelers are used if you bDoPredictiveAvoidance=true, to scan for potential impacts if the player
	 *             were to rotate towards that direction and primitively collide the camera so that it pulls in before
	 *             impacting the occluder.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Collision")
	TArray<FIMGPenetrationAvoidanceFeeler> PenetrationAvoidanceFeelers;

	UPROPERTY(Transient)
	float AimLineToDesiredPosBlockedPct;

	UPROPERTY(Transient)
	TArray<TObjectPtr<const AActor>> DebugActorsHitDuringCameraPenetration;

#if ENABLE_DRAW_DEBUG
	mutable float LastDrawDebugTime = -MAX_FLT;
#endif

protected:
	FVector UpdateCameraLag(const FVector& DesiredPivotLocation, float DeltaTime);
	FRotator UpdateRotationDamping(const FRotator& TargetRotation, float DeltaTime);
	void UpdateShake(float DeltaTime);
	void ResetShake();
	double UpdatePelvisOffsetSpring(const FVector& UnlaggedPivotLocation, float DeltaTime);
	void ResetPelvisOffsetSpring();
	void SetTargetStanceOffset(FVector NewTargetOffset);
	void UpdateStanceOffset(float DeltaTime);

	FVector LaggedPivotLocation = FVector::ZeroVector;
	bool bHasLaggedPivotLocation = false;

	FIMGCameraRotationDampingState RotationDampingState;
	FIMGCameraShakeState ShakeState;
	float ShakeAmplitudeScale = 1.f;
	float ShakeFrequencyScale = 1.f;
	TWeakObjectPtr<const AActor> CachedViewTarget;
	TWeakObjectPtr<const APlayerController> CachedViewController;

	double PelvisOffset = 0.0;
	double PelvisOffsetVelocity = 0.0;
	bool bHasPelvisOffset = false;
	TWeakObjectPtr<const USkeletalMeshComponent> CachedPelvisMesh;
	TWeakObjectPtr<const USkeletalMesh> CachedPelvisMeshAsset;
	FName CachedPelvisSocket = NAME_None;

	FVector InitialStanceOffset = FVector::ZeroVector;
	FVector TargetStanceOffset = FVector::ZeroVector;
	float StanceOffsetBlendPct = 1.0f;
	FVector CurrentStanceOffset = FVector::ZeroVector;
	
};
