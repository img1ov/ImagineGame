

#include "GameModes/IMGExperienceManager.h"
#include "IMGLogChannels.h"

#if WITH_EDITOR

void UIMGExperienceManager::OnPlayInEditorBegun()
{
	// A previous PIE world's async teardown can still own requests when a new run starts.
	if (!GameFeaturePluginRequestCountMap.IsEmpty())
	{
		UE_LOG(LogIMGExperience, Verbose, TEXT("Preserving %d game feature request counts across PIE runs while teardown completes."), GameFeaturePluginRequestCountMap.Num());
	}
}

void UIMGExperienceManager::NotifyOfPluginActivation(const FString PluginURL)
{
	if (GIsEditor)
	{
		UIMGExperienceManager* ExperienceManagerSubsystem = GEngine->GetEngineSubsystem<UIMGExperienceManager>();
		check(ExperienceManagerSubsystem);

		// Track the number of requesters who activate this plugin. Multiple load/activation requests are always allowed because concurrent requests are handled.
		int32& Count = ExperienceManagerSubsystem->GameFeaturePluginRequestCountMap.FindOrAdd(PluginURL);
		++Count;
	}
}

bool UIMGExperienceManager::RequestToDeactivatePlugin(const FString PluginURL)
{
	if (GIsEditor)
	{
		UIMGExperienceManager* ExperienceManagerSubsystem = GEngine->GetEngineSubsystem<UIMGExperienceManager>();
		check(ExperienceManagerSubsystem);

		// Only let the last requester to get this far deactivate the plugin
		int32& Count = ExperienceManagerSubsystem->GameFeaturePluginRequestCountMap.FindChecked(PluginURL);
		--Count;

		if (Count == 0)
		{
			ExperienceManagerSubsystem->GameFeaturePluginRequestCountMap.Remove(PluginURL);
			return true;
		}

		return false;
	}

	return true;
}

#endif
