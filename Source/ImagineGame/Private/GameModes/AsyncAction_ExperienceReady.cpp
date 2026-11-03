#include "GameModes/AsyncAction_ExperienceReady.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameModes/IMGExperienceManagerComponent.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsyncAction_ExperienceReady)

UAsyncAction_ExperienceReady::UAsyncAction_ExperienceReady(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UAsyncAction_ExperienceReady* UAsyncAction_ExperienceReady::WaitForExperienceReady(UObject* InWorldContextObject)
{
	UAsyncAction_ExperienceReady* Action = nullptr;

	if (UWorld* World = GEngine->GetWorldFromContextObject(InWorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		Action = NewObject<UAsyncAction_ExperienceReady>();
		Action->WorldPtr = World;
		Action->RegisterWithGameInstance(World);
	}

	return Action;
}

void UAsyncAction_ExperienceReady::Activate()
{
	if (bActivated || bFinished)
	{
		return;
	}
	bActivated = true;
	WorldTearDownHandle = FWorldDelegates::OnWorldBeginTearDown.AddUObject(this, &ThisClass::HandleWorldBeginTearDown);
	if (UWorld* World = WorldPtr.Get())
	{
		if (World->bIsTearingDown)
		{
			SetReadyToDestroy();
			return;
		}
		if (AGameStateBase* GameState = World->GetGameState())
		{
			Step2_ListenToExperienceLoading(GameState);
		}
		else
		{
			World->GameStateSetEvent.AddUObject(this, &ThisClass::Step1_HandleGameStateSet);
		}
	}
	else
	{
		SetReadyToDestroy();
	}
}

void UAsyncAction_ExperienceReady::Step1_HandleGameStateSet(AGameStateBase* GameState)
{
	if (UWorld* World = WorldPtr.Get())
	{
		World->GameStateSetEvent.RemoveAll(this);
	}

	Step2_ListenToExperienceLoading(GameState);
}

void UAsyncAction_ExperienceReady::Step2_ListenToExperienceLoading(AGameStateBase* GameState)
{
	if (bFinished)
	{
		return;
	}
	UIMGExperienceManagerComponent* ExperienceComponent = GameState ? GameState->FindComponentByClass<UIMGExperienceManagerComponent>() : nullptr;
	if (!ExperienceComponent)
	{
		SetReadyToDestroy();
		return;
	}
	ExperienceComponentPtr = ExperienceComponent;
	ExperienceComponent->CallOrRegister_OnExperienceLoadFailed(FOnIMGExperienceLoadFailed::FDelegate::CreateUObject(this, &ThisClass::HandleExperienceFailed));
	if (bFinished)
	{
		return;
	}

	if (ExperienceComponent->IsExperienceLoaded())
	{
		UWorld* World = GameState->GetWorld();
		check(World);
		ReadyTimer = World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &ThisClass::Step4_BroadcastReady));
	}
	else
	{
		ExperienceComponent->CallOrRegister_OnExperienceLoaded(FOnIMGExperienceLoaded::FDelegate::CreateUObject(this, &ThisClass::Step3_HandleExperienceLoaded));
	}
}

void UAsyncAction_ExperienceReady::Step3_HandleExperienceLoaded(const UIMGExperienceDefinition* CurrentExperience)
{
	Step4_BroadcastReady();
}

void UAsyncAction_ExperienceReady::Step4_BroadcastReady()
{
	if (bFinished)
	{
		return;
	}
	UIMGExperienceManagerComponent* ExperienceComponent = ExperienceComponentPtr.Get();
	if (!ExperienceComponent || !ExperienceComponent->IsExperienceLoaded())
	{
		SetReadyToDestroy();
		return;
	}
	bFinished = true;
	OnReady.Broadcast();
	SetReadyToDestroy();
}

void UAsyncAction_ExperienceReady::HandleExperienceFailed(const FString&)
{
	if (bFinished)
	{
		return;
	}
	SetReadyToDestroy();
}

void UAsyncAction_ExperienceReady::HandleWorldBeginTearDown(UWorld* World)
{
	if (WorldPtr.Get() == World)
	{
		SetReadyToDestroy();
	}
}

void UAsyncAction_ExperienceReady::SetReadyToDestroy()
{
	bFinished = true;
	FWorldDelegates::OnWorldBeginTearDown.Remove(WorldTearDownHandle);
	WorldTearDownHandle.Reset();
	if (UWorld* World = WorldPtr.Get())
	{
		World->GameStateSetEvent.RemoveAll(this);
		World->GetTimerManager().ClearTimer(ReadyTimer);
	}
	if (UIMGExperienceManagerComponent* ExperienceComponent = ExperienceComponentPtr.Get())
	{
		ExperienceComponent->RemoveExperienceLoadDelegates(this);
	}
	ExperienceComponentPtr.Reset();
	Super::SetReadyToDestroy();
}
