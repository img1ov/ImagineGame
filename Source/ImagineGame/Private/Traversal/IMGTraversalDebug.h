#pragma once

#include "CoreMinimal.h"
#include "DrawDebugHelpers.h"

class UWorld;
class AActor;
struct FHitResult;
struct FIMGTraversalAnimationSelection;
struct FIMGTraversalCandidate;

namespace IMGTraversalDebug
{
#if ENABLE_DRAW_DEBUG
	enum class EStage : uint8
	{
		Search = 1,
		Intersections = 2,
		Compute = 4,
		Validate = 8,
		Output = 16,
		Execution = 32
	};

	bool IsEnabled(int32 RequiredLevel = 1);
	bool IsStageEnabled(EStage Stage);
	void Shutdown();
	void BeginTraversalSnapshot(const AActor* Actor, bool bReset = true);
	void SetTraversalSnapshotCaptureEnabled(bool bEnabled);
	void BeginTraversalCandidateSnapshot();
	void RollbackTraversalCandidateSnapshot();
	void DiscardTraversalSnapshot(const AActor* Actor = nullptr);
	void CandidateOutput(const UWorld* World, const FIMGTraversalCandidate& Candidate, const FString& Label);
	void SuccessfulTraversal(const AActor* Actor, const FIMGTraversalCandidate& Candidate,
		const FIMGTraversalAnimationSelection& Selection);
	void SweepSphere(const UWorld* World, const FVector& Start, const FVector& End, float Radius,
		const FHitResult* Hit, const FString& Label, const FColor& Color, EStage Stage, int32 RequiredLevel = 3);
	void Intersection(const UWorld* World, const FHitResult& Hit, bool bAccepted, const FString& Label,
		EStage Stage = EStage::Intersections, int32 RequiredLevel = 3);
	void Measure(const UWorld* World, const FVector& Start, const FVector& End, const FString& Label,
		float Value, const FColor& Color);
	// -2 uses process lifetime and records a snapshot command; -1 uses success lifetime; 0 is live-only.
	void Line(const UWorld* World, const FVector& Start, const FVector& End, const FColor& Color,
		int32 RequiredLevel = 1, float Thickness = 1.f, float Duration = -2.f, EStage Stage = EStage::Execution);
	void Arrow(const UWorld* World, const FVector& Start, const FVector& End, const FColor& Color,
		int32 RequiredLevel = 1, float Duration = -2.f, EStage Stage = EStage::Execution);
	void Point(const UWorld* World, const FVector& Location, const FColor& Color,
		int32 RequiredLevel = 1, float Size = 10.f, float Duration = -2.f, EStage Stage = EStage::Execution);
	void Sphere(const UWorld* World, const FVector& Center, float Radius, const FColor& Color,
		int32 RequiredLevel = 1, float Duration = -2.f, EStage Stage = EStage::Execution);
	void Capsule(const UWorld* World, const FVector& Center, float HalfHeight, float Radius, const FColor& Color,
		int32 RequiredLevel = 1, float Duration = -2.f, EStage Stage = EStage::Execution);
	void String(const UWorld* World, const FVector& Location, const FString& Text, const FColor& Color,
		int32 RequiredLevel = 1, float Duration = -2.f, EStage Stage = EStage::Execution,
		FName Key = NAME_None, const AActor* Owner = nullptr);
#endif
}
