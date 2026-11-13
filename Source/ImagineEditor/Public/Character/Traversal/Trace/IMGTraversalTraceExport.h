#pragma once
#include "CoreMinimal.h"
#include "Character/Traversal/IMGTraversalDebugTypes.h"

namespace IMGTraversalRewind
{
bool FrameToJson(const FIMGTraversalDebugFrame& Frame, FString& OutText);
bool RecordToJson(const UStruct* Type, const void* Record, FString& OutText);
bool ExportVisibleRange(uint64 ActorId, FString& OutPath, FString& OutError);
} // namespace IMGTraversalRewind
