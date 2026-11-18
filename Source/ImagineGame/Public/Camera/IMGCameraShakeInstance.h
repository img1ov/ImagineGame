#pragma once

#include "Camera/CameraShakeBase.h"
#include "Engine/EngineTypes.h"
#include "IMGCameraShakeInstance.generated.h"

class UIMGCameraComponent;
class UIMGCameraShakeDefinition;
class UIMGCameraShakePattern;
class AActor;

/** Internal playback handle. Configuration belongs to a definition, never to an instance subclass. */
UCLASS(Transient, NotBlueprintable, BlueprintType, HideDropdown, HideCategories = (CameraShake))
class IMAGINEGAME_API UIMGCameraShakeInstance : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UIMGCameraShakeInstance(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintPure, Category = "Shake")
	bool IsContinuous() const { return bContinuous; }

	UFUNCTION(BlueprintPure, Category = "Shake")
	bool IsPlaying() const { return IsActive() && !IsFinished(); }

	UFUNCTION(BlueprintPure, Category = "Shake")
	bool IsReleasing() const;

	UIMGCameraShakePattern* GetIMGPattern() const;
	float GetSpatialScale(const FVector& CameraLocation, const FVector& TargetLocation) const;
	const UIMGCameraShakeDefinition* GetDefinition() const { return Definition; }

private:
	friend class UIMGCameraComponent;

	void BindSourceOwner(UObject* Source);
	void UnbindSourceOwner();

	UFUNCTION()
	void HandleSourceActorEndPlay(AActor* Actor, EEndPlayReason::Type EndPlayReason);

	UPROPERTY(Transient)
	TObjectPtr<const UIMGCameraShakeDefinition> Definition;

	TWeakObjectPtr<UObject> SourceOwner;
	TWeakObjectPtr<AActor> SourceActor;
	TOptional<FVector> SourceLocation;
	double StartTime = 0.0;
	uint64 StartFrame = MAX_uint64;
	bool bContinuous = false;
	bool bSourceEndedPlay = false;
	bool bSourceComponentHasBegunPlay = false;
	bool bHasUpdatedView = false;
};
