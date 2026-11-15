#pragma once

#include "Animation/AnimInstanceProxy.h"

struct FBlendedCurve;

struct IMAGINEGAME_API FIMGAnimInstanceProxy : public FAnimInstanceProxy
{
	explicit FIMGAnimInstanceProxy(UAnimInstance* Instance);

	/** Montage-owned float curves are absolute overrides, including on additive tracks. */
	void EvaluateSparseMontageCurves(FName SlotName, const FBlendedCurve& BaseCurve, FBlendedCurve& OutCurve) const;
};
