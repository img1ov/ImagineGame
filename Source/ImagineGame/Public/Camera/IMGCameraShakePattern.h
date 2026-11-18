#pragma once

#include "Camera/CameraShakeBase.h"
#include "Camera/IMGCameraShake.h"
#include "IMGCameraShakePattern.generated.h"

/** Adapts the numeric oscillators/envelopes to a camera view; contains no gameplay sampling. */
UCLASS(EditInlineNew)
class IMAGINEGAME_API UIMGCameraShakePattern : public UCameraShakePattern
{
	GENERATED_BODY()

public:
	void Prepare(const FIMGCameraShakeSettings& Settings, bool bContinuous, int32 RandomSeed);
	void SetFrequencyScale(float Scale) { FrequencyScale = Scale; }
	float GetFrequencyScale() const { return FrequencyScale; }
	void Resume();
	bool IsReleasing() const { return State.IsReleasing(); }
	double GetEnvelopeLevel() const { return State.GetEnvelopeLevel(); }
	double GetReleaseLevel() const { return State.GetReleaseLevel(); }

protected:
	virtual void GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const override;
	virtual void StartShakePatternImpl(const FCameraShakePatternStartParams& Params) override;
	virtual void UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params, FCameraShakePatternUpdateResult& OutResult) override;
	virtual void ScrubShakePatternImpl(const FCameraShakePatternScrubParams& Params, FCameraShakePatternUpdateResult& OutResult) override;
	virtual bool IsFinishedImpl() const override { return !State.IsActive(); }
	virtual void StopShakePatternImpl(const FCameraShakePatternStopParams& Params) override;
	virtual void TeardownShakePatternImpl() override;

private:
	void ApplyValues(const FMinimalViewInfo& View, float Scale, FCameraShakePatternUpdateResult& OutResult) const;

	FIMGCameraShakeSettings ActiveSettings;
	FIMGCameraShakeState State;
	TArray<double> Values;
	int32 Seed = 0;
	float FrequencyScale = 1.f;
	uint64 LastUpdateFrame = MAX_uint64;
	bool bPrepared = false;
};
