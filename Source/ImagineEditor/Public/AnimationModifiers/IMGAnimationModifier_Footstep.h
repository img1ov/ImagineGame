// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimationModifier.h"
#include "Engine/EngineTypes.h"
#include "GameplayTagContainer.h"
#include "IMGAnimationModifier_Footstep.generated.h"

/**
 * Generates the complete IMG footstep authoring output:
 * IMG context-effect notifies plus the Pose Search Phase curve.
 *
 * The notify track and Phase curve configured below are owned by this modifier and
 * are replaced each time it is applied.
 */
UCLASS(BlueprintType, meta = (DisplayName = "AnimationModifier_Footstep"))
class UIMGAnimationModifier_Footstep : public UAnimationModifier
{
	GENERATED_BODY()

public:
	UIMGAnimationModifier_Footstep();

	virtual void OnApply_Implementation(UAnimSequence* Animation) override;
	virtual void OnRevert_Implementation(UAnimSequence* Animation) override;

	/** Explicit locomotion event sent to the IMG Context Effects pipeline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep", meta = (Categories = "Foley.Event"))
	FGameplayTag EffectTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection", meta = (ClampMin = "1", UIMin = "1"))
	int32 SampleRate = 80;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection")
	FName LeftDetectionBone = TEXT("ball_l");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection")
	FName RightDetectionBone = TEXT("ball_r");

	/** Maximum height above the locally estimated foot ground height that counts as planted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection", meta = (ClampMin = "0.0", Units = "cm"))
	float GroundHeightThreshold = 4.0f;

	/** A foot is re-armed after leaving the ground or reaching this XY speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection", meta = (ClampMin = "0.0", Units = "cm/s"))
	float LiftSpeedThreshold = 200.0f;

	/** After lifting, falling below this XY speed emits the plant event. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection", meta = (ClampMin = "0.0", Units = "cm/s"))
	float PlantSpeedThreshold = 110.0f;

	/** For non-looping clips, emit an event at time zero when a foot starts planted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection")
	bool bEmitInitialContactEvents = true;

	/** Generated track owned by this modifier. Existing events on it are replaced. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Effect")
	FName NotifyTrackName = TEXT("Footstep");

	/** Old authoring tracks removed when this modifier is applied. Missing tracks are ignored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cleanup", meta = (AdvancedDisplay))
	TArray<FName> LegacyNotifyTracksToRemove;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Effect")
	FName LeftEffectSocket = TEXT("foot_l");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Effect")
	FName RightEffectSocket = TEXT("foot_r");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Effect|Trace")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECollisionChannel::ECC_Visibility;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Effect|Trace")
	FVector TraceEndOffset = FVector(0.0f, 0.0f, -50.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Effect|VFX")
	FVector VFXScale = FVector::OneVector;

	/** Pose Search phase: left plant = LeftPhaseValue, right plant = RightPhaseValue. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phase")
	FName PhaseCurveName = TEXT("Phase");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phase")
	float LeftPhaseValue = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phase")
	float RightPhaseValue = -1.0f;
};
