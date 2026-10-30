
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
class USkeletalMeshComponent;
class USkeletalMesh;
class UAnimInstance;

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

	/** Follow the signed world-height displacement already applied by foot placement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Pelvis Offset")
	bool bEnablePelvisOffset = false;

	/** Final pose curve containing AfterIK - BeforeIK in world cm, including the pose's blend weight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Pelvis Offset", meta = (EditCondition = "bEnablePelvisOffset"))
	FName PelvisOffsetCurveName = TEXT("FootIK_PelvisDeltaZ");

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

	/** Camera-local visual offsets after the orbit and before collision; control rotation remains immediate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Shake")
	bool bEnableShake = false;

	/** All axis amplitudes default to zero. Configure the pattern and override GetShakeInput to drive it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Third Person|Shake", meta = (EditCondition = "bEnableShake"))
	FIMGCameraShakeSettings ShakeSettings;

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
	double UpdatePelvisOffset(float DeltaTime);
	void ResetPelvisOffset();
	FRotator UpdateRotationDamping(const FRotator& TargetRotation, float DeltaTime);
	void UpdateShake(float DeltaTime);
	void ResetShake();
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
	TWeakObjectPtr<const UAnimInstance> CachedPelvisAnimInstance;
	FName CachedPelvisOffsetCurve = NAME_None;
	FName CachedPelvisBone = NAME_None;

	FVector InitialStanceOffset = FVector::ZeroVector;
	FVector TargetStanceOffset = FVector::ZeroVector;
	float StanceOffsetBlendPct = 1.0f;
	FVector CurrentStanceOffset = FVector::ZeroVector;
	
};
