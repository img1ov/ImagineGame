#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "IMGTraversalTypes.generated.h"

class AIMGCharacter;
class UAnimMontage;
class UPrimitiveComponent;

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

/** Persistent geometry, safety and validation settings shared by prediction and authority. */
USTRUCT(BlueprintType)
struct FIMGTraversalDetectorSettings
{
	GENERATED_BODY()

	/** Speed used only by geometry exit conditions that require a running traversal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float RunningSpeedThreshold = 350.f;

	/** Broad-phase probe length; matches the reference manual detector's 2.5 m search. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float SearchDistance = 250.f;

	/**
	 * Maximum planar distance from the character origin to a returned front ledge.
	 * This is an absolute detector bound; animation initiation distance remains owned by Motion Matching data.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MaxMovingSpeed = 500.f;

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
	FIMGTraversalDetectorSettings Settings;
};

/** Geometry and feasible actions; animation assets never enter the detector. */
USTRUCT(BlueprintType)
struct FIMGTraversalCandidate
{
	GENERATED_BODY()

	/** Geometry-only default. Blueprint may use or ignore it; SelectTraversalAnimation owns the final action. */
	UPROPERTY(BlueprintReadOnly)
	EIMGTraversalAction RecommendedAction = EIMGTraversalAction::None;

	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UPrimitiveComponent> HitComponent = nullptr;

	/** Component that owns the front ledge anchor. HitComponent remains its Blueprint-compatible alias. */
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

	UPROPERTY(BlueprintReadOnly)
	float ObstacleDepth = 0.f;

	/** Free distance after the front ledge before another obstacle is encountered. */
	UPROPERTY(BlueprintReadOnly)
	float HorizontalClearance = 0.f;

	/** Free vertical space above the ledge measured by the dedicated clearance probe. */
	UPROPERTY(BlueprintReadOnly)
	float VerticalClearance = 0.f;

	UPROPERTY(BlueprintReadOnly)
	float DropHeight = 0.f;

	/** Same semantic as BackLedgeHeight in the existing CHT input. */
	UPROPERTY(BlueprintReadOnly)
	float BackLedgeHeight = 0.f;

	UPROPERTY(BlueprintReadOnly)
	float DistanceToLedge = 0.f;

	/** Vertical extent of the grouped broad-phase face hits that produced this candidate. */
	UPROPERTY(BlueprintReadOnly)
	float FrontEdgeVerticalSpan = 0.f;

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
		return (Action == EIMGTraversalAction::Mantle && bCanMantle)
			|| (Action == EIMGTraversalAction::Vault && bCanVault)
			|| (Action == EIMGTraversalAction::Hurdle && bCanHurdle);
	}
};

/** GASP Chooser and PoseMatch decide this in a Blueprint child of the native ability. */
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
	TObjectPtr<UPrimitiveComponent> HitComponent = nullptr;

	UPROPERTY()
	TObjectPtr<UPrimitiveComponent> FrontLedgeComponent = nullptr;

	UPROPERTY()
	TObjectPtr<UPrimitiveComponent> BackLedgeComponent = nullptr;

	UPROPERTY()
	TArray<TObjectPtr<UPrimitiveComponent>> ObstacleComponents;

	UPROPERTY()
	TObjectPtr<UPrimitiveComponent> BackFloorComponent = nullptr;

	UPROPERTY()
	FTransform FrontLedge = FTransform::Identity;

	UPROPERTY()
	FTransform BackLedge = FTransform::Identity;

	UPROPERTY()
	FTransform BackFloor = FTransform::Identity;

	UPROPERTY()
	FTransform LocalFrontLedge = FTransform::Identity;

	UPROPERTY()
	FTransform LocalBackLedge = FTransform::Identity;

	UPROPERTY()
	FTransform LocalBackFloor = FTransform::Identity;

	UPROPERTY()
	bool bHasBackLedge = false;

	UPROPERTY()
	bool bHasBackFloor = false;

	UPROPERTY()
	bool bActive = false;
};
