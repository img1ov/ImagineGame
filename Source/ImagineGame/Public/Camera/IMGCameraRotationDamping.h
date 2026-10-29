#pragma once

#include "CoreMinimal.h"

#include "IMGCameraRotationDamping.generated.h"

USTRUCT(BlueprintType)
struct IMAGINEGAME_API FIMGCameraRotationDampingAxisSettings
{
	GENERATED_BODY()

	/** Zero bypasses damping on this axis. Frequencies are in Hz, not spring stiffness. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rotation Damping", meta = (ClampMin = "0", ClampMax = "30", ForceUnits = "Hz"))
	float AngleFrequency = 2.f;

	/** One is critical damping; lower values allow overshoot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rotation Damping", meta = (ClampMin = "0", ClampMax = "10"))
	float AngleDampingRatio = 1.f;

	/** Zero uses the target angular velocity directly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rotation Damping", meta = (ClampMin = "0", ClampMax = "30", ForceUnits = "Hz"))
	float AngularVelocityFrequency = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rotation Damping", meta = (ClampMin = "0", ClampMax = "10"))
	float AngularVelocityDampingRatio = 1.f;

	/** Maximum angular error as a fraction of half this axis's FOV; zero follows the target immediately. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rotation Damping", meta = (ClampMin = "0", ClampMax = "1"))
	float MaxErrorFraction = 0.6f;

	bool IsValid() const;
};

USTRUCT(BlueprintType)
struct IMAGINEGAME_API FIMGCameraRotationDampingSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rotation Damping")
	FIMGCameraRotationDampingAxisSettings Yaw;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rotation Damping")
	FIMGCameraRotationDampingAxisSettings Pitch;
};

/** Caller-owned numeric history. Each camera instance needs its own state. */
struct IMAGINEGAME_API FIMGCameraRotationDampingState
{
	void Reset();

	/**
	 * FOV is (horizontal, vertical) in degrees. Roll passes through; invalid targets return zero and clear history.
	 * Invalid configuration/time or a gap longer than 32/120 seconds clears history and follows the target.
	 */
	FRotator Update(const FRotator& TargetRotation, const FVector2D& FieldOfViewDegrees, float DeltaTime,
		const FIMGCameraRotationDampingSettings& Settings);

private:
	FVector2D PreviousTargetAngles = FVector2D::ZeroVector;
	FVector2D SmoothedAngles = FVector2D::ZeroVector;
	FVector2D AngleRates = FVector2D::ZeroVector;
	FVector2D SmoothedTargetRates = FVector2D::ZeroVector;
	FVector2D TargetRateDerivatives = FVector2D::ZeroVector;
	bool bInitialized = false;
	bool bHasTargetRates = false;
};
