#pragma once
#include "CoreMinimal.h"
#include "Character/Traversal/IMGTraversalDebugTypes.h"
class IDetailCustomization;
namespace IMGTraversalRewind
{
TSharedRef<IDetailCustomization> MakeTraversalDetailsCustomization(
    TSharedPtr<const FIMGTraversalDebugFrame> Frame, int32 Lane, TSharedRef<TMap<FName, int32>> Pages);
}
