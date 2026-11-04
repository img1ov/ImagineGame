#include "Camera/IMGCameraDamping.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGCameraDamping)

namespace
{
	bool IsFiniteValue(double Value)
	{
		return FMath::IsFinite(Value);
	}

	bool IsFiniteValue(const FVector& Value)
	{
		return !Value.ContainsNaN();
	}

	template <typename T>
	void AdvanceSpringImpl(const T& Target, double SpringConstant, double DampingRatio, double DeltaTime,
		T& Value, T& Velocity)
	{
		if (!IsFiniteValue(Target) || !IsFiniteValue(Value) || !IsFiniteValue(Velocity)
			|| !FMath::IsFinite(SpringConstant) || SpringConstant < 0.0
			|| !FMath::IsFinite(DampingRatio) || DampingRatio < 0.0
			|| !FMath::IsFinite(DeltaTime) || DeltaTime < 0.0)
		{
			Value = IsFiniteValue(Target) ? Target : T(0.0);
			Velocity = T(0.0);
			return;
		}
		if (SpringConstant <= UE_SMALL_NUMBER)
		{
			Value = Target;
			Velocity = T(0.0);
			return;
		}
		if (DeltaTime <= UE_SMALL_NUMBER)
		{
			return;
		}

		const T Displacement = Value - Target;
		const double HalfDamping = DampingRatio * FMath::Sqrt(SpringConstant);
		const double OmegaSquared = SpringConstant - FMath::Square(HalfDamping);
		T NextDisplacement;
		if (OmegaSquared >= UE_SMALL_NUMBER)
		{
			const double Omega = FMath::Sqrt(OmegaSquared);
			const T C = (Velocity + HalfDamping * Displacement) / Omega;
			const double Sin = FMath::Sin(Omega * DeltaTime);
			const double Cos = FMath::Cos(Omega * DeltaTime);
			const double Decay = FMath::Exp(-HalfDamping * DeltaTime);
			const T Wave = Displacement * Cos + C * Sin;
			NextDisplacement = Decay * Wave;
			Velocity = Decay * (-HalfDamping * Wave + Omega * (C * Cos - Displacement * Sin));
		}
		else
		{
			// Match the reference's slow-mode approximation, including at critical damping.
			const double Rate = -HalfDamping + FMath::Sqrt(FMath::Max(-OmegaSquared, 0.0));
			NextDisplacement = Displacement * FMath::Exp(Rate * DeltaTime);
			Velocity = Rate * NextDisplacement;
		}
		Value = Target + NextDisplacement;
		if (!IsFiniteValue(Value) || !IsFiniteValue(Velocity))
		{
			Value = Target;
			Velocity = T(0.0);
		}
	}
}

void IMGCameraDamping::AdvanceSpring(double Target, double SpringConstant, double DampingRatio, double DeltaTime,
	double& Value, double& Velocity)
{
	AdvanceSpringImpl(Target, SpringConstant, DampingRatio, DeltaTime, Value, Velocity);
}

void IMGCameraDamping::AdvanceSpring(const FVector& Target, double SpringConstant, double DampingRatio, double DeltaTime,
	FVector& Value, FVector& Velocity)
{
	AdvanceSpringImpl(Target, SpringConstant, DampingRatio, DeltaTime, Value, Velocity);
}

void FIMGCameraLocationDampingState::Reset()
{
	LaggedLocation = FVector::ZeroVector;
	Velocity = FVector::ZeroVector;
	PreviousTargetLocation = FVector::ZeroVector;
	bInitialized = false;
}

FVector FIMGCameraLocationDampingState::Update(const FVector& DesiredLocation, float DeltaTime,
	float ResponseSpeed, float DampingRatio, bool bUseSubstepping, float MaxSubstepTime, float MaxDistance,
	bool bSnapToTarget)
{
	if (DesiredLocation.ContainsNaN())
	{
		const FVector Fallback = bInitialized && !LaggedLocation.ContainsNaN()
			? LaggedLocation : FVector::ZeroVector;
		Reset();
		return Fallback;
	}

	const auto SnapToTarget = [this, &DesiredLocation]()
	{
		LaggedLocation = DesiredLocation;
		Velocity = FVector::ZeroVector;
		PreviousTargetLocation = DesiredLocation;
		bInitialized = true;
		return LaggedLocation;
	};
	if (!bInitialized || bSnapToTarget || ResponseSpeed <= 0.0f)
	{
		return SnapToTarget();
	}
	if (!FMath::IsFinite(DeltaTime) || DeltaTime < 0.f || !FMath::IsFinite(ResponseSpeed))
	{
		return SnapToTarget();
	}
	if (!FMath::IsFinite(DampingRatio) || DampingRatio < 0.f || DampingRatio > 10.f
		|| !FMath::IsFinite(MaxDistance) || MaxDistance < 0.f
		|| LaggedLocation.ContainsNaN() || Velocity.ContainsNaN() || PreviousTargetLocation.ContainsNaN())
	{
		return SnapToTarget();
	}

	int32 NumSteps = 1;
	if (bUseSubstepping)
	{
		constexpr int32 MaxLagSubsteps = 32;
		if (!FMath::IsFinite(MaxSubstepTime) || MaxSubstepTime <= 0.f)
		{
			return SnapToTarget();
		}
		const double MaxTimeStep = FMath::Max(double(MaxSubstepTime), 1.0 / 200.0);
		if (DeltaTime > MaxTimeStep * MaxLagSubsteps)
		{
			return SnapToTarget();
		}
		NumSteps = FMath::Clamp(FMath::CeilToInt(DeltaTime / MaxTimeStep), 1, MaxLagSubsteps);
	}
	if (DeltaTime > UE_SMALL_NUMBER)
	{
		const double SpringConstant = FMath::Square(double(ResponseSpeed));
		const double StepTime = double(DeltaTime) / NumSteps;
		for (int32 StepIndex = 0; StepIndex < NumSteps; ++StepIndex)
		{
			// Reconstruct the target path from sampled pivots, rather than feeding lag error back into that path.
			const FVector StepTarget = FMath::Lerp(PreviousTargetLocation, DesiredLocation,
				double(StepIndex + 1) / NumSteps);
			IMGCameraDamping::AdvanceSpring(StepTarget, SpringConstant, DampingRatio, StepTime,
				LaggedLocation, Velocity);
		}
	}
	PreviousTargetLocation = DesiredLocation;

	if (MaxDistance > 0.0f)
	{
		const FVector FromTarget = LaggedLocation - DesiredLocation;
		if (FromTarget.SizeSquared() > FMath::Square(double(MaxDistance)))
		{
			LaggedLocation = DesiredLocation + FromTarget.GetClampedToMaxSize(MaxDistance);
			// A hard positional constraint must not leave spring momentum to be released later.
			Velocity = FVector::ZeroVector;
		}
	}
	if (LaggedLocation.ContainsNaN() || Velocity.ContainsNaN())
	{
		return SnapToTarget();
	}

	return LaggedLocation;
}

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
