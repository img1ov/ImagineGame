#pragma once

#include "AnimationModifiers/IMGOneShotAnimationModifier.h"

#include "IMGAnimationModifier_ApplyAnimationCurves.generated.h"

USTRUCT(BlueprintType)
struct FIMGAnimationCurveDefinition
{
	GENERATED_BODY()

	/** Curve names are matched without regard to letter case. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curve")
	FName CurveName;

	/** Value written as a key at time zero. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curve")
	float DefaultValue = 0.0f;
};

/** Adds constant-initialized float curves to an animation sequence. */
UCLASS(DisplayName = "AnimationModifier_ApplyAnimationCurves")
class UIMGAnimationModifier_ApplyAnimationCurves : public UIMGOneShotAnimationModifier
{
	GENERATED_BODY()

public:
	virtual void OnApply_Implementation(UAnimSequence* Animation) override;

private:
	UPROPERTY(EditAnywhere, Category = "Curves", meta = (TitleProperty = "CurveName"))
	TArray<FIMGAnimationCurveDefinition> Curves;

	/** Replace an existing curve and all of its keys when its name matches. */
	UPROPERTY(EditAnywhere, Category = "Curves", meta = (DisplayName = "Override"))
	bool bOverrideExistingCurves = true;
};
