// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimationModifiers/IMGAnimationModifier_Footstep.h"

#include "Animation/AnimSequence.h"
#include "AnimationBlueprintLibrary.h"
#include "IMGAnimationModifier_FootstepUtils.h"
#include "ImagineEditor.h"
#include "Feedback/ContextEffects/AnimNotify_IMGContextEffects.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGAnimationModifier_Footstep)

namespace IMG::AnimationModifiers::Footstep
{
	static float GetPhaseValue(const FFootPlantEvent& Event, float LeftValue, float RightValue)
	{
		return Event.bIsLeftFoot ? LeftValue : RightValue;
	}

	static int32 BuildAlternatingEvents(const TArray<FFootPlantEvent>& Source, TArray<FFootPlantEvent>& OutEvents)
	{
		OutEvents.Reset();
		int32 IgnoredCoincidentEventCount = 0;

		for (int32 SourceIndex = 0; SourceIndex < Source.Num();)
		{
			const int32 GroupStartIndex = SourceIndex;
			const float GroupTime = Source[GroupStartIndex].Time;
			bool bHasLeftEvent = false;
			bool bHasRightEvent = false;
			while (SourceIndex < Source.Num()
				&& FMath::IsNearlyEqual(Source[SourceIndex].Time, GroupTime, UE_KINDA_SMALL_NUMBER))
			{
				bHasLeftEvent |= Source[SourceIndex].bIsLeftFoot;
				bHasRightEvent |= !Source[SourceIndex].bIsLeftFoot;
				++SourceIndex;
			}

			bool bUseLeftFoot = bHasLeftEvent;
			if (bHasLeftEvent && bHasRightEvent)
			{
				++IgnoredCoincidentEventCount;
				if (!OutEvents.IsEmpty())
				{
					// At a two-foot boundary, continue the established alternating cadence.
					bUseLeftFoot = !OutEvents.Last().bIsLeftFoot;
				}
				else
				{
					// A non-looping Start commonly begins with both feet inside the contact
					// thresholds. Choose the foot opposite the next unambiguous plant so the
					// first interval is usable instead of producing two keys at time zero.
					for (int32 LookAheadIndex = SourceIndex; LookAheadIndex < Source.Num();)
					{
						const float LookAheadTime = Source[LookAheadIndex].Time;
						bool bLookAheadHasLeft = false;
						bool bLookAheadHasRight = false;
						while (LookAheadIndex < Source.Num()
							&& FMath::IsNearlyEqual(Source[LookAheadIndex].Time, LookAheadTime, UE_KINDA_SMALL_NUMBER))
						{
							bLookAheadHasLeft |= Source[LookAheadIndex].bIsLeftFoot;
							bLookAheadHasRight |= !Source[LookAheadIndex].bIsLeftFoot;
							++LookAheadIndex;
						}

						if (bLookAheadHasLeft != bLookAheadHasRight)
						{
							bUseLeftFoot = !bLookAheadHasLeft;
							break;
						}
					}
				}
			}

			const FFootPlantEvent PhaseEvent{ GroupTime, bUseLeftFoot };
			if (OutEvents.IsEmpty() || OutEvents.Last().bIsLeftFoot != PhaseEvent.bIsLeftFoot)
			{
				OutEvents.Add(PhaseEvent);
			}
			else
			{
				// Keep the latest duplicate plant for this foot; Phase requires alternating feet.
				OutEvents.Last() = PhaseEvent;
			}
		}

		return IgnoredCoincidentEventCount;
	}

	static bool BuildPhaseKeys(
		const TArray<FFootPlantEvent>& PlantEvents,
		float SequenceLength,
		bool bLoop,
		float LeftValue,
		float RightValue,
		TArray<FRichCurveKey>& OutKeys)
	{
		OutKeys.Reset();

		TArray<FFootPlantEvent> AlternatingEvents;
		BuildAlternatingEvents(PlantEvents, AlternatingEvents);
		if (AlternatingEvents.Num() < 2)
		{
			OutKeys.Emplace(0.0f, 0.0f);
			OutKeys.Emplace(SequenceLength, 0.0f);
			for (FRichCurveKey& Key : OutKeys)
			{
				Key.InterpMode = RCIM_Linear;
			}
			return false;
		}

		TArray<FFootPlantEvent> PhaseEvents = AlternatingEvents;
		float StartValue = GetPhaseValue(PhaseEvents[0], LeftValue, RightValue);
		float EndValue = GetPhaseValue(PhaseEvents.Last(), LeftValue, RightValue);
		if (bLoop)
		{
			const FFootPlantEvent& LastEvent = PhaseEvents.Last();
			const FFootPlantEvent& FirstEvent = PhaseEvents[0];
			const float WrappedDuration = SequenceLength - LastEvent.Time + FirstEvent.Time;
			if (WrappedDuration > UE_SMALL_NUMBER)
			{
				const float TimeFromLastEventToBoundary = SequenceLength - LastEvent.Time;
				if (LastEvent.bIsLeftFoot != FirstEvent.bIsLeftFoot)
				{
					const float AlphaAtStart = TimeFromLastEventToBoundary / WrappedDuration;
					StartValue = FMath::Lerp(
						GetPhaseValue(LastEvent, LeftValue, RightValue),
						GetPhaseValue(FirstEvent, LeftValue, RightValue),
						AlphaAtStart);
				}
				else
				{
					// An odd/missed event count would otherwise make the loop boundary flat.
					// Insert an implicit opposite-foot extremum halfway across the wrapped gap.
					const float HalfWrappedDuration = WrappedDuration * 0.5f;
					const float SameFootValue = GetPhaseValue(LastEvent, LeftValue, RightValue);
					const float OppositeFootValue = LastEvent.bIsLeftFoot ? RightValue : LeftValue;
					float VirtualEventTime = LastEvent.Time + HalfWrappedDuration;
					if (TimeFromLastEventToBoundary <= HalfWrappedDuration)
					{
						StartValue = FMath::Lerp(
							SameFootValue,
							OppositeFootValue,
							TimeFromLastEventToBoundary / HalfWrappedDuration);
					}
					else
					{
						StartValue = FMath::Lerp(
							OppositeFootValue,
							SameFootValue,
							(TimeFromLastEventToBoundary - HalfWrappedDuration) / HalfWrappedDuration);
					}

					if (VirtualEventTime < SequenceLength)
					{
						PhaseEvents.Add(FFootPlantEvent{ VirtualEventTime, !LastEvent.bIsLeftFoot });
					}
					else
					{
						VirtualEventTime -= SequenceLength;
						PhaseEvents.Insert(FFootPlantEvent{ VirtualEventTime, !LastEvent.bIsLeftFoot }, 0);
					}
				}
				EndValue = StartValue;
			}
		}
		else
		{
			// Extend the alternating cadence beyond both boundaries with virtual plants.
			// This produces a bounded triangle wave without inventing extra sound notifies.
			const FFootPlantEvent& FirstEvent = PhaseEvents[0];
			const FFootPlantEvent& SecondEvent = PhaseEvents[1];
			const float FirstInterval = SecondEvent.Time - FirstEvent.Time;
			const FFootPlantEvent& PreviousEvent = PhaseEvents[PhaseEvents.Num() - 2];
			const FFootPlantEvent& LastEvent = PhaseEvents.Last();
			const float LastInterval = LastEvent.Time - PreviousEvent.Time;
			if (FirstInterval <= UE_SMALL_NUMBER || LastInterval <= UE_SMALL_NUMBER)
			{
				OutKeys.Emplace_GetRef(0.0f, 0.0f).InterpMode = RCIM_Linear;
				OutKeys.Emplace_GetRef(SequenceLength, 0.0f).InterpMode = RCIM_Linear;
				return false;
			}

			while (PhaseEvents[0].Time > 0.0f)
			{
				PhaseEvents.Insert(FFootPlantEvent{
					PhaseEvents[0].Time - FirstInterval,
					!PhaseEvents[0].bIsLeftFoot }, 0);
			}

			while (PhaseEvents.Last().Time < SequenceLength)
			{
				PhaseEvents.Add(FFootPlantEvent{
					PhaseEvents.Last().Time + LastInterval,
					!PhaseEvents.Last().bIsLeftFoot });
			}

			auto EvaluatePhaseAtTime = [LeftValue, RightValue](
				const TArray<FFootPlantEvent>& Events, float Time)
			{
				for (int32 EventIndex = 0; EventIndex < Events.Num(); ++EventIndex)
				{
					if (Events[EventIndex].Time >= Time)
					{
						if (EventIndex == 0 || FMath::IsNearlyEqual(Events[EventIndex].Time, Time))
						{
							return GetPhaseValue(Events[EventIndex], LeftValue, RightValue);
						}

						const FFootPlantEvent& Before = Events[EventIndex - 1];
						const FFootPlantEvent& After = Events[EventIndex];
						const float Alpha = (Time - Before.Time) / (After.Time - Before.Time);
						return FMath::Lerp(
							GetPhaseValue(Before, LeftValue, RightValue),
							GetPhaseValue(After, LeftValue, RightValue),
							Alpha);
					}
				}

				return GetPhaseValue(Events.Last(), LeftValue, RightValue);
			};

			StartValue = EvaluatePhaseAtTime(PhaseEvents, 0.0f);
			EndValue = EvaluatePhaseAtTime(PhaseEvents, SequenceLength);
		}

		OutKeys.Emplace(0.0f, StartValue);
		for (const FFootPlantEvent& Event : PhaseEvents)
		{
			if (Event.Time > UE_SMALL_NUMBER && Event.Time < SequenceLength - UE_SMALL_NUMBER)
			{
				OutKeys.Emplace(Event.Time, GetPhaseValue(Event, LeftValue, RightValue));
			}
		}
		OutKeys.Emplace(SequenceLength, EndValue);

		for (FRichCurveKey& Key : OutKeys)
		{
			Key.InterpMode = RCIM_Linear;
		}
		return true;
	}

	static void ResetOwnedNotifyTrack(UAnimSequence* Animation, FName TrackName)
	{
		if (UAnimationBlueprintLibrary::IsValidAnimNotifyTrackName(Animation, TrackName))
		{
			UAnimationBlueprintLibrary::RemoveAnimationNotifyTrack(Animation, TrackName);
		}
		UAnimationBlueprintLibrary::AddAnimationNotifyTrack(Animation, TrackName);
	}

	static int32 RemoveStaleFootstepContextEffectNotifies(
		UAnimSequence* Animation,
		const FGameplayTag& FootstepRoot)
	{
		const int32 RemovedCount = Animation->Notifies.RemoveAll(
			[&FootstepRoot](const FAnimNotifyEvent& Event)
			{
				const UAnimNotify_IMGContextEffects* ContextEffectNotify =
					Cast<UAnimNotify_IMGContextEffects>(Event.Notify);
				return IsValid(ContextEffectNotify)
					&& ContextEffectNotify->Effect.MatchesTag(FootstepRoot);
			});

		if (RemovedCount > 0)
		{
			Animation->RefreshCacheData();
		}
		return RemovedCount;
	}
}

UIMGAnimationModifier_Footstep::UIMGAnimationModifier_Footstep()
{
	LegacyNotifyTracksToRemove = {
		TEXT("Footstep Left"),
		TEXT("Footstep Right")
	};
}

void UIMGAnimationModifier_Footstep::OnApply_Implementation(UAnimSequence* Animation)
{
	Super::OnApply_Implementation(Animation);

	using namespace IMG::AnimationModifiers::Footstep;

	if (!IsValid(Animation))
	{
		UE_LOG(LogImagineEditor, Error, TEXT("IMG Footstep modifier failed: animation is invalid."));
		return;
	}

	const FGameplayTag FootstepRoot = FGameplayTag::RequestGameplayTag(TEXT("Foley.Event"), false);
	if (!EffectTag.IsValid() || !FootstepRoot.IsValid() || !EffectTag.MatchesTag(FootstepRoot))
	{
		UE_LOG(LogImagineEditor, Error,
			TEXT("IMG Footstep modifier skipped '%s': EffectTag must be a child of Foley.Event."),
			*Animation->GetPathName());
		return;
	}

	if (NotifyTrackName.IsNone() || PhaseCurveName.IsNone()
		|| LeftEffectSocket.IsNone() || RightEffectSocket.IsNone())
	{
		UE_LOG(LogImagineEditor, Error,
			TEXT("IMG Footstep modifier skipped '%s': track, phase curve, and socket names must be set."),
			*Animation->GetPathName());
		return;
	}

	if (GroundHeightThreshold < 0.0f || PlantSpeedThreshold < 0.0f || LiftSpeedThreshold < 0.0f
		|| PlantSpeedThreshold > LiftSpeedThreshold)
	{
		UE_LOG(LogImagineEditor, Error,
			TEXT("IMG Footstep modifier skipped '%s': detection thresholds must be non-negative and PlantSpeedThreshold cannot exceed LiftSpeedThreshold."),
			*Animation->GetPathName());
		return;
	}

	TGuardValue<bool> ForceRootLockGuard(Animation->bForceRootLock, false);

	FFootKinematicData KinematicData;
	FString Error;
	if (!BuildFootKinematicData(
		Animation, LeftDetectionBone, RightDetectionBone, SampleRate, KinematicData, Error))
	{
		UE_LOG(LogImagineEditor, Error, TEXT("IMG Footstep modifier skipped '%s': %s"),
			*Animation->GetPathName(), *Error);
		return;
	}

	TArray<FFootPlantEvent> PlantEvents;
	DetectFootPlantEvents(
		KinematicData,
		GroundHeightThreshold,
		LiftSpeedThreshold,
		PlantSpeedThreshold,
		Animation->bLoop,
		bEmitInitialContactEvents,
		PlantEvents);

	const int32 DetectedPlantCount = PlantEvents.Num();
	TArray<FFootPlantEvent> NormalizedPlantEvents;
	const int32 IgnoredCoincidentPlantCount =
		BuildAlternatingEvents(PlantEvents, NormalizedPlantEvents);
	PlantEvents = MoveTemp(NormalizedPlantEvents);

	// This modifier owns all IMG footstep context-effect notifies on the animation.
	// Remove older generated/manual copies even when they were left on another track;
	// unrelated notify types (for example PoseSearchBlockTransition) are preserved.
	const int32 RemovedStaleNotifyCount =
		RemoveStaleFootstepContextEffectNotifies(Animation, FootstepRoot);

	int32 RemovedLegacyTrackCount = 0;
	for (const FName LegacyTrackName : LegacyNotifyTracksToRemove)
	{
		if (!LegacyTrackName.IsNone()
			&& LegacyTrackName != NotifyTrackName
			&& UAnimationBlueprintLibrary::IsValidAnimNotifyTrackName(Animation, LegacyTrackName))
		{
			UAnimationBlueprintLibrary::RemoveAnimationNotifyTrack(Animation, LegacyTrackName);
			++RemovedLegacyTrackCount;
		}
	}

	ResetOwnedNotifyTrack(Animation, NotifyTrackName);
	for (const FFootPlantEvent& Event : PlantEvents)
	{
		UAnimNotify_IMGContextEffects* Notify = NewObject<UAnimNotify_IMGContextEffects>(
			Animation, NAME_None, RF_Transactional);

		FIMGContextEffectAnimNotifyVFXSettings VFXSettings;
		VFXSettings.Scale = VFXScale;
		FIMGContextEffectAnimNotifyAudioSettings AudioSettings;
		FIMGContextEffectAnimNotifyTraceSettings TraceSettings;
		TraceSettings.TraceChannel = TraceChannel;
		TraceSettings.EndTraceLocationOffset = TraceEndOffset;
		TraceSettings.bIgnoreActor = true;

		Notify->SetParameters(
			EffectTag,
			FVector::ZeroVector,
			FRotator::ZeroRotator,
			VFXSettings,
			AudioSettings,
			true,
			Event.bIsLeftFoot ? LeftEffectSocket : RightEffectSocket,
			true,
			TraceSettings);

		UAnimationBlueprintLibrary::AddAnimationNotifyEventObject(
			Animation, Event.Time, Notify, NotifyTrackName);
	}

	TArray<FRichCurveKey> PhaseKeys;
	const bool bHasUsablePhase = BuildPhaseKeys(
		PlantEvents,
		Animation->GetPlayLength(),
		Animation->bLoop,
		LeftPhaseValue,
		RightPhaseValue,
		PhaseKeys);

	if (!ReplaceFloatCurve(Animation, PhaseCurveName, PhaseKeys, Error))
	{
		UE_LOG(LogImagineEditor, Error, TEXT("IMG Footstep modifier failed to write '%s' on '%s': %s"),
			*PhaseCurveName.ToString(), *Animation->GetPathName(), *Error);
		return;
	}

	if (!bHasUsablePhase)
	{
		UE_LOG(LogImagineEditor, Warning,
			TEXT("IMG Footstep modifier wrote a zero '%s' fallback on '%s': fewer than two alternating plants were detected."),
			*PhaseCurveName.ToString(), *Animation->GetPathName());
	}

	int32 InitialContactCount = 0;
	for (const FFootPlantEvent& Event : PlantEvents)
	{
		if (Event.Time <= UE_SMALL_NUMBER)
		{
			++InitialContactCount;
		}
	}

	UE_LOG(LogImagineEditor, Display,
		TEXT("IMG Footstep modifier applied to '%s': EffectTag=%s, DetectedPlants=%d, Notifies=%d, InitialContacts=%d, IgnoredCoincidentPlants=%d, PhaseKeys=%d, RemovedStaleNotifies=%d, RemovedLegacyTracks=%d, Loop=%s."),
		*Animation->GetPathName(), *EffectTag.ToString(), DetectedPlantCount, PlantEvents.Num(), InitialContactCount,
		IgnoredCoincidentPlantCount, PhaseKeys.Num(), RemovedStaleNotifyCount, RemovedLegacyTrackCount,
		Animation->bLoop ? TEXT("true") : TEXT("false"));
}

void UIMGAnimationModifier_Footstep::OnRevert_Implementation(UAnimSequence* Animation)
{
	Super::OnRevert_Implementation(Animation);

	if (!IsValid(Animation))
	{
		return;
	}

	if (!NotifyTrackName.IsNone()
		&& UAnimationBlueprintLibrary::IsValidAnimNotifyTrackName(Animation, NotifyTrackName))
	{
		UAnimationBlueprintLibrary::RemoveAnimationNotifyTrack(Animation, NotifyTrackName);
	}

	IMG::AnimationModifiers::Footstep::RemoveFloatCurve(Animation, PhaseCurveName);
	UE_LOG(LogImagineEditor, Display, TEXT("IMG Footstep modifier reverted from '%s'."), *Animation->GetPathName());
}
