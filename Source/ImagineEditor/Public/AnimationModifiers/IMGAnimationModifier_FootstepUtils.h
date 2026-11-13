// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Curves/RichCurve.h"

class UAnimSequence;

namespace IMG::AnimationModifiers::Footstep
{
	struct FFootKinematicSample
	{
		float Time = 0.0f;
		FVector LeftPosition = FVector::ZeroVector;
		FVector RightPosition = FVector::ZeroVector;
		FVector RootPosition = FVector::ZeroVector;
		float LeftSpeedXY = 0.0f;
		float RightSpeedXY = 0.0f;
	};

	struct FFootKinematicData
	{
		TArray<FFootKinematicSample> Samples;
	};

	struct FFootPlantEvent
	{
		float Time = 0.0f;
		bool bIsLeftFoot = false;
	};

	bool BuildFootKinematicData(
		UAnimSequence* Animation,
		FName LeftFootBone,
		FName RightFootBone,
		int32 SampleRate,
		FFootKinematicData& OutData,
		FString& OutError);

	void BuildLocalFootGroundHeights(
		const TArray<FFootKinematicSample>& Samples,
		TArray<float>& OutLeftHeights,
		TArray<float>& OutRightHeights,
		TArray<float>& OutRootHeightRanges);

	void DetectFootPlantEvents(
		const FFootKinematicData& Data,
		float GroundHeightThreshold,
		float LiftSpeedThreshold,
		float PlantSpeedThreshold,
		bool bLoop,
		bool bEmitInitialContactEvents,
		TArray<FFootPlantEvent>& OutEvents);

	bool ReplaceFloatCurve(
		UAnimSequence* Animation,
		FName CurveName,
		const TArray<FRichCurveKey>& Keys,
		FString& OutError);

	void RemoveFloatCurve(UAnimSequence* Animation, FName CurveName);
}
