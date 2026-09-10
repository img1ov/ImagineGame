// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DebugCameraController.h"

#include "IMGDebugCameraController.generated.h"

class UObject;


/**
 * AIMGDebugCameraController
 *
 *	Used for controlling the debug camera when it is enabled via the cheat manager.
 */
UCLASS()
class AIMGDebugCameraController : public ADebugCameraController
{
	GENERATED_BODY()

public:

	AIMGDebugCameraController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:

	virtual void AddCheats(bool bForce) override;
};
