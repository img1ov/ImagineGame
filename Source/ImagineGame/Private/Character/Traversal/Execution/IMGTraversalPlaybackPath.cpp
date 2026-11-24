#include "Character/Traversal/Execution/IMGTraversalPlaybackPath.h"

#include "Animation/AnimMontage.h"
#include "Character/Traversal/IMGTraversalTypes.h"
#include "Character/Traversal/Execution/IMGTraversalWarpWindows.h"

bool FIMGTraversalPlaybackPath::Build(const FIMGTraversalAnimationSelection& Selection,
    TConstArrayView<FIMGTraversalWarpWindow> Windows, bool bVerboseDiagnostics,
    TFunctionRef<void(const TCHAR*, const FString&, bool)> LogEvent, FString& OutFailure)
{
	*this = FIMGTraversalPlaybackPath();
	OutFailure.Reset();
	auto Reject = [&OutFailure](const FString& Reason) { OutFailure = Reason; return false; };
	auto LogTraversalEvent = [&LogEvent](const TCHAR* Event, const FString& Detail, bool bWarning = false) {
		LogEvent(Event, Detail, bWarning);
	};
	if (!IsValid(Selection.Montage) || !FMath::IsFinite(Selection.StartTime) || Selection.StartTime < 0.f ||
	    Selection.StartTime >= Selection.Montage->GetPlayLength())
	{
		return Reject(TEXT("Montage or start time is invalid"));
	}
	const UAnimMontage& Montage = *Selection.Montage;
	MontageLength = Montage.GetPlayLength();
	const float PlaybackRate = Selection.PlayRate * Montage.RateScale;
	if (!FMath::IsFinite(MontageLength) || !FMath::IsFinite(PlaybackRate) || PlaybackRate <= 0.f)
	{
		return Reject(TEXT("Montage length or effective play rate is invalid"));
	}
	if (!Montage.bEnableAutoBlendOut || !FMath::IsFinite(Montage.BlendOutTriggerTime) ||
	    !FMath::IsFinite(Montage.BlendOut.GetBlendTime()) || Montage.BlendOut.GetBlendTime() < 0.f)
	{
		return Reject(
		    TEXT("Traversal needs automatic montage blend-out with finite timing to release movement"));
	}
	TSet<int32> VisitedSections;
	int32 SectionIndex = Montage.GetSectionIndexFromPosition(Selection.StartTime);
	float TerminalSectionStart = 0.f;
	while (Montage.IsValidSectionIndex(SectionIndex))
	{
		if (VisitedSections.Contains(SectionIndex))
		{
			return Reject(TEXT("Traversal needs a finite section path; the montage section chain loops"));
		}
		VisitedSections.Add(SectionIndex);
		float SectionStart = 0.f;
		float SectionEnd = 0.f;
		Montage.GetSectionStartAndEndTime(SectionIndex, SectionStart, SectionEnd);
		if (!FMath::IsFinite(SectionStart) || !FMath::IsFinite(SectionEnd) || SectionStart < 0.f ||
		    SectionEnd <= SectionStart || SectionEnd > MontageLength)
		{
			return Reject(TEXT("Montage section has an invalid time range"));
		}
		TerminalSectionStart = PlaybackEnd;
		// Adjacent sections form one extraction range. Jumps retain separate animation intervals.
		if (!PlaybackRanges.IsEmpty() && PlaybackRanges.Last().Y == SectionStart)
		{
			PlaybackRanges.Last().Y = SectionEnd;
		}
		else
		{
			PlaybackRanges.Emplace(SectionStart, SectionEnd);
			PlaybackOffsets.Add(PlaybackEnd);
		}
		if (bVerboseDiagnostics)
		{
			LogTraversalEvent(TEXT("PlaybackSection"),
			    FString::Printf(TEXT("Section=%s Range=%.6f..%.6f PlaybackOffset=%.6f Next=%s"),
			        *Montage.GetSectionName(SectionIndex).ToString(), SectionStart, SectionEnd, PlaybackEnd,
			        *Montage.CompositeSections[SectionIndex].NextSectionName.ToString()));
		}
		PlaybackEnd += SectionEnd - SectionStart;
		const FName NextSection = Montage.CompositeSections[SectionIndex].NextSectionName;
		if (NextSection.IsNone())
		{
			break;
		}
		SectionIndex = Montage.GetSectionIndex(NextSection);
		if (!Montage.IsValidSectionIndex(SectionIndex))
		{
			return Reject(TEXT("Montage section links to an unknown next section"));
		}
	}
	if (PlaybackRanges.IsEmpty() || !FMath::IsFinite(PlaybackEnd))
	{
		return Reject(TEXT("Montage start time has no valid section path"));
	}
	PlaybackStart = Selection.StartTime - PlaybackRanges[0].X;
	PlacementTime = PlaybackEnd;
	if (Selection.Montage->bEnableAutoBlendOut && Selection.Montage->BlendOutTriggerTime >= 0.f)
	{
		const float BlendStart = FMath::Max(FMath::Max(PlaybackStart, TerminalSectionStart),
		    PlaybackEnd - Selection.Montage->BlendOutTriggerTime * PlaybackRate);
		PlacementTime =
		    FMath::Min(PlaybackEnd, BlendStart + Selection.Montage->BlendOut.GetBlendTime() * PlaybackRate);
	}
	if (bVerboseDiagnostics)
	{
		LogTraversalEvent(TEXT("PlaybackPath"),
		    FString::Printf(TEXT("Montage=%s Length=%.6f StartTime=%.6f PlaybackStart=%.6f PlaybackEnd=%.6f "
		                         "PlacementTime=%.6f Ranges=%d"),
		        *GetNameSafe(Selection.Montage.Get()), MontageLength, Selection.StartTime, PlaybackStart,
		        PlaybackEnd, PlacementTime, PlaybackRanges.Num()));
	}
	for (const FIMGTraversalWarpWindow& Window : Windows)
	{
		if (!FMath::IsFinite(Window.StartTime) || !FMath::IsFinite(Window.EndTime) ||
		    Window.EndTime <= Window.StartTime || Window.StartTime < 0.f || Window.EndTime > MontageLength)
		{
			return Reject(TEXT("Warp window has an invalid time range"));
		}
		for (int32 RangeIndex = 0; RangeIndex < PlaybackRanges.Num(); ++RangeIndex)
		{
			const FVector2f& Range = PlaybackRanges[RangeIndex];
			const float EntryTime = RangeIndex == 0 ? Selection.StartTime : Range.X;
			if (Window.EndTime <= Range.X || Window.StartTime >= Range.Y)
			{
				continue;
			}
			if (Window.StartTime < Range.X || Window.EndTime > Range.Y)
			{
				if (Window.EndTime > EntryTime &&
				    PlaybackOffsets[RangeIndex] + FMath::Max(Window.StartTime - Range.X, 0.f) < PlacementTime)
				{
					return Reject(TEXT("Warp window crosses a discontinuous section boundary"));
				}
				continue;
			}
			WindowPlaybackTimes.Add(&Window,
			    FVector2f(PlaybackOffsets[RangeIndex] + (Window.StartTime - Range.X),
			        PlaybackOffsets[RangeIndex] + (Window.EndTime - Range.X)));
			ReachableWindows.Add(&Window);
			break;
		}
	}
	ReachableWindows.StableSort(
	    [this](const FIMGTraversalWarpWindow& A, const FIMGTraversalWarpWindow& B) {
		    return WindowPlaybackTimes.FindChecked(&A).X < WindowPlaybackTimes.FindChecked(&B).X;
	    });
	return true;
}

const FIMGTraversalWarpWindow* FIMGTraversalPlaybackPath::SelectTranslationWindow(
    TConstArrayView<FIMGTraversalWarpWindow> Windows, FName Name, int32 Index, float MinEnd, float MaxEnd) const
{
	if (Index >= 0)
	{
		const FIMGTraversalWarpWindow* Window =
		    IMGTraversalWarpWindows::FindTranslationWindow(Windows, Name, Index, -1.f, MontageLength);
		const FVector2f* Times = WindowPlaybackTimes.Find(Window);
		return Times && Times->Y > MinEnd && Times->Y <= MaxEnd ? Window : nullptr;
	}
	if (Name.IsNone() || Index != -1)
	{
		return nullptr;
	}
	const FIMGTraversalWarpWindow* Result = nullptr;
	float LastEnd = MinEnd;
	for (const FIMGTraversalWarpWindow& Window : Windows)
	{
		const FVector2f* Times = WindowPlaybackTimes.Find(&Window);
		if (Times && Window.Modifier && Window.Modifier->bWarpTranslation &&
		    Window.Modifier->WarpTargetName == Name && Times->Y > MinEnd && Times->Y <= MaxEnd &&
		    Times->Y >= LastEnd)
		{
			Result = &Window;
			LastEnd = Times->Y;
		}
	}
	return Result;
}
