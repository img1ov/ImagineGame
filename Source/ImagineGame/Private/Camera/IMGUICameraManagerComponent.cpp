
#include "Camera/IMGUICameraManagerComponent.h"

#include "GameFramework/PlayerController.h"
#include "Camera/IMGPlayerCameraManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGUICameraManagerComponent)

UIMGUICameraManagerComponent* UIMGUICameraManagerComponent::GetComponent(APlayerController* PC)
{
	if (IsValid(PC))
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
	bWantsInitializeComponent = false;
}

void UIMGUICameraManagerComponent::SetViewTarget(AActor* InViewTarget, FViewTargetTransitionParams TransitionParams)
{
	AIMGPlayerCameraManager* Manager = Cast<AIMGPlayerCameraManager>(GetOwner());
	if (!IsValid(Manager) || Manager->IsActorBeingDestroyed()) { return; }
	TGuardValue<bool> UpdatingViewTargetGuard(bUpdatingViewTarget, true);

	ViewTarget = IsValid(InViewTarget) ? InViewTarget : nullptr;
	Manager->SetViewTarget(ViewTarget.Get(), TransitionParams);
}
