// Fill out your copyright notice in the Description page of Project Settings.


#include "System/IMGAssetManager.h"

#include "IMGLogChannels.h"
#include "AbilitySystem/IMGGameplayCueManager.h"
#include "Character/IMGPawnData.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "System/IMGGameData.h"

const FName FIMGBundles::Equipped("Equipped");

//////////////////////////////////////////////////////////////////////

static FAutoConsoleCommand CVarDumpLoadedAssets(
	TEXT("IMG.DumpLoadedAssets"),
	TEXT("Shows all assets that were loaded via the asset manager and are currently in memory."),
	FConsoleCommandDelegate::CreateStatic(UIMGAssetManager::DumpLoadedAssets)
);

//////////////////////////////////////////////////////////////////////

#define STARTUP_JOB_WEIMGHTED(JobFunc, JobWeight) StartupJobs.Add(FAssetManagerStartupJob(#JobFunc, [this](const FAssetManagerStartupJob& StartupJob, TSharedPtr<FStreamableHandle>& LoadHandle){JobFunc;}, JobWeight))
#define STARTUP_JOB(JobFunc) STARTUP_JOB_WEIMGHTED(JobFunc, 1.f)

//////////////////////////////////////////////////////////////////////

static bool ShouldAllowMissingGameData()
{
#if UE_BUILD_SHIPPING
	return false;
#else
	// Default: allow booting in Development builds even when required assets aren't set up yet.
	// Use -RequireGameData to force a hard fail (useful for CI / QA).
	return !FParse::Param(FCommandLine::Get(), TEXT("RequireGameData"));
#endif
}

UIMGAssetManager::UIMGAssetManager()
{
	DefaultPawnData = nullptr;
}

UIMGAssetManager& UIMGAssetManager::Get()
{
	check(GEngine);

	if (UIMGAssetManager* Singleton = Cast<UIMGAssetManager>(GEngine->AssetManager))
	{
		return *Singleton;
	}

	UE_LOG(LogIMG, Fatal, TEXT("Invalid AssetManagerClassName in DefaultEngine.ini.  It must be set to IMGAssetManager!"));

	// Fatal error above prevents this from being called.
	return *NewObject<UIMGAssetManager>();
}

void UIMGAssetManager::DumpLoadedAssets()
{
	UE_LOG(LogIMG, Log, TEXT("========== Start Dumping Loaded Assets =========="));

	for (const UObject* LoadedAsset : Get().LoadedAssets)
	{
		UE_LOG(LogIMG, Log, TEXT("  %s"), *GetNameSafe(LoadedAsset));
	}

	UE_LOG(LogIMG, Log, TEXT("... %d assets in loaded pool"), Get().LoadedAssets.Num());
	UE_LOG(LogIMG, Log, TEXT("========== Finish Dumping Loaded Assets =========="));
}

const UIMGGameData& UIMGAssetManager::GetGameData()
{
	// If a fallback was created earlier, use it.
	if (TObjectPtr<UPrimaryDataAsset> const* pResult = GameDataMap.Find(UIMGGameData::StaticClass()))
	{
		return *CastChecked<UIMGGameData>(*pResult);
	}

	// IMGGameDataPath is a UPROPERTY(Config) (see IMGAssetManager.h). If it isn't set in DefaultGame.ini,
	// the load will fail with an empty path and crash later with an unhelpful "at ." message.
	if (IMGGameDataPath.IsNull())
	{
		if (ShouldAllowMissingGameData())
		{
			UE_LOG(LogIMG, Error, TEXT("IMGGmeDataPath is not set. Configure it in Config/DefaultGame.ini under [/Script/ImagineGame.IMGAssetManager]. Example: IMGGameDataPath=/Game/System/DA_IMGGameData.DA_IMGGameData. Using a transient fallback UIMGGameData."));

			UIMGGameData* Fallback = NewObject<UIMGGameData>(GetTransientPackage(), UIMGGameData::StaticClass());
			GameDataMap.Add(UIMGGameData::StaticClass(), Fallback);
			return *Fallback;
		}

		UE_LOG(LogIMG, Fatal, TEXT("IMGGameDataPath is not set. Configure it in Config/DefaultGame.ini under [/Script/ImagineGame.IMGAssetManager]. Example: IMGGameDataPath=/Game/System/DA_IMGGameData.DA_IMGGameData"));
	}
	return GetOrLoadTypedGameData<UIMGGameData>(IMGGameDataPath);
}

UIMGPawnData* UIMGAssetManager::GetDefaultPawnData() const
{
	// Make missing/failed default pawn data non-fatal so the editor can boot during early project setup.
	// This is only a convenience default; experiences/world settings can provide their own pawn data.
	static bool bLoggedMissing = false;
	static FString LastFailedPath;
	static FString LastLoadedPath;

	if (DefaultPawnData.IsNull())
	{
		if (!bLoggedMissing)
		{
			UE_LOG(LogIMG, Error, TEXT("DefaultPawnData is not set. Configure it in Config/DefaultGame.ini under [/Script/ImagineGame.IMGAssetManager]."));
			bLoggedMissing = true;
		}
		return nullptr;
	}

	UIMGPawnData* Loaded = DefaultPawnData.Get();
	if (!Loaded)
	{
		Loaded = DefaultPawnData.LoadSynchronous();
	}

	const FString PathString = DefaultPawnData.ToString();
	if (!Loaded)
	{
		if (!PathString.Equals(LastFailedPath, ESearchCase::CaseSensitive))
		{
			UE_LOG(LogIMG, Error, TEXT("Failed to load DefaultPawnData at %s."), *PathString);
			LastFailedPath = PathString;
		}
		return nullptr;
	}

	// Track it like other asset manager loads.
	UIMGAssetManager::Get().AddLoadedAsset(Loaded);

	if (!PathString.Equals(LastLoadedPath, ESearchCase::CaseSensitive))
	{
		UE_LOG(LogIMG, Display, TEXT("Loaded DefaultPawnData: %s"), *PathString);
		LastLoadedPath = PathString;
	}

	return Loaded;
}

UObject* UIMGAssetManager::SynchronousLoadAsset(const FSoftObjectPath& AssetPath)
{
	if (AssetPath.IsValid())
	{
		TUniquePtr<FScopeLogTime> LogTimePtr;

		if (ShouldLogAssetLoads())
		{
			LogTimePtr = MakeUnique<FScopeLogTime>(*FString::Printf(TEXT("Synchronously loaded asset [%s]"), *AssetPath.ToString()), nullptr, FScopeLogTime::ScopeLog_Seconds);
		}

		if (UAssetManager::IsInitialized())
		{
			return UAssetManager::GetStreamableManager().LoadSynchronous(AssetPath, false);
		}

		// Use LoadObject if asset manager isn't ready yet.
		return AssetPath.TryLoad();
	}

	return nullptr;
}

bool UIMGAssetManager::ShouldLogAssetLoads()
{
	static bool bLogAssetLoads = FParse::Param(FCommandLine::Get(), TEXT("LogAssetLoads"));
	return bLogAssetLoads;
}

void UIMGAssetManager::AddLoadedAsset(const UObject* Asset)
{
	if (ensureAlways(Asset))
	{
		FScopeLock LoadedAssetsLock(&LoadedAssetsCritical);
		LoadedAssets.Add(Asset);
	}
}

void UIMGAssetManager::StartInitialLoading()
{
	SCOPED_BOOT_TIMING("UIMGAssetManager::StartInitialLoading");

	// This does all of the scanning, need to do this now even if loads are deferred
	Super::StartInitialLoading();

	STARTUP_JOB(InitializeGameplayCueManager());

	{
		// Load base game data asset
		STARTUP_JOB_WEIMGHTED(GetGameData(), 25.f);
	}

	// Run all the queued up startup jobs
	DoAllStartupJobs();
}

void UIMGAssetManager::PreBeginPIE(bool bStartSimulate)
{
	Super::PreBeginPIE(bStartSimulate);

	{
		FScopedSlowTask SlowTask(0, NSLOCTEXT("IMGEditor", "BeginLoadingPIEData", "Loading PIE Data"));
		const bool bShowCancelButton = false;
		const bool bAllowInPIE = true;
		SlowTask.MakeDialog(bShowCancelButton, bAllowInPIE);

		const UIMGGameData& LocalGameDataCommon = GetGameData();

		// Intentionally after GetGameData to avoid counting GameData time in this timer
		SCOPE_LOG_TIME_IN_SECONDS(TEXT("PreBeginPIE asset preloading complete"), nullptr);

		// You could add preloading of anything else needed for the experience we'll be using here
		// (e.g., by grabbing the default experience from the world settings + the experience override in developer settings)
	}
}

UPrimaryDataAsset* UIMGAssetManager::LoadGameDataOfClass(TSubclassOf<UPrimaryDataAsset> DataClass,
	const TSoftObjectPtr<UPrimaryDataAsset>& DataClassPath, FPrimaryAssetType PrimaryAssetType)
{
	UPrimaryDataAsset* Asset = nullptr;

	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("Loading GameData Object"), STAT_GameData, STATGROUP_LoadTime);
	if (DataClassPath.IsNull())
	{
		if (ShouldAllowMissingGameData())
		{
			UE_LOG(LogIMG, Error, TEXT("GameData path is null for class %s (PrimaryAssetType %s). This usually means the corresponding Config UPROPERTY was not set. Using a transient fallback."),
				*GetNameSafe(DataClass.Get()), *PrimaryAssetType.ToString());
			Asset = NewObject<UPrimaryDataAsset>(GetTransientPackage(), DataClass.Get());
		}
		else
		{
			UE_LOG(LogIMG, Fatal, TEXT("GameData path is null for class %s (PrimaryAssetType %s). This usually means the corresponding Config UPROPERTY was not set (e.g. Config/DefaultGame.ini [/Script/ImagineGame.IMGAssetManager])."),
				*GetNameSafe(DataClass.Get()), *PrimaryAssetType.ToString());
		}
	}
	
	if (!DataClassPath.IsNull())
	{
#if WITH_EDITOR
		FScopedSlowTask SlowTask(0, FText::Format(NSLOCTEXT("IMGEditor", "BeginLoadingGameDataTask", "Loading GameData {0}"), FText::FromName(DataClass->GetFName())));
		const bool bShowCancelButton = false;
		const bool bAllowInPIE = true;
		SlowTask.MakeDialog(bShowCancelButton, bAllowInPIE);
#endif
		UE_LOG(LogIMG, Log, TEXT("Loading GameData: %s ..."), *DataClassPath.ToString());
		SCOPE_LOG_TIME_IN_SECONDS(TEXT("    ... GameData loaded!"), nullptr);

		// Always load by explicit path. This keeps runtime behavior independent of AssetManager type scanning setup.
		Asset = DataClassPath.LoadSynchronous();
		if (Asset)
		{
			UE_LOG(LogIMG, Display, TEXT("Loaded GameData asset at %s. Type %s."), *DataClassPath.ToString(), *PrimaryAssetType.ToString());
		}
	}

	if (Asset)
	{
		GameDataMap.Add(DataClass, Asset);
	}
	else
	{
		// It is not acceptable to fail to load any GameData asset. It will result in soft failures that are hard to diagnose.
		// In Development builds we allow booting so missing assets/config can be created later.
		if (ShouldAllowMissingGameData())
		{
			UE_LOG(LogIMG, Error, TEXT("Failed to load GameData asset at %s. Type %s. Using a transient fallback."),
				*DataClassPath.ToString(), *PrimaryAssetType.ToString());
			Asset = NewObject<UPrimaryDataAsset>(GetTransientPackage(), DataClass.Get());
			GameDataMap.Add(DataClass, Asset);
		}
		else
		{
			UE_LOG(LogIMG, Fatal, TEXT("Failed to load GameData asset at %s. Type %s. This is not recoverable and likely means you do not have the correct data to run %s."),
				*DataClassPath.ToString(), *PrimaryAssetType.ToString(), FApp::GetProjectName());
		}
	}

	return Asset;
}

void UIMGAssetManager::DoAllStartupJobs()
{
	SCOPED_BOOT_TIMING("UIMGAssetManager::DoAllStartupJobs");
	const double AllStartupJobsStartTime = FPlatformTime::Seconds();

	if (IsRunningDedicatedServer())
	{
		// No need for periodic progress updates, just run the jobs
		for (const FAssetManagerStartupJob& StartupJob : StartupJobs)
		{
			StartupJob.DoJob();
		}
	}
	else
	{
		if (StartupJobs.Num() > 0)
		{
			float TotalJobValue = 0.0f;
			for (const FAssetManagerStartupJob& StartupJob : StartupJobs)
			{
				TotalJobValue += StartupJob.JobWeight;
			}

			float AccumulatedJobValue = 0.0f;
			for (FAssetManagerStartupJob& StartupJob : StartupJobs)
			{
				const float JobValue = StartupJob.JobWeight;
				StartupJob.SubstepProgressDelegate.BindLambda([This = this, AccumulatedJobValue, JobValue, TotalJobValue](float NewProgress)
					{
						const float SubstepAdjustment = FMath::Clamp(NewProgress, 0.0f, 1.0f) * JobValue;
						const float OverallPercentWithSubstep = (AccumulatedJobValue + SubstepAdjustment) / TotalJobValue;

						This->UpdateInitialGameContentLoadPercent(OverallPercentWithSubstep);
					});

				StartupJob.DoJob();

				StartupJob.SubstepProgressDelegate.Unbind();

				AccumulatedJobValue += JobValue;

				UpdateInitialGameContentLoadPercent(AccumulatedJobValue / TotalJobValue);
			}
		}
		else
		{
			UpdateInitialGameContentLoadPercent(1.0f);
		}
	}

	StartupJobs.Empty();

	UE_LOG(LogIMG, Display, TEXT("All startup jobs took %.2f seconds to complete"), FPlatformTime::Seconds() - AllStartupJobsStartTime);
}

void UIMGAssetManager::InitializeGameplayCueManager()
{
	SCOPED_BOOT_TIMING("UIMGAssetManager::InitializeGameplayCueManager");
	
	UIMGGameplayCueManager* GCM = UIMGGameplayCueManager::Get();
	check(GCM);
	GCM->LoadAlwaysLoadedCues();
}

void UIMGAssetManager::UpdateInitialGameContentLoadPercent(float GameContentPercent)
{
	// Could route this to the early startup loading screen
}
