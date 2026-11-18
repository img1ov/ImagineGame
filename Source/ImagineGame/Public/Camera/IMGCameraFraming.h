#pragma once

#include "CoreMinimal.h"
#include "IMGCameraFraming.generated.h"

/** Distances are centimeters. Attachment, framing offsets, orbit and final aim are independent. */
USTRUCT(BlueprintType)
struct IMAGINEGAME_API FIMGCameraFramingSettings
{
	GENERATED_BODY()

	/** Positive places the camera behind the orbit pivot; zero and negative offsets are supported. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit", meta = (Units = "cm"))
	float OrbitDistance = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit", meta = (Units = "cm"))
	float ShoulderOffset = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pivot", meta = (Units = "cm"))
	FVector BasePivotLocalOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pivot", meta = (Units = "cm"))
	float BasePivotHeight = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pivot")
	bool bUseTargetUpForPivotHeight = false;

	/** Optional camera-up offset, distinct from the pivot's world-up height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit", meta = (Units = "cm"))
	float CameraVerticalOffset = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit", meta = (ClampMin = "-45", ClampMax = "45", Units = "deg"))
	float OrbitPitchOffset = 0.f;

	/** Applied only when collision displaces the camera away from its original collision axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim", meta = (ClampMin = "0", ClampMax = "1"))
	float PostCollisionLookAtWeight = 0.625f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim", meta = (ClampMin = "1", Units = "cm"))
	float MinFocusDistance = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim", meta = (ClampMin = "0", Units = "cm"))
	float OffAxisTolerance = 1.f;
};

/** Sampled facts only. The owner supplies target identity and lifecycle. */
struct IMAGINEGAME_API FIMGCameraFramingInput
{
	FVector Anchor = FVector::ZeroVector;
	FRotator OrbitRotation = FRotator::ZeroRotator;
	FRotator TargetRotation = FRotator::ZeroRotator;
	float MinPitch = -89.f;
	float MaxPitch = 89.f;
	TOptional<FVector> FocusPoint;
};

/** Construction geometry accompanies the raw view through blending and collision. */
struct IMAGINEGAME_API FIMGCameraFramingResult
{
	FVector BasePivot = FVector::ZeroVector;
	FVector OrbitPivot = FVector::ZeroVector;
	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	float PostCollisionLookAtWeight = 0.f;
	float MinFocusDistance = 50.f;
	float OffAxisTolerance = 1.f;
	float MinPitch = -89.f;
	float MaxPitch = 89.f;
	TOptional<FVector> FocusPoint;
	float FocusWeight = 0.f;
	bool bValid = false;

	void Blend(const FIMGCameraFramingResult& Other, float Weight);
};

namespace IMGCameraFraming
{
	IMAGINEGAME_API FIMGCameraFramingResult Build(const FIMGCameraFramingInput& Input, const FIMGCameraFramingSettings& Settings);
	IMAGINEGAME_API FRotator ResolveLookAt(const FIMGCameraFramingResult& Frame, const FVector& RawLocation,
		const FRotator& RawRotation, const FVector& CollisionRoot, const FVector& FinalLocation, const FRotator& FinalRotation);
}
