

#include "Character/IMGPawnData.h"
#include "Camera/IMGCameraMode_Default.h"

UIMGPawnData::UIMGPawnData(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DefaultCameraMode = UIMGCameraMode_Default::StaticClass();
}
