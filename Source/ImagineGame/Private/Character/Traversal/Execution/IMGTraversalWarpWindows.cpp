#include "Character/Traversal/Execution/IMGTraversalWarpWindows.h"

#include "Animation/AnimMontage.h"
#include "AnimNotifyState_MotionWarping.h"
#include "Character/Traversal/IMGTraversalTypes.h"
#include "RootMotionModifier.h"

void IMGTraversalWarpWindows::Collect(
    const UAnimMontage* Montage, bool bSearchAnimationSegments, TArray<FIMGTraversalWarpWindow>& OutWindows)
{
	OutWindows.Reset();
	if (!Montage)
	{
		return;
	}
	TArray<FVector2D> ModifierRanges;
	auto Collect = [&OutWindows, &ModifierRanges, Montage](
	                   const UAnimSequenceBase* Animation, float Offset, float RangeStart, float RangeEnd)
	{
		if (!Animation)
		{
			return;
		}
		for (const FAnimNotifyEvent& Event : Animation->Notifies)
		{
			const UAnimNotifyState_MotionWarping* Notify = Cast<UAnimNotifyState_MotionWarping>(Event.NotifyStateClass);
			if (!Notify || !Notify->RootMotionModifier)
			{
				continue;
			}
			// Trimmed segments can leave a notify overhanging the montage; keep the window inside its timeline.
			const float MontageLength = Montage->GetPlayLength();
			const float Start = FMath::Clamp(
			    FMath::Clamp(Event.GetTriggerTime(), 0.f, Animation->GetPlayLength()) + Offset, 0.f, MontageLength);
			const float End = FMath::Clamp(
			    FMath::Clamp(Event.GetEndTriggerTime(), 0.f, Animation->GetPlayLength()) + Offset, 0.f, MontageLength);
			if (End <= RangeStart || Start >= RangeEnd)
			{
				continue;
			}
			// UE deduplicates modifiers by animation and time range, irrespective of target name.
			const FVector2D Range(Start, End);
			if (ModifierRanges.Contains(Range))
			{
				continue;
			}
			ModifierRanges.Add(Range);
			const URootMotionModifier_Warp* Modifier = Cast<URootMotionModifier_Warp>(Notify->RootMotionModifier);
			FIMGTraversalWarpWindow& Window = OutWindows.AddDefaulted_GetRef();
			Window.Modifier = Modifier;
			Window.StartTime = Start;
			Window.EndTime = End;
		}
	};
	Collect(Montage, 0.f, 0.f, Montage->GetPlayLength());
	if (bSearchAnimationSegments)
	{
		for (const FSlotAnimationTrack& Slot : Montage->SlotAnimTracks)
		{
			for (const FAnimSegment& Segment : Slot.AnimTrack.AnimSegments)
			{
				// Match the runtime MotionWarping component's segment-time conversion.
				Collect(Segment.GetAnimReference(), Segment.StartPos - Segment.AnimStartTime, Segment.StartPos,
				    Segment.GetEndPos());
			}
		}
	}
	OutWindows.StableSort(
	    [](const FIMGTraversalWarpWindow& A, const FIMGTraversalWarpWindow& B) { return A.StartTime < B.StartTime; });
}

const FIMGTraversalWarpWindow* IMGTraversalWarpWindows::FindTranslationWindow(
    TConstArrayView<FIMGTraversalWarpWindow> Windows, FName TargetName, int32 WindowIndex, float MinEndTime,
    float MaxEndTime)
{
	if (TargetName.IsNone() || WindowIndex < -1)
	{
		return nullptr;
	}
	const FIMGTraversalWarpWindow* Result = nullptr;
	int32 Ordinal = 0;
	for (const FIMGTraversalWarpWindow& Window : Windows)
	{
		if (Window.Modifier && Window.Modifier->WarpTargetName == TargetName && Window.Modifier->bWarpTranslation &&
		    Window.EndTime > Window.StartTime)
		{
			const bool bEligible = Window.EndTime > MinEndTime && Window.EndTime <= MaxEndTime;
			// An explicit authored ordinal must not shift when PoseSearch skips an earlier window.
			if (WindowIndex == Ordinal)
			{
				return bEligible ? &Window : nullptr;
			}
			if (bEligible)
			{
				Result = &Window;
			}
			++Ordinal;
		}
	}
	return WindowIndex == -1 ? Result : nullptr;
}
