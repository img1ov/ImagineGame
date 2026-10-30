#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
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
	Hurdle,
	Vault,
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"))
	float ManualMaxDistance = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bAllowStandingManual = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"))
	float StandingMaxDistance = 150.f;

	/** Fixed Automatic initiation limit (cm); character speed changes search length, not this acceptance limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"))
	float AutomaticMaxDistance = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "-1", ClampMax = "1"))
	float ManualMovementDirectionDot = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "-1", ClampMax = "1"))
	float AutomaticMovementDirectionDot = 0.8f;

	/** Falling requests still require movement intent and valid geometry/animation coverage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bAllowAutomaticInAir = true;

	/** Allow for movement/ability messages arriving on different server ticks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"))
	float ServerDistanceTolerance = 15.f;
};

/** Persistent geometry, safety and validation settings shared by prediction and authority. */
USTRUCT(BlueprintType)
struct FIMGTraversalDetectorSettings
{
	GENERATED_BODY()

	/** Trace channel used for traversal surfaces and traversal-specific clearance probes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	TEnumAsByte<ECollisionChannel> TraceChannel = IMG_TraceChannel_Traversal;

	/** Used only by the advisory action recommendation, never to reject a CHT selection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float RunningSpeedThreshold = 350.f;

	/** Absolute forward search length cap (cm), defaulting to the reference manual search's 250 cm; caps every layer's final length. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search", meta = (ClampMin = "1", ForceUnits = "cm"))
	float SearchDistance = 250.f;

	/**
	 * Project extension: scales forward search lengths for all requests using CMC planar speed, preserving origins, sphere radii, and layer spacing.
	 * Scale = Lerp(MinSearchDistanceScale, 1, Clamp(Speed / SpeedForMaxSearchDistance, 0, 1)).
	 * Only probe length changes here; GA/Blueprint decides whether to detect while stationary. The detector does not check input or skip detection.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search")
	bool bScaleSearchDistanceBySpeed = true;

	/**
	 * Fraction of each layer's maximum length used while stationary. Default 0.3: a 250 cm layer shrinks to 75 cm, a 150 cm layer to 45 cm.
	 * This is a project-specific short-range search ratio, not a native speed-scaling constant from the reference algorithm.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search",
	    meta = (ClampMin = "0.01", ClampMax = "1", EditCondition = "bScaleSearchDistanceBySpeed"))
	float MinSearchDistanceScale = 0.3f;

	/** CMC planar speed (cm/s) at which each layer reaches full length; higher speeds add no length, and vertical falling speed is excluded. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search",
	    meta = (ClampMin = "1", EditCondition = "bScaleSearchDistanceBySpeed"))
	float SpeedForMaxSearchDistance = 600.f;

	/**
	 * Maximum lengths (cm) for Automatic request layers from bottom to top, following the reference layered search: 250, 250, 150, 150, 150, 150.
	 * Each entry is capped by SearchDistance, then multiplied by the speed scale; Manual/AI use the uniform SearchDistance cap.
	 * An empty array uses the uniform cap for all layers; extra layers use the last entry. Input, speed thresholds, and initiation distance are configured elsewhere.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search", meta = (ClampMin = "1", ForceUnits = "cm"))
	TArray<float> AutomaticSearchLayerDistances = {250.f, 250.f, 150.f, 150.f, 150.f, 150.f};

	/**
	 * Maximum planar distance (cm) from the returned front ledge to the actor origin; an independent animation coverage cap unaffected by probe speed scaling.
	 * May exceed a shorter SearchDistance; a probe hit is still required, then InitiationSettings and CHT determine whether initiation is allowed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search", meta = (ClampMin = "1", ForceUnits = "cm"))
	float MaxAnimationReachDistance = 250.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MinHeight = 32.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MaxHeight = 300.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MaxDepth = 300.f;

	/** Forward free space measured from the front ledge; mirrors the reference detector's clearance probe. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MaxHorizontalClearance = 300.f;

	/** A blocker this close behind the ledge invalidates every supported traversal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MinHorizontalClearance = 35.f;

	/** Downward depth probes measure the first drop even when it is not a valid landing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float DepthProbeLength = 500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MaxDrop = 180.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MinWidth = 60.f;

	/** Manual-facing threshold; 0.3 is the reference manual traversal value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MinFacingDot = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "-1", ClampMax = "1"))
	float AutomaticMinFacingDot = 0.8f;

	/** Default height is relative to capsule feet; enabled uses valid CMC support, falling back to feet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height")
	bool bUseGroundPositionForHeight = false;

	/** Obstacle/carrier linear speed limit (cm/s), not the traversing character's speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MaxMovingSpeed = 500.f;

	/** Obstacle/carrier angular speed limit (degrees/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MaxAngularSpeedDegrees = 85.f;

	/** Bounds delayed Blueprint/network use of a geometric snapshot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MaxCandidateAge = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MaxHurdleHeight = 110.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MaxVaultHeight = 300.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bAllowSimulatingPhysics = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probes", meta = (ClampMin = "1"))
	float SearchProbeRadius = 21.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probes")
	float SearchStartHeightOffset = -75.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probes", meta = (ClampMin = "1", ClampMax = "16"))
	int32 MaxSearchLayers = 6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probes", meta = (ClampMin = "1"))
	float TopProbeRadius = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probes", meta = (ClampMin = "0"))
	float TopHeightTolerance = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probes", meta = (ClampMin = "0", ClampMax = "1"))
	float HandholdMinNormalZ = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probes", meta = (ClampMin = "1"))
	float DepthProbeRadius = 27.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probes", meta = (ClampMin = "0"))
	float DepthHeightTolerance = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probes", meta = (ClampMin = "1", ClampMax = "16"))
	int32 MaxDepthSamples = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probes", meta = (ClampMin = "0", ClampMax = "8"))
	int32 BackLedgeRefinementSteps = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probes", meta = (ClampMin = "1"))
	float HorizontalClearanceRadius = 22.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probes", meta = (ClampMin = "0"))
	float HorizontalClearanceVerticalOffset = 15.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probes", meta = (ClampMin = "0"))
	float HorizontalClearanceBackOffset = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probes", meta = (ClampMin = "1"))
	float VerticalClearanceRadius = 17.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probes")
	float HandholdPathVerticalOffset = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probes", meta = (ClampMin = "0"))
	float HandholdPathNormalOffset = 33.5f;

	/** Low obstacles may retain the requested heading instead of turning into the face normal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probes", meta = (ClampMin = "0"))
	float UseRequestDirectionBelowHeight = 80.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edges", meta = (ClampMin = "0"))
	float EdgePlanarTolerance = 9.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edges", meta = (ClampMin = "0", ClampMax = "90"))
	float EdgeHeadingToleranceDegrees = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edges", meta = (ClampMin = "1"))
	float ScoreDistanceScale = 500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edges", meta = (ClampMin = "0"))
	float DistanceScoreWeight = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edges", meta = (ClampMin = "0"))
	float HeadingScoreWeight = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edges", meta = (ClampMin = "1"))
	float SecondaryHitPenalty = 1.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slopes", meta = (ClampMin = "0", ClampMax = "1"))
	float WalkableSlopeMinNormalZ = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slopes", meta = (ClampMin = "0"))
	float WalkableSlopeDistanceTolerance = 13.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slopes", meta = (ClampMin = "0", ClampMax = "89"))
	float MaxClearanceSlopeAngle = 42.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probes", meta = (ClampMin = "0", ClampMax = "90"))
	float LargeApproachAngle = 37.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probes", meta = (ClampMin = "0", ClampMax = "90"))
	float MaxApproachAngle = 42.5f;

	/** Component or owning actor tags; use project surface semantics, not engine entity classes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Policy")
	TArray<FName> BlockedSurfaceTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Policy")
	TArray<FName> AutomaticBlockedSurfaceTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Policy")
	FName StairSurfaceTag = TEXT("Traversal.Stairs");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Policy")
	bool bRejectAutomaticHighOpenApproach = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Policy", meta = (ClampMin = "1", ClampMax = "16"))
	int32 AutomaticMinDepthSamples = 1;

	/** Drop below the approach ground for automatic crossing of shallow tops with open space behind. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Policy", meta = (ClampMin = "0"))
	float AutomaticMaxGroundDrop = 150.f;

	/** Reject spans strictly below this cutoff when standing on stairs, matching the reference comparison. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Policy", meta = (ClampMin = "0"))
	float StairMinEdgeSpan = 22.5f;

	/** Replaces StairMinEdgeSpan for tagged faces/tops; equality is accepted by the span filter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Policy", meta = (ClampMin = "0"))
	float TaggedStairMinEdgeSpan = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Policy", meta = (ClampMin = "0"))
	float StairAmbiguousEdgeSpan = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Policy", meta = (ClampMin = "0"))
	float StairMaxPlanarDrift = 10.f;
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

	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UPrimitiveComponent> BackFloorComponent = nullptr;

	UPROPERTY(BlueprintReadOnly)
	FTransform FrontLedge = FTransform::Identity;

	UPROPERTY(BlueprintReadOnly)
	FTransform BackLedge = FTransform::Identity;

	UPROPERTY(BlueprintReadOnly)
	FTransform BackFloor = FTransform::Identity;

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

	UPROPERTY(BlueprintReadOnly)
	bool bCanMantle = false;

	UPROPERTY(BlueprintReadOnly)
	bool bCanVault = false;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EIMGTraversalLandingMode Mode = EIMGTraversalLandingMode::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName TargetName = TEXT("BackFloor");

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName AnchorTargetName = TEXT("BackLedge");

	/** Explicit alternate contact for assets without a rear-contact translation window. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName FallbackAnchorTargetName = TEXT("FrontLedge");

	/** Translation-window ordinal in montage order; -1 selects the last eligible window. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "-1"))
	int32 AnchorWindowIndex = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "-1"))
	int32 LandingWindowIndex = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName DistanceCurveName = TEXT("Distance_From_Ledge");

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bAbsoluteCurveDistance = true;

	/** Conservative straight-path rejection is opt-in; it does not represent a warped animation arc. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bCheckStraightPath = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"))
	float CapsuleClearance = 3.f;
};

/**
 * Execution rules after animation selection: define warp geometry bindings, required targets, landing calculation, and valid parameter ranges.
 * Does not select a Montage or modify StartTime/PlayRate returned by SelectTraversalAnimation; execution is rejected if the rules are not satisfied.
 * Warp window times and Modifier parameters remain configured in animation Notifies; prediction and the server validate using the same rules.
 */
USTRUCT(BlueprintType)
struct IMAGINEGAME_API FIMGTraversalAnimationSettings
{
	GENERATED_BODY()

	FIMGTraversalAnimationSettings();

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FIMGTraversalWarpBinding> WarpBindings;

	/** Required contacts must retain a window after the selected start time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FName> RequiredTargets;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FIMGTraversalLandingSettings Landing;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"))
	float MinVerticalClearance = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"))
	float MinHorizontalClearance = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.01"))
	float MinPlayRate = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.01"))
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
	FIMGTraversalAnimationSettings Settings;
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
	FIMGTraversalAnimationSettings Settings;
};

/** Resolved targets, not client-authored geometry, are replicated to simulated proxies. */
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

/** Server-owned state for simulated proxies; GAS continues to replicate montage playback. */
USTRUCT()
struct FIMGTraversalRepState
{
	GENERATED_BODY()

	UPROPERTY()
	uint16 Sequence = 0;

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

	UPROPERTY()
	TArray<FIMGTraversalWarpTarget> WarpTargets;

	/** Authority-owned history; a proxy must not capture late native target replication as previous state. */
	UPROPERTY()
	TArray<FMotionWarpingTarget> PreviousWarpTargets;

	UPROPERTY()
	bool bActive = false;
};
