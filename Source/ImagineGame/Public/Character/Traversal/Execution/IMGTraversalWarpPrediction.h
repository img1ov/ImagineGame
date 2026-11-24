#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

class AIMGCharacter;
class USkeletalMeshComponent;
class FIMGTraversalPlaybackPath;
struct FIMGTraversalAnimationSelection;
struct FIMGTraversalWarpWindow;

/** Root-motion projection for supported native warp windows. Does not mutate gameplay objects. */
class FIMGTraversalWarpPrediction
{
public:
	FIMGTraversalWarpPrediction(AIMGCharacter& InCharacter, const FIMGTraversalAnimationSelection& InSelection,
	    const FIMGTraversalPlaybackPath& InPlayback, TConstArrayView<const FIMGTraversalWarpWindow*> InWindows,
	    const FTransform& InInitialPose, bool bInEnabled, bool bInVerboseDiagnostics, FString& InFailureReason,
	    TFunctionRef<void(const TCHAR*, const FString&, bool)> InLogEvent);
	bool Project(const FIMGTraversalWarpWindow& ReferenceWindow, const TMap<FName, FTransform>& WorldTargets, FVector& Center);

private:
	FTransform ExtractMotion(float From, float To) const;
	void AdvanceRootMotion(float From, float To, FTransform& Pose) const;
	bool ResolveWindowPose(const FIMGTraversalWarpWindow& Window, const FTransform& WarpTarget,
	    const FTransform& EntryPose, FTransform& EndPose);

	AIMGCharacter* Character;
	const FIMGTraversalAnimationSelection& Selection;
	const FIMGTraversalPlaybackPath& Playback;
	TConstArrayView<const FIMGTraversalWarpWindow*> Windows;
	FTransform InitialPose;
	const USkeletalMeshComponent* Mesh;
	FQuat BaseRotation;
	FVector MeshScale;
	FTransform MeshRelative;
	bool bEnabled;
	bool bVerboseDiagnostics;
	FString& PredictionUnavailableReason;
	TFunctionRef<void(const TCHAR*, const FString&, bool)> LogEvent;
	TMap<const FIMGTraversalWarpWindow*, FTransform> RootOffsets;
	TMap<const FIMGTraversalWarpWindow*, FTransform> NativeTails;
};
