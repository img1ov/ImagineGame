#pragma once

#include "CoreMinimal.h"

#include "IMGCameraShake.generated.h"

UENUM(BlueprintType)
enum class EIMGCameraShakeWaveform : uint8
{
	Sine,
	DampedSine
};

UENUM(BlueprintType)
enum class EIMGCameraShakeCurve : uint8
{
	Linear,
	SmoothStep,
	Exponential
};

USTRUCT(BlueprintType)
struct IMAGINEGAME_API FIMGCameraShakeOscillatorSettings
{
	GENERATED_BODY()

	/** Centimeters for position axes, degrees for rotation and FOV. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oscillator", meta = (ClampMin = "0"))
	float Amplitude = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oscillator", meta = (ClampMin = "0", ClampMax = "100", ForceUnits = "Hz"))
	float Frequency = 1.f;

	/** Initial phase in cycles; a negative value selects a seeded random phase. Applied when a new instance starts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oscillator", meta = (ClampMin = "-1", ClampMax = "1"))
	float Phase = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oscillator")
	EIMGCameraShakeWaveform Waveform = EIMGCameraShakeWaveform::Sine;

	/** Periodic attenuation of DampedSine, matching GTA's secondary decay oscillator. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oscillator", meta = (ClampMin = "0", ClampMax = "40", EditCondition = "Waveform == EIMGCameraShakeWaveform::DampedSine"))
	float Decay = 1.f;

	/** Constant offset in the same units as Amplitude; also scaled by the envelope. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oscillator")
	float DCOffset = 0.f;

	bool IsValid() const;
};

USTRUCT(BlueprintType)
struct IMAGINEGAME_API FIMGCameraShakeEnvelopeSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Envelope", meta = (ClampMin = "0", ForceUnits = "s"))
	float PreDelayTime = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Envelope", meta = (ClampMin = "0", ForceUnits = "s"))
	float AttackTime = 0.15f;

	/** With decay, attack reaches one before decaying to SustainLevel. Without decay, attack reaches SustainLevel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Envelope", meta = (ClampMin = "0", ForceUnits = "s"))
	float DecayTime = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Envelope", meta = (ClampMin = "0", ClampMax = "1"))
	float SustainLevel = 1.f;

	/** Negative means hold until Release; zero proceeds directly to release. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Envelope", meta = (ClampMin = "-1", ForceUnits = "s"))
	float HoldTime = -1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Envelope", meta = (ClampMin = "0", ForceUnits = "s"))
	float ReleaseTime = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Envelope")
	EIMGCameraShakeCurve AttackCurve = EIMGCameraShakeCurve::Linear;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Envelope")
	EIMGCameraShakeCurve ReleaseCurve = EIMGCameraShakeCurve::Exponential;

	bool IsValid() const;
};

USTRUCT(BlueprintType)
struct IMAGINEGAME_API FIMGCameraShakeSettings
{
	GENERATED_BODY()

	/** Camera-local position axes: X forward, Y right, Z up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Location")
	FIMGCameraShakeOscillatorSettings X;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Location")
	FIMGCameraShakeOscillatorSettings Y;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Location")
	FIMGCameraShakeOscillatorSettings Z;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rotation")
	FIMGCameraShakeOscillatorSettings Pitch;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rotation")
	FIMGCameraShakeOscillatorSettings Yaw;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rotation")
	FIMGCameraShakeOscillatorSettings Roll;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FOV")
	FIMGCameraShakeOscillatorSettings FOV;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Envelope")
	FIMGCameraShakeEnvelopeSettings Envelope;

	/** Used only by oscillators with a negative Phase. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oscillator")
	int32 RandomSeed = 0;

	bool IsValid() const;
};

/** Caller-owned numeric instance. No actor, gait, clock, Tick, replication or camera-manager dependency. */
struct IMAGINEGAME_API FIMGCameraShakeState
{
	/** Repeated starts are idempotent; starting during release resumes attack without resetting oscillator phases. */
	void Start(const FIMGCameraShakeSettings& Settings);
	void Release();
	void Reset();

	bool IsActive() const { return Phase != EPhase::Stopped; }
	bool IsReleasing() const { return Phase == EPhase::Release; }
	double GetEnvelopeLevel() const { return EnvelopeLevel; }

	/** Invalid inputs or a gap longer than 32/120 seconds stop this instance and return zero offsets. */
	void Update(float DeltaTime, float AmplitudeScale, float FrequencyScale, const FIMGCameraShakeSettings& Settings,
		FVector& OutLocationOffset, FRotator& OutRotationOffset, float& OutFOVOffset);

private:
	enum class EPhase : uint8 { Stopped, PreDelay, Attack, Decay, Hold, Release };
	void UpdateEnvelope(double DeltaTime, const FIMGCameraShakeEnvelopeSettings& Settings);

	double Angles[7] = {};
	double DecayAngles[7] = {};
	double EnvelopeLevel = 0.0;
	double ReleaseStartLevel = 0.0;
	double PhaseTime = 0.0;
	EPhase Phase = EPhase::Stopped;
};
