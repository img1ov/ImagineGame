#include "GameModes/IMGExperienceManagerComponent.h"

#include "IMGLogChannels.h"
#include "Engine/StreamableManager.h"
#include "GameFeatureAction.h"
#include "GameFeaturesSubsystem.h"
#include "GameFeaturesSubsystemSettings.h"
#include "GameModes/IMGExperienceActionSet.h"
#include "GameModes/IMGExperienceDefinition.h"
#include "GameModes/IMGExperienceManager.h"
#include "Net/UnrealNetwork.h"
#include "Settings/IMGSettingsLocal.h"
#include "System/IMGAssetManager.h"
#include "TimerManager.h"
#include "UObject/StrongObjectPtr.h"

namespace IMGConsoleVariables
{
	static float ExperienceLoadRandomDelayMin = 0.0f;
	static FAutoConsoleVariableRef CVarExperienceLoadRandomDelayMin(
		TEXT("IMG.chaos.ExperienceDelayLoad.MinSecs"), ExperienceLoadRandomDelayMin,
		TEXT("Fixed delay before experience load completion, in seconds."), ECVF_Default);
	static float ExperienceLoadRandomDelayRange = 0.0f;
	static FAutoConsoleVariableRef CVarExperienceLoadRandomDelayRange(
		TEXT("IMG.chaos.ExperienceDelayLoad.RandomSecs"), ExperienceLoadRandomDelayRange,
		TEXT("Random additional delay before experience load completion, in seconds."), ECVF_Default);
	float GetExperienceLoadDelayDuration()
	{
		return FMath::Max(0.0f, ExperienceLoadRandomDelayMin + FMath::FRand() * ExperienceLoadRandomDelayRange);
	}
}

UIMGExperienceManagerComponent::UIMGExperienceManagerComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsReplicatedByDefault(true);
}

void UIMGExperienceManagerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (!bEndingPlay)
	{
		bEndingPlay = true;
		BeginDeactivation();
		FOnIMGExperienceLoadFailed FailureDelegates = MoveTemp(OnExperienceLoadFailed);
		FailureDelegates.Broadcast(TEXT("Experience ended"));
	}
	Super::EndPlay(EndPlayReason);
}

void UIMGExperienceManagerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, CurrentExperience);
}

void UIMGExperienceManagerComponent::SetCurrentExperience(const FPrimaryAssetId& ExperienceId)
{
	if (bEndingPlay || bLoadFailed || LoadState != EIMGExperienceLoadState::Unloaded || CurrentExperience)
	{
		UE_LOG(LogIMGExperience, Warning, TEXT("Ignoring repeated experience assignment: %s"), *ExperienceId.ToString());
		return;
	}
	const FSoftObjectPath AssetPath = UIMGAssetManager::Get().GetPrimaryAssetPath(ExperienceId);
	UClass* AssetClass = Cast<UClass>(AssetPath.TryLoad());
	if (bEndingPlay || bLoadFailed)
	{
		return;
	}
	if (!AssetClass || !AssetClass->IsChildOf(UIMGExperienceDefinition::StaticClass()))
	{
		FailExperienceLoad(FString::Printf(TEXT("Invalid experience definition: %s (%s)"), *ExperienceId.ToString(), *AssetPath.ToString()));
		return;
	}
	CurrentExperience = GetDefault<UIMGExperienceDefinition>(AssetClass);
	StartExperienceLoad();
}

void UIMGExperienceManagerComponent::CallOrRegister_OnExperienceLoaded_HighPriority(FOnIMGExperienceLoaded::FDelegate&& Delegate)
{
	if (IsExperienceLoaded())
	{
		Delegate.ExecuteIfBound(CurrentExperience);
	}
	else if (!bEndingPlay && !bLoadFailed && LoadState != EIMGExperienceLoadState::Deactivating)
	{
		OnExperienceLoaded_HighPriority.Add(MoveTemp(Delegate));
	}
}

void UIMGExperienceManagerComponent::CallOrRegister_OnExperienceLoaded(FOnIMGExperienceLoaded::FDelegate&& Delegate)
{
	if (IsExperienceLoaded())
	{
		Delegate.ExecuteIfBound(CurrentExperience);
	}
	else if (!bEndingPlay && !bLoadFailed && LoadState != EIMGExperienceLoadState::Deactivating)
	{
		OnExperienceLoaded.Add(MoveTemp(Delegate));
	}
}

void UIMGExperienceManagerComponent::RemoveExperienceLoadDelegates(const UObject* Listener)
{
	auto IsListener = [Listener](const FOnIMGExperienceLoaded::FDelegate& Delegate)
	{
		return Delegate.IsBoundToObject(Listener);
	};
	OnExperienceLoaded_HighPriority.RemoveAll(IsListener);
	OnExperienceLoaded.RemoveAll(IsListener);
	OnExperienceLoaded_LowPriority.RemoveAll(IsListener);
	OnExperienceLoadFailed.RemoveAll(Listener);
}

void UIMGExperienceManagerComponent::CallOrRegister_OnExperienceLoaded_LowPriority(FOnIMGExperienceLoaded::FDelegate&& Delegate)
{
	if (IsExperienceLoaded())
	{
		Delegate.ExecuteIfBound(CurrentExperience);
	}
	else if (!bEndingPlay && !bLoadFailed && LoadState != EIMGExperienceLoadState::Deactivating)
	{
		OnExperienceLoaded_LowPriority.Add(MoveTemp(Delegate));
	}
}

void UIMGExperienceManagerComponent::CallOrRegister_OnExperienceLoadFailed(FOnIMGExperienceLoadFailed::FDelegate&& Delegate)
{
	if (bEndingPlay || bLoadFailed)
	{
		Delegate.ExecuteIfBound(bLoadFailed ? ExperienceLoadError : FString(TEXT("Experience ended")));
	}
	else
	{
		OnExperienceLoadFailed.Add(MoveTemp(Delegate));
	}
}

const UIMGExperienceDefinition* UIMGExperienceManagerComponent::GetCurrentExperienceChecked() const
{
	check(IsExperienceLoaded());
	return CurrentExperience;
}

bool UIMGExperienceManagerComponent::IsExperienceLoaded() const
{
	return !bEndingPlay && !bLoadFailed && LoadState == EIMGExperienceLoadState::Loaded && CurrentExperience != nullptr;
}

void UIMGExperienceManagerComponent::OnRep_CurrentExperience()
{
	if (CurrentExperience)
	{
		StartExperienceLoad();
	}
}

void UIMGExperienceManagerComponent::StartExperienceLoad()
{
	if (bEndingPlay || bLoadFailed || LoadState != EIMGExperienceLoadState::Unloaded || !CurrentExperience)
	{
		return;
	}
	LoadState = EIMGExperienceLoadState::Loading;
	UE_LOG(LogIMGExperience, Log, TEXT("EXPERIENCE: Loading %s (%s)"),
		*CurrentExperience->GetPrimaryAssetId().ToString(), *GetClientServerContextString(this));
	if (const FWorldContext* Context = GEngine->GetWorldContextFromWorld(GetWorld()))
	{
		ExperienceWorldContext = Context->ContextHandle;
	}
	else
	{
		FailExperienceLoad(TEXT("Experience world context is unavailable"));
		return;
	}
	TSet<FPrimaryAssetId> BundleAssets;
	BundleAssets.Add(CurrentExperience->GetPrimaryAssetId());
	for (const UIMGExperienceActionSet* ActionSet : CurrentExperience->ActionSets)
	{
		if (ActionSet)
		{
			BundleAssets.Add(ActionSet->GetPrimaryAssetId());
		}
	}
	TArray<FName> BundlesToLoad { FIMGBundles::Equipped };
	for (const FPrimaryAssetId& AssetId : BundleAssets)
	{
		if (!UIMGAssetManager::Get().GetPrimaryAssetPath(AssetId).IsValid())
		{
			FailExperienceLoad(FString::Printf(TEXT("Unregistered experience bundle asset: %s"), *AssetId.ToString()));
			return;
		}
	}
	const ENetMode NetMode = GetOwner()->GetNetMode();
	if (GIsEditor || NetMode != NM_DedicatedServer)
	{
		BundlesToLoad.Add(UGameFeaturesSubsystemSettings::LoadStateClient);
	}
	if (GIsEditor || NetMode != NM_Client)
	{
		BundlesToLoad.Add(UGameFeaturesSubsystemSettings::LoadStateServer);
	}
	TSharedPtr<FStreamableHandle> Handle = UIMGAssetManager::Get().ChangeBundleStateForPrimaryAssets(
		BundleAssets.Array(), BundlesToLoad, {}, false, FStreamableDelegate(), FStreamableManager::AsyncLoadHighPriority);
	// AssetManager can pump messages while building the request.
	if (bEndingPlay || bLoadFailed)
	{
		if (Handle.IsValid())
		{
			Handle->CancelHandle();
		}
		return;
	}
	ExperienceLoadHandle = MoveTemp(Handle);
	if (!ExperienceLoadHandle.IsValid() || ExperienceLoadHandle->HasLoadCompleted())
	{
		OnExperienceLoadComplete();
	}
	else if (ExperienceLoadHandle->WasCanceled())
	{
		FailExperienceLoad(TEXT("Experience asset loading cancelled"));
	}
	else
	{
		ExperienceLoadHandle->BindCompleteDelegate(FStreamableDelegate::CreateUObject(this, &ThisClass::OnExperienceLoadComplete));
		ExperienceLoadHandle->BindCancelDelegate(FStreamableDelegate::CreateWeakLambda(this, [this]()
		{
			if (LoadState == EIMGExperienceLoadState::Loading)
			{
				FailExperienceLoad(TEXT("Experience asset loading cancelled"));
			}
		}));
	}
}

void UIMGExperienceManagerComponent::OnExperienceLoadComplete()
{
	if (bEndingPlay || bLoadFailed || LoadState != EIMGExperienceLoadState::Loading)
	{
		return;
	}
	auto CheckLoadHandle = [this](const TSharedPtr<FStreamableHandle>& Handle)
	{
		if (!Handle.IsValid())
		{
			return true;
		}
		TArray<UObject*> LoadedAssets;
		Handle->GetLoadedAssets(LoadedAssets);
		const bool bChildFailed = Handle->FindMatchingHandle([](const FStreamableHandle& Child)
		{
			return Child.WasCanceled() || Child.HasError();
		}).IsValid();
		if (bChildFailed || LoadedAssets.Contains(nullptr))
		{
			FailExperienceLoad(TEXT("Failed to load experience asset bundles"));
			return false;
		}
		return true;
	};
	if (!CheckLoadHandle(ExperienceLoadHandle))
	{
		return;
	}
	// Unchanged bundle states may return no new handle; validate their retained primary handles too.
	UIMGAssetManager& AssetManager = UIMGAssetManager::Get();
	if (!CheckLoadHandle(AssetManager.GetPrimaryAssetHandle(CurrentExperience->GetPrimaryAssetId())))
	{
		return;
	}
	for (const UIMGExperienceActionSet* ActionSet : CurrentExperience->ActionSets)
	{
		if (ActionSet && !CheckLoadHandle(AssetManager.GetPrimaryAssetHandle(ActionSet->GetPrimaryAssetId())))
		{
			return;
		}
	}
	TArray<FString> PluginURLs;
	auto CollectPlugins = [this, &PluginURLs](const TArray<FString>& PluginNames)
	{
		for (const FString& PluginName : PluginNames)
		{
			FString URL;
			if (!UGameFeaturesSubsystem::Get().GetPluginURLByName(PluginName, URL))
			{
				FailExperienceLoad(FString::Printf(TEXT("Unknown game feature plugin: %s"), *PluginName));
				return false;
			}
			PluginURLs.AddUnique(URL);
		}
		return true;
	};
	if (!CollectPlugins(CurrentExperience->GameFeaturesToEnable))
	{
		return;
	}
	for (const UIMGExperienceActionSet* ActionSet : CurrentExperience->ActionSets)
	{
		if (ActionSet && !CollectPlugins(ActionSet->GameFeaturesToEnable))
		{
			return;
		}
	}
	LoadState = EIMGExperienceLoadState::LoadingGameFeatures;
	// Plugin completions can be synchronous; finish only after dispatch unwinds.
	bExecutingCallbacks = true;
	for (const FString& URL : PluginURLs)
	{
		if (bEndingPlay || bLoadFailed)
		{
			break;
		}
		GameFeaturePluginURLs.Add(URL);
		PendingGameFeaturePluginURLs.Add(URL);
		UIMGExperienceManager::NotifyOfPluginActivation(URL);
		TWeakObjectPtr<ThisClass> WeakThis(this);
		UGameFeaturesSubsystem::Get().LoadAndActivateGameFeaturePlugin(URL,
			FGameFeaturePluginLoadComplete::CreateLambda([WeakThis, URL](const UE::GameFeatures::FResult& Result)
			{
				if (ThisClass* Component = WeakThis.Get())
				{
					Component->OnGameFeaturePluginLoadComplete(Result, URL);
				}
				else
				{
					ReleaseGameFeaturePluginRequest(URL);
				}
			}));
	}
	bExecutingCallbacks = false;
	if (bEndingPlay || bLoadFailed)
	{
		BeginDeactivation();
	}
	else if (PendingGameFeaturePluginURLs.IsEmpty())
	{
		OnExperienceFullLoadCompleted();
	}
}

void UIMGExperienceManagerComponent::OnGameFeaturePluginLoadComplete(const UE::GameFeatures::FResult& Result, FString PluginURL)
{
	if (PendingGameFeaturePluginURLs.Remove(PluginURL) == 0)
	{
		return;
	}
	if (bDeactivationStarted)
	{
		ReleaseGameFeaturePluginRequest(PluginURL);
		return;
	}
	if (Result.HasError() && !bEndingPlay && !bLoadFailed)
	{
		FailExperienceLoad(FString::Printf(TEXT("Game feature %s failed: %s"), *PluginURL, *Result.GetError()));
	}
	if (!bExecutingCallbacks)
	{
		if (bEndingPlay || bLoadFailed)
		{
			BeginDeactivation();
		}
		else if (PendingGameFeaturePluginURLs.IsEmpty())
		{
			OnExperienceFullLoadCompleted();
		}
	}
}

void UIMGExperienceManagerComponent::OnExperienceFullLoadCompleted()
{
	if (bEndingPlay || bLoadFailed ||
		(LoadState != EIMGExperienceLoadState::LoadingGameFeatures && LoadState != EIMGExperienceLoadState::LoadingChaosTestingDelay))
	{
		return;
	}
	if (LoadState != EIMGExperienceLoadState::LoadingChaosTestingDelay)
	{
		const float Delay = IMGConsoleVariables::GetExperienceLoadDelayDuration();
		if (Delay > 0.0f)
		{
			LoadState = EIMGExperienceLoadState::LoadingChaosTestingDelay;
			GetWorld()->GetTimerManager().SetTimer(ExperienceLoadDelayTimer, this, &ThisClass::OnExperienceFullLoadCompleted, Delay, false);
			return;
		}
	}
	LoadState = EIMGExperienceLoadState::ExecutingActions;
	FGameFeatureActivatingContext Context;
	Context.SetRequiredWorldContextHandle(ExperienceWorldContext);
	TArray<UGameFeatureAction*> Actions;
	for (UGameFeatureAction* Action : CurrentExperience->Actions)
	{
		if (Action)
		{
			Actions.AddUnique(Action);
		}
	}
	for (const UIMGExperienceActionSet* ActionSet : CurrentExperience->ActionSets)
	{
		if (ActionSet)
		{
			for (UGameFeatureAction* Action : ActionSet->Actions)
			{
				if (Action)
				{
					Actions.AddUnique(Action);
				}
			}
		}
	}
	bExecutingCallbacks = true;
	for (UGameFeatureAction* Action : Actions)
	{
		RegisteredActions.Add(Action);
		Action->OnGameFeatureRegistering();
		if (bEndingPlay || bLoadFailed)
		{
			break;
		}
		LoadedActions.Add(Action);
		Action->OnGameFeatureLoading();
		if (bEndingPlay || bLoadFailed)
		{
			break;
		}
		ActivatedActions.Add(Action);
		Action->OnGameFeatureActivating(Context);
		if (bEndingPlay || bLoadFailed)
		{
			break;
		}
	}
	bExecutingCallbacks = false;
	if (bEndingPlay || bLoadFailed)
	{
		BeginDeactivation();
		return;
	}
	LoadState = EIMGExperienceLoadState::Loaded;
	// Check between listeners too: any callback can synchronously end the world.
	auto Dispatch = [this](TArray<FOnIMGExperienceLoaded::FDelegate>& Delegates)
	{
		TArray<FOnIMGExperienceLoaded::FDelegate> Callbacks = MoveTemp(Delegates);
		// Preserve native multicast's reverse traversal within each priority.
		for (int32 Index = Callbacks.Num() - 1; Index >= 0; --Index)
		{
			if (!IsExperienceLoaded())
			{
				return;
			}
			Callbacks[Index].ExecuteIfBound(CurrentExperience);
		}
	};
	Dispatch(OnExperienceLoaded_HighPriority);
	Dispatch(OnExperienceLoaded);
	Dispatch(OnExperienceLoaded_LowPriority);
	if (!IsExperienceLoaded())
	{
		return;
	}
#if !UE_SERVER
	UIMGSettingsLocal::Get()->OnExperienceLoaded();
#endif
}

void UIMGExperienceManagerComponent::FailExperienceLoad(const FString& Reason)
{
	if (bEndingPlay || bLoadFailed)
	{
		return;
	}
	bLoadFailed = true;
	ExperienceLoadError = Reason;
	UE_LOG(LogIMGExperience, Error, TEXT("EXPERIENCE: %s (%s)"), *Reason, *GetClientServerContextString(this));
	BeginDeactivation();
	FOnIMGExperienceLoadFailed FailureDelegates = MoveTemp(OnExperienceLoadFailed);
	FailureDelegates.Broadcast(ExperienceLoadError);
}

void UIMGExperienceManagerComponent::BeginDeactivation()
{
	if (bDeactivationStarted)
	{
		return;
	}
	LoadState = EIMGExperienceLoadState::Deactivating;
	OnExperienceLoaded_HighPriority.Reset();
	OnExperienceLoaded.Reset();
	OnExperienceLoaded_LowPriority.Reset();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ExperienceLoadDelayTimer);
	}
	if (ExperienceLoadHandle.IsValid())
	{
		// CancelHandle can synchronously invoke cancellation; state is already terminal.
		TSharedPtr<FStreamableHandle> Handle = MoveTemp(ExperienceLoadHandle);
		if (!Handle->HasLoadCompleted())
		{
			Handle->CancelHandle();
		}
		else
		{
			Handle->ReleaseHandle();
		}
	}
	if (bExecutingCallbacks)
	{
		return;
	}
	bDeactivationStarted = true;

	TArray<TStrongObjectPtr<UGameFeatureAction>> ActionsToUnregister;
	for (UGameFeatureAction* Action : RegisteredActions)
	{
		ActionsToUnregister.Emplace(Action);
	}
	const int32 NumLoadedActions = LoadedActions.Num();
	RegisteredActions.Reset();
	LoadedActions.Reset();
	TArray<FString> CompletedPluginURLs = MoveTemp(GameFeaturePluginURLs);
	CompletedPluginURLs.RemoveAll([this](const FString& URL)
	{
		// In-flight requests are released by their own completion, even if this component is gone.
		return PendingGameFeaturePluginURLs.Contains(URL);
	});
	TWeakObjectPtr<ThisClass> WeakThis(this);
	// Retain shared action assets, never the component or its old World, across asynchronous teardown.
	auto Finish = MakeShared<TFunction<void()>>(
		[Actions = MoveTemp(ActionsToUnregister), NumLoadedActions, PluginURLs = MoveTemp(CompletedPluginURLs), WeakThis]() mutable
		{
			for (int32 Index = NumLoadedActions - 1; Index >= 0; --Index)
			{
				Actions[Index]->OnGameFeatureUnloading();
			}
			for (int32 Index = Actions.Num() - 1; Index >= 0; --Index)
			{
				Actions[Index]->OnGameFeatureUnregistering();
			}
			Actions.Reset();
			for (int32 Index = PluginURLs.Num() - 1; Index >= 0; --Index)
			{
				ReleaseGameFeaturePluginRequest(PluginURLs[Index]);
			}
			PluginURLs.Reset();
			if (ThisClass* Component = WeakThis.Get())
			{
				Component->OnAllActionsDeactivated();
			}
		});
	// Synchronous completions make this negative until all pausers have registered.
	auto RemainingPausers = MakeShared<int32>(0);
	FGameFeatureDeactivatingContext Context(TEXT(""), [Finish, RemainingPausers](FStringView)
	{
		check(IsInGameThread());
		if (--*RemainingPausers == 0)
		{
			const auto Completion = Finish;
			(*Completion)();
		}
	});
	Context.SetRequiredWorldContextHandle(ExperienceWorldContext);
	for (int32 Index = ActivatedActions.Num() - 1; Index >= 0; --Index)
	{
		ActivatedActions[Index]->OnGameFeatureDeactivating(Context);
	}
	ActivatedActions.Reset();
	*RemainingPausers += Context.GetNumPausers();
	if (*RemainingPausers == 0)
	{
		(*Finish)();
	}
}

void UIMGExperienceManagerComponent::ReleaseGameFeaturePluginRequest(const FString& PluginURL)
{
	if (UIMGExperienceManager::RequestToDeactivatePlugin(PluginURL))
	{
		UGameFeaturesSubsystem::Get().DeactivateGameFeaturePlugin(PluginURL);
	}
}

void UIMGExperienceManagerComponent::OnAllActionsDeactivated()
{
	LoadState = bLoadFailed ? EIMGExperienceLoadState::Failed : EIMGExperienceLoadState::Unloaded;
	// Keep replicated selection stable; local clearing could permit a late OnRep restart.
}

bool UIMGExperienceManagerComponent::ShouldShowLoadingScreen(FString& OutReason) const
{
	if (bEndingPlay)
	{
		return false;
	}
	if (bLoadFailed)
	{
		OutReason = FString::Printf(TEXT("Experience failed: %s"), *ExperienceLoadError);
		return true;
	}
	if (!IsExperienceLoaded())
	{
		OutReason = TEXT("Experience still loading");
		return true;
	}
	return false;
}
