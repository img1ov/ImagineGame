#pragma once

#include "Camera/IMGCameraShake.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "IMGCameraShakeDefinition.generated.h"

UENUM()
enum class EIMGCameraShakeOverflow : uint8
{
	ReplaceOldest,
	ReplaceWeakest
};

/** Immutable presentation configuration. Sources may use an asset or own a transient copy of inline settings. */
UCLASS(BlueprintType)
class IMAGINEGAME_API UIMGCameraShakeDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Shake")
	FIMGCameraShakeSettings Settings;

	UPROPERTY(EditAnywhere, Category = "Shake")
	FGameplayTag ShakeCategory;

	UPROPERTY(EditAnywhere, Category = "Scheduling", meta = (ClampMin = "1"))
	int32 MaxInstances = 1;

	UPROPERTY(EditAnywhere, Category = "Scheduling", meta = (ClampMin = "0", Units = "s"))
	float MinTimeBetweenInstances = 0.f;

	UPROPERTY(EditAnywhere, Category = "Scheduling")
	EIMGCameraShakeOverflow Overflow = EIMGCameraShakeOverflow::ReplaceOldest;

	UPROPERTY(EditAnywhere, Category = "Spatial", meta = (ClampMin = "0", Units = "cm"))
	float InnerRadius = 500.f;

	UPROPERTY(EditAnywhere, Category = "Spatial", meta = (ClampMin = "0", Units = "cm"))
	float OuterRadius = 20000.f;

	/** Reduces angular shake when the camera is unusually far from its target. Zero disables compensation. */
	UPROPERTY(EditAnywhere, Category = "Spatial", meta = (ClampMin = "0", Units = "cm"))
	float IdealTargetDistance = 500.f;

	bool IsValidForPlayback(bool bContinuous, bool bSpatial) const;
};
