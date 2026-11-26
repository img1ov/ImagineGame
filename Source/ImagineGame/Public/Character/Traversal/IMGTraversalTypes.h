#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "GameplayAbilitySpecHandle.h"
#include "Physics/IMGCollisionChannels.h"
#include "RootMotionModifier.h"
#include "IMGTraversalTypes.generated.h"

class AIMGCharacter;
class UAnimMontage;
class UPrimitiveComponent;
class URootMotionModifier_Warp;

/** Rule category for this request; does not control GA activation timing, attempt frequency, or input bindings. */
UENUM(BlueprintType)
enum class EIMGTraversalRequestSource : uint8
{
	/** Manual request: uses common detection and manual initiation conditions; standing initiation is allowed by default. */
	Manual,
	/** Request initiated by external automatic decisions: applies automatic-only limits; repeated attempts remain caller-driven. */
	Automatic,
	/** AI request: reuses manual initiation conditions and may use an active path target when input/acceleration is unavailable. */
	AI
};

UENUM(BlueprintType)
enum class EIMGTraversalAnchor : uint8
{
	FrontLedge,
	BackLedge,
	BackFloor
};

UENUM(BlueprintType)
enum class EIMGTraversalLandingMode : uint8
{
	None,
	DetectedFloor,
	DistanceCurve
};

UENUM(BlueprintType)
enum class EIMGTraversalAction : uint8
{
	None,
	/** Cross the obstacle with confirmed walkable rear support; landing rules remain montage-specific. */
	Hurdle,
	/** Cross the obstacle without confirmed rear support; leave the authored airborne exit intact. */
	Vault,
	/** Climb onto the obstacle top; rear-floor landing rules do not apply. */
	Mantle
};

UENUM(BlueprintType)
enum class EIMGTraversalFailure : uint8
{
	None,
	InvalidCharacter,
	InvalidMovement,
	NoSurface,
	InvalidSurface,
	NoFrontLedge,
	InsufficientWidth,
	UnsafeTop,
	BlockedPath,
	NoSupportedAction,
	MovingTarget,
	StaleCandidate,
	OutOfReach
};

/** Initiation policy is independent of geometric search and animation coverage. Distances are planar cm. */
USTRUCT(BlueprintType)
struct FIMGTraversalInitiationSettings
{
	GENERATED_BODY()

	/** Enables initiation limits only; geometry detection and external triggering remain independent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Manual and AI",
	    meta = (ClampMin = "0", EditCondition = "bEnabled", ForceUnits = "cm"))
	float ManualMaxDistance = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Manual and AI",
	    meta = (ClampMin = "-1", ClampMax = "1", EditCondition = "bEnabled"))
	float ManualMovementDirectionDot = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Manual and AI", meta = (EditCondition = "bEnabled"))
	bool bAllowStandingManual = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Manual and AI",
	    meta = (ClampMin = "0", EditCondition = "bEnabled && bAllowStandingManual", ForceUnits = "cm"))
	float StandingMaxDistance = 150.f;

	/** Fixed Automatic initiation limit (cm); character speed changes search length, not this acceptance limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Automatic",
	    meta = (ClampMin = "0", EditCondition = "bEnabled", ForceUnits = "cm"))
	float AutomaticMaxDistance = 150.f;

	/** Minimum movement-to-ledge dot; 0.65 allows about 49.5 degrees, including diagonal input. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Automatic",
	    meta = (ClampMin = "-1", ClampMax = "1", EditCondition = "bEnabled"))
	float AutomaticMovementDirectionDot = 0.65f;

	/** Falling requests still require movement intent and valid geometry/animation coverage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Automatic", meta = (EditCondition = "bEnabled"))
	bool bAllowAutomaticInAir = true;

	/** Allow for movement/ability messages arriving on different server ticks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Networking",
	    meta = (ClampMin = "0", EditCondition = "bEnabled", ForceUnits = "cm"))
	float ServerDistanceTolerance = 15.f;
};

/** Persistent geometry, safety and validation settings shared by prediction and authority. */
USTRUCT(BlueprintType)
struct FIMGTraversalDetectorSettings
{
	GENERATED_BODY()

	/** Absolute forward search length cap (cm), defaulting to the reference manual search's 250 cm; caps every layer's final length. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search", meta = (ClampMin = "1", ForceUnits = "cm"))
	float SearchDistance = 250.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search", meta = (ClampMin = "1", ForceUnits = "cm"))
	float SearchProbeRadius = 21.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search", meta = (ForceUnits = "cm"))
	float SearchStartHeightOffset = -75.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search", meta = (ClampMin = "1", ClampMax = "16"))
	int32 MaxSearchLayers = 6;

	/**
	 * Project extension: scales forward search lengths for all requests using CMC planar speed, preserving origins, sphere radii, and layer spacing.
	 * Scale = Lerp(MinSearchDistanceScale, 1, Clamp(Speed / SpeedForMaxSearchDistance, 0, 1)).
	 * Only probe length changes here; GA/Blueprint decides whether to detect while stationary. The detector does not check input or skip detection.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search|Speed Scaling")
	bool bScaleSearchDistanceBySpeed = true;

	/**
	 * Fraction of each layer's maximum length used while stationary. Default 0.3: a 250 cm layer shrinks to 75 cm, a 150 cm layer to 45 cm.
	 * This is a project-specific short-range search ratio, not a native speed-scaling constant from the reference algorithm.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search|Speed Scaling",
	    meta = (ClampMin = "0.01", ClampMax = "1", EditCondition = "bScaleSearchDistanceBySpeed"))
	float MinSearchDistanceScale = 0.3f;

	/** CMC planar speed (cm/s) at which each layer reaches full length; higher speeds add no length, and vertical falling speed is excluded. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search|Speed Scaling",
	    meta = (ClampMin = "1", EditCondition = "bScaleSearchDistanceBySpeed", ForceUnits = "cm/s"))
	float SpeedForMaxSearchDistance = 600.f;

	/** Bottom layers using AutomaticLowerSearchDistance; remaining layers use AutomaticUpperSearchDistance. Does not add layers; zero uses only the upper distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search|Automatic", meta = (ClampMin = "0", ClampMax = "16"))
	int32 AutomaticLowerLayerCount = 2;

	/** Automatic-only length cap for the bottom layers, before the common SearchDistance cap and speed scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search|Automatic",
	    meta = (ClampMin = "1", ForceUnits = "cm", EditCondition = "AutomaticLowerLayerCount > 0"))
	float AutomaticLowerSearchDistance = 250.f;

	/** Automatic-only length cap for all layers above the bottom group, before the common SearchDistance cap and speed scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search|Automatic", meta = (ClampMin = "1", ForceUnits = "cm"))
	float AutomaticUpperSearchDistance = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry", meta = (ForceUnits = "cm", ClampMin = "0"))
	float MinHeight = 32.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry", meta = (ForceUnits = "cm", ClampMin = "0"))
	float MaxHeight = 300.5f;

	/** Default height is relative to capsule feet; enabled uses valid CMC support, falling back to feet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry")
	bool bUseGroundPositionForHeight = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry", meta = (ForceUnits = "cm", ClampMin = "1"))
	float MinWidth = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry", meta = (ForceUnits = "cm", ClampMin = "1"))
	float MaxDepth = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry", meta = (ForceUnits = "cm", ClampMin = "0"))
	float MaxDrop = 180.f;

	/**
	 * Maximum planar distance (cm) from the returned front ledge to the actor origin; an independent animation coverage cap unaffected by probe speed scaling.
	 * May exceed a shorter SearchDistance; a probe hit is still required, then InitiationSettings and CHT determine whether initiation is allowed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry", meta = (ClampMin = "1", ForceUnits = "cm"))
	float MaxAnimationReachDistance = 250.f;

	/** Manual-facing threshold; 0.3 is the reference manual traversal value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Facing",
	    meta = (ClampMin = "-1", ClampMax = "1", DisplayName = "Manual / AI Min Facing Dot"))
	float MinFacingDot = 0.3f;

	/** Automatic-facing threshold; 0.65 allows about 49.5 degrees from the surface approach direction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Facing", meta = (ClampMin = "-1", ClampMax = "1"))
	float AutomaticMinFacingDot = 0.65f;

	/** Low obstacles may retain the requested heading instead of turning into the face normal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Facing", meta = (ClampMin = "0", ForceUnits = "cm"))
	float UseRequestDirectionBelowHeight = 80.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handhold", meta = (ClampMin = "1", ForceUnits = "cm"))
	float TopProbeRadius = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handhold", meta = (ClampMin = "0", ForceUnits = "cm"))
	float TopHeightTolerance = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handhold", meta = (ClampMin = "0", ClampMax = "1"))
	float HandholdMinNormalZ = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handhold", meta = (ForceUnits = "cm"))
	float HandholdPathVerticalOffset = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handhold", meta = (ClampMin = "0", ForceUnits = "cm"))
	float HandholdPathNormalOffset = 33.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handhold",
	    meta = (ClampMin = "0", ClampMax = "90", ForceUnits = "deg"))
	float LargeApproachAngle = 37.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handhold",
	    meta = (ClampMin = "0", ClampMax = "90", ForceUnits = "deg"))
	float MaxApproachAngle = 42.5f;

	/** Downward depth probes measure the first drop even when it is not a valid landing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Depth", meta = (ClampMin = "1", ForceUnits = "cm"))
	float DepthProbeLength = 500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Depth", meta = (ClampMin = "1", ForceUnits = "cm"))
	float DepthProbeRadius = 27.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Depth", meta = (ClampMin = "0", ForceUnits = "cm"))
	float DepthHeightTolerance = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Depth", meta = (ClampMin = "1", ClampMax = "16"))
	int32 MaxDepthSamples = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Depth", meta = (ClampMin = "0", ClampMax = "8"))
	int32 BackLedgeRefinementSteps = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Depth", meta = (ClampMin = "1", ClampMax = "16"))
	int32 AutomaticMinDepthSamples = 1;

	/** A blocker this close behind the ledge invalidates every supported traversal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clearance", meta = (ForceUnits = "cm", ClampMin = "0"))
	float MinHorizontalClearance = 35.f;

	/** Forward free space measured from the front ledge; mirrors the reference detector's clearance probe. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clearance", meta = (ForceUnits = "cm", ClampMin = "1"))
	float MaxHorizontalClearance = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clearance", meta = (ClampMin = "1", ForceUnits = "cm"))
	float HorizontalClearanceRadius = 22.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clearance", meta = (ClampMin = "0", ForceUnits = "cm"))
	float HorizontalClearanceVerticalOffset = 15.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clearance", meta = (ClampMin = "0", ForceUnits = "cm"))
	float HorizontalClearanceBackOffset = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clearance", meta = (ClampMin = "1", ForceUnits = "cm"))
	float VerticalClearanceRadius = 17.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clearance",
	    meta = (ClampMin = "0", ClampMax = "89", ForceUnits = "deg"))
	float MaxClearanceSlopeAngle = 42.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edges", meta = (ClampMin = "0", ForceUnits = "cm"))
	float EdgePlanarTolerance = 9.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edges", meta = (ClampMin = "0", ClampMax = "90", ForceUnits = "deg"))
	float EdgeHeadingToleranceDegrees = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring", meta = (ClampMin = "1", ForceUnits = "cm"))
	float ScoreDistanceScale = 500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring", meta = (ClampMin = "0"))
	float DistanceScoreWeight = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring", meta = (ClampMin = "0"))
	float HeadingScoreWeight = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring", meta = (ClampMin = "1"))
	float SecondaryHitPenalty = 1.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slopes", meta = (ClampMin = "0", ClampMax = "1"))
	float WalkableSlopeMinNormalZ = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slopes", meta = (ClampMin = "0", ForceUnits = "cm"))
	float WalkableSlopeDistanceTolerance = 13.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stairs")
	FName StairSurfaceTag = TEXT("Traversal.Stairs");

	/** Reject spans strictly below this cutoff when standing on stairs, matching the reference comparison. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stairs", meta = (ClampMin = "0", ForceUnits = "cm"))
	float StairMinEdgeSpan = 22.5f;

	/** Replaces StairMinEdgeSpan for tagged faces/tops; equality is accepted by the span filter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stairs", meta = (ClampMin = "0", ForceUnits = "cm"))
	float TaggedStairMinEdgeSpan = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stairs", meta = (ClampMin = "0", ForceUnits = "cm"))
	float StairAmbiguousEdgeSpan = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stairs", meta = (ClampMin = "0", ForceUnits = "cm"))
	float StairMaxPlanarDrift = 10.f;

	/** Climbing exclusions on the component or owning actor; do not restrict rear-floor support. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Policy")
	TArray<FName> BlockedSurfaceTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Policy")
	TArray<FName> AutomaticBlockedSurfaceTags;

	/** Vehicle-like angular surfaces retain a closely aligned request heading (dot > 0.95) and require both side supports. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Policy")
	FName AngularSurfaceTag = TEXT("Traversal.Angular");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Policy")
	bool bRejectAutomaticHighOpenApproach = true;

	/** Drop below the approach ground for automatic crossing of shallow tops with open space behind. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Policy", meta = (ClampMin = "0", ForceUnits = "cm"))
	float AutomaticMaxGroundDrop = 150.f;

	/** Trace channel used for traversal surfaces and traversal-specific clearance probes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	TEnumAsByte<ECollisionChannel> TraceChannel = IMG_TraceChannel_Traversal;

	/** Rear-floor support queries, independent of climbable surfaces. Capsule clearance still uses movement collision. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	TEnumAsByte<ECollisionChannel> FloorTraceChannel = ECC_Visibility;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	bool bAllowSimulatingPhysics = false;

	/** Obstacle/carrier linear speed limit (cm/s), not the traversing character's speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation", meta = (ForceUnits = "cm/s", ClampMin = "0"))
	float MaxMovingSpeed = 500.f;

	/** Obstacle/carrier angular speed limit (degrees/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation", meta = (ForceUnits = "deg/s", ClampMin = "0"))
	float MaxAngularSpeedDegrees = 85.f;

	/** Bounds delayed Blueprint/network use of a geometric snapshot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation", meta = (ForceUnits = "s", ClampMin = "0.001"))
	float MaxCandidateAge = 0.25f;

	/** Used only by the advisory action recommendation, never to reject a CHT selection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recommendation",
	    meta = (ForceUnits = "cm/s", ClampMin = "0", DisplayName = "Recommended Running Speed Threshold"))
	float RunningSpeedThreshold = 350.f;

	/** Advisory hurdle height for RecommendedAction; does not restrict chooser animation coverage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recommendation",
	    meta = (ForceUnits = "cm", ClampMin = "0", DisplayName = "Recommended Max Hurdle Height"))
	float MaxHurdleHeight = 110.f;

	/** Advisory vault height for RecommendedAction; independent of the hurdle recommendation limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recommendation",
	    meta = (ForceUnits = "cm", ClampMin = "0", DisplayName = "Recommended Max Vault Height"))
	float MaxVaultHeight = 300.5f;
};

/** A single, bounded synchronous geometry request. The detector owns no live state. */
USTRUCT(BlueprintType)
struct FIMGTraversalQuery
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<AIMGCharacter> Character = nullptr;

	UPROPERTY(BlueprintReadWrite)
	FVector Direction = FVector::ForwardVector;

	UPROPERTY(BlueprintReadWrite)
	EIMGTraversalRequestSource Source = EIMGTraversalRequestSource::Manual;

	UPROPERTY(BlueprintReadWrite)
	FIMGTraversalDetectorSettings Settings;
};

/** Geometry and feasible actions; animation assets never enter the detector. */
USTRUCT(BlueprintType)
struct FIMGTraversalCandidate
{
	GENERATED_BODY()

	/** Advisory only; animation coverage belongs to CHT. */
	UPROPERTY(BlueprintReadOnly)
	EIMGTraversalAction RecommendedAction = EIMGTraversalAction::None;

	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UPrimitiveComponent> HitComponent = nullptr;

	/** Top support may differ from the component that supplied the front-face hit. */
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UPrimitiveComponent> FrontLedgeComponent = nullptr;

	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UPrimitiveComponent> BackLedgeComponent = nullptr;

	/** Every component that contributed obstacle geometry and must be tracked/ignored during execution. */
	UPROPERTY(BlueprintReadOnly)
	TArray<TObjectPtr<UPrimitiveComponent>> ObstacleComponents;

	/** Floor carriers added by placement, including any floor-only collision ignore. */
	UPROPERTY()
	TArray<TObjectPtr<UPrimitiveComponent>> FloorOnlyComponents;

	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UPrimitiveComponent> BackFloorComponent = nullptr;

	UPROPERTY(BlueprintReadOnly)
	FTransform FrontLedge = FTransform::Identity;

	UPROPERTY(BlueprintReadOnly)
	FTransform BackLedge = FTransform::Identity;

	UPROPERTY(BlueprintReadOnly)
	FTransform BackFloor = FTransform::Identity;
	/** Min/max world Z of the sampled obstacle top, up to its first depth discontinuity. */
	UPROPERTY()
	FVector2D TopSupportHeightRange = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	FTransform LocalFrontLedge = FTransform::Identity;

	UPROPERTY(BlueprintReadOnly)
	FTransform LocalBackLedge = FTransform::Identity;

	UPROPERTY(BlueprintReadOnly)
	FTransform LocalBackFloor = FTransform::Identity;

	UPROPERTY(BlueprintReadOnly)
	FVector FrontLedgeNormal = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	FVector BackLedgeNormal = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	FVector ApproachDirection = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	float ObstacleHeight = 0.f;

	/** Rear-edge depth when bHasBackLedge; otherwise only the sampled top extent, possibly zero. */
	UPROPERTY(BlueprintReadOnly)
	float ObstacleDepth = 0.f;

	/** XY surface distance from the front ledge, capped at the fully swept horizontal range. */
	UPROPERTY(BlueprintReadOnly)
	float HorizontalClearance = 0.f;

	/** Surface height above the ledge; a miss returns the fully swept configured cap. */
	UPROPERTY(BlueprintReadOnly)
	float VerticalClearance = 0.f;

	/** Front ledge to sampled/selected floor; without bHasBackFloor this is a probe result, not a landing. */
	UPROPERTY(BlueprintReadOnly)
	float DropHeight = 0.f;

	/** Rear ledge height above the sampled or animation-selected landing floor. */
	UPROPERTY(BlueprintReadOnly)
	float BackLedgeHeight = 0.f;

	/** Mesh component origin to front ledge in world-space 3D, retained for animation selection. */
	UPROPERTY(BlueprintReadOnly)
	float DistanceToLedge = 0.f;

	/** Actor origin to front ledge in XY; initiation policy does not use the mesh-space 3D distance. */
	UPROPERTY(BlueprintReadOnly)
	float PlanarDistanceToLedge = 0.f;

	/** Vertical extent of the grouped broad-phase face hits that produced this candidate. */
	UPROPERTY(BlueprintReadOnly)
	float FrontEdgeVerticalSpan = 0.f;

	UPROPERTY(BlueprintReadOnly)
	float FrontEdgePlanarDrift = 0.f;

	/** Nearest broad-phase obstruction across the middle search layers. */
	UPROPERTY(BlueprintReadOnly)
	float MidHeightApproachClearance = 0.f;

	/** Geometry fact only; the caller decides whether an open high approach should suppress traversal. */
	UPROPERTY(BlueprintReadOnly)
	bool bHasHighOpenApproach = false;

	UPROPERTY(BlueprintReadOnly)
	double DetectionTime = 0.0;

	UPROPERTY(BlueprintReadOnly)
	float Speed = 0.f;

	UPROPERTY(BlueprintReadOnly)
	EIMGTraversalRequestSource RequestSource = EIMGTraversalRequestSource::Manual;

	UPROPERTY(BlueprintReadOnly)
	float ClimbAngle = 0.f;

	UPROPERTY(BlueprintReadOnly)
	float GroundSlopeAngle = 0.f;

	UPROPERTY(BlueprintReadOnly)
	FVector GroundNormal = FVector::UpVector;

	/** Current CMC support point, or capsule feet when no valid support exists (including in air). */
	UPROPERTY(BlueprintReadOnly)
	FVector GroundLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	TEnumAsByte<EMovementMode> MovementMode = MOVE_None;

	UPROPERTY(BlueprintReadOnly)
	bool bHasFrontLedge = false;

	UPROPERTY(BlueprintReadOnly)
	bool bHasBackLedge = false;

	/** A usable walkable surface was sampled after the back ledge; final landing clearance is animation-dependent. */
	UPROPERTY(BlueprintReadOnly)
	bool bHasBackFloor = false;

	/** Front contact is available; the chosen montage resolves the climb onto the obstacle top. */
	UPROPERTY(BlueprintReadOnly)
	bool bCanMantle = false;

	/** Front and rear ledges exist, but no usable rear floor was confirmed. */
	UPROPERTY(BlueprintReadOnly)
	bool bCanVault = false;

	/** Front and rear ledges plus usable rear support exist. */
	UPROPERTY(BlueprintReadOnly)
	bool bCanHurdle = false;

	bool Supports(EIMGTraversalAction Action) const
	{
		return (Action == EIMGTraversalAction::Mantle && bCanMantle) ||
		    (Action == EIMGTraversalAction::Vault && bCanVault) ||
		    (Action == EIMGTraversalAction::Hurdle && bCanHurdle);
	}
};

/** One geometric target may be consumed by any number of animation windows. */
USTRUCT(BlueprintType)
struct FIMGTraversalWarpBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName TargetName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EIMGTraversalAnchor Anchor = EIMGTraversalAnchor::FrontLedge;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FTransform Offset = FTransform::Identity;
};

USTRUCT(BlueprintType)
struct FIMGTraversalLandingSettings
{
	GENERATED_BODY()

	/** Hurdle rear-floor placement only. None retains the authored trajectory and any bound floor contact. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EIMGTraversalLandingMode Mode = EIMGTraversalLandingMode::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landing Target",
	    meta = (EditCondition = "Mode != EIMGTraversalLandingMode::None"))
	FName TargetName = TEXT("BackFloor");

	/** Landing evaluation ends at this translation window, even when it is the last; excludes the montage tail. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landing Target",
	    meta = (ClampMin = "-1", EditCondition = "Mode != EIMGTraversalLandingMode::None"))
	int32 LandingWindowIndex = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distance Curve",
	    meta = (EditCondition = "Mode == EIMGTraversalLandingMode::DistanceCurve"))
	FName AnchorTargetName = TEXT("BackLedge");

	/** Explicit alternate contact for assets without a rear-contact translation window. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distance Curve",
	    meta = (EditCondition = "Mode == EIMGTraversalLandingMode::DistanceCurve"))
	FName FallbackAnchorTargetName = TEXT("FrontLedge");

	/** Translation-window ordinal in montage order; -1 selects the last eligible window. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distance Curve",
	    meta = (ClampMin = "-1", EditCondition = "Mode == EIMGTraversalLandingMode::DistanceCurve"))
	int32 AnchorWindowIndex = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distance Curve",
	    meta = (EditCondition = "Mode == EIMGTraversalLandingMode::DistanceCurve"))
	FName DistanceCurveName = TEXT("Distance_From_Ledge");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distance Curve",
	    meta = (EditCondition = "Mode == EIMGTraversalLandingMode::DistanceCurve"))
	bool bAbsoluteCurveDistance = true;

	/** Conservative straight-path rejection is opt-in; it does not represent a warped animation arc. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation",
	    meta = (EditCondition = "Mode != EIMGTraversalLandingMode::None"))
	bool bCheckStraightPath = false;

	/** Vertical margin for landing validation queries; not an additional lift applied to the Warp Target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation",
	    meta = (ClampMin = "0", EditCondition = "Mode != EIMGTraversalLandingMode::None", ForceUnits = "cm"))
	float CapsuleClearance = 3.f;
};

/**
 * Execution rules after animation selection: define warp geometry bindings, required targets, landing calculation, and valid parameter ranges.
 * Does not select a Montage or modify StartTime/PlayRate returned by SelectTraversalAnimation; execution is rejected if the rules are not satisfied.
 * Warp window times and Modifier parameters remain configured in animation Notifies; prediction and the server validate using the same rules.
 */
USTRUCT(BlueprintType)
struct IMAGINEGAME_API FIMGTraversalExecutionSettings
{
	GENERATED_BODY()

	FIMGTraversalExecutionSettings();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp Targets", meta = (TitleProperty = "TargetName"))
	TArray<FIMGTraversalWarpBinding> WarpBindings;

	/** Required contacts must retain a window after the selected start time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp Targets")
	TArray<FName> RequiredTargets;

	/** Explicit rear-floor landing for Hurdle. Mantle and Vault require Mode=None. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FIMGTraversalLandingSettings Landing;

	/** Bounds local capsule recovery at the authored Mantle ground handoff; not an extra Warp lift. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground Handoff", meta = (ClampMin = "0", ForceUnits = "cm"))
	float MaxGroundAdjustment = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clearance", meta = (ClampMin = "0", ForceUnits = "cm"))
	float MinVerticalClearance = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clearance", meta = (ClampMin = "0", ForceUnits = "cm"))
	float MinHorizontalClearance = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playback", meta = (ClampMin = "0.01"))
	float MinPlayRate = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playback", meta = (ClampMin = "0.01"))
	float MaxPlayRate = 3.f;
};

/** Default execution settings shared by all montages for an Action; each Action should have only one entry. */
USTRUCT(BlueprintType)
struct FIMGTraversalActionSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EIMGTraversalAction Action = EIMGTraversalAction::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FIMGTraversalExecutionSettings Settings;
};

/**
 * Execution settings specific to the selected Action/Montage; used only when the animation differs from the action's common rules.
 * A match uses Settings in full; only unmatched pairs fall back to ActionSettings. Does not reselect animation or merge fields.
 */
USTRUCT(BlueprintType)
struct FIMGTraversalMontageSettings
{
	GENERATED_BODY()

	/** Matches the montage already returned by SelectTraversalAnimation; not a fallback animation or a new selection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<UAnimMontage> Montage = nullptr;

	/** Forms the matching key with Montage; does not override the selected Action. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EIMGTraversalAction Action = EIMGTraversalAction::None;

	/**
	 * Replaces the entire configuration, including WarpBindings, RequiredTargets, Landing, clearance, and the PlayRate range.
	 * Copy the corresponding ActionSettings.Settings before editing special cases; a new struct's defaults do not match the action's configuration.
	 * For example, Hurdle enables DistanceCurve by default, whereas a new struct defaults Landing.Mode to None.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FIMGTraversalExecutionSettings Settings;
};

/** Resolved authority targets are shared with the owning prediction and simulated proxies. */
USTRUCT()
struct FIMGTraversalWarpTarget
{
	GENERATED_BODY()

	UPROPERTY()
	FName Name = NAME_None;

	UPROPERTY()
	TObjectPtr<UPrimitiveComponent> Component = nullptr;

	UPROPERTY()
	FTransform LocalTransform = FTransform::Identity;
};

struct FIMGTraversalWarpWindow
{
	/** Null preserves a non-Warp modifier's range so placement cannot silently omit its effect. */
	const URootMotionModifier_Warp* Modifier = nullptr;
	float StartTime = 0.f;
	float EndTime = 0.f;
};

/** Chooser and PoseMatch decide this in a Blueprint child of the native ability. */
USTRUCT(BlueprintType)
struct FIMGTraversalAnimationSelection
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EIMGTraversalAction Action = EIMGTraversalAction::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<UAnimMontage> Montage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float StartTime = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float PlayRate = 1.f;
};

/** Server allow-list entry for a Blueprint-selected action/montage pair. */
USTRUCT(BlueprintType)
struct FIMGTraversalAllowedMontage
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EIMGTraversalAction Action = EIMGTraversalAction::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UAnimMontage> Montage = nullptr;
};

UENUM()
enum class EIMGTraversalMovementPhase : uint8
{
	Warping,
	GroundExit,
	Falling
};

/** Server-owned targets and proxy presentation state; GAS owns ability and montage lifetime. */
USTRUCT()
struct FIMGTraversalRepState
{
	GENERATED_BODY()

	UPROPERTY()
	uint16 Sequence = 0;

	UPROPERTY()
	FGameplayAbilitySpecHandle AbilitySpec;

	UPROPERTY()
	int16 ActivationPredictionKey = 0;

	UPROPERTY()
	EIMGTraversalAction Action = EIMGTraversalAction::None;

	UPROPERTY()
	TObjectPtr<UAnimMontage> Montage = nullptr;

	UPROPERTY()
	uint8 MontagePlayInstanceId = 0;

	UPROPERTY()
	float MontageStartTime = 0.f;

	UPROPERTY()
	TArray<TObjectPtr<UPrimitiveComponent>> ObstacleComponents;

	/** Keeps floor-only validation consistent when authority targets replace predicted targets. */
	UPROPERTY()
	TArray<TObjectPtr<UPrimitiveComponent>> FloorOnlyComponents;

	UPROPERTY()
	TArray<FIMGTraversalWarpTarget> WarpTargets;

	/** Authority-owned history; a proxy must not capture late native target replication as previous state. */
	UPROPERTY()
	TArray<FMotionWarpingTarget> PreviousWarpTargets;

	UPROPERTY()
	bool bActive = false;

	UPROPERTY()
	EIMGTraversalMovementPhase MovementPhase = EIMGTraversalMovementPhase::Warping;

	/** Monotonic authored playback position; negative disables ground handoff for airborne actions. */
	UPROPERTY()
	float GroundHandoffTime = -1.f;

	UPROPERTY()
	float MaxGroundAdjustment = 10.f;
};
