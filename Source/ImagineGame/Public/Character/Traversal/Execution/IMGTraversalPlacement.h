#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

class AIMGCharacter;
struct FIMGTraversalAnimationSelection;
struct FIMGTraversalExecutionSettings;
struct FIMGTraversalCandidate;
struct FIMGTraversalDetectorSettings;
struct FIMGTraversalWarpTarget;
struct FIMGTraversalWarpWindow;

namespace IMGTraversalPlacement
{
bool PrepareTargets(AIMGCharacter& Character, const FIMGTraversalExecutionSettings& Settings,
    const FIMGTraversalDetectorSettings& Geometry, TConstArrayView<FIMGTraversalWarpWindow> Windows,
    FIMGTraversalCandidate& Candidate, const FIMGTraversalAnimationSelection& Selection,
    TArray<FIMGTraversalWarpTarget>& OutTargets, float& OutGroundHandoffTime, bool bVerboseDiagnostics,
    TFunctionRef<void(const TCHAR*, const FString&, bool)> LogEvent);
}
