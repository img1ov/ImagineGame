#pragma once

#include "CoreMinimal.h"

class UAnimMontage;
struct FIMGTraversalWarpWindow;

namespace IMGTraversalWarpWindows
{
void Collect(const UAnimMontage* Montage, bool bSearchAnimationSegments, TArray<FIMGTraversalWarpWindow>& OutWindows);
const FIMGTraversalWarpWindow* FindTranslationWindow(TConstArrayView<FIMGTraversalWarpWindow> Windows,
    FName TargetName, int32 WindowIndex, float MinEndTime, float MaxEndTime);
}
