#include "Character/Traversal/IMGTraversalRewindData.h"

namespace IMGTraversalRewind
{
int32 GetEventLane(const FIMGTraversalDebugEvent& Event)
{
	if (Event.Name == TEXT("DetectionBegin") || Event.Name == TEXT("DetectionResult"))
	{
		return 1;
	}
	if (Event.Name == TEXT("Activate") || Event.Name == TEXT("PreflightGate") ||
	    Event.Name == TEXT("TargetDataReady") || Event.Name == TEXT("LocalDetectionRejected") ||
	    Event.Name == TEXT("LocalSelectionRejected"))
	{
		return 0;
	}
	if (Event.Name == TEXT("TraversalStarted") || Event.Name == TEXT("End") ||
	    Event.Name == TEXT("TraversalAborted") || Event.Name == TEXT("MontageInterruptedOrCancelled") ||
	    Event.Name == TEXT("MontageInterrupted") || Event.Name == TEXT("MontageTaskCancelled") ||
	    Event.Name == TEXT("ExecutionAcquired") || Event.Name == TEXT("ExecutionReleasing") ||
	    Event.Name == TEXT("ExecutionReleased") || Event.Name == TEXT("ExecutionState"))
	{
		return 2;
	}
	if (Event.Name.StartsWith(TEXT("Snapshot")) || Event.Name.StartsWith(TEXT("Predicted")) ||
	    Event.Name.StartsWith(TEXT("Replay")) || Event.Name.StartsWith(TEXT("TargetData")) ||
	    Event.Name.StartsWith(TEXT("Authority")) || Event.Name == TEXT("AwaitTargetData") ||
	    Event.Name == TEXT("PreTraversalCorrectionDiscarded") || Event.Name == TEXT("MontagePrerequisite"))
	{
		return 3;
	}
	if (Event.Stage == EIMGTraversalDebugStage::Network || Event.Stage == EIMGTraversalDebugStage::Replay)
	{
		return 3;
	}
	if (Event.Stage == EIMGTraversalDebugStage::Execution)
	{
		return 2;
	}
	if (Event.Stage <= EIMGTraversalDebugStage::Output)
	{
		return Event.Phase == EIMGTraversalDebugPhase::Placement ||
		        Event.Phase == EIMGTraversalDebugPhase::Execution
		    ? 2
		    : 1;
	}
	switch (Event.Phase)
	{
		case EIMGTraversalDebugPhase::Placement:
		case EIMGTraversalDebugPhase::Execution:
			return 2;
		case EIMGTraversalDebugPhase::Replication:
		case EIMGTraversalDebugPhase::Replay:
			return 3;
		default:
			return 0;
	}
}
bool IsTimelineEvent(const FIMGTraversalDebugEvent& Event)
{
	if (Event.bWarning)
	{
		return true;
	}
	// High-frequency bookkeeping and placement diagnostics do not become timeline markers.
	if (Event.Name == TEXT("Detail") || Event.Name == TEXT("ExecutionState") ||
	    Event.Name == TEXT("ReplayCaptured") || Event.Name == TEXT("ReplayRestored") ||
	    Event.Name == TEXT("PlaybackSection") || Event.Name == TEXT("PlaybackPath") ||
	    Event.Name == TEXT("PlacementWindow") || Event.Name == TEXT("PlacementEvaluation") ||
	    Event.Name == TEXT("LandingEvaluation"))
	{
		return false;
	}
	return true;
}
bool IsInspectorEvent(const FIMGTraversalDebugEvent& Event)
{
	if (Event.bWarning || Event.Name != TEXT("Detail"))
	{
		return true;
	}
	return Event.Stage != EIMGTraversalDebugStage::Search &&
	    Event.Stage != EIMGTraversalDebugStage::Intersections &&
	    Event.Stage != EIMGTraversalDebugStage::Output;
}
} // namespace IMGTraversalRewind
