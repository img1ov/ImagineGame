#pragma once

#include "AnimationModifier.h"

#include "IMGAnimationModifier_FootContactCurves.generated.h"

/**
 * Generates independent left/right planted-state curves for foot IK.
 * Curve value 1 means planted; 0 means swinging.
 */
UCLASS(BlueprintType, meta = (DisplayName = "AnimationModifier_FootContactCurves"))
class UIMGAnimationModifier_FootContactCurves : public UAnimationModifier
{
	GENERATED_BODY()

public:
	virtual void OnApply_Implementation(UAnimSequence* Animation) override;
	virtual void OnRevert_Implementation(UAnimSequence* Animation) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curves")
	FName LeftContactCurveName = TEXT("contact_l");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curves")
	FName RightContactCurveName = TEXT("contact_r");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection", meta = (ClampMin = "1", UIMin = "1"))
	int32 SampleRate = 80;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection")
	FName LeftDetectionBone = TEXT("ball_l");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection")
	FName RightDetectionBone = TEXT("ball_r");

	/** Maximum height above the locally estimated foot ground height that counts as contact. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection", meta = (ClampMin = "0.0", Units = "cm"))
	float GroundHeightThreshold = 4.0f;

	/** A planted foot leaves contact when its XY speed reaches this value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection", meta = (ClampMin = "0.0", Units = "cm/s"))
	float LiftSpeedThreshold = 200.0f;

	/** A swinging foot enters contact when it is near the ground and below this XY speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection", meta = (ClampMin = "0.0", Units = "cm/s"))
	float PlantSpeedThreshold = 110.0f;

	/** Time centered on each contact transition over which values blend between 0 and 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curves", meta = (ClampMin = "0.0", Units = "s"))
	float TransitionBlendTime = 0.1f;
};
