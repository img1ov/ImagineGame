#include "Camera/IMGCameraFraming.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGCameraFraming)

namespace
{
	float Finite(float Value, float Default, float Min, float Max)
	{
		return FMath::Clamp(FMath::IsFinite(Value) ? Value : Default, Min, Max);
	}
}

void FIMGCameraFramingResult::Blend(const FIMGCameraFramingResult& Other, float Weight)
{
	if (!FMath::IsFinite(Weight) || Weight <= 0.f) { return; }
	if (Weight >= 1.f) { *this = Other; return; }
	if (!Other.bValid)
	{
		PostCollisionLookAtWeight *= 1.f - Weight;
		FocusWeight *= 1.f - Weight;
		return;
	}
	if (!bValid)
	{
		*this = Other;
		PostCollisionLookAtWeight *= Weight;
		FocusWeight *= Weight;
		return;
	}
	BasePivot = FMath::Lerp(BasePivot, Other.BasePivot, Weight);
	OrbitPivot = FMath::Lerp(OrbitPivot, Other.OrbitPivot, Weight);
	Location = FMath::Lerp(Location, Other.Location, Weight);
	Rotation = FQuat::Slerp(Rotation.Quaternion(), Other.Rotation.Quaternion(), Weight).Rotator();
	PostCollisionLookAtWeight = FMath::Lerp(PostCollisionLookAtWeight, Other.PostCollisionLookAtWeight, Weight);
	MinFocusDistance = FMath::Lerp(MinFocusDistance, Other.MinFocusDistance, Weight);
	OffAxisTolerance = FMath::Lerp(OffAxisTolerance, Other.OffAxisTolerance, Weight);
	MinPitch = FMath::Lerp(MinPitch, Other.MinPitch, Weight);
	MaxPitch = FMath::Lerp(MaxPitch, Other.MaxPitch, Weight);
	FocusWeight = FMath::Lerp(FocusWeight, Other.FocusWeight, Weight);
	if (FocusPoint.IsSet() && Other.FocusPoint.IsSet()) { FocusPoint = FMath::Lerp(*FocusPoint, *Other.FocusPoint, Weight); }
	else if (Other.FocusPoint.IsSet()) { FocusPoint = Other.FocusPoint; }
}

FIMGCameraFramingResult IMGCameraFraming::Build(const FIMGCameraFramingInput& Input, const FIMGCameraFramingSettings& Settings)
{
	FIMGCameraFramingResult Result;
	if (Input.Anchor.ContainsNaN() || Input.OrbitRotation.ContainsNaN() || Input.TargetRotation.ContainsNaN()) { return Result; }
	Result.MinPitch = Finite(Input.MinPitch, -89.f, -89.9f, 89.9f);
	Result.MaxPitch = Finite(Input.MaxPitch, 89.f, Result.MinPitch, 89.9f);
	FRotator Orbit = Input.OrbitRotation.GetNormalized();
	Orbit.Pitch = FMath::Clamp(Orbit.Pitch + Finite(Settings.OrbitPitchOffset, 0.f, -45.f, 45.f),
		double(Result.MinPitch), double(Result.MaxPitch));
	Orbit.Roll = 0.0;
	const FRotator FreeOrbit = Orbit;
	const FVector Offset = Settings.BasePivotLocalOffset.ContainsNaN() ? FVector::ZeroVector
		: Settings.BasePivotLocalOffset.GetClampedToMaxSize(1000.0);
	const FQuat TargetBasis = Input.TargetRotation.Quaternion();
	const FVector HeightAxis = Settings.bUseTargetUpForPivotHeight ? TargetBasis.GetUpVector() : FVector::UpVector;
	Result.BasePivot = Input.Anchor + TargetBasis.RotateVector(Offset)
		+ HeightAxis * Finite(Settings.BasePivotHeight, 0.f, -1000.f, 1000.f);
	Result.MinFocusDistance = Finite(Settings.MinFocusDistance, 50.f, 1.f, 10000.f);
	Result.OffAxisTolerance = Finite(Settings.OffAxisTolerance, 1.f, 0.f, 100.f);
	const float Shoulder = Finite(Settings.ShoulderOffset, 30.f, -1000.f, 1000.f);
	const float Vertical = Finite(Settings.CameraVerticalOffset, 0.f, -1000.f, 1000.f);
	const float OrbitDistance = Finite(Settings.OrbitDistance, 300.f, -10000.f, 10000.f);
	bool bValidFocus = false;
	if (Input.FocusPoint.IsSet() && !Input.FocusPoint->ContainsNaN())
	{
		const FVector ToFocus = *Input.FocusPoint - Result.BasePivot;
		const double Distance = ToFocus.Size();
		if (FMath::IsFinite(Distance) && Distance >= Result.MinFocusDistance)
		{
			bValidFocus = true;
			// Compensate the displaced pivot before constructing the camera orbit.
			Orbit = ToFocus.Rotation();
			Orbit.Yaw -= FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Shoulder / Distance, -1.0, 1.0)));
			Orbit.Pitch -= FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Vertical / Distance, -1.0, 1.0)));
			Orbit.Pitch = FMath::Clamp(Orbit.Pitch, double(Result.MinPitch), double(Result.MaxPitch));
		}
	}
	const FQuat Basis = Orbit.Quaternion();
	Result.OrbitPivot = Result.BasePivot
		+ Basis.GetRightVector() * Shoulder + Basis.GetUpVector() * Vertical;
	Result.Location = Result.OrbitPivot - Basis.GetForwardVector() * OrbitDistance;
	Result.Rotation = Orbit;
	Result.PostCollisionLookAtWeight = Finite(Settings.PostCollisionLookAtWeight, 0.625f, 0.f, 1.f);
	if (bValidFocus
		&& FVector::DistSquared(*Input.FocusPoint, Result.Location) >= FMath::Square(double(Result.MinFocusDistance)))
	{
		Result.FocusPoint = Input.FocusPoint;
		Result.FocusWeight = 1.f;
		Result.Rotation = (*Input.FocusPoint - Result.Location).Rotation();
		Result.Rotation.Pitch = FMath::Clamp(Result.Rotation.Pitch, double(Result.MinPitch), double(Result.MaxPitch));
	}
	else if (bValidFocus)
	{
		const FQuat FreeBasis = FreeOrbit.Quaternion();
		Result.OrbitPivot = Result.BasePivot + FreeBasis.GetRightVector() * Shoulder + FreeBasis.GetUpVector() * Vertical;
		Result.Location = Result.OrbitPivot - FreeBasis.GetForwardVector() * OrbitDistance;
		Result.Rotation = FreeOrbit;
	}
	Result.bValid = true;
	return Result;
}

FRotator IMGCameraFraming::ResolveLookAt(const FIMGCameraFramingResult& Frame, const FVector& RawLocation,
	const FRotator& RawRotation, const FVector& CollisionRoot, const FVector& FinalLocation, const FRotator& FinalRotation)
{
	if (!Frame.bValid || RawLocation.ContainsNaN() || FinalLocation.ContainsNaN() || CollisionRoot.ContainsNaN()
		|| RawRotation.ContainsNaN() || FinalRotation.ContainsNaN()) { return FinalRotation; }
	FVector DesiredFront;
	FVector ReferenceFront = Frame.Rotation.Vector();
	FQuat RemoveCollisionRotation = FQuat::Identity;
	float Weight = Frame.PostCollisionLookAtWeight;
	if (Frame.FocusPoint.IsSet() && Frame.FocusWeight > UE_SMALL_NUMBER)
	{
		DesiredFront = *Frame.FocusPoint - FinalLocation;
		if (DesiredFront.SizeSquared() < FMath::Square(double(Frame.MinFocusDistance))) { return FinalRotation; }
		ReferenceFront = *Frame.FocusPoint - RawLocation;
		if (!ReferenceFront.Normalize()) { return FinalRotation; }
		Weight = Frame.FocusWeight;
		RemoveCollisionRotation = (FinalRotation.Quaternion() * RawRotation.Quaternion().Inverse()).Inverse();
	}
	else
	{
		const FVector OriginalAxis = RawLocation - CollisionRoot;
		const double Length = OriginalAxis.Size();
		if (Length <= UE_KINDA_SMALL_NUMBER || Weight <= 0.f) { return FinalRotation; }
		const FVector FinalDelta = FinalLocation - CollisionRoot;
		const FVector Axis = OriginalAxis / Length;
		if ((FinalDelta - Axis * FVector::DotProduct(FinalDelta, Axis)).SizeSquared() <= FMath::Square(double(Frame.OffAxisTolerance))) { return FinalRotation; }
		const double Ratio = FMath::Clamp(FinalDelta.Size() / Length, 0.0, 1.0);
		DesiredFront = FMath::Lerp(CollisionRoot, Frame.OrbitPivot, Ratio) - FinalLocation;
	}
	if (!DesiredFront.Normalize()) { return FinalRotation; }
	// Apply a geometric correction without discarding the current visual rotation offsets.
	const FQuat Delta = FQuat::FindBetweenNormals(ReferenceFront, DesiredFront) * RemoveCollisionRotation;
	FRotator Result = (FQuat::Slerp(FQuat::Identity, Delta, Weight) * FinalRotation.Quaternion()).Rotator();
	Result.Pitch = FMath::Clamp(Result.Pitch, double(Frame.MinPitch), double(Frame.MaxPitch));
	return Result;
}
