
#pragma once

#include "Components/GameStateComponent.h"
#include "LoadingProcessInterface.h"
#include "Engine/TimerHandle.h"

#include "IMGExperienceManagerComponent.generated.h"

class UIMGExperienceDefinition;
class UGameFeatureAction;
struct FStreamableHandle;
namespace UE::GameFeatures { struct FResult; }

DECLARE_MULTICAST_DELEGATE_OneParam(FOnIMGExperienceLoaded, const UIMGExperienceDefinition* /*Experience*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnIMGExperienceLoadFailed, const FString& /*Reason*/);

enum class EIMGExperienceLoadState
{
	Unloaded,
	Loading,
	LoadingGameFeatures,
	LoadingChaosTestingDelay,
	ExecutingActions,
	Loaded,
	Deactivating,
	Failed
};

UCLASS()
class IMAGINEGAME_API UIMGExperienceManagerComponent final : public UGameStateComponent, public ILoadingProcessInterface
{
	GENERATED_BODY()
	
public:
	
	UIMGExperienceManagerComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~UActorComponent interface
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~End of UActorComponent interface

	//~ILoadingProcessInterface interface
	virtual bool ShouldShowLoadingScreen(FString& OutReason) const override;
	//~End of ILoadingProcessInterface
	
	// Tries to set the current experience, either a UI or gameplay one
	void SetCurrentExperience(const FPrimaryAssetId& ExperienceId);

	// Ensures the delegate is called once the experience has been loaded,
	// before others are called.
	// However, if the experience has already loaded, calls the delegate immediately.
	void CallOrRegister_OnExperienceLoaded_HighPriority(FOnIMGExperienceLoaded::FDelegate&& Delegate);

	// Ensures the delegate is called once the experience has been loaded
	// If the experience has already loaded, calls the delegate immediately
	void CallOrRegister_OnExperienceLoaded(FOnIMGExperienceLoaded::FDelegate&& Delegate);

	// Ensures the delegate is called once the experience has been loaded
	// If the experience has already loaded, calls the delegate immediately
	void CallOrRegister_OnExperienceLoaded_LowPriority(FOnIMGExperienceLoaded::FDelegate&& Delegate);

	// This returns the current experience if it is fully loaded, asserting otherwise
	// (i.e., if you called it too soon)
	const UIMGExperienceDefinition* GetCurrentExperienceChecked() const;

	// Returns true if the experience is fully loaded
	bool IsExperienceLoaded() const;
	bool HasExperienceLoadFailed() const { return bLoadFailed; }
	const FString& GetExperienceLoadError() const { return ExperienceLoadError; }
	// Also notifies pending waiters on EndPlay; this is a local lifecycle result.
	void CallOrRegister_OnExperienceLoadFailed(FOnIMGExperienceLoadFailed::FDelegate&& Delegate);
	// In-flight callbacks must still guard their own completion after unsubscribing.
	void RemoveExperienceLoadDelegates(const UObject* Listener);

private:
	
	UFUNCTION()
	void OnRep_CurrentExperience();

	void StartExperienceLoad();
	void OnExperienceLoadComplete();
	void OnGameFeaturePluginLoadComplete(const UE::GameFeatures::FResult& Result, FString PluginURL);
	void OnExperienceFullLoadCompleted();
	void FailExperienceLoad(const FString& Reason);
	void BeginDeactivation();

	void OnAllActionsDeactivated();
	static void ReleaseGameFeaturePluginRequest(const FString& PluginURL);

private:
	
	UPROPERTY(ReplicatedUsing = OnRep_CurrentExperience)
	TObjectPtr<const UIMGExperienceDefinition> CurrentExperience;
	FString ExperienceLoadError;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UGameFeatureAction>> RegisteredActions;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UGameFeatureAction>> LoadedActions;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UGameFeatureAction>> ActivatedActions;

	TSharedPtr<FStreamableHandle> ExperienceLoadHandle;
	FTimerHandle ExperienceLoadDelayTimer;
	FName ExperienceWorldContext;
	bool bEndingPlay = false;
	bool bLoadFailed = false;
	bool bExecutingCallbacks = false;
	bool bDeactivationStarted = false;

	EIMGExperienceLoadState LoadState = EIMGExperienceLoadState::Unloaded;

	TSet<FString> PendingGameFeaturePluginURLs;
	TArray<FString> GameFeaturePluginURLs;

	/**
	 * Delegate called when the experience has finished loading just before others
	 * (e.g., subsystems that set up for regular gameplay)
	 */
	TArray<FOnIMGExperienceLoaded::FDelegate> OnExperienceLoaded_HighPriority;

	/** Delegate called when the experience has finished loading */
	TArray<FOnIMGExperienceLoaded::FDelegate> OnExperienceLoaded;

	/** Delegate called when the experience has finished loading */
	TArray<FOnIMGExperienceLoaded::FDelegate> OnExperienceLoaded_LowPriority;
	FOnIMGExperienceLoadFailed OnExperienceLoadFailed;
};
