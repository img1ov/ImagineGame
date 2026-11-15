#include "Animation/IMGAnimInstanceProxy.h"

#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimMontageEvaluationState.h"
#include "BoneContainer.h"

FIMGAnimInstanceProxy::FIMGAnimInstanceProxy(UAnimInstance* Instance)
	: FAnimInstanceProxy(Instance)
{
}

void FIMGAnimInstanceProxy::EvaluateSparseMontageCurves(FName SlotName, const FBlendedCurve& BaseCurve, FBlendedCurve& OutCurve) const
{
	check(&BaseCurve != &OutCurve);
	OutCurve = BaseCurve;
	if (SlotName.IsNone() || !GetRequiredBones().IsValid())
	{
		return;
	}

	const TArray<FMontageEvaluationState>& EvaluationData = GetMontageEvaluationData();
	const auto IsUsableState = [SlotName](const FMontageEvaluationState& State)
	{
		const UAnimMontage* Montage = State.Montage.Get();
		const float Weight = State.BlendInfo.GetBlendedValue();
		return Montage && Montage->IsValidSlot(SlotName) && FMath::IsFinite(State.MontagePosition)
			&& FMath::IsFinite(Weight) && Weight > ZERO_ANIMWEIGHT_THRESH;
	};

	double TotalWeight = 0.0;
	for (const FMontageEvaluationState& State : EvaluationData)
	{
		if (IsUsableState(State))
		{
			TotalWeight += State.BlendInfo.GetBlendedValue();
		}
	}
	if (TotalWeight <= ZERO_ANIMWEIGHT_THRESH)
	{
		return;
	}

	const double WeightScale = 1.0 / FMath::Max(1.0, TotalWeight);
	for (const FMontageEvaluationState& State : EvaluationData)
	{
		if (!IsUsableState(State))
		{
			continue;
		}

		// Stopped instances still contribute throughout their blend out.
		const UAnimMontage* Montage = State.Montage.Get();
		const double Weight = State.BlendInfo.GetBlendedValue() * WeightScale;
		const FAnimExtractContext ExtractContext(static_cast<double>(State.MontagePosition), false, State.DeltaTimeRecord);
		FBlendedCurve MontageCurve;
		MontageCurve.InitFrom(GetRequiredBones());
		Montage->EvaluateCurveData(MontageCurve, ExtractContext);

		MontageCurve.ForEachElement([&BaseCurve, &OutCurve, Weight](const UE::Anim::FCurveElement& Curve)
		{
			if (Curve.Name.IsNone() || !FMath::IsFinite(Curve.Value))
			{
				return;
			}

			// Always subtract the original base, so crossfades do not depend on evaluation order.
			const double Value = OutCurve.Get(Curve.Name) + Weight * (static_cast<double>(Curve.Value) - BaseCurve.Get(Curve.Name));
			const float BlendedValue = static_cast<float>(Value);
			if (FMath::IsFinite(BlendedValue))
			{
				OutCurve.Set(Curve.Name, BlendedValue);
				OutCurve.SetFlags(Curve.Name, OutCurve.GetFlags(Curve.Name) | Curve.Flags);
			}
		});
	}
}
