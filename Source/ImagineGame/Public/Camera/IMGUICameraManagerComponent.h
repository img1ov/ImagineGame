
#pragma once

#include "Camera/PlayerCameraManager.h"

#include "IMGUICameraManagerComponent.generated.h"

class AIMGPlayerCameraManager;

class AActor;
class APlayerController;

UCLASS( Transient, Within=IMGPlayerCameraManager )
class IMAGINEGAME_API UIMGUICameraManagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	static UIMGUICameraManagerComponent* GetComponent(APlayerController* PC);

public:
	UIMGUICameraManagerComponent();

	bool IsSettingViewTarget() const { return bUpdatingViewTarget; }
	AActor* GetViewTarget() const { return ViewTarget.Get(); }
	void SetViewTarget(AActor* InViewTarget, FViewTargetTransitionParams TransitionParams = FViewTargetTransitionParams());

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> ViewTarget;

	UPROPERTY(Transient)
	bool bUpdatingViewTarget = false;
};
