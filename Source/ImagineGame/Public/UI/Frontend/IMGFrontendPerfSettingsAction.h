#pragma once

#include "GameFeatureAction.h"

#include "IMGFrontendPerfSettingsAction.generated.h"

class UObject;
struct FGameFeatureActivatingContext;
struct FGameFeatureDeactivatingContext;

//////////////////////////////////////////////////////////////////////
// UIMGFrontendPerfSettingsAction

/**
 * GameFeatureAction responsible for telling the user settings to apply frontend/menu specific performance settings
 */
UCLASS(MinimalAPI, meta = (DisplayName = "Use Frontend Perf Settings"))
class UIMGFrontendPerfSettingsAction final : public UGameFeatureAction
{
	GENERATED_BODY()

public:
	//~UGameFeatureAction interface
	virtual void OnGameFeatureActivating(FGameFeatureActivatingContext& Context) override;
	virtual void OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context) override;
	//~End of UGameFeatureAction interface

private:
	static int32 ApplicationCounter;
};
