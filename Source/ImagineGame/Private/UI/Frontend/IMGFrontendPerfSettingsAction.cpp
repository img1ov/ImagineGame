#include "UI/Frontend/IMGFrontendPerfSettingsAction.h"

#include "Settings/IMGSettingsLocal.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGFrontendPerfSettingsAction)

struct FGameFeatureActivatingContext;
struct FGameFeatureDeactivatingContext;

//////////////////////////////////////////////////////////////////////
// UIMGFrontendPerfSettingsAction

// Game user settings (and engine performance/scalability settings they drive)
// are global, so there's no point in tracking this per world for multi-player PIE:
// we just apply it if any PIE world is in the menu.
//
// However, by default we won't apply front-end performance stuff in the editor
// unless the developer setting ApplyFrontEndPerformanceOptionsInPIE is enabled
int32 UIMGFrontendPerfSettingsAction::ApplicationCounter = 0;

void UIMGFrontendPerfSettingsAction::OnGameFeatureActivating(FGameFeatureActivatingContext& Context)
{
	ApplicationCounter++;
	if (ApplicationCounter == 1)
	{
		UIMGSettingsLocal::Get()->SetShouldUseFrontendPerformanceSettings(true);
	}
}

void UIMGFrontendPerfSettingsAction::OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context)
{
	ApplicationCounter--;
	check(ApplicationCounter >= 0);

	if (ApplicationCounter == 0)
	{
		UIMGSettingsLocal::Get()->SetShouldUseFrontendPerformanceSettings(false);
	}
}

