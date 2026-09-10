// Copyright Epic Games, Inc. All Rights Reserved.

#include "Camera/IMGUICameraManagerComponent.h"

#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "Camera/IMGPlayerCameraManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGUICameraManagerComponent)

class AActor;
class FDebugDisplayInfo;

UIMGUICameraManagerComponent* UIMGUICameraManagerComponent::GetComponent(APlayerController* PC)
{
	if (PC != nullptr)
	{
		if (AIMGPlayerCameraManager* PCCamera = Cast<AIMGPlayerCameraManager>(PC->PlayerCameraManager))
		{
			return PCCamera->GetUICameraComponent();
		}
	}

	return nullptr;
}

UIMGUICameraManagerComponent::UIMGUICameraManagerComponent()
{
	bWantsInitializeComponent = true;

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		// Register "showdebug" hook.
		if (!IsRunningDedicatedServer())
		{
			AHUD::OnShowDebugInfo.AddUObject(this, &ThisClass::OnShowDebugInfo);
		}
	}
}

void UIMGUICameraManagerComponent::InitializeComponent()
{
	Super::InitializeComponent();
}

void UIMGUICameraManagerComponent::SetViewTarget(AActor* InViewTarget, FViewTargetTransitionParams TransitionParams)
{
	TGuardValue<bool> UpdatingViewTargetGuard(bUpdatingViewTarget, true);

	ViewTarget = InViewTarget;
	CastChecked<AIMGPlayerCameraManager>(GetOwner())->SetViewTarget(ViewTarget, TransitionParams);
}

bool UIMGUICameraManagerComponent::NeedsToUpdateViewTarget() const
{
	return false;
}

void UIMGUICameraManagerComponent::UpdateViewTarget(struct FTViewTarget& OutVT, float DeltaTime)
{
}

void UIMGUICameraManagerComponent::OnShowDebugInfo(AHUD* HUD, UCanvas* Canvas, const FDebugDisplayInfo& DisplayInfo, float& YL, float& YPos)
{
}
