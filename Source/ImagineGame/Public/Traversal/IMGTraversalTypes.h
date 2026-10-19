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
	StaleCandidate
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
	float ForwardDistance = 75.f;

	/** Grounded forward probes grow toward this distance as forward speed approaches ForwardDistanceAtMaxSpeed. */
	UPROPERTY(BlueprintReadWrite)
	float MaxForwardDistance = 350.f;

	UPROPERTY(BlueprintReadWrite)
	float ForwardDistanceAtMaxSpeed = 500.f;

	UPROPERTY(BlueprintReadWrite)
	float MinHeight = 35.f;

	UPROPERTY(BlueprintReadWrite)
	float MaxHeight = 220.f;

	UPROPERTY(BlueprintReadWrite)
	float MaxDepth = 300.f;

	UPROPERTY(BlueprintReadWrite)
	float MaxDrop = 180.f;

	UPROPERTY(BlueprintReadWrite)
	float MinWidth = 70.f;

	UPROPERTY(BlueprintReadWrite)
	float MaxMovingSpeed = 500.f;

	UPROPERTY(BlueprintReadWrite)
	float MaxAngularSpeedDegrees = 85.f;

	UPROPERTY(BlueprintReadWrite)
	float MaxHurdleHeight = 110.f;

	UPROPERTY(BlueprintReadWrite)
	float MaxVaultHeight = 160.f;

	UPROPERTY(BlueprintReadWrite)
	bool bAllowSimulatingPhysics = false;
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

	UPROPERTY(BlueprintReadOnly)
	float DropHeight = 0.f;

	/** Same semantic as BackLedgeHeight in the existing CHT input. */
	UPROPERTY(BlueprintReadOnly)
	float BackLedgeHeight = 0.f;

	UPROPERTY(BlueprintReadOnly)
	float DistanceToLedge = 0.f;

	UPROPERTY(BlueprintReadOnly)
	float DetectionTime = 0.f;

	UPROPERTY(BlueprintReadOnly)
	float Speed = 0.f;

	UPROPERTY(BlueprintReadOnly)
	TEnumAsByte<EMovementMode> MovementMode = MOVE_None;

	UPROPERTY(BlueprintReadOnly)
	bool bHasFrontLedge = false;

	UPROPERTY(BlueprintReadOnly)
	bool bHasBackLedge = false;

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
	TObjectPtr<UPrimitiveComponent> HitComponent = nullptr;

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
