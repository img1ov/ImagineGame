#include "Camera/IMGCameraShake.h"

#include "Math/RandomStream.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGCameraShake)

namespace
{
constexpr double ExponentialDecay = 7.0;
constexpr float MaxUpdateInterval = 32.f / 120.f;

double EvaluateCurve(double Progress, EIMGCameraShakeCurve Curve)
{
	Progress = FMath::Clamp(Progress, 0.0, 1.0);
	switch (Curve)
	{
	case EIMGCameraShakeCurve::SmoothStep:
		return Progress * Progress * (3.0 - 2.0 * Progress);
	case EIMGCameraShakeCurve::Exponential:
		return (1.0 - FMath::Exp(-ExponentialDecay * Progress)) / (1.0 - FMath::Exp(-ExponentialDecay));
	default:
		return Progress;
	}
}

double InverseCurve(double Level, EIMGCameraShakeCurve Curve)
{
	Level = FMath::Clamp(Level, 0.0, 1.0);
	if (Level <= 0.0 || Level >= 1.0)
	{
		return Level;
	}
	if (Curve == EIMGCameraShakeCurve::Linear)
	{
		return Level;
	}
	if (Curve == EIMGCameraShakeCurve::Exponential)
	{
		return -FMath::Loge(1.0 - Level * (1.0 - FMath::Exp(-ExponentialDecay))) / ExponentialDecay;
	}
	double Low = 0.0;
	double High = 1.0;
	for (int32 Index = 0; Index < 20; ++Index)
	{
		const double Mid = (Low + High) * 0.5;
		if (EvaluateCurve(Mid, Curve) < Level)
		{
			Low = Mid;
		}
		else
		{
			High = Mid;
		}
	}
	return (Low + High) * 0.5;
}

bool IsValidCurve(EIMGCameraShakeCurve Curve)
{
	return Curve == EIMGCameraShakeCurve::Linear || Curve == EIMGCameraShakeCurve::SmoothStep
		|| Curve == EIMGCameraShakeCurve::Exponential;
}
}

bool FIMGCameraShakeOscillatorSettings::IsValid() const
{
	return FMath::IsFinite(Amplitude) && Amplitude >= 0.f && FMath::IsFinite(DCOffset)
		&& FMath::IsFinite(Frequency) && Frequency >= 0.f && Frequency <= 100.f
		&& FMath::IsFinite(Phase) && Phase >= -1.f && Phase <= 1.f
		&& FMath::IsFinite(Decay) && Decay >= 0.f && Decay <= 40.f
		&& (Waveform == EIMGCameraShakeWaveform::Sine || Waveform == EIMGCameraShakeWaveform::DampedSine);
}

bool FIMGCameraShakeEnvelopeSettings::IsValid() const
{
	return FMath::IsFinite(PreDelayTime) && PreDelayTime >= 0.f
		&& FMath::IsFinite(AttackTime) && AttackTime >= 0.f && FMath::IsFinite(DecayTime) && DecayTime >= 0.f
		&& FMath::IsFinite(SustainLevel) && SustainLevel >= 0.f && SustainLevel <= 1.f
		&& FMath::IsFinite(HoldTime) && HoldTime >= -1.f
		&& FMath::IsFinite(ReleaseTime) && ReleaseTime >= 0.f
		&& IsValidCurve(AttackCurve) && IsValidCurve(ReleaseCurve);
}

bool FIMGCameraShakeSettings::IsValid() const
{
	return X.IsValid() && Y.IsValid() && Z.IsValid() && Pitch.IsValid() && Yaw.IsValid()
		&& Roll.IsValid() && FOV.IsValid() && Envelope.IsValid();
}

void FIMGCameraShakeState::Reset()
{
	for (int32 Axis = 0; Axis < 7; ++Axis)
	{
		Angles[Axis] = 0.0;
		DecayAngles[Axis] = 0.0;
	}
	EnvelopeLevel = 0.0;
	ReleaseStartLevel = 0.0;
	PhaseTime = 0.0;
	Phase = EPhase::Stopped;
}

void FIMGCameraShakeState::Start(const FIMGCameraShakeSettings& Settings)
{
	if (!Settings.IsValid())
	{
		Reset();
		return;
	}
	if (IsActive())
	{
		if (IsReleasing())
		{
			const double Peak = Settings.Envelope.DecayTime > 0.f ? 1.0 : Settings.Envelope.SustainLevel;
			EnvelopeLevel = FMath::Min(EnvelopeLevel, Peak);
			PhaseTime = InverseCurve(Peak > 0.0 ? EnvelopeLevel / Peak : 0.0, Settings.Envelope.AttackCurve)
				* Settings.Envelope.AttackTime;
			Phase = EPhase::Attack;
		}
		return;
	}

	Reset();
	FRandomStream Random(Settings.RandomSeed);
	const FIMGCameraShakeOscillatorSettings* Oscillators[] = {
		&Settings.X, &Settings.Y, &Settings.Z, &Settings.Pitch, &Settings.Yaw, &Settings.Roll, &Settings.FOV };
	for (int32 Axis = 0; Axis < 7; ++Axis)
	{
		const double InitialPhase = Oscillators[Axis]->Phase < 0.f ? Random.GetFraction() : Oscillators[Axis]->Phase;
		Angles[Axis] = InitialPhase * UE_TWO_PI;
	}
	Phase = EPhase::PreDelay;
}

void FIMGCameraShakeState::Release()
{
	if (IsActive() && !IsReleasing())
	{
		ReleaseStartLevel = EnvelopeLevel;
		PhaseTime = 0.0;
		Phase = EPhase::Release;
	}
}

void FIMGCameraShakeState::UpdateEnvelope(double DeltaTime, const FIMGCameraShakeEnvelopeSettings& Settings)
{
	// At most five forward phase transitions; zero-length phases must also complete on a zero-time update.
	for (int32 Transition = 0; Transition < 6 && IsActive(); ++Transition)
	{
		double Duration = 0.0;
		switch (Phase)
		{
		case EPhase::PreDelay: Duration = Settings.PreDelayTime; break;
		case EPhase::Attack: Duration = Settings.AttackTime; break;
		case EPhase::Decay: Duration = Settings.DecayTime; break;
		case EPhase::Hold:
			EnvelopeLevel = Settings.SustainLevel;
			if (Settings.HoldTime < 0.f)
			{
				return;
			}
			Duration = Settings.HoldTime;
			break;
		case EPhase::Release: Duration = Settings.ReleaseTime; break;
		default: return;
		}

		const double StepTime = FMath::Min(DeltaTime, FMath::Max(Duration - PhaseTime, 0.0));
		PhaseTime += StepTime;
		DeltaTime -= StepTime;
		const double Progress = Duration > 0.0 ? FMath::Clamp(PhaseTime / Duration, 0.0, 1.0) : 1.0;
		switch (Phase)
		{
		case EPhase::PreDelay: EnvelopeLevel = 0.0; break;
		case EPhase::Attack:
			EnvelopeLevel = EvaluateCurve(Progress, Settings.AttackCurve)
				* (Settings.DecayTime > 0.f ? 1.0 : Settings.SustainLevel);
			break;
		case EPhase::Decay: EnvelopeLevel = FMath::Lerp(1.0, double(Settings.SustainLevel), Progress); break;
		case EPhase::Hold: break;
		case EPhase::Release:
			EnvelopeLevel = ReleaseStartLevel * (Settings.ReleaseCurve == EIMGCameraShakeCurve::Exponential
				? FMath::Exp(-ExponentialDecay * Progress) : 1.0 - EvaluateCurve(Progress, Settings.ReleaseCurve));
			break;
		default: break;
		}
		if (Progress < 1.0)
		{
			return;
		}
		PhaseTime = 0.0;
		switch (Phase)
		{
		case EPhase::PreDelay: Phase = EPhase::Attack; break;
		case EPhase::Attack: Phase = EPhase::Decay; break;
		case EPhase::Decay: Phase = EPhase::Hold; break;
		case EPhase::Hold: Release(); break;
		case EPhase::Release: Reset(); return;
		default: return;
		}
	}
}

void FIMGCameraShakeState::Update(float DeltaTime, float AmplitudeScale, float FrequencyScale,
	const FIMGCameraShakeSettings& Settings, FVector& OutLocationOffset, FRotator& OutRotationOffset, float& OutFOVOffset)
{
	OutLocationOffset = FVector::ZeroVector;
	OutRotationOffset = FRotator::ZeroRotator;
	OutFOVOffset = 0.f;
	if (!Settings.IsValid() || !FMath::IsFinite(DeltaTime) || DeltaTime < 0.f || DeltaTime > MaxUpdateInterval
		|| !FMath::IsFinite(AmplitudeScale) || AmplitudeScale < 0.f
		|| !FMath::IsFinite(FrequencyScale) || FrequencyScale < 0.f)
	{
		Reset();
		return;
	}
	if (!IsActive())
	{
		return;
	}
	UpdateEnvelope(DeltaTime, Settings.Envelope);
	if (!IsActive())
	{
		return;
	}

	const FIMGCameraShakeOscillatorSettings* Oscillators[] = {
		&Settings.X, &Settings.Y, &Settings.Z, &Settings.Pitch, &Settings.Yaw, &Settings.Roll, &Settings.FOV };
	double Values[7] = {};
	for (int32 Axis = 0; Axis < 7; ++Axis)
	{
		const FIMGCameraShakeOscillatorSettings& Oscillator = *Oscillators[Axis];
		Angles[Axis] = FMath::Fmod(Angles[Axis] + UE_TWO_PI * double(Oscillator.Frequency) * FrequencyScale * DeltaTime, double(UE_TWO_PI));
		double Wave = FMath::Sin(Angles[Axis]);
		if (Oscillator.Waveform == EIMGCameraShakeWaveform::DampedSine)
		{
			Wave = 0.0;
			if (Oscillator.Frequency > UE_SMALL_NUMBER)
			{
				DecayAngles[Axis] = FMath::Fmod(DecayAngles[Axis] + UE_TWO_PI * double(FrequencyScale) * DeltaTime / Oscillator.Frequency, double(UE_TWO_PI));
				Wave = FMath::Sin(Angles[Axis]) * FMath::Exp(-Oscillator.Decay * (FMath::Sin(DecayAngles[Axis]) * 0.5 + 0.5));
			}
		}
		Values[Axis] = (Oscillator.Amplitude * Wave + Oscillator.DCOffset) * EnvelopeLevel * AmplitudeScale;
	}
	OutLocationOffset = FVector(Values[0], Values[1], Values[2]);
	OutRotationOffset = FRotator(Values[3], Values[4], Values[5]);
	OutFOVOffset = float(Values[6]);
	if (OutLocationOffset.ContainsNaN() || OutRotationOffset.ContainsNaN() || !FMath::IsFinite(OutFOVOffset))
	{
		Reset();
		OutLocationOffset = FVector::ZeroVector;
		OutRotationOffset = FRotator::ZeroRotator;
		OutFOVOffset = 0.f;
	}
}
