#include "Performance/IMGPerformanceSettings.h"

#include "Engine/PlatformSettingsManager.h"
#include "Misc/EnumRange.h"
#include "Performance/IMGPerformanceStatTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGPerformanceSettings)

//////////////////////////////////////////////////////////////////////

UIMGPlatformSpecificRenderingSettings::UIMGPlatformSpecificRenderingSettings()
{
	MobileFrameRateLimits.Append({ 20, 30, 45, 60, 90, 120 });
}

const UIMGPlatformSpecificRenderingSettings* UIMGPlatformSpecificRenderingSettings::Get()
{
	UIMGPlatformSpecificRenderingSettings* Result = UPlatformSettingsManager::Get().GetSettingsForPlatform<ThisClass>();
	check(Result);
	return Result;
}

//////////////////////////////////////////////////////////////////////

UIMGPerformanceSettings::UIMGPerformanceSettings()
{
	PerPlatformSettings.Initialize(UIMGPlatformSpecificRenderingSettings::StaticClass());

	CategoryName = TEXT("Game");

	DesktopFrameRateLimits.Append({ 30, 60, 120, 144, 160, 165, 180, 200, 240, 360 });

	// Default to all stats are allowed
	FIMGPerformanceStatGroup& StatGroup = UserFacingPerformanceStats.AddDefaulted_GetRef();
	for (EIMGDisplayablePerformanceStat PerfStat : TEnumRange<EIMGDisplayablePerformanceStat>())
	{
		StatGroup.AllowedStats.Add(PerfStat);
	}
}

