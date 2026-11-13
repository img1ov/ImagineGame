#pragma once
#include "Character/Traversal/IMGTraversalDebugTypes.h"

namespace IMGTraversalRewind
{
int32 GetEventLane(const FIMGTraversalDebugEvent& Event);
bool IsTimelineEvent(const FIMGTraversalDebugEvent& Event);
bool IsInspectorEvent(const FIMGTraversalDebugEvent& Event);
} // namespace IMGTraversalRewind
