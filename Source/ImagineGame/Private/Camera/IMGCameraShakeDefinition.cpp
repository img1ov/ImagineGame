#include "Camera/IMGCameraShakeDefinition.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGCameraShakeDefinition)

bool UIMGCameraShakeDefinition::IsValidForPlayback(bool bContinuous, bool bSpatial) const
{
	FIMGCameraShakeSettings Candidate = Settings;
	if (bContinuous) { Candidate.MakeContinuous(); }
	const double Duration = Candidate.GetDuration();
	return Candidate.IsValid() && (bContinuous ? Duration != 0.0 : Duration > 0.0) && MaxInstances > 0
		&& FMath::IsFinite(MinTimeBetweenInstances) && MinTimeBetweenInstances >= 0.f
		&& (Overflow == EIMGCameraShakeOverflow::ReplaceOldest || Overflow == EIMGCameraShakeOverflow::ReplaceWeakest)
		&& (!bSpatial || (FMath::IsFinite(InnerRadius) && InnerRadius >= 0.f
			&& FMath::IsFinite(OuterRadius) && OuterRadius > InnerRadius
			&& FMath::IsFinite(IdealTargetDistance) && IdealTargetDistance >= 0.f));
}
