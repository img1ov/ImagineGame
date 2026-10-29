#include "Camera/IMGCameraRotationDamping.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGCameraRotationDamping)

namespace
{
constexpr float MaxStepSeconds = 1.f / 120.f;
constexpr int32 MaxSubsteps = 32;

void ClampAngleError(double& Angle, double& Rate, double Target, double MaxError)
{
	const double Error = FMath::FindDeltaAngleDegrees(Target, Angle);
	const double ClampedError = FMath::Clamp(Error, -MaxError, MaxError);
	Angle = Target + ClampedError;
	if (Error != ClampedError)
	{
		Rate = 0.0;
	}
}
}

bool FIMGCameraRotationDampingAxisSettings::IsValid() const
{
	return FMath::IsFinite(AngleFrequency) && AngleFrequency >= 0.f && AngleFrequency <= 30.f
		&& FMath::IsFinite(AngleDampingRatio) && AngleDampingRatio >= 0.f && AngleDampingRatio <= 10.f
		&& FMath::IsFinite(AngularVelocityFrequency) && AngularVelocityFrequency >= 0.f && AngularVelocityFrequency <= 30.f
		&& FMath::IsFinite(AngularVelocityDampingRatio) && AngularVelocityDampingRatio >= 0.f && AngularVelocityDampingRatio <= 10.f
		&& FMath::IsFinite(MaxErrorFraction) && MaxErrorFraction >= 0.f && MaxErrorFraction <= 1.f;
}

void FIMGCameraRotationDampingState::Reset()
{
	PreviousTargetAngles = FVector2D::ZeroVector;
	SmoothedAngles = FVector2D::ZeroVector;
	AngleRates = FVector2D::ZeroVector;
	SmoothedTargetRates = FVector2D::ZeroVector;
	TargetRateDerivatives = FVector2D::ZeroVector;
	bInitialized = false;
	bHasTargetRates = false;
}

FRotator FIMGCameraRotationDampingState::Update(const FRotator& TargetRotation, const FVector2D& FieldOfViewDegrees,
	float DeltaTime, const FIMGCameraRotationDampingSettings& Settings)
{
	if (TargetRotation.ContainsNaN())
	{
		Reset();
		return FRotator::ZeroRotator;
	}

	const FRotator Target = TargetRotation.GetNormalized();
	if (!Settings.Yaw.IsValid() || !Settings.Pitch.IsValid() || FieldOfViewDegrees.ContainsNaN()
		|| FieldOfViewDegrees.X <= 0.0 || FieldOfViewDegrees.X >= 180.0
		|| FieldOfViewDegrees.Y <= 0.0 || FieldOfViewDegrees.Y >= 180.0
		|| !FMath::IsFinite(DeltaTime) || DeltaTime < 0.f || DeltaTime > MaxStepSeconds * MaxSubsteps)
	{
		Reset();
		return Target;
	}

	const FVector2D TargetAngles(Target.Yaw, Target.Pitch);
	const FIMGCameraRotationDampingAxisSettings* AxisSettings[] = { &Settings.Yaw, &Settings.Pitch };
	if (!bInitialized)
	{
		PreviousTargetAngles = TargetAngles;
		SmoothedAngles = TargetAngles;
		bInitialized = true;
		return Target;
	}

	if (DeltaTime <= UE_SMALL_NUMBER)
	{
		// Keep current limits and axis bypasses effective while paused, without advancing the springs.
		for (int32 Axis = 0; Axis < 2; ++Axis)
		{
			const FIMGCameraRotationDampingAxisSettings& AxisConfig = *AxisSettings[Axis];
			const double MaxError = AxisConfig.AngleFrequency <= 0.f ? 0.0
				: FieldOfViewDegrees[Axis] * 0.5 * AxisConfig.MaxErrorFraction;
			ClampAngleError(SmoothedAngles[Axis], AngleRates[Axis], TargetAngles[Axis], MaxError);
			SmoothedAngles[Axis] = FRotator::NormalizeAxis(SmoothedAngles[Axis]);
		}
		PreviousTargetAngles = TargetAngles;
		bHasTargetRates = false;
		return FRotator(SmoothedAngles.Y, SmoothedAngles.X, Target.Roll);
	}

	const FVector2D TargetDelta(
		FMath::FindDeltaAngleDegrees(PreviousTargetAngles.X, TargetAngles.X),
		FMath::FindDeltaAngleDegrees(PreviousTargetAngles.Y, TargetAngles.Y));
	const FVector2D TargetRates = TargetDelta / DeltaTime;
	if (!bHasTargetRates)
	{
		SmoothedTargetRates = TargetRates;
		TargetRateDerivatives = FVector2D::ZeroVector;
		bHasTargetRates = true;
	}

	const int32 NumSteps = FMath::Clamp(FMath::CeilToInt(DeltaTime / MaxStepSeconds), 1, MaxSubsteps);
	const float StepTime = DeltaTime / NumSteps;
	for (int32 StepIndex = 0; StepIndex < NumSteps; ++StepIndex)
	{
		const FVector2D StepTargets = PreviousTargetAngles + TargetDelta * (double(StepIndex + 1) / NumSteps);
		for (int32 Axis = 0; Axis < 2; ++Axis)
		{
			const FIMGCameraRotationDampingAxisSettings& AxisConfig = *AxisSettings[Axis];
			const double StepTarget = StepTargets[Axis];
			const double MaxError = FieldOfViewDegrees[Axis] * 0.5 * AxisConfig.MaxErrorFraction;
			if (AxisConfig.AngleFrequency <= 0.f || MaxError <= 0.0)
			{
				SmoothedAngles[Axis] = StepTarget;
				AngleRates[Axis] = 0.0;
				SmoothedTargetRates[Axis] = TargetRates[Axis];
				TargetRateDerivatives[Axis] = 0.0;
				continue;
			}

			if (AxisConfig.AngularVelocityFrequency <= 0.f)
			{
				SmoothedTargetRates[Axis] = TargetRates[Axis];
				TargetRateDerivatives[Axis] = 0.0;
			}
			else
			{
				FMath::SpringDamper(SmoothedTargetRates[Axis], TargetRateDerivatives[Axis], TargetRates[Axis], 0.0,
					StepTime, AxisConfig.AngularVelocityFrequency, AxisConfig.AngularVelocityDampingRatio);
			}

			SmoothedAngles[Axis] += SmoothedTargetRates[Axis] * StepTime;
			ClampAngleError(SmoothedAngles[Axis], AngleRates[Axis], StepTarget, MaxError);
			FMath::SpringDamper(SmoothedAngles[Axis], AngleRates[Axis], StepTarget, 0.0,
				StepTime, AxisConfig.AngleFrequency, AxisConfig.AngleDampingRatio);
			// Bound the final spring too, including underdamped overshoot and a shrinking FOV.
			ClampAngleError(SmoothedAngles[Axis], AngleRates[Axis], StepTarget, MaxError);
		}
	}

	if (SmoothedAngles.ContainsNaN() || AngleRates.ContainsNaN()
		|| SmoothedTargetRates.ContainsNaN() || TargetRateDerivatives.ContainsNaN())
	{
		Reset();
		return Target;
	}

	PreviousTargetAngles = TargetAngles;
	SmoothedAngles.X = FRotator::NormalizeAxis(SmoothedAngles.X);
	SmoothedAngles.Y = FRotator::NormalizeAxis(SmoothedAngles.Y);
	return FRotator(SmoothedAngles.Y, SmoothedAngles.X, Target.Roll);
}
