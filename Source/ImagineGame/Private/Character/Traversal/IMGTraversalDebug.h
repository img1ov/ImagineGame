#pragma once

#include "CoreMinimal.h"
#include "Character/Traversal/IMGTraversalDebugTypes.h"

DECLARE_LOG_CATEGORY_EXTERN(LogIMGTraversal, Log, All);
#define IMG_TRAVERSAL_DEBUG_ENABLED (!(UE_BUILD_SHIPPING || UE_BUILD_TEST))

class UWorld;
class AActor;
class UAnimMontage;
struct FHitResult;
struct FIMGTraversalAnimationSelection;
struct FIMGTraversalCandidate;
struct FIMGTraversalRepState;
struct FIMGTraversalQuery;
enum class EIMGTraversalFailure : uint8;

namespace IMGTraversalDebug
{
#if IMG_TRAVERSAL_DEBUG_ENABLED
using EStage = EIMGTraversalDebugStage;
using EPhase = EIMGTraversalDebugPhase;
using EPrimitive = EIMGTraversalDebugPrimitive;
using FCommand = FIMGTraversalDebugShape;
enum class ELifetime : uint8
{
	Snapshot,
	Live
};

struct FObservation
{
	TWeakObjectPtr<const AActor> Actor;
	FIMGTraversalDebugFrame Data;
	TArray<FCommand> LiveCommands;
	TArray<FCommand> PendingGeometry;
	TArray<FCommand> PlacementGeometry;
	uint64 PlacementAttempt = 0;
	TArray<FIMGTraversalDebugFrame> History;
	TMap<FName, FString> LastSignals;
	uint64 Attempt = 0;
	uint64 LiveFrame = 0;
	uint64 RecordingSerial = 0;
	uint32 Dropped = 0;
	double UpdatedAt = 0;
	bool bDirty = false;
	TWeakObjectPtr<const UAnimMontage> HeightMontage;
	TArray<FVector2f> HeightPath;
	FTransform HeightInitialPose;
	FTransform HeightMeshRelative;
	uint64 HeightCapture = 0;
	uint64 HeightPreparedFrame = MAX_uint64;
	bool bHeightStarted = false;
};

// A synchronous observation stack; never retained by gameplay or SavedMove closures.
class FScope
{
public:
	FScope(const AActor* Actor, EPhase Phase);
	FScope(
		const FIMGTraversalQuery& Query, const FIMGTraversalCandidate& Candidate, const EIMGTraversalFailure& Failure);
	~FScope();
	FScope(const FScope&) = delete;
	FScope& operator=(const FScope&) = delete;
	const AActor* GetActor() const
	{
		return Actor.Get();
	}
	EPhase GetPhase() const
	{
		return Phase;
	}
	uint64 GetAttempt() const
	{
		return Attempt;
	}
	void CountQuery()
	{
		if (StartTime > 0)
		{
			++Queries;
		}
	}

private:
	TWeakObjectPtr<const AActor> Actor;
	FScope* Previous = nullptr;
	EPhase Phase = EPhase::External;
	uint64 Attempt = 0;
	double StartTime = 0;
	int32 Queries = 0;
	const FIMGTraversalCandidate* Candidate = nullptr;
	const FIMGTraversalQuery* DetectionQuery = nullptr;
	const EIMGTraversalFailure* Failure = nullptr;
};

bool IsEnabled();
bool IsObserving(const AActor* Actor);
void Startup();
void Shutdown();
void ReleaseActor(const AActor* Actor);
void FlushRecording();
void CountQuery();
template <typename F> auto Query(F&& Function)
{
	CountQuery();
	return Function();
}
void Event(const AActor* Actor, const TCHAR* Name, const FString& Detail, bool bWarning = false,
	EStage Stage = EStage::Lifecycle, const FString& AbilitySpec = FString(), int32 PredictionKey = 0);
void State(const AActor* Actor, const TCHAR* Name, const FIMGTraversalRepState& State, const TCHAR* Reason = TEXT(""));
void CandidateOutput(const UWorld* World, const FIMGTraversalCandidate& Candidate, const FString& Label);
void ExecutionStarted(
	const AActor* Actor, const FIMGTraversalCandidate& Candidate, const FIMGTraversalAnimationSelection& Selection);
void PrepareHeightCapture(const AActor* Actor, const FIMGTraversalAnimationSelection& Selection,
	const FTransform& InitialPose, TConstArrayView<FVector2f> PlaybackRanges);
void SweepSphere(const UWorld* World, const FVector& Start, const FVector& End, float Radius, const FHitResult* Hit,
	const FString& Label, const FColor& Color, EStage Stage);
void Intersection(const UWorld* World, const FHitResult& Hit, bool bAccepted, const FString& Label,
	EStage Stage = EStage::Intersections);
void Measure(const UWorld* World, const FVector& Start, const FVector& End, const FString& Label, float Value,
	const FColor& Color);
void Line(const UWorld* World, const FVector& Start, const FVector& End, const FColor& Color, float Thickness = 1.f,
	ELifetime Lifetime = ELifetime::Snapshot, EStage Stage = EStage::Execution);
void Arrow(const UWorld* World, const FVector& Start, const FVector& End, const FColor& Color,
	ELifetime Lifetime = ELifetime::Snapshot, EStage Stage = EStage::Execution);
void Point(const UWorld* World, const FVector& Location, const FColor& Color, float Size = 3.f,
	ELifetime Lifetime = ELifetime::Snapshot, EStage Stage = EStage::Execution);
void Sphere(const UWorld* World, const FVector& Center, float Radius, const FColor& Color,
	ELifetime Lifetime = ELifetime::Snapshot, EStage Stage = EStage::Execution);
void Capsule(const UWorld* World, const FVector& Center, float HalfHeight, float Radius, const FColor& Color,
	ELifetime Lifetime = ELifetime::Snapshot, EStage Stage = EStage::Execution);
void PlacementCapsule(const UWorld* World, const FVector& Center, float HalfHeight, float Radius, const FColor& Color,
	const TCHAR* Purpose);
// Text belongs to the inspector/recording; there is no world-space text renderer.
void Message(
	const FString& Text, EStage Stage = EStage::Execution, FName Key = NAME_None, const AActor* Owner = nullptr);
void MessageAt(const FVector& Location, const FString& Text, EStage Stage = EStage::Execution, FName Key = NAME_None,
	const AActor* Owner = nullptr);
#endif
} // namespace IMGTraversalDebug

#if IMG_TRAVERSAL_DEBUG_ENABLED
#define IMG_TRAVERSAL_QUERY(Expression) IMGTraversalDebug::Query([&]() { return Expression; })
#else
#define IMG_TRAVERSAL_QUERY(Expression) (Expression)
#endif
