// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Camera/PlayerCameraManager.h"

#include "IMGUICameraManagerComponent.generated.h"

class AIMGPlayerCameraManager;

class AActor;
class AHUD;
class APlayerController;
class FDebugDisplayInfo;
class UCanvas;
class UObject;

UCLASS( Transient, Within=IMGPlayerCameraManager )
class IMAGINEGAME_API UIMGUICameraManagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	static UIMGUICameraManagerComponent* GetComponent(APlayerController* PC);

public:
	UIMGUICameraManagerComponent();	
	virtual void InitializeComponent() override;

	bool IsSettingViewTarget() const { return bUpdatingViewTarget; }
	AActor* GetViewTarget() const { return ViewTarget; }
	void SetViewTarget(AActor* InViewTarget, FViewTargetTransitionParams TransitionParams = FViewTargetTransitionParams());

	bool NeedsToUpdateViewTarget() const;
	void UpdateViewTarget(struct FTViewTarget& OutVT, float DeltaTime);

	void OnShowDebugInfo(AHUD* HUD, UCanvas* Canvas, const FDebugDisplayInfo& DisplayInfo, float& YL, float& YPos);

private:
	UPROPERTY(Transient)
	TObjectPtr<AActor> ViewTarget;
	
	UPROPERTY(Transient)
	bool bUpdatingViewTarget;
};
