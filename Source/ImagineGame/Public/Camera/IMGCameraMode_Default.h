#pragma once

#include "Camera/IMGCameraMode_ThirdPerson.h"

#include "IMGCameraMode_Default.generated.h"

/** Default third person camera for pawns without an authored camera mode. */
UCLASS(Blueprintable)
class IMAGINEGAME_API UIMGCameraMode_Default : public UIMGCameraMode_ThirdPerson
{
	GENERATED_BODY()

public:
	UIMGCameraMode_Default();
};
