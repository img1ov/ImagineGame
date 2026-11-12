#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

class AIMGCharacter;
struct FIMGTraversalAnimationSelection;
struct FIMGTraversalAnimationSettings;
struct FIMGTraversalCandidate;
struct FIMGTraversalDetectorSettings;
struct FIMGTraversalWarpTarget;
struct FIMGTraversalWarpWindow;

namespace IMGTraversalPlacement
{
bool PrepareTargets(AIMGCharacter& Character, const FIMGTraversalAnimationSettings& Settings,
    const FIMGTraversalDetectorSettings& Geometry, TConstArrayView<FIMGTraversalWarpWindow> Windows,
    FIMGTraversalCandidate& Candidate, const FIMGTraversalAnimationSelection& Selection,
    TArray<FIMGTraversalWarpTarget>& OutTargets, bool bVerboseDiagnostics,
    TFunctionRef<void(const TCHAR*, const FString&, bool)> LogEvent);
}
