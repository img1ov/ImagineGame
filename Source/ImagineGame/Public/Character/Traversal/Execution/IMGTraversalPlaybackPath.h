#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

struct FIMGTraversalAnimationSelection;
struct FIMGTraversalWarpWindow;

/** Finite authored section path and its mapping to a monotonic playback timeline. */
class FIMGTraversalPlaybackPath
{
public:
	bool Build(const FIMGTraversalAnimationSelection& Selection, TConstArrayView<FIMGTraversalWarpWindow> Windows,
	    bool bVerboseDiagnostics, TFunctionRef<void(const TCHAR*, const FString&, bool)> LogEvent, FString& OutFailure);
	const TArray<FVector2f>& GetRanges() const { return PlaybackRanges; }
	const TArray<float>& GetOffsets() const { return PlaybackOffsets; }
	const TMap<const FIMGTraversalWarpWindow*, FVector2f>& GetWindowTimes() const { return WindowPlaybackTimes; }
	const TArray<const FIMGTraversalWarpWindow*>& GetReachableWindows() const { return ReachableWindows; }
	float GetStartTime() const { return PlaybackStart; }
	float GetPlacementTime() const { return PlacementTime; }
	const FIMGTraversalWarpWindow* SelectTranslationWindow(TConstArrayView<FIMGTraversalWarpWindow> Windows,
	    FName Name, int32 Index, float MinEnd, float MaxEnd) const;

private:
	TArray<FVector2f> PlaybackRanges;
	TArray<float> PlaybackOffsets;
	TMap<const FIMGTraversalWarpWindow*, FVector2f> WindowPlaybackTimes;
	TArray<const FIMGTraversalWarpWindow*> ReachableWindows;
	float MontageLength = 0.f;
	float PlaybackStart = 0.f;
	float PlaybackEnd = 0.f;
	float PlacementTime = 0.f;
};
