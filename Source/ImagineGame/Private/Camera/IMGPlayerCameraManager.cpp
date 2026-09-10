// Copyright Epic Games, Inc. All Rights Reserved.

#include "Camera/IMGPlayerCameraManager.h"

#include "Async/TaskGraphInterfaces.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Camera/IMGCameraComponent.h"
#include "Camera/IMGUICameraManagerComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGPlayerCameraManager)

class FDebugDisplayInfo;

static FName UICameraComponentName(TEXT("UICamera"));

AIMGPlayerCameraManager::AIMGPlayerCameraManager(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DefaultFOV = IMG_CAMERA_DEFAULT_FOV;
	ViewPitchMin = IMG_CAMERA_DEFAULT_PITCH_MIN;
	ViewPitchMax = IMG_CAMERA_DEFAULT_PITCH_MAX;

	UICamera = CreateDefaultSubobject<UIMGUICameraManagerComponent>(UICameraComponentName);
}

UIMGUICameraManagerComponent* AIMGPlayerCameraManager::GetUICameraComponent() const
{
	return UICamera;
}

void AIMGPlayerCameraManager::UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime)
{
	// If the UI Camera is looking at something, let it have priority.
	if (UICamera->NeedsToUpdateViewTarget())
	{
		Super::UpdateViewTarget(OutVT, DeltaTime);
		UICamera->UpdateViewTarget(OutVT, DeltaTime);
		return;
	}

	Super::UpdateViewTarget(OutVT, DeltaTime);
}

void AIMGPlayerCameraManager::DisplayDebug(UCanvas* Canvas, const FDebugDisplayInfo& DebugDisplay, float& YL, float& YPos)
{
	check(Canvas);

	FDisplayDebugManager& DisplayDebugManager = Canvas->DisplayDebugManager;

	DisplayDebugManager.SetFont(GEngine->GetSmallFont());
	DisplayDebugManager.SetDrawColor(FColor::Yellow);
	DisplayDebugManager.DrawString(FString::Printf(TEXT("IMGPlayerCameraManager: %s"), *GetNameSafe(this)));

	Super::DisplayDebug(Canvas, DebugDisplay, YL, YPos);

	const APawn* Pawn = (PCOwner ? PCOwner->GetPawn() : nullptr);

	if (const UIMGCameraComponent* CameraComponent = UIMGCameraComponent::FindCameraComponent(Pawn))
	{
		CameraComponent->DrawDebug(Canvas);
	}
}
