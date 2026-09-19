#pragma once

#include "AnimationModifiers/IMGOneShotAnimationModifier.h"

#include "IMGAnimationModifier_RemoveAnimationCurves.generated.h"

/** Removes selected curves, or every float and transform curve, from an animation sequence. */
UCLASS(DisplayName = "AnimationModifier_RemoveAnimationCurves")
class UIMGAnimationModifier_RemoveAnimationCurves : public UIMGOneShotAnimationModifier
{
	GENERATED_BODY()

public:
	virtual void OnApply_Implementation(UAnimSequence* Animation) override;

private:
	UPROPERTY(EditAnywhere, Category = "Curves", meta = (DisplayName = "Remove All Curves"))
	bool bRemoveAllCurves = true;

	/** Curve names are matched without regard to letter case when Remove All Curves is disabled. */
	UPROPERTY(EditAnywhere, Category = "Curves", meta = (EditCondition = "!bRemoveAllCurves", EditConditionHides))
	TArray<FName> CurveNames;
};
