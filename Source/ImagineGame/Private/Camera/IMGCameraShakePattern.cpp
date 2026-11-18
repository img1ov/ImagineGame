#include "Camera/IMGCameraShakePattern.h"

#include "CoreGlobals.h"
#include "IMGLogChannels.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGCameraShakePattern)

void UIMGCameraShakePattern::Prepare(const FIMGCameraShakeSettings& Settings, bool bContinuous, int32 RandomSeed)
{
	ActiveSettings = Settings;
	if (bContinuous) { ActiveSettings.MakeContinuous(); }
	Seed = RandomSeed;
	bPrepared = true;
}

void UIMGCameraShakePattern::GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const
{
	const auto& Pattern = ActiveSettings;
	const double Duration = Pattern.GetDuration();
	OutInfo.Duration = Duration < 0.0 ? FCameraShakeDuration::Infinite() : FCameraShakeDuration(float(FMath::Min(Duration, double(MAX_flt))));
	OutInfo.BlendIn = Pattern.bUseEnvelope ? Pattern.Envelope.AttackTime : 0.f;
	OutInfo.BlendOut = Pattern.bUseEnvelope ? Pattern.Envelope.ReleaseTime : 0.f;
}

void UIMGCameraShakePattern::StartShakePatternImpl(const FCameraShakePatternStartParams& Params)
{
	// Runtime preparation belongs to CameraComponent, before collision, rather than the native modifier.
	if (!bPrepared)
	{
		UE_LOG(LogIMG, Warning, TEXT("IMG camera shakes must be started through IMGCameraComponent: %s"), *GetNameSafe(GetOuter()));
		State.Reset();
		return;
	}
	if (Params.bIsRestarting && State.IsActive()) { State.AutoStart(ActiveSettings); }
	else { State.Start(ActiveSettings, Seed); }
	Values.Reset();
	LastUpdateFrame = MAX_uint64;
}

void UIMGCameraShakePattern::Resume()
{
	State.AutoStart(ActiveSettings);
}

void UIMGCameraShakePattern::UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params, FCameraShakePatternUpdateResult& OutResult)
{
	if (!FMath::IsFinite(Params.ShakeScale) || !FMath::IsFinite(Params.DynamicScale)
		|| !FMath::IsFinite(Params.GetTotalScale()) || Params.ShakeScale < 0.f || Params.DynamicScale < 0.f)
	{
		State.Reset();
		Values.Reset();
		return;
	}
	// A repeated view query can reapply the sample but cannot advance this instance's clock twice.
	if (LastUpdateFrame != GFrameCounter)
	{
		State.Update(Params.DeltaTime, FrequencyScale, ActiveSettings, Values);
		LastUpdateFrame = GFrameCounter;
	}
	ApplyValues(Params.POV, Params.GetTotalScale(), OutResult);
}

void UIMGCameraShakePattern::ScrubShakePatternImpl(const FCameraShakePatternScrubParams& Params, FCameraShakePatternUpdateResult& OutResult)
{
	State.Start(ActiveSettings, Seed);
	State.Update(Params.AbsoluteTime, FrequencyScale, ActiveSettings, Values);
	LastUpdateFrame = MAX_uint64;
	ApplyValues(Params.POV, Params.GetTotalScale(), OutResult);
}

void UIMGCameraShakePattern::ApplyValues(const FMinimalViewInfo& View, float Scale, FCameraShakePatternUpdateResult& OutResult) const
{
	OutResult.Location = View.Location;
	OutResult.Rotation = View.Rotation;
	OutResult.FOV = View.FOV;
	// World and camera-local components coexist, so this adapter applies space and scale exactly once.
	OutResult.Flags = ECameraShakePatternUpdateResultFlags::ApplyAsAbsolute;
	if (!FMath::IsFinite(Scale) || Scale <= 0.f || Values.Num() != ActiveSettings.Components.Num()) { return; }
	for (int32 Index = 0; Index < Values.Num(); ++Index)
	{
		const double Value = Values[Index] * Scale;
		if (!FMath::IsFinite(Value)) { continue; }
		switch (ActiveSettings.Components[Index].Component)
		{
		case EIMGCameraShakeComponent::WorldLocationX: OutResult.Location.X += Value; break;
		case EIMGCameraShakeComponent::WorldLocationY: OutResult.Location.Y += Value; break;
		case EIMGCameraShakeComponent::WorldLocationZ: OutResult.Location.Z += Value; break;
		case EIMGCameraShakeComponent::CameraLocationX: OutResult.Location += OutResult.Rotation.RotateVector(FVector(Value, 0, 0)); break;
		case EIMGCameraShakeComponent::CameraLocationY: OutResult.Location += OutResult.Rotation.RotateVector(FVector(0, Value, 0)); break;
		case EIMGCameraShakeComponent::CameraLocationZ: OutResult.Location += OutResult.Rotation.RotateVector(FVector(0, 0, Value)); break;
		case EIMGCameraShakeComponent::Pitch:
			OutResult.Rotation = (OutResult.Rotation.Quaternion() * FRotator(Value, 0, 0).Quaternion()).Rotator();
			break;
		case EIMGCameraShakeComponent::Yaw: OutResult.Rotation.Yaw = FRotator::NormalizeAxis(OutResult.Rotation.Yaw + Value); break;
		case EIMGCameraShakeComponent::Roll: OutResult.Rotation.Roll = FRotator::NormalizeAxis(OutResult.Rotation.Roll + Value); break;
		case EIMGCameraShakeComponent::FieldOfView: OutResult.FOV = float(FMath::Clamp(double(OutResult.FOV) + Value, 5.0, 170.0)); break;
		case EIMGCameraShakeComponent::ZoomFactor:
			if (1.0 + Value > UE_SMALL_NUMBER)
			{
				const double Aspect = FMath::IsFinite(View.AspectRatio) && View.AspectRatio > UE_SMALL_NUMBER ? View.AspectRatio : 16.0 / 9.0;
				const double VerticalFov = 2.0 * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(OutResult.FOV) * 0.5) / Aspect);
				const double ZoomedFov = FMath::Clamp(VerticalFov / (1.0 + Value), 0.0, FMath::DegreesToRadians(179.0));
				OutResult.FOV = float(FMath::Clamp(FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Tan(ZoomedFov * 0.5) * Aspect)), 5.0, 170.0));
			}
			break;
		}
	}
	OutResult.Rotation.Pitch = FMath::ClampAngle(OutResult.Rotation.Pitch, -89.9, 89.9);
	if (OutResult.Location.ContainsNaN() || OutResult.Rotation.ContainsNaN() || !FMath::IsFinite(OutResult.FOV))
	{
		OutResult.Location = View.Location;
		OutResult.Rotation = View.Rotation;
		OutResult.FOV = View.FOV;
	}
}

void UIMGCameraShakePattern::StopShakePatternImpl(const FCameraShakePatternStopParams& Params)
{
	if (Params.bImmediately || !ActiveSettings.bUseEnvelope || ActiveSettings.Envelope.ReleaseTime <= 0.f) { State.Reset(); Values.Reset(); }
	else { State.Release(); }
}

void UIMGCameraShakePattern::TeardownShakePatternImpl()
{
	State.Reset();
	Values.Reset();
	LastUpdateFrame = MAX_uint64;
	bPrepared = false;
}
