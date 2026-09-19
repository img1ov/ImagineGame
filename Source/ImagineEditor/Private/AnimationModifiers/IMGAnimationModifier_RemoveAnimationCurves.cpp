#include "AnimationModifiers/IMGAnimationModifier_RemoveAnimationCurves.h"

#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGAnimationModifier_RemoveAnimationCurves)

namespace IMG::AnimationModifiers
{
	bool MatchesAnyCurveName(const FName ExistingName, const TArray<FName>& RequestedNames)
	{
		const FString ExistingString = ExistingName.ToString();
		return RequestedNames.ContainsByPredicate([&ExistingString](const FName RequestedName)
		{
			return !RequestedName.IsNone()
				&& ExistingString.Equals(RequestedName.ToString(), ESearchCase::IgnoreCase);
		});
	}
}

void UIMGAnimationModifier_RemoveAnimationCurves::OnApply_Implementation(UAnimSequence* Animation)
{
	if (Animation == nullptr)
	{
		return;
	}

	IAnimationDataController& Controller = Animation->GetController();
	if (bRemoveAllCurves)
	{
		Controller.RemoveAllCurvesOfType(ERawCurveTrackTypes::RCT_Float);
		Controller.RemoveAllCurvesOfType(ERawCurveTrackTypes::RCT_Transform);
	}
	else if (!CurveNames.IsEmpty())
	{
		TArray<FAnimationCurveIdentifier> CurvesToRemove;
		const TScriptInterface<IAnimationDataModel> DataModel = Animation->GetDataModelInterface();

		for (const FFloatCurve& Curve : DataModel->GetFloatCurves())
		{
			if (IMG::AnimationModifiers::MatchesAnyCurveName(Curve.GetName(), CurveNames))
			{
				CurvesToRemove.Emplace(Curve.GetName(), ERawCurveTrackTypes::RCT_Float);
			}
		}

		for (const FTransformCurve& Curve : DataModel->GetTransformCurves())
		{
			if (IMG::AnimationModifiers::MatchesAnyCurveName(Curve.GetName(), CurveNames))
			{
				CurvesToRemove.Emplace(Curve.GetName(), ERawCurveTrackTypes::RCT_Transform);
			}
		}

		for (const FAnimationCurveIdentifier& CurveId : CurvesToRemove)
		{
			Controller.RemoveCurve(CurveId);
		}
	}

	RemoveAfterApply(Animation);
}
