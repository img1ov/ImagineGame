
#pragma once

#include "Camera/PlayerCameraManager.h"

#include "IMGPlayerCameraManager.generated.h"

class FDebugDisplayInfo;
class UCanvas;
class UObject;


#define IMG_CAMERA_DEFAULT_FOV			(80.0f)
#define IMG_CAMERA_DEFAULT_PITCH_MIN	(-89.0f)
#define IMG_CAMERA_DEFAULT_PITCH_MAX	(89.0f)

class UIMGUICameraManagerComponent;

/**
 * AIMGPlayerCameraManager
 *
 *	The base player camera manager class used by this project.
 */
UCLASS(notplaceable)
class IMAGINEGAME_API AIMGPlayerCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()

public:

	AIMGPlayerCameraManager(const FObjectInitializer& ObjectInitializer);

	UIMGUICameraManagerComponent* GetUICameraComponent() const;

protected:

	virtual void DisplayDebug(UCanvas* Canvas, const FDebugDisplayInfo& DebugDisplay, float& YL, float& YPos) override;

private:
	/** Explicit UI requests are forwarded through the native view-target transition path. */
	UPROPERTY(Transient)
	TObjectPtr<UIMGUICameraManagerComponent> UICamera;
};
