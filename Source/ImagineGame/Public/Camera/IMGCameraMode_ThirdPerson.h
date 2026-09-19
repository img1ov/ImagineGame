
#pragma once

#include "Camera/IMGCameraMode.h"
#include "Curves/CurveFloat.h"
#include "Camera/IMGPenetrationAvoidanceFeeler.h"
#include "DrawDebugHelpers.h"
#include "IMGCameraMode_ThirdPerson.generated.h"

class UCurveVector;

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

protected:

	virtual void UpdateView(float DeltaTime) override;
	virtual void OnActivation() override;

	void UpdateForTarget(float DeltaTime);
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

	/** Lag the camera pivot like a spring arm. Rotation and collision remain immediate. */
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
	FVector UpdateCameraLag(float DeltaTime);
	void SetTargetStanceOffset(FVector NewTargetOffset);
	void UpdateStanceOffset(float DeltaTime);

	FVector LaggedPivotLocation = FVector::ZeroVector;
	bool bHasLaggedPivotLocation = false;

	FVector InitialStanceOffset = FVector::ZeroVector;
	FVector TargetStanceOffset = FVector::ZeroVector;
	float StanceOffsetBlendPct = 1.0f;
	FVector CurrentStanceOffset = FVector::ZeroVector;
	
};
