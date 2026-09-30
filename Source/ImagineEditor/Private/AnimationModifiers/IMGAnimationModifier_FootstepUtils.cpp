// Copyright Epic Games, Inc. All Rights Reserved.

#include "IMGAnimationModifier_FootstepUtils.h"

#include "Animation/AnimData/CurveIdentifier.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AnimationBlueprintLibrary.h"
#include "AnimPose.h"

namespace IMG::AnimationModifiers::Footstep
{
	bool BuildFootKinematicData(
		UAnimSequence* Animation,
		FName LeftFootBone,
		FName RightFootBone,
		int32 SampleRate,
		FFootKinematicData& OutData,
		FString& OutError)
	{
		OutData = FFootKinematicData();
		OutError.Reset();

		if (!IsValid(Animation))
		{
			OutError = TEXT("Animation is invalid.");
			return false;
		}

		const USkeleton* Skeleton = Animation->GetSkeleton();
		if (!IsValid(Skeleton))
		{
			OutError = TEXT("Animation has no skeleton.");
			return false;
		}

		if (LeftFootBone.IsNone() || RightFootBone.IsNone())
		{
			OutError = TEXT("Left and right detection bone names must be set.");
			return false;
		}

		const FReferenceSkeleton& ReferenceSkeleton = Skeleton->GetReferenceSkeleton();
		if (ReferenceSkeleton.FindBoneIndex(LeftFootBone) == INDEX_NONE
			|| ReferenceSkeleton.FindBoneIndex(RightFootBone) == INDEX_NONE)
		{
			OutError = FString::Printf(
				TEXT("Detection bones are missing from skeleton '%s' (left='%s', right='%s')."),
				*Skeleton->GetName(), *LeftFootBone.ToString(), *RightFootBone.ToString());
			return false;
		}

		if (SampleRate <= 0)
		{
			OutError = TEXT("Sample rate must be greater than zero.");
			return false;
		}

		const float SequenceLength = Animation->GetPlayLength();
		if (SequenceLength <= UE_SMALL_NUMBER)
		{
			OutError = TEXT("Animation has zero duration.");
			return false;
		}

		const float SampleStep = 1.0f / static_cast<float>(SampleRate);
		const FName RootBoneName = ReferenceSkeleton.GetBoneName(0);
		const int32 SampleCount = FMath::Max(2, FMath::CeilToInt(SequenceLength / SampleStep) + 1);
		OutData.Samples.Reserve(SampleCount);

		FVector PreviousLeftPosition = FVector::ZeroVector;
		FVector PreviousRightPosition = FVector::ZeroVector;
		float PreviousTime = 0.0f;

		// Keep the authored root translation in component-space bone positions.
		const FAnimPoseEvaluationOptions EvaluationOptions{
			EAnimDataEvalType::Raw, true, false, true, nullptr, true, false };

		for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
		{
			const float SampleTime = SampleIndex == SampleCount - 1
				? SequenceLength
				: FMath::Min(static_cast<float>(SampleIndex) * SampleStep, SequenceLength);

			FAnimPose Pose;
			UAnimPoseExtensions::GetAnimPoseAtTime(Animation, SampleTime, EvaluationOptions, Pose);

			FFootKinematicSample& Sample = OutData.Samples.AddDefaulted_GetRef();
			Sample.Time = SampleTime;
			Sample.LeftPosition = UAnimPoseExtensions::GetBonePose(Pose, LeftFootBone, EAnimPoseSpaces::World).GetLocation();
			Sample.RightPosition = UAnimPoseExtensions::GetBonePose(Pose, RightFootBone, EAnimPoseSpaces::World).GetLocation();
			Sample.RootPosition = UAnimPoseExtensions::GetBonePose(Pose, RootBoneName, EAnimPoseSpaces::World).GetLocation();

			if (SampleIndex > 0)
			{
				const float DeltaTime = SampleTime - PreviousTime;
				if (DeltaTime > UE_SMALL_NUMBER)
				{
					Sample.LeftSpeedXY = static_cast<float>((Sample.LeftPosition - PreviousLeftPosition).Size2D()) / DeltaTime;
					Sample.RightSpeedXY = static_cast<float>((Sample.RightPosition - PreviousRightPosition).Size2D()) / DeltaTime;
				}
			}

			PreviousLeftPosition = Sample.LeftPosition;
			PreviousRightPosition = Sample.RightPosition;
			PreviousTime = SampleTime;
		}

		if (OutData.Samples.Num() > 1)
		{
			OutData.Samples[0].LeftSpeedXY = OutData.Samples[1].LeftSpeedXY;
			OutData.Samples[0].RightSpeedXY = OutData.Samples[1].RightSpeedXY;
		}

		return true;
	}

	void BuildLocalFootGroundHeights(
		const TArray<FFootKinematicSample>& Samples,
		TArray<float>& OutLeftHeights,
		TArray<float>& OutRightHeights,
		TArray<float>& OutRootHeightRanges)
	{
		// Compare each foot in the root's frame. Widen the band by the root's Z
		// travel in the window: a world-stationary planted foot changes its
		// root-relative height by exactly that amount on a slope.
		constexpr float GroundWindowSeconds = 0.25f;
		OutLeftHeights.Reset(Samples.Num());
		OutRightHeights.Reset(Samples.Num());
		OutRootHeightRanges.Reset(Samples.Num());
		for (int32 SampleIndex = 0; SampleIndex < Samples.Num(); ++SampleIndex)
		{
			float LeftMinimum = TNumericLimits<float>::Max();
			float RightMinimum = TNumericLimits<float>::Max();
			float RootMinimum = TNumericLimits<float>::Max();
			float RootMaximum = TNumericLimits<float>::Lowest();
			for (int32 NearbyIndex = SampleIndex; NearbyIndex >= 0
				&& Samples[SampleIndex].Time - Samples[NearbyIndex].Time <= GroundWindowSeconds; --NearbyIndex)
			{
				const FFootKinematicSample& Nearby = Samples[NearbyIndex];
				LeftMinimum = FMath::Min(LeftMinimum, static_cast<float>(Nearby.LeftPosition.Z - Nearby.RootPosition.Z));
				RightMinimum = FMath::Min(RightMinimum, static_cast<float>(Nearby.RightPosition.Z - Nearby.RootPosition.Z));
				RootMinimum = FMath::Min(RootMinimum, static_cast<float>(Nearby.RootPosition.Z));
				RootMaximum = FMath::Max(RootMaximum, static_cast<float>(Nearby.RootPosition.Z));
			}
			for (int32 NearbyIndex = SampleIndex + 1; NearbyIndex < Samples.Num()
				&& Samples[NearbyIndex].Time - Samples[SampleIndex].Time <= GroundWindowSeconds; ++NearbyIndex)
			{
				const FFootKinematicSample& Nearby = Samples[NearbyIndex];
				LeftMinimum = FMath::Min(LeftMinimum, static_cast<float>(Nearby.LeftPosition.Z - Nearby.RootPosition.Z));
				RightMinimum = FMath::Min(RightMinimum, static_cast<float>(Nearby.RightPosition.Z - Nearby.RootPosition.Z));
				RootMinimum = FMath::Min(RootMinimum, static_cast<float>(Nearby.RootPosition.Z));
				RootMaximum = FMath::Max(RootMaximum, static_cast<float>(Nearby.RootPosition.Z));
			}
			OutLeftHeights.Add(LeftMinimum);
			OutRightHeights.Add(RightMinimum);
			OutRootHeightRanges.Add(RootMaximum - RootMinimum);
		}
	}

	void DetectFootPlantEvents(
		const FFootKinematicData& Data,
		float GroundHeightThreshold,
		float LiftSpeedThreshold,
		float PlantSpeedThreshold,
		bool bLoop,
		bool bEmitInitialContactEvents,
		TArray<FFootPlantEvent>& OutEvents)
	{
		OutEvents.Reset();
		if (Data.Samples.IsEmpty())
		{
			return;
		}

		TArray<float> LeftLocalGroundHeights;
		TArray<float> RightLocalGroundHeights;
		TArray<float> RootHeightRanges;
		BuildLocalFootGroundHeights(Data.Samples, LeftLocalGroundHeights, RightLocalGroundHeights, RootHeightRanges);

		auto DetectFoot = [&Data, &LeftLocalGroundHeights, &RightLocalGroundHeights, &RootHeightRanges,
			GroundHeightThreshold, LiftSpeedThreshold, PlantSpeedThreshold,
			bLoop, bEmitInitialContactEvents, &OutEvents](bool bIsLeftFoot)
		{
			auto GetHeightAboveGround = [&Data, &LeftLocalGroundHeights, &RightLocalGroundHeights, &RootHeightRanges, bIsLeftFoot](int32 SampleIndex)
			{
				const FFootKinematicSample& Sample = Data.Samples[SampleIndex];
				const FVector& Position = bIsLeftFoot ? Sample.LeftPosition : Sample.RightPosition;
				const float LocalGroundHeight = bIsLeftFoot ? LeftLocalGroundHeights[SampleIndex] : RightLocalGroundHeights[SampleIndex];
				return static_cast<float>(Position.Z - Sample.RootPosition.Z) - LocalGroundHeight
					- RootHeightRanges[SampleIndex];
			};

			auto GetSpeed = [bIsLeftFoot](const FFootKinematicSample& Sample)
			{
				return bIsLeftFoot ? Sample.LeftSpeedXY : Sample.RightSpeedXY;
			};

			auto IsPlantCandidate = [&](int32 SampleIndex)
			{
				return GetHeightAboveGround(SampleIndex) <= GroundHeightThreshold
					&& GetSpeed(Data.Samples[SampleIndex]) <= PlantSpeedThreshold;
			};

			auto ShouldArmPlant = [&](int32 SampleIndex)
			{
				return GetHeightAboveGround(SampleIndex) > GroundHeightThreshold
					|| GetSpeed(Data.Samples[SampleIndex]) >= LiftSpeedThreshold;
			};

			auto EstimatePlantTime = [&](int32 PreviousIndex, int32 CurrentIndex)
			{
				const FFootKinematicSample& Previous = Data.Samples[PreviousIndex];
				const FFootKinematicSample& Current = Data.Samples[CurrentIndex];
				float Alpha = 0.0f;
				const float HeightLimit = GroundHeightThreshold;
				const float PreviousHeight = GetHeightAboveGround(PreviousIndex);
				const float CurrentHeight = GetHeightAboveGround(CurrentIndex);
				if (PreviousHeight > HeightLimit && CurrentHeight <= HeightLimit)
				{
					const float HeightDelta = PreviousHeight - CurrentHeight;
					if (HeightDelta > UE_SMALL_NUMBER)
					{
						Alpha = FMath::Max(Alpha, (PreviousHeight - HeightLimit) / HeightDelta);
					}
				}

				const float PreviousSpeed = GetSpeed(Previous);
				const float CurrentSpeed = GetSpeed(Current);
				if (PreviousSpeed > PlantSpeedThreshold && CurrentSpeed <= PlantSpeedThreshold)
				{
					const float SpeedDelta = PreviousSpeed - CurrentSpeed;
					if (SpeedDelta > UE_SMALL_NUMBER)
					{
						Alpha = FMath::Max(Alpha, (PreviousSpeed - PlantSpeedThreshold) / SpeedDelta);
					}
				}

				return FMath::Lerp(Previous.Time, Current.Time, FMath::Clamp(Alpha, 0.0f, 1.0f));
			};

			const int32 SampleCount = Data.Samples.Num();
			const int32 LastSampleExclusive = bLoop && SampleCount > 1 ? SampleCount - 1 : SampleCount;
			bool bPlantArmed = false;

			if (bLoop && SampleCount > 2)
			{
				// The final sample duplicates time zero for a loop; the penultimate sample is the real predecessor.
				bPlantArmed = ShouldArmPlant(SampleCount - 2);
				if (bPlantArmed && IsPlantCandidate(0))
				{
					OutEvents.Add(FFootPlantEvent{ 0.0f, bIsLeftFoot });
					bPlantArmed = false;
				}
			}
			else if (bEmitInitialContactEvents && IsPlantCandidate(0))
			{
				OutEvents.Add(FFootPlantEvent{ 0.0f, bIsLeftFoot });
			}

			if (ShouldArmPlant(0))
			{
				bPlantArmed = true;
			}

			for (int32 SampleIndex = 1; SampleIndex < LastSampleExclusive; ++SampleIndex)
			{
				if (bPlantArmed && IsPlantCandidate(SampleIndex))
				{
					OutEvents.Add(FFootPlantEvent{
						EstimatePlantTime(SampleIndex - 1, SampleIndex), bIsLeftFoot });
					bPlantArmed = false;
				}
				else if (ShouldArmPlant(SampleIndex))
				{
					bPlantArmed = true;
				}
			}
		};

		DetectFoot(true);
		DetectFoot(false);

		OutEvents.Sort([](const FFootPlantEvent& A, const FFootPlantEvent& B)
		{
			return A.Time != B.Time ? A.Time < B.Time : A.bIsLeftFoot && !B.bIsLeftFoot;
		});
	}

	bool ReplaceFloatCurve(
		UAnimSequence* Animation,
		FName CurveName,
		const TArray<FRichCurveKey>& Keys,
		FString& OutError)
	{
		OutError.Reset();
		if (!IsValid(Animation) || CurveName.IsNone())
		{
			OutError = TEXT("Animation and curve name must be valid.");
			return false;
		}

		RemoveFloatCurve(Animation, CurveName);

		IAnimationDataController& Controller = Animation->GetController();
		const FAnimationCurveIdentifier CurveId(CurveName, ERawCurveTrackTypes::RCT_Float);
		if (!Controller.AddCurve(CurveId))
		{
			OutError = FString::Printf(TEXT("Could not add curve '%s'."), *CurveName.ToString());
			return false;
		}

		if (!Controller.SetCurveKeys(CurveId, Keys))
		{
			OutError = FString::Printf(TEXT("Could not set keys for curve '%s'."), *CurveName.ToString());
			return false;
		}

		return true;
	}

	void RemoveFloatCurve(UAnimSequence* Animation, FName CurveName)
	{
		if (IsValid(Animation) && !CurveName.IsNone()
			&& UAnimationBlueprintLibrary::DoesCurveExist(Animation, CurveName, ERawCurveTrackTypes::RCT_Float))
		{
			UAnimationBlueprintLibrary::RemoveCurve(Animation, CurveName, false);
		}
	}
}
