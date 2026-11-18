#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "IMGCameraCollision.generated.h"

class AActor;
class UPrimitiveComponent;
class UWorld;

/** Distances are centimeters, angles are degrees, and spring constants are unit-mass k. */
USTRUCT(BlueprintType)
struct IMAGINEGAME_API FIMGCameraCollisionSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Camera;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query")
	bool bTraceComplex = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query")
	bool bIgnorePawnsForOcclusion = true;

	/** Skips occlusion only; the camera body still cannot stop inside this geometry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query")
	FName IgnoreOcclusionTag = TEXT("IgnoreCameraOcclusion");

	/** Explicitly permits clipping. Recognized on either the actor or the hit component. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query")
	FName IgnoreCollisionTag = TEXT("IgnoreCameraCollision");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query", meta = (ClampMin = "1", ClampMax = "32"))
	int32 MaxIgnoredHitRetries = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probe", meta = (ClampMin = "1", Units = "cm"))
	float ProbeRadius = 24.f;

	/** Limits the probe to fit inside the followed capsule. No world collision is skipped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probe", meta = (ClampMin = "0", Units = "cm"))
	float TargetRadiusMargin = 7.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probe", meta = (ClampMin = "0.01", ClampMax = "100", Units = "cm"))
	float Clearance = 2.f;

	/** Slightly reduce consecutive test radii to avoid contact-tolerance oscillation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probe", meta = (ClampMin = "0.01", ClampMax = "1", Units = "cm"))
	float ConsecutiveTestRadiusReduction = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probe", meta = (ClampMin = "0"))
	float RadiusRecoverySpring = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probe", meta = (ClampMin = "0", ClampMax = "10"))
	float RadiusRecoveryDamping = 0.7f;

	/** Zero uses the unlagged fallback; one tries to reach the rendered, lagged pivot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Root", meta = (ClampMin = "0", ClampMax = "1"))
	float RootToPivotBlend = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Root", meta = (ClampMin = "0"))
	float RootRecoverySpring = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Root", meta = (ClampMin = "0", ClampMax = "10"))
	float RootRecoveryDamping = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Root", meta = (ClampMin = "1", ClampMax = "16"))
	int32 PenetrationRecoveryIterations = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Root", meta = (ClampMin = "1", ClampMax = "64"))
	int32 MaxPenetrationComponentTests = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Root", meta = (ClampMin = "0", Units = "cm"))
	float MaxPenetrationRecoveryDistance = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recovery", meta = (ClampMin = "0"))
	float DistanceRecoverySpring = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recovery", meta = (ClampMin = "0", ClampMax = "10"))
	float DistanceRecoveryDamping = 0.8f;

	/** Smooth broad-probe occlusion within the hard camera-body limit. Zero uses immediate occlusion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Occlusion", meta = (ClampMin = "0", Units = "s"))
	float OcclusionPushInBlendTime = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recovery", meta = (ClampMin = "0", Units = "cm"))
	float DistanceRecoveryTolerance = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recovery")
	bool bPauseRecoveryWhenStationary = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recovery", meta = (ClampMin = "0", Units = "cm/s"))
	float StationaryRootSpeed = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recovery", meta = (ClampMin = "0", Units = "cm/s"))
	float StationaryCameraSpeed = 10.f;

	/** Searches in the camera's direction of travel without changing the player's view orientation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Occlusion Search")
	bool bSweepToAvoidPopIn = false;

	/** Optional automatic orbit adjustment. Suppressed during player look input and mode blending. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Occlusion Search")
	bool bMoveTowardsClearLineOfSight = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Occlusion Search")
	bool bPersistPushInBehaviour = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Occlusion Search", meta = (ClampMin = "2", ClampMax = "16"))
	int32 SweepTests = 6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Occlusion Search", meta = (ClampMin = "2", ClampMax = "16"))
	int32 PathTests = 6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Occlusion Search", meta = (ClampMin = "0", ClampMax = "90", Units = "deg"))
	float MaxReorientationSweepAngle = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Occlusion Search", meta = (ClampMin = "0", ClampMax = "120", Units = "deg"))
	float MaxTravelSweepAngle = 90.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Occlusion Search", meta = (ClampMin = "0", Units = "cm/s"))
	float MinCameraSpeedForTravelSweep = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Occlusion Search", meta = (ClampMin = "0", Units = "deg/s"))
	float MinReorientationSpeedToMaintainDirection = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Occlusion Search", meta = (ClampMin = "0"))
	float ReorientationSpring = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Occlusion Search", meta = (ClampMin = "0", ClampMax = "10"))
	float ReorientationDamping = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Occlusion Search", meta = (ClampMin = "0", Units = "deg"))
	float LookInputAngleThreshold = 0.05f;

	/** The broad probe preserves framing; this smaller sphere protects the final camera body. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clipping", meta = (ClampMin = "0.5", Units = "cm"))
	float CameraBodyRadius = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clipping", meta = (ClampMin = "0", Units = "cm"))
	float ClippingDetectionLength = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clipping", meta = (ClampMin = "1", ClampMax = "16"))
	int32 ClippingSearchSteps = 4;

	/** Radius compensation for immediate occlusion; smooth occlusion uses body clearance directly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clipping", meta = (EditCondition = "OcclusionPushInBlendTime <= 0"))
	bool bPullBackByProbeRadius = true;

	/** Fraction of inward travel available to grow compensation. One can cancel the entire inward step. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clipping", meta = (ClampMin = "0", ClampMax = "1"))
	float PullBackGrowthFraction = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clipping", meta = (ClampMin = "0"))
	float PullBackReleaseSpring = 15.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clipping", meta = (ClampMin = "0", ClampMax = "10"))
	float PullBackDamping = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Push Beyond", meta = (ClampMin = "1", ClampMax = "16"))
	int32 MaxPushBeyondActors = 4;

	/** Total component sweeps across all requested actors per solve. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Push Beyond", meta = (ClampMin = "1", ClampMax = "128"))
	int32 MaxPushBeyondComponentTests = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Push Beyond", meta = (ClampMin = "0", Units = "cm"))
	float MaxPushBeyondDistance = 1000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Push Beyond", meta = (ClampMin = "1", ClampMax = "2"))
	float PushBeyondDistanceScale = 1.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Push Beyond", meta = (ClampMin = "0", Units = "cm"))
	float PushBeyondClearance = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Push Beyond", meta = (ClampMin = "0"))
	float PushBeyondReleaseSpring = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Push Beyond", meta = (ClampMin = "0", ClampMax = "10"))
	float PushBeyondDamping = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Near Clip")
	bool bScanNearClip = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Near Clip", meta = (ClampMin = "0.1", Units = "cm"))
	float MinNearClip = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Near Clip", meta = (ClampMin = "0.1", Units = "cm"))
	float MaxNearClip = 10.f;

	/** Number of conservative box slices covering the near frustum. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Near Clip", meta = (ClampMin = "1", ClampMax = "16"))
	int32 NearClipSlices = 4;

	FIMGCameraCollisionSettings Sanitized() const;
};

enum class EIMGCameraCollisionStage : uint8
{
	Origin, Root, Occlusion, Search, CameraLimit, Clipping, PushBeyond, PullBack, Final, NearClip
};

enum class EIMGCameraCollisionDebugShape : uint8 { Sphere, Capsule, Box };

/** Bounded, opt-in query evidence. The output owner handles rendering. */
struct IMAGINEGAME_API FIMGCameraCollisionDebugQuery
{
	FVector Start = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
	FVector Extent = FVector::ZeroVector;
	FVector HitLocation = FVector::ZeroVector;
	FVector HitNormal = FVector::ZeroVector;
	FQuat Rotation = FQuat::Identity;
	float Radius = 0.f;
	float HalfHeight = 0.f;
	EIMGCameraCollisionStage Stage = EIMGCameraCollisionStage::Origin;
	EIMGCameraCollisionDebugShape Shape = EIMGCameraCollisionDebugShape::Sphere;
	bool bBlocked = false;
	bool bIgnored = false;
	bool bSweep = false;
};

/** Per-update world facts. No character, movement, ability or animation dependency. */
struct IMAGINEGAME_API FIMGCameraCollisionInput
{
	FVector Origin = FVector::ZeroVector;
	FVector Fallback = FVector::ZeroVector;
	FVector Pivot = FVector::ZeroVector;
	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	float MaxProbeRadius = MAX_flt;
	float MinPitch = -89.f;
	float MaxPitch = 89.f;
	bool bForcePushIn = false;
	bool bCollectDebug = false;
	int32 MaxDebugQueries = 128;
	TArray<TWeakObjectPtr<const AActor>, TInlineAllocator<4>> IgnoredActors;
	TArray<TWeakObjectPtr<const UPrimitiveComponent>, TInlineAllocator<4>> IgnoredOcclusionComponents;
	TArray<TWeakObjectPtr<const AActor>, TInlineAllocator<4>> PushBeyondActors;
};

/** A mode describes a request; the final output chain owns and advances the solver once. */
struct IMAGINEGAME_API FIMGCameraCollisionRequest
{
	FIMGCameraCollisionSettings Settings;
	FIMGCameraCollisionInput Input;
	TWeakObjectPtr<const AActor> Target;
	TWeakObjectPtr<const AActor> Anchor;
	TWeakObjectPtr<const AActor> Controller;
	TArray<TWeakObjectPtr<AActor>, TInlineAllocator<3>> AssistActors;
	float ReportPenetrationPercent = 0.f;
};

struct IMAGINEGAME_API FIMGCameraCollisionResult
{
	FVector Origin = FVector::ZeroVector;
	FVector Root = FVector::ZeroVector;
	FVector DesiredLocation = FVector::ZeroVector;
	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	float ProbeRadius = 0.f;
	float DistanceRatio = 1.f;
	float NearClipPlane = -1.f;
	float DesiredDistance = 0.f;
	float OcclusionDistance = 0.f;
	float HardLimitDistance = 0.f;
	float DampedDistance = 0.f;
	float ClippingDistance = 0.f;
	float PushBeyondDistance = 0.f;
	float PullBackRadius = 0.f;
	float FinalDistance = 0.f;
	int32 QueryCount = 0;
	int32 IgnoredHitCount = 0;
	bool bHasSafeRoot = false;
	bool bHasSafeLocation = false;
	bool bRecoveredOrigin = false;
	bool bInitialPenetration = false;
	bool bClipping = false;
	bool bPausedRecovery = false;
	bool bPushBeyondBudgetExceeded = false;
	bool bDebugTruncated = false;
	bool bOccluded = false;
	bool bRecovering = false;
	TArray<FIMGCameraCollisionDebugQuery> DebugQueries;
	TArray<TWeakObjectPtr<const UPrimitiveComponent>, TInlineAllocator<8>> HitComponents;
};

/** Numeric history belongs to one rendered camera, including while its modes are blending. */
struct IMAGINEGAME_API FIMGCameraCollisionState
{
	void Reset() { *this = FIMGCameraCollisionState(); }

	FVector PreviousRoot = FVector::ZeroVector;
	FVector PreviousDesiredLocation = FVector::ZeroVector;
	FVector PreviousSafeLocation = FVector::ZeroVector;
	FRotator AutomaticOrbitOffset = FRotator::ZeroRotator;
	FVector2D ReorientationVelocity = FVector2D::ZeroVector;
	double Radius = 0.0;
	double RadiusVelocity = 0.0;
	double FallbackFraction = 1.0;
	double FallbackVelocity = 0.0;
	double RootFraction = 1.0;
	double RootVelocity = 0.0;
	double OrbitDistance = 0.0;
	double OrbitVelocity = 0.0;
	double PreviousDampedOrbitDistance = 0.0;
	double PreviousDistanceRatio = 1.0;
	double PullBackRadius = 0.0;
	double PullBackVelocity = 0.0;
	double PushBeyondDistance = 0.0;
	double PushBeyondVelocity = 0.0;
	bool bInitialized = false;
	bool bHasSafeLocation = false;
	bool bPauseRecovery = false;
	bool bWasPushedIn = false;
	bool bWasPushedBeyond = false;
};

namespace IMGCameraCollision
{
	IMAGINEGAME_API FIMGCameraCollisionResult Solve(const UWorld& World,
		const FIMGCameraCollisionInput& Input, const FIMGCameraCollisionSettings& Settings,
		float DeltaTime, FIMGCameraCollisionState& State);

	/** Operates on the final projection; returns a per-view near plane and never changes camera position. */
	IMAGINEGAME_API float ScanNearClip(const UWorld& World, const FIMGCameraCollisionInput& Input,
		const FIMGCameraCollisionSettings& Settings, const FVector2D& HalfFovTangents,
		FIMGCameraCollisionResult& Diagnostics);
}
