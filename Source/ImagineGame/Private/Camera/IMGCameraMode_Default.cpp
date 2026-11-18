#include "Camera/IMGCameraMode_Default.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGCameraMode_Default)

UIMGCameraMode_Default::UIMGCameraMode_Default()
{
	BlendTime = 0.25f;
	BlendFunction = EIMGCameraModeBlendFunction::EaseInOut;
	BlendExponent = 2.f;

	const float DefaultPitches[] = { ViewPitchMin, 0.f, ViewPitchMax };
	for (const float Pitch : DefaultPitches)
	{
		TargetOffsetY.EditorCurveData.UpdateOrAddKey(Pitch, 0.f);
		TargetOffsetZ.EditorCurveData.UpdateOrAddKey(Pitch, -50.f);
	}
	TargetOffsetY.EditorCurveData.AutoSetTangents();
	TargetOffsetZ.EditorCurveData.AutoSetTangents();
}
