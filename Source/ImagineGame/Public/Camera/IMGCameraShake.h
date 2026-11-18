#pragma once

#include "CoreMinimal.h"
#include "IMGCameraShake.generated.h"

UENUM()
enum class EIMGCameraShakeWaveform : uint8 { Sine, DampedSine };

UENUM()
enum class EIMGCameraShakeCurve : uint8
{
	Linear, Exponential,
	SlowIn, SlowOut, SlowInOut,
	VerySlowIn, VerySlowOut, VerySlowInSlowOut, SlowInVerySlowOut, VerySlowInVerySlowOut,
	EaseIn, EaseOut,
	QuadraticIn, QuadraticOut, QuadraticInOut,
	CubicIn, CubicOut, CubicInOut,
	QuarticIn, QuarticOut, QuarticInOut,
	QuinticIn, QuinticOut, QuinticInOut,
	CircularIn, CircularOut, CircularInOut
};

UENUM()
enum class EIMGCameraShakeComponent : uint8
{
	WorldLocationX, WorldLocationY, WorldLocationZ,
	CameraLocationX, CameraLocationY, CameraLocationZ,
	Pitch, Yaw, Roll, FieldOfView, ZoomFactor
};

USTRUCT()
struct IMAGINEGAME_API FIMGCameraShakeEnvelopeSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Envelope", meta = (ClampMin = "0", Units = "s"))
	float PreDelayTime = 0.f;

	UPROPERTY(EditAnywhere, Category = "Envelope", meta = (ClampMin = "0", Units = "s"))
	float AttackTime = 0.f;

	UPROPERTY(EditAnywhere, Category = "Envelope", meta = (ClampMin = "0", Units = "s"))
	float DecayTime = 0.f;

	/** With decay, attack reaches one before decaying to sustain. Otherwise attack reaches sustain directly. */
	UPROPERTY(EditAnywhere, Category = "Envelope", meta = (ClampMin = "0", ClampMax = "10"))
	float SustainLevel = 1.f;

	/** Negative holds until release. Continuous playback overrides the overall hold at runtime. */
	UPROPERTY(EditAnywhere, Category = "Envelope", meta = (ClampMin = "-1", Units = "s"))
	float HoldTime = 1.f;

	UPROPERTY(EditAnywhere, Category = "Envelope", meta = (ClampMin = "0", Units = "s"))
	float ReleaseTime = 0.f;

	UPROPERTY(EditAnywhere, Category = "Envelope")
	EIMGCameraShakeCurve AttackCurve = EIMGCameraShakeCurve::Linear;

	UPROPERTY(EditAnywhere, Category = "Envelope")
	EIMGCameraShakeCurve ReleaseCurve = EIMGCameraShakeCurve::Exponential;

	bool IsValid() const;
	double GetDuration() const;
};

USTRUCT()
struct IMAGINEGAME_API FIMGCameraShakeOscillatorSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Component")
	EIMGCameraShakeComponent Component = EIMGCameraShakeComponent::Pitch;

	/** Position uses cm, rotation/FOV use degrees, and zoom is dimensionless. Camera XYZ are forward/right/up. */
	UPROPERTY(EditAnywhere, Category = "Oscillator", meta = (ClampMin = "0"))
	float Amplitude = 0.f;

	UPROPERTY(EditAnywhere, Category = "Oscillator", meta = (ClampMin = "0", ClampMax = "100", Units = "Hz"))
	float Frequency = 1.f;

	/** Initial phase in cycles; negative selects a fresh random phase from the instance seed. */
	UPROPERTY(EditAnywhere, Category = "Oscillator", meta = (ClampMin = "-1", ClampMax = "1"))
	float Phase = -1.f;

	UPROPERTY(EditAnywhere, Category = "Oscillator")
	EIMGCameraShakeWaveform Waveform = EIMGCameraShakeWaveform::Sine;

	/** Periodic attenuation driven by a secondary oscillator, not a one-way fade. */
	UPROPERTY(EditAnywhere, Category = "Oscillator", meta = (ClampMin = "0", ClampMax = "40", EditCondition = "Waveform == EIMGCameraShakeWaveform::DampedSine"))
	float Decay = 1.f;

	UPROPERTY(EditAnywhere, Category = "Oscillator")
	float DCOffset = 0.f;

	UPROPERTY(EditAnywhere, Category = "Envelope")
	bool bUseEnvelope = false;

	UPROPERTY(EditAnywhere, Category = "Envelope", meta = (EditCondition = "bUseEnvelope"))
	FIMGCameraShakeEnvelopeSettings Envelope;

	bool IsValid() const;
};

USTRUCT()
struct IMAGINEGAME_API FIMGCameraShakeSettings
{
	GENERATED_BODY()

	/** Evaluated in order. Multiple oscillators may contribute to the same component. */
	UPROPERTY(EditAnywhere, Category = "Pattern", meta = (TitleProperty = "Component"))
	TArray<FIMGCameraShakeOscillatorSettings> Components;

	UPROPERTY(EditAnywhere, Category = "Envelope")
	bool bUseEnvelope = true;

	UPROPERTY(EditAnywhere, Category = "Envelope", meta = (EditCondition = "bUseEnvelope"))
	FIMGCameraShakeEnvelopeSettings Envelope;

	bool IsValid() const;
	double GetDuration() const;
	/** Continuous playback owns the overall hold; the configured envelope toggle remains effective. */
	void MakeContinuous();
};

/** Numeric history only; the caller supplies an immutable pattern, seed, scales and elapsed time. */
struct IMAGINEGAME_API FIMGCameraShakeState
{
	void Start(const FIMGCameraShakeSettings& Settings, int32 RandomSeed);
	/** Preserves oscillator phases, resumes an overall release, and refreshes a finite overall hold. */
	void AutoStart(const FIMGCameraShakeSettings& Settings);
	void Release();
	void Reset();

	bool IsActive() const { return bActive; }
	bool IsReleasing() const;
	double GetEnvelopeLevel() const;
	double GetReleaseLevel() const;

	/** Closed-form oscillator steps and bounded envelope transitions handle long positive frames too. */
	void Update(float DeltaTime, float FrequencyScale, const FIMGCameraShakeSettings& Settings, TArray<double>& OutValues);

private:
	enum class EPhase : uint8 { Stopped, PreDelay, Attack, Decay, Hold, Release };
	void StartEnvelope(int32 Index, const FIMGCameraShakeEnvelopeSettings& Settings, double InitialLevel);
	void UpdateEnvelope(int32 Index, double DeltaTime, const FIMGCameraShakeEnvelopeSettings& Settings);

	TArray<double> Angles;
	TArray<double> DecayAngles;
	// Index zero is the overall envelope; component envelopes follow in pattern order.
	TArray<double> EnvelopeLevels;
	TArray<double> ReleaseStartLevels;
	TArray<double> PhaseTimes;
	TArray<EPhase> Phases;
	bool bActive = false;
	bool bHasOverallEnvelope = false;
};
