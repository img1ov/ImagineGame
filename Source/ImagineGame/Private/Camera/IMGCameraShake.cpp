#include "Camera/IMGCameraShake.h"

#include "Math/RandomStream.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGCameraShake)

namespace
{
	double SlowIn(double T) { return 1.0 - FMath::Cos(T * UE_HALF_PI); }
	double SlowOut(double T) { return FMath::Sin(T * UE_HALF_PI); }
	double SlowInOut(double T) { return 0.5 * (1.0 - FMath::Cos(T * UE_PI)); }

	double EvaluateCurve(double T, EIMGCameraShakeCurve Curve)
	{
		T = FMath::Clamp(T, 0.0, 1.0);
		switch (Curve)
		{
		case EIMGCameraShakeCurve::Linear: return T;
		case EIMGCameraShakeCurve::Exponential: return 1.0 - FMath::Exp(-7.0 * T);
		case EIMGCameraShakeCurve::SlowIn: return SlowIn(T);
		case EIMGCameraShakeCurve::SlowOut: return SlowOut(T);
		case EIMGCameraShakeCurve::SlowInOut: return SlowInOut(T);
		case EIMGCameraShakeCurve::VerySlowIn: return SlowIn(SlowIn(T));
		case EIMGCameraShakeCurve::VerySlowOut: return SlowOut(SlowOut(T));
		case EIMGCameraShakeCurve::VerySlowInSlowOut: return SlowIn(SlowInOut(T));
		case EIMGCameraShakeCurve::SlowInVerySlowOut: return SlowOut(SlowInOut(T));
		case EIMGCameraShakeCurve::VerySlowInVerySlowOut: return SlowInOut(SlowInOut(T));
		case EIMGCameraShakeCurve::EaseIn: return T * T * T;
		case EIMGCameraShakeCurve::EaseOut: return 1.0 - FMath::Pow(1.0 - T, 3);
		case EIMGCameraShakeCurve::CircularIn: return 1.0 - FMath::Sqrt(FMath::Max(0.0, 1.0 - T * T));
		case EIMGCameraShakeCurve::CircularOut: return FMath::Sqrt(FMath::Max(0.0, 1.0 - FMath::Square(T - 1.0)));
		case EIMGCameraShakeCurve::CircularInOut:
			return T < 0.5 ? 0.5 * (1.0 - FMath::Sqrt(FMath::Max(0.0, 1.0 - FMath::Square(2.0 * T))))
				: 0.5 * (1.0 + FMath::Sqrt(FMath::Max(0.0, 1.0 - FMath::Square(2.0 * T - 2.0))));
		default: break;
		}
		const int32 Polynomial = int32(Curve) - int32(EIMGCameraShakeCurve::QuadraticIn);
		const int32 Power = 2 + Polynomial / 3;
		switch (Polynomial % 3)
		{
		case 0: return FMath::Pow(T, Power);
		case 1: return 1.0 - FMath::Pow(1.0 - T, Power);
		default: return T < 0.5 ? 0.5 * FMath::Pow(2.0 * T, Power) : 1.0 - 0.5 * FMath::Pow(2.0 * (1.0 - T), Power);
		}
	}

	double InverseCurve(double Level, EIMGCameraShakeCurve Curve)
	{
		if (Level <= 0.0) { return 0.0; }
		if (Level >= 1.0) { return 1.0; }
		double Low = 0.0;
		double High = 1.0;
		for (int32 Iteration = 0; Iteration < 20; ++Iteration)
		{
			const double Mid = (Low + High) * 0.5;
			if (EvaluateCurve(Mid, Curve) < Level) { Low = Mid; }
			else { High = Mid; }
		}
		return (Low + High) * 0.5;
	}

	bool IsValidCurve(EIMGCameraShakeCurve Curve)
	{
		return uint8(Curve) <= uint8(EIMGCameraShakeCurve::CircularInOut);
	}
}

bool FIMGCameraShakeEnvelopeSettings::IsValid() const
{
	return FMath::IsFinite(PreDelayTime) && PreDelayTime >= 0.f
		&& FMath::IsFinite(AttackTime) && AttackTime >= 0.f
		&& FMath::IsFinite(DecayTime) && DecayTime >= 0.f
		&& FMath::IsFinite(SustainLevel) && SustainLevel >= 0.f && SustainLevel <= 10.f
		&& FMath::IsFinite(HoldTime) && HoldTime >= -1.f
		&& FMath::IsFinite(ReleaseTime) && ReleaseTime >= 0.f
		&& IsValidCurve(AttackCurve) && IsValidCurve(ReleaseCurve);
}

double FIMGCameraShakeEnvelopeSettings::GetDuration() const
{
	return HoldTime < 0.f ? -1.0 : double(PreDelayTime) + AttackTime + DecayTime + HoldTime + ReleaseTime;
}

bool FIMGCameraShakeOscillatorSettings::IsValid() const
{
	return uint8(Component) <= uint8(EIMGCameraShakeComponent::ZoomFactor)
		&& FMath::IsFinite(Amplitude) && Amplitude >= 0.f && FMath::IsFinite(DCOffset)
		&& FMath::IsFinite(Frequency) && Frequency >= 0.f && Frequency <= 100.f
		&& FMath::IsFinite(Phase) && Phase >= -1.f && Phase <= 1.f
		&& FMath::IsFinite(Decay) && Decay >= 0.f && Decay <= 40.f
		&& (Waveform == EIMGCameraShakeWaveform::Sine || Waveform == EIMGCameraShakeWaveform::DampedSine)
		&& (!bUseEnvelope || Envelope.IsValid());
}

bool FIMGCameraShakeSettings::IsValid() const
{
	if (Components.IsEmpty() || (bUseEnvelope && !Envelope.IsValid())) { return false; }
	for (const auto& Component : Components) { if (!Component.IsValid()) { return false; } }
	return true;
}

void FIMGCameraShakeSettings::MakeContinuous()
{
	if (bUseEnvelope) { Envelope.HoldTime = -1.f; }
}

double FIMGCameraShakeSettings::GetDuration() const
{
	double ComponentDuration = 0.0;
	for (const auto& Component : Components)
	{
		const double Duration = Component.bUseEnvelope ? Component.Envelope.GetDuration() : -1.0;
		if (Duration < 0.0) { ComponentDuration = -1.0; break; }
		ComponentDuration = FMath::Max(ComponentDuration, Duration);
	}
	const double OverallDuration = bUseEnvelope ? Envelope.GetDuration() : -1.0;
	if (OverallDuration < 0.0) { return ComponentDuration; }
	return ComponentDuration < 0.0 ? OverallDuration : FMath::Min(OverallDuration, ComponentDuration);
}

void FIMGCameraShakeState::Reset()
{
	Angles.Reset();
	DecayAngles.Reset();
	EnvelopeLevels.Reset();
	ReleaseStartLevels.Reset();
	PhaseTimes.Reset();
	Phases.Reset();
	bActive = false;
	bHasOverallEnvelope = false;
}

void FIMGCameraShakeState::StartEnvelope(int32 Index, const FIMGCameraShakeEnvelopeSettings& Settings, double InitialLevel)
{
	const double Peak = Settings.DecayTime > 0.f ? 1.0 : Settings.SustainLevel;
	EnvelopeLevels[Index] = FMath::Clamp(InitialLevel, 0.0, FMath::Max(Peak, double(Settings.SustainLevel)));
	ReleaseStartLevels[Index] = Settings.SustainLevel;
	if (EnvelopeLevels[Index] > Peak)
	{
		// Sustain may exceed the attack peak; resume along that segment without dropping the current level.
		Phases[Index] = EPhase::Decay;
		PhaseTimes[Index] = (EnvelopeLevels[Index] - Peak) / (Settings.SustainLevel - Peak) * Settings.DecayTime;
		return;
	}
	Phases[Index] = EnvelopeLevels[Index] > UE_SMALL_NUMBER ? EPhase::Attack : EPhase::PreDelay;
	PhaseTimes[Index] = Phases[Index] == EPhase::Attack
		? InverseCurve(Peak > 0.0 ? EnvelopeLevels[Index] / Peak : 0.0, Settings.AttackCurve) * Settings.AttackTime : 0.0;
}

void FIMGCameraShakeState::Start(const FIMGCameraShakeSettings& Settings, int32 RandomSeed)
{
	Reset();
	if (!Settings.IsValid()) { return; }
	const int32 Count = Settings.Components.Num();
	Angles.SetNumZeroed(Count);
	DecayAngles.SetNumZeroed(Count);
	EnvelopeLevels.Init(1.0, Count + 1);
	ReleaseStartLevels.Init(1.0, Count + 1);
	PhaseTimes.Init(0.0, Count + 1);
	Phases.Init(EPhase::Hold, Count + 1);
	bActive = true;
	bHasOverallEnvelope = Settings.bUseEnvelope;
	if (bHasOverallEnvelope) { StartEnvelope(0, Settings.Envelope, 0.0); }
	FRandomStream Random(RandomSeed);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const auto& Component = Settings.Components[Index];
		Angles[Index] = (Component.Phase < 0.f ? Random.GetFraction() : Component.Phase) * UE_TWO_PI;
		if (Component.bUseEnvelope) { StartEnvelope(Index + 1, Component.Envelope, 0.0); }
	}
}

void FIMGCameraShakeState::AutoStart(const FIMGCameraShakeSettings& Settings)
{
	if (!bActive || !bHasOverallEnvelope || !Settings.IsValid()) { return; }
	if (Phases[0] == EPhase::Release) { StartEnvelope(0, Settings.Envelope, EnvelopeLevels[0]); }
	else if (Phases[0] == EPhase::Hold) { PhaseTimes[0] = 0.0; }
}

void FIMGCameraShakeState::Release()
{
	if (!bActive) { return; }
	if (!bHasOverallEnvelope) { Reset(); return; }
	if (Phases[0] != EPhase::Release)
	{
		ReleaseStartLevels[0] = EnvelopeLevels[0];
		PhaseTimes[0] = 0.0;
		Phases[0] = EPhase::Release;
	}
}

bool FIMGCameraShakeState::IsReleasing() const { return bActive && bHasOverallEnvelope && Phases[0] == EPhase::Release; }
double FIMGCameraShakeState::GetEnvelopeLevel() const { return bActive ? EnvelopeLevels[0] : 0.0; }
double FIMGCameraShakeState::GetReleaseLevel() const
{
	return IsReleasing() ? (ReleaseStartLevels[0] > UE_SMALL_NUMBER ? EnvelopeLevels[0] / ReleaseStartLevels[0] : 0.0) : 1.0;
}

void FIMGCameraShakeState::UpdateEnvelope(int32 Index, double DeltaTime, const FIMGCameraShakeEnvelopeSettings& Settings)
{
	for (int32 Transition = 0; Transition < 6 && Phases[Index] != EPhase::Stopped; ++Transition)
	{
		double Duration = 0.0;
		switch (Phases[Index])
		{
		case EPhase::PreDelay: Duration = Settings.PreDelayTime; break;
		case EPhase::Attack: Duration = Settings.AttackTime; break;
		case EPhase::Decay: Duration = Settings.DecayTime; break;
		case EPhase::Hold:
			EnvelopeLevels[Index] = Settings.SustainLevel;
			if (Settings.HoldTime < 0.f) { return; }
			Duration = Settings.HoldTime;
			break;
		case EPhase::Release: Duration = Settings.ReleaseTime; break;
		default: return;
		}
		const double Step = FMath::Min(DeltaTime, FMath::Max(0.0, Duration - PhaseTimes[Index]));
		PhaseTimes[Index] += Step;
		DeltaTime -= Step;
		const double Progress = Duration > 0.0 ? FMath::Clamp(PhaseTimes[Index] / Duration, 0.0, 1.0) : 1.0;
		switch (Phases[Index])
		{
		case EPhase::PreDelay: EnvelopeLevels[Index] = 0.0; break;
		case EPhase::Attack:
			EnvelopeLevels[Index] = EvaluateCurve(Progress, Settings.AttackCurve) * (Settings.DecayTime > 0.f ? 1.0 : Settings.SustainLevel);
			break;
		case EPhase::Decay: EnvelopeLevels[Index] = FMath::Lerp(1.0, double(Settings.SustainLevel), Progress); break;
		case EPhase::Hold: break;
		case EPhase::Release:
			EnvelopeLevels[Index] = ReleaseStartLevels[Index] * (1.0 - EvaluateCurve(Progress, Settings.ReleaseCurve));
			break;
		default: break;
		}
		if (Progress < 1.0) { return; }
		PhaseTimes[Index] = 0.0;
		switch (Phases[Index])
		{
		case EPhase::PreDelay: Phases[Index] = EPhase::Attack; break;
		case EPhase::Attack: Phases[Index] = EPhase::Decay; break;
		case EPhase::Decay: Phases[Index] = EPhase::Hold; break;
		case EPhase::Hold:
			ReleaseStartLevels[Index] = EnvelopeLevels[Index];
			Phases[Index] = EPhase::Release;
			break;
		case EPhase::Release: Phases[Index] = EPhase::Stopped; EnvelopeLevels[Index] = 0.0; return;
		default: return;
		}
	}
}

void FIMGCameraShakeState::Update(float DeltaTime, float FrequencyScale, const FIMGCameraShakeSettings& Settings, TArray<double>& OutValues)
{
	OutValues.Init(0.0, Settings.Components.Num());
	if (!Settings.IsValid() || !FMath::IsFinite(DeltaTime) || DeltaTime < 0.f
		|| !FMath::IsFinite(FrequencyScale) || FrequencyScale < 0.f
		|| (bActive && Angles.Num() != Settings.Components.Num()))
	{
		Reset();
		return;
	}
	if (!bActive) { return; }
	if (bHasOverallEnvelope)
	{
		UpdateEnvelope(0, DeltaTime, Settings.Envelope);
		if (Phases[0] == EPhase::Stopped) { Reset(); return; }
	}
	// Return released frequency smoothly to unity without alternating its sign around unity.
	const double EffectiveFrequencyScale = FMath::Lerp(1.0, double(FrequencyScale), GetReleaseLevel());
	bool bHasActiveComponent = false;
	for (int32 Index = 0; Index < Settings.Components.Num(); ++Index)
	{
		const auto& Component = Settings.Components[Index];
		if (Component.bUseEnvelope)
		{
			UpdateEnvelope(Index + 1, DeltaTime, Component.Envelope);
			if (Phases[Index + 1] == EPhase::Stopped) { continue; }
		}
		bHasActiveComponent = true;
		Angles[Index] = FMath::Fmod(Angles[Index] + UE_TWO_PI * double(Component.Frequency) * EffectiveFrequencyScale * DeltaTime, double(UE_TWO_PI));
		double Wave = FMath::Sin(Angles[Index]);
		if (Component.Waveform == EIMGCameraShakeWaveform::DampedSine)
		{
			Wave = 0.0;
			if (Component.Frequency > UE_SMALL_NUMBER)
			{
				DecayAngles[Index] = FMath::Fmod(DecayAngles[Index] + UE_TWO_PI * EffectiveFrequencyScale * DeltaTime / Component.Frequency, double(UE_TWO_PI));
				Wave = FMath::Sin(Angles[Index]) * FMath::Exp(-Component.Decay * (0.5 + 0.5 * FMath::Sin(DecayAngles[Index])));
			}
		}
		OutValues[Index] = (Component.Amplitude * Wave + Component.DCOffset) * EnvelopeLevels[0] * EnvelopeLevels[Index + 1];
		if (!FMath::IsFinite(OutValues[Index])) { Reset(); OutValues.Init(0.0, Settings.Components.Num()); return; }
	}
	if (!bHasActiveComponent) { Reset(); }
}
