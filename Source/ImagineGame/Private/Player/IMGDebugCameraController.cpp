// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/IMGDebugCameraController.h"
#include "Player/IMGCheatManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGDebugCameraController)


AIMGDebugCameraController::AIMGDebugCameraController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Use the same cheat class as IMGPlayerController to allow toggling the debug camera through cheats.
	CheatClass = UIMGCheatManager::StaticClass();
}

void AIMGDebugCameraController::AddCheats(bool bForce)
{
	// Mirrors IMGPlayerController's AddCheats() to avoid the player becoming stuck in the debug camera.
#if USING_CHEAT_MANAGER
	Super::AddCheats(true);
#else //#if USING_CHEAT_MANAGER
	Super::AddCheats(bForce);
#endif // #else //#if USING_CHEAT_MANAGER
}
