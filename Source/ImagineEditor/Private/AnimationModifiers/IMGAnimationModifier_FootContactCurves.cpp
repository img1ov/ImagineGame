#include "AnimationModifiers/IMGAnimationModifier_FootContactCurves.h"

#include "Animation/AnimSequence.h"
#include "IMGAnimationModifier_FootstepUtils.h"
#include "ImagineEditor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGAnimationModifier_FootContactCurves)

namespace IMG::AnimationModifiers::FootContactCurves
{
	struct FContactTransition
	{
		float Time = 0.0f;
		float FromValue = 0.0f;
		float ToValue = 0.0f;
	};

	static void DetectContactStates(
		const TArray<Footstep::FFootKinematicSample>& Samples,
		bool bLeftFoot,
		const TArray<float>& LocalGroundHeights,
		const TArray<float>& RootHeightRanges,
		float GroundHeightThreshold,
		float LiftSpeedThreshold,
		float PlantSpeedThreshold,
		TArray<bool>& OutStates)
	{
		OutStates.Reset(Samples.Num());
		bool bInContact = false;

		for (int32 SampleIndex = 0; SampleIndex < Samples.Num(); ++SampleIndex)
		{
			const Footstep::FFootKinematicSample& Sample = Samples[SampleIndex];
			const FVector& Position = bLeftFoot ? Sample.LeftPosition : Sample.RightPosition;
			const float SpeedXY = bLeftFoot ? Sample.LeftSpeedXY : Sample.RightSpeedXY;
			const bool bNearGround = static_cast<float>(Position.Z - Sample.RootPosition.Z)
				- LocalGroundHeights[SampleIndex]
				- RootHeightRanges[SampleIndex]
				<= GroundHeightThreshold;

			if (bInContact)
			{
				bInContact = bNearGround && SpeedXY < LiftSpeedThreshold;
			}
			else
			{
				bInContact = bNearGround && SpeedXY <= PlantSpeedThreshold;
			}

			OutStates.Add(bInContact);
		}
	}

	static void BuildTransitions(
		const TArray<Footstep::FFootKinematicSample>& Samples,
		const TArray<bool>& States,
		bool bLoop,
		TArray<FContactTransition>& OutTransitions)
	{
		OutTransitions.Reset();
		if (Samples.IsEmpty() || States.Num() != Samples.Num())
		{
			return;
		}

		if (bLoop && States.Last() != States[0])
		{
			OutTransitions.Add(FContactTransition{
				0.0f, States.Last() ? 1.0f : 0.0f, States[0] ? 1.0f : 0.0f });
		}

		for (int32 SampleIndex = 1; SampleIndex < Samples.Num(); ++SampleIndex)
		{
			if (States[SampleIndex - 1] != States[SampleIndex])
			{
				OutTransitions.Add(FContactTransition{
					Samples[SampleIndex].Time,
					States[SampleIndex - 1] ? 1.0f : 0.0f,
					States[SampleIndex] ? 1.0f : 0.0f });
			}
		}
	}

	static float EvaluateSmoothedContact(
		float Time,
		float BaseValue,
		float SequenceLength,
		bool bLoop,
		float BlendTime,
		const TArray<FContactTransition>& Transitions)
	{
		if (BlendTime <= UE_SMALL_NUMBER)
		{
			return BaseValue;
		}

		const float HalfBlend = BlendTime * 0.5f;
		float NearestDistance = TNumericLimits<float>::Max();
		const FContactTransition* NearestTransition = nullptr;
		float NearestTransitionTime = 0.0f;

		for (const FContactTransition& Transition : Transitions)
		{
			const int32 FirstOffset = bLoop ? -1 : 0;
			const int32 LastOffset = bLoop ? 1 : 0;
			for (int32 Offset = FirstOffset; Offset <= LastOffset; ++Offset)
			{
				const float CandidateTime = Transition.Time + static_cast<float>(Offset) * SequenceLength;
				const float Distance = FMath::Abs(Time - CandidateTime);
				if (Distance <= HalfBlend && Distance < NearestDistance)
				{
					NearestDistance = Distance;
					NearestTransition = &Transition;
					NearestTransitionTime = CandidateTime;
				}
			}
		}

		if (NearestTransition == nullptr)
		{
			return BaseValue;
		}

		const float Alpha = FMath::Clamp(
			(Time - (NearestTransitionTime - HalfBlend)) / BlendTime, 0.0f, 1.0f);
		const float SmoothAlpha = Alpha * Alpha * (3.0f - 2.0f * Alpha);
		return FMath::Lerp(NearestTransition->FromValue, NearestTransition->ToValue, SmoothAlpha);
	}

	static void BuildContactKeys(
		const TArray<Footstep::FFootKinematicSample>& Samples,
		const TArray<bool>& States,
		float SequenceLength,
		bool bLoop,
		float BlendTime,
		TArray<FRichCurveKey>& OutKeys)
	{
		OutKeys.Reset(Samples.Num());
		TArray<FContactTransition> Transitions;
		BuildTransitions(Samples, States, bLoop, Transitions);

		for (int32 SampleIndex = 0; SampleIndex < Samples.Num(); ++SampleIndex)
		{
			const float Value = EvaluateSmoothedContact(
				Samples[SampleIndex].Time,
				States[SampleIndex] ? 1.0f : 0.0f,
				SequenceLength,
				bLoop,
				BlendTime,
				Transitions);

			FRichCurveKey& Key = OutKeys.Emplace_GetRef(Samples[SampleIndex].Time, Value);
			Key.InterpMode = RCIM_Linear;
		}
	}
}

void UIMGAnimationModifier_FootContactCurves::OnApply_Implementation(UAnimSequence* Animation)
{
	Super::OnApply_Implementation(Animation);

	using namespace IMG::AnimationModifiers;

	if (!IsValid(Animation))
	{
		UE_LOG(LogImagineEditor, Error, TEXT("IMG Foot Contact modifier failed: animation is invalid."));
		return;
	}

	if (LeftContactCurveName.IsNone() || RightContactCurveName.IsNone()
		|| LeftContactCurveName == RightContactCurveName)
	{
		UE_LOG(LogImagineEditor, Error,
			TEXT("IMG Foot Contact modifier skipped '%s': contact curve names must be set and unique."),
			*Animation->GetPathName());
		return;
	}

	if (GroundHeightThreshold < 0.0f || PlantSpeedThreshold < 0.0f || LiftSpeedThreshold < 0.0f
		|| PlantSpeedThreshold > LiftSpeedThreshold)
	{
		UE_LOG(LogImagineEditor, Error,
			TEXT("IMG Foot Contact modifier skipped '%s': detection thresholds must be non-negative and PlantSpeedThreshold cannot exceed LiftSpeedThreshold."),
			*Animation->GetPathName());
		return;
	}

	TGuardValue<bool> ForceRootLockGuard(Animation->bForceRootLock, false);

	Footstep::FFootKinematicData KinematicData;
	FString Error;
	if (!Footstep::BuildFootKinematicData(
		Animation, LeftDetectionBone, RightDetectionBone, SampleRate, KinematicData, Error))
	{
		UE_LOG(LogImagineEditor, Error, TEXT("IMG Foot Contact modifier skipped '%s': %s"),
			*Animation->GetPathName(), *Error);
		return;
	}

	TArray<bool> LeftStates;
	TArray<bool> RightStates;
	TArray<float> LeftLocalGroundHeights;
	TArray<float> RightLocalGroundHeights;
	TArray<float> RootHeightRanges;
	Footstep::BuildLocalFootGroundHeights(
		KinematicData.Samples, LeftLocalGroundHeights, RightLocalGroundHeights, RootHeightRanges);
	FootContactCurves::DetectContactStates(
		KinematicData.Samples, true, LeftLocalGroundHeights, RootHeightRanges,
		GroundHeightThreshold, LiftSpeedThreshold, PlantSpeedThreshold, LeftStates);
	FootContactCurves::DetectContactStates(
		KinematicData.Samples, false, RightLocalGroundHeights, RootHeightRanges,
		GroundHeightThreshold, LiftSpeedThreshold, PlantSpeedThreshold, RightStates);

	TArray<FRichCurveKey> LeftKeys;
	TArray<FRichCurveKey> RightKeys;
	FootContactCurves::BuildContactKeys(
		KinematicData.Samples, LeftStates, Animation->GetPlayLength(), Animation->bLoop,
		TransitionBlendTime, LeftKeys);
	FootContactCurves::BuildContactKeys(
		KinematicData.Samples, RightStates, Animation->GetPlayLength(), Animation->bLoop,
		TransitionBlendTime, RightKeys);

	if (!Footstep::ReplaceFloatCurve(Animation, LeftContactCurveName, LeftKeys, Error))
	{
		UE_LOG(LogImagineEditor, Error, TEXT("IMG Foot Contact modifier failed to write '%s' on '%s': %s"),
			*LeftContactCurveName.ToString(), *Animation->GetPathName(), *Error);
		return;
	}

	if (!Footstep::ReplaceFloatCurve(Animation, RightContactCurveName, RightKeys, Error))
	{
		UE_LOG(LogImagineEditor, Error, TEXT("IMG Foot Contact modifier failed to write '%s' on '%s': %s"),
			*RightContactCurveName.ToString(), *Animation->GetPathName(), *Error);
		return;
	}

	UE_LOG(LogImagineEditor, Display,
		TEXT("IMG Foot Contact modifier applied to '%s': %s=%d keys, %s=%d keys, Loop=%s."),
		*Animation->GetPathName(),
		*LeftContactCurveName.ToString(), LeftKeys.Num(),
		*RightContactCurveName.ToString(), RightKeys.Num(),
		Animation->bLoop ? TEXT("true") : TEXT("false"));
}

void UIMGAnimationModifier_FootContactCurves::OnRevert_Implementation(UAnimSequence* Animation)
{
	Super::OnRevert_Implementation(Animation);

	if (!IsValid(Animation))
	{
		return;
	}

	IMG::AnimationModifiers::Footstep::RemoveFloatCurve(Animation, LeftContactCurveName);
	IMG::AnimationModifiers::Footstep::RemoveFloatCurve(Animation, RightContactCurveName);
	UE_LOG(LogImagineEditor, Display, TEXT("IMG Foot Contact modifier reverted from '%s'."), *Animation->GetPathName());
}
