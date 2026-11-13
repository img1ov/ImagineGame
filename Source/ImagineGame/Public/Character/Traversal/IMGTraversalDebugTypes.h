#pragma once

#include "CoreMinimal.h"
#include "IMGTraversalDebugTypes.generated.h"

UENUM()
enum class EIMGTraversalDebugStage : uint8
{
	Search,
	Intersections,
	Compute,
	Validate,
	Output,
	Execution,
	Network,
	Replay,
	Lifecycle
};

UENUM()
enum class EIMGTraversalDebugPhase : uint8
{
	External,
	Preflight,
	Activation,
	ServerValidation,
	Placement,
	Execution,
	Replication,
	Replay,
	Lifecycle
};

UENUM()
enum class EIMGTraversalDebugPrimitive : uint8
{
	Line,
	Arrow,
	Point,
	Sphere,
	Capsule
};

USTRUCT()
struct IMAGINEGAME_API FIMGTraversalDebugShape
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Geometry")
	EIMGTraversalDebugPrimitive Primitive = EIMGTraversalDebugPrimitive::Line;
	UPROPERTY(VisibleAnywhere, Category = "Geometry")
	EIMGTraversalDebugStage Stage = EIMGTraversalDebugStage::Execution;
	UPROPERTY(VisibleAnywhere, Category = "Geometry")
	FVector Start = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, Category = "Geometry")
	FVector End = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, Category = "Geometry")
	FColor Color = FColor::White;
	UPROPERTY(VisibleAnywhere, Category = "Geometry")
	float Size = 1.f;
	UPROPERTY(VisibleAnywhere, Category = "Geometry")
	float Radius = 0.f;
	UPROPERTY(VisibleAnywhere, Category = "Geometry")
	float Thickness = 0.f;
	UPROPERTY(VisibleAnywhere, Category = "Geometry")
	int64 Attempt = 0;
	UPROPERTY(VisibleAnywhere, Category = "Geometry")
	FString Purpose;
};
IMAGINEGAME_API FArchive& operator<<(FArchive& Ar, FIMGTraversalDebugShape& Value);

USTRUCT()
struct IMAGINEGAME_API FIMGTraversalDebugEvent
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Event")
	FString Name;
	UPROPERTY(VisibleAnywhere, Category = "Event")
	FString Detail;
	UPROPERTY(VisibleAnywhere, Category = "Event")
	EIMGTraversalDebugPhase Phase = EIMGTraversalDebugPhase::External;
	UPROPERTY(VisibleAnywhere, Category = "Event")
	EIMGTraversalDebugStage Stage = EIMGTraversalDebugStage::Lifecycle;
	UPROPERTY(VisibleAnywhere, Category = "Event")
	double Time = 0;
	UPROPERTY(VisibleAnywhere, Category = "Identity")
	int64 Attempt = 0;
	UPROPERTY(VisibleAnywhere, Category = "Identity")
	FString AbilitySpec;
	UPROPERTY(VisibleAnywhere, Category = "Identity")
	int32 PredictionKey = 0;
	UPROPERTY(VisibleAnywhere, Category = "Event")
	bool bWarning = false;
	UPROPERTY(VisibleAnywhere, Category = "Identity")
	int32 Sequence = 0;
	UPROPERTY(VisibleAnywhere, Category = "Identity")
	int32 MontageInstance = 0;
	UPROPERTY(VisibleAnywhere, Category = "Event")
	bool bActive = false;
};
IMAGINEGAME_API FArchive& operator<<(FArchive& Ar, FIMGTraversalDebugEvent& Value);

USTRUCT()
struct IMAGINEGAME_API FIMGTraversalDebugCandidate
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category = "Candidate")
	int64 Attempt = 0;
	UPROPERTY(VisibleAnywhere, Category = "Candidate")
	EIMGTraversalDebugPhase Phase = EIMGTraversalDebugPhase::External;
	UPROPERTY(VisibleAnywhere, Category = "Candidate")
	FString Label;
	UPROPERTY(VisibleAnywhere, Category = "Candidate")
	FString Source;
	UPROPERTY(VisibleAnywhere, Category = "Candidate")
	FString RecommendedAction;
	UPROPERTY(VisibleAnywhere, Category = "Candidate")
	bool bCanMantle = false;
	UPROPERTY(VisibleAnywhere, Category = "Candidate")
	bool bCanVault = false;
	UPROPERTY(VisibleAnywhere, Category = "Candidate")
	bool bCanHurdle = false;
	UPROPERTY(VisibleAnywhere, Category = "Candidate")
	float ObstacleHeight = 0;
	UPROPERTY(VisibleAnywhere, Category = "Candidate")
	float ObstacleDepth = 0;
	UPROPERTY(VisibleAnywhere, Category = "Candidate")
	float DropHeight = 0;
	UPROPERTY(VisibleAnywhere, Category = "Candidate")
	float BackLedgeHeight = 0;
	UPROPERTY(VisibleAnywhere, Category = "Candidate")
	float HorizontalClearance = 0;
	UPROPERTY(VisibleAnywhere, Category = "Candidate")
	float VerticalClearance = 0;
	UPROPERTY(VisibleAnywhere, Category = "Candidate")
	float Distance3D = 0;
	UPROPERTY(VisibleAnywhere, Category = "Candidate")
	float DistanceXY = 0;
	UPROPERTY(VisibleAnywhere, Category = "Candidate")
	float EdgeSpan = 0;
	UPROPERTY(VisibleAnywhere, Category = "Candidate")
	float MidHeightClearance = 0;
	UPROPERTY(VisibleAnywhere, Category = "Candidate")
	bool bHighOpenApproach = false;
	UPROPERTY(VisibleAnywhere, Category = "Candidate")
	float ClimbAngle = 0;
	UPROPERTY(VisibleAnywhere, Category = "Candidate")
	float GroundSlopeAngle = 0;
	UPROPERTY(VisibleAnywhere, Category = "Candidate")
	bool bHasBackLedge = false;
	UPROPERTY(VisibleAnywhere, Category = "Candidate")
	bool bHasBackFloor = false;
	UPROPERTY(VisibleAnywhere, Category = "Candidate")
	FTransform FrontLedge = FTransform::Identity;
	UPROPERTY(VisibleAnywhere, Category = "Candidate")
	FTransform BackLedge = FTransform::Identity;
	UPROPERTY(VisibleAnywhere, Category = "Candidate")
	FTransform BackFloor = FTransform::Identity;
	UPROPERTY(VisibleAnywhere, Category = "Candidate")
	FVector FrontNormal = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, Category = "Candidate")
	FVector BackNormal = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, Category = "Candidate")
	FVector GroundNormal = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, Category = "Candidate")
	FVector GroundLocation = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, Category = "Candidate")
	TArray<FString> Components;
};
IMAGINEGAME_API FArchive& operator<<(FArchive& Ar, FIMGTraversalDebugCandidate& Value);

USTRUCT()
struct IMAGINEGAME_API FIMGTraversalDebugFrame
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Trace")
	uint64 Cycle = 0;
	UPROPERTY(VisibleAnywhere, Category = "Trace")
	uint64 ActorId = 0;
	UPROPERTY(VisibleAnywhere, Category = "Trace")
	double Time = 0;
	UPROPERTY(VisibleAnywhere, Category = "Trace")
	float DeltaSeconds = 0;
	UPROPERTY(VisibleAnywhere, Category = "Trace")
	uint64 Frame = 0;
	UPROPERTY(VisibleAnywhere, Category = "Identity")
	FString Actor;
	UPROPERTY(VisibleAnywhere, Category = "Identity")
	FString World;
	UPROPERTY(VisibleAnywhere, Category = "Identity")
	int32 Role = 0;
	UPROPERTY(VisibleAnywhere, Category = "Identity")
	int32 NetMode = 0;
	UPROPERTY(VisibleAnywhere, Category = "Identity")
	FString AbilitySpec;
	UPROPERTY(VisibleAnywhere, Category = "Identity")
	int32 PredictionKey = 0;
	UPROPERTY(VisibleAnywhere, Category = "Identity")
	int32 Sequence = 0;
	UPROPERTY(VisibleAnywhere, Category = "Identity")
	int32 MontageInstance = 0;
	UPROPERTY(VisibleAnywhere, Category = "Execution")
	FString Action;
	UPROPERTY(VisibleAnywhere, Category = "Execution")
	FString Montage;
	UPROPERTY(VisibleAnywhere, Category = "Execution")
	bool bActive = false;
	UPROPERTY(VisibleAnywhere, Category = "Movement")
	FVector Location = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, Category = "Movement")
	FVector Velocity = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, Category = "Movement")
	FString MovementMode;
	UPROPERTY(VisibleAnywhere, Category = "Execution")
	float MontagePosition = 0.f;
	UPROPERTY(VisibleAnywhere, Category = "Execution")
	float MontageLength = 0.f;
	UPROPERTY(VisibleAnywhere, Category = "Execution")
	float MontagePhase = 0.f;
	UPROPERTY(VisibleAnywhere, Category = "Detection")
	int64 Attempt = 0;
	UPROPERTY(VisibleAnywhere, Category = "Detection")
	FString Source;
	UPROPERTY(VisibleAnywhere, Category = "Detection")
	FString Failure;
	UPROPERTY(VisibleAnywhere, Category = "Detection")
	FVector Direction = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, Category = "Detection")
	float ObstacleHeight = 0.f;
	UPROPERTY(VisibleAnywhere, Category = "Detection")
	float ObstacleDepth = 0.f;
	UPROPERTY(VisibleAnywhere, Category = "Detection")
	float DropHeight = 0.f;
	UPROPERTY(VisibleAnywhere, Category = "Detection")
	float HorizontalClearance = 0.f;
	UPROPERTY(VisibleAnywhere, Category = "Detection")
	float VerticalClearance = 0.f;
	UPROPERTY(VisibleAnywhere, Category = "Detection")
	float DetectionMilliseconds = 0.f;
	UPROPERTY(VisibleAnywhere, Category = "Detection")
	int32 Queries = 0;
	UPROPERTY(VisibleAnywhere, Category = "Trace")
	int32 Dropped = 0;
	UPROPERTY(VisibleAnywhere, Category = "Events")
	TArray<FIMGTraversalDebugEvent> Events;
	UPROPERTY(VisibleAnywhere, Category = "Detection")
	TArray<FIMGTraversalDebugCandidate> Candidates;
	UPROPERTY(VisibleAnywhere, Category = "Geometry")
	TArray<FIMGTraversalDebugShape> Geometry;
	UPROPERTY(VisibleAnywhere, Category = "Execution")
	FString MontageAssetPath;
	UPROPERTY(VisibleAnywhere, Category = "Execution")
	FString AbilityClassPath;
	UPROPERTY(VisibleAnywhere, Category = "Height")
	bool bHasHeightSample = false;
	UPROPERTY(VisibleAnywhere, Category = "Trace")
	uint64 HeightCapture = 0;
	UPROPERTY(VisibleAnywhere, Category = "Height")
	double HeightReferenceZ = 0;
	/** Unwarped montage root motion relative to the prepared capsule center, starting at Selection.StartTime. */
	UPROPERTY(VisibleAnywhere, Category = "Height")
	double MontageRootHeight = 0;
	/** Observed capsule height from the same origin; includes Motion Warping, collision and CMC corrections. */
	UPROPERTY(VisibleAnywhere, Category = "Height")
	double WarpedCapsuleHeight = 0;
};
IMAGINEGAME_API FArchive& operator<<(FArchive& Ar, FIMGTraversalDebugFrame& Value);
namespace IMGTraversalDebugSerialization
{
IMAGINEGAME_API FArchive& SerializeFrame(FArchive& Ar, FIMGTraversalDebugFrame& Value, uint8 Version);
}

class UWorld;
struct FMinimalViewInfo;
namespace IMGTraversalDebugPresentation
{
IMAGINEGAME_API bool IsShown();
IMAGINEGAME_API bool IsGraphShown();
IMAGINEGAME_API void Clear(UWorld* World);
IMAGINEGAME_API void Draw(UWorld* World, const FIMGTraversalDebugFrame& Frame);
IMAGINEGAME_API void DrawGraphs(UWorld* World, const FMinimalViewInfo& View,
    TConstArrayView<FIMGTraversalDebugFrame> Frames, double CursorTime = -1);
} // namespace IMGTraversalDebugPresentation
