#include "Camera/IMGCameraMode_Default.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGCameraMode_Default)

UIMGCameraMode_Default::UIMGCameraMode_Default()
{
	bUseRuntimeFloatCurves = true;
	TargetOffsetX.GetRichCurve()->AddKey(0.0f, -300.0f);
	TargetOffsetY.GetRichCurve()->AddKey(0.0f, 0.0f);
	TargetOffsetZ.GetRichCurve()->AddKey(0.0f, -50.f);
}
