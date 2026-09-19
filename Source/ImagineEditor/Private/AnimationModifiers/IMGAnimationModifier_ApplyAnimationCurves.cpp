#include "AnimationModifiers/IMGAnimationModifier_ApplyAnimationCurves.h"

#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGAnimationModifier_ApplyAnimationCurves)

namespace IMG::AnimationModifiers
{
	FName FindFloatCurveNameIgnoreCase(const UAnimSequence* Animation, const FName RequestedName)
	{
		const FString RequestedString = RequestedName.ToString();
		for (const FFloatCurve& Curve : Animation->GetDataModelInterface()->GetFloatCurves())
		{
			if (Curve.GetName().ToString().Equals(RequestedString, ESearchCase::IgnoreCase))
			{
				return Curve.GetName();
			}
		}

		return NAME_None;
	}
}

void UIMGAnimationModifier_ApplyAnimationCurves::OnApply_Implementation(UAnimSequence* Animation)
{
	if (Animation == nullptr)
	{
		return;
	}

	IAnimationDataController& Controller = Animation->GetController();

	for (const FIMGAnimationCurveDefinition& Curve : Curves)
	{
		if (Curve.CurveName.IsNone())
		{
			continue;
		}

		const FName ExistingCurveName = IMG::AnimationModifiers::FindFloatCurveNameIgnoreCase(Animation, Curve.CurveName);
		if (!ExistingCurveName.IsNone())
		{
			if (!bOverrideExistingCurves)
			{
				continue;
			}

			Controller.RemoveCurve(FAnimationCurveIdentifier(ExistingCurveName, ERawCurveTrackTypes::RCT_Float));
		}

		const FAnimationCurveIdentifier CurveId(Curve.CurveName, ERawCurveTrackTypes::RCT_Float);
		if (Controller.AddCurve(CurveId))
		{
			FRichCurveKey DefaultKey;
			DefaultKey.Time = 0.0f;
			DefaultKey.Value = Curve.DefaultValue;
			Controller.SetCurveKey(CurveId, DefaultKey);
		}
	}

	RemoveAfterApply(Animation);
}
