// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameSettingValueDiscrete.h"

#include "IMGSettingValueDiscrete_PerfStat.generated.h"

enum class EIMGDisplayablePerformanceStat : uint8;
enum class EIMGStatDisplayMode : uint8;

class UObject;

UCLASS()
class UIMGSettingValueDiscrete_PerfStat : public UGameSettingValueDiscrete
{
	GENERATED_BODY()

public:

	UIMGSettingValueDiscrete_PerfStat();

	void SetStat(EIMGDisplayablePerformanceStat InStat);

	/** UGameSettingValue */
	virtual void StoreInitial() override;
	virtual void ResetToDefault() override;
	virtual void RestoreToInitial() override;

	/** UGameSettingValueDiscrete */
	virtual void SetDiscreteOptionByIndex(int32 Index) override;
	virtual int32 GetDiscreteOptionIndex() const override;
	virtual TArray<FText> GetDiscreteOptions() const override;

protected:
	/** UGameSettingValue */
	virtual void OnInitialized() override;

	void AddMode(FText&& Label, EIMGStatDisplayMode Mode);
protected:
	TArray<FText> Options;
	TArray<EIMGStatDisplayMode> DisplayModes;

	EIMGDisplayablePerformanceStat StatToDisplay;
	EIMGStatDisplayMode InitialMode;
};
