// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Phases/IMGGamePhaseSubsystem.h"

#include "AbilitySystem/IMGAbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "AbilitySystem/Phases/IMGGamePhaseAbility.h"
#include "AbilitySystem/Phases/IMGGamePhaseLog.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGGamePhaseSubsystem)

class UIMGGameplayAbility;
class UObject;

DEFINE_LOG_CATEGORY(LogIMGGamePhase);

//////////////////////////////////////////////////////////////////////
// UIMGGamePhaseSubsystem

UIMGGamePhaseSubsystem::UIMGGamePhaseSubsystem()
{
}

void UIMGGamePhaseSubsystem::PostInitialize()
{
	Super::PostInitialize();
}

bool UIMGGamePhaseSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (Super::ShouldCreateSubsystem(Outer))
	{
		//UWorld* World = Cast<UWorld>(Outer);
		//check(World);

		//return World->GetAuthGameMode() != nullptr;
		//return nullptr;
		return true;
	}

	return false;
}

bool UIMGGamePhaseSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UIMGGamePhaseSubsystem::StartPhase(TSubclassOf<UIMGGamePhaseAbility> PhaseAbility, FIMGGamePhaseDelegate PhaseEndedCallback)
{
	UWorld* World = GetWorld();
	UIMGAbilitySystemComponent* GameState_ASC = World->GetGameState()->FindComponentByClass<UIMGAbilitySystemComponent>();
	if (ensure(GameState_ASC))
	{
		FGameplayAbilitySpec PhaseSpec(PhaseAbility, 1, 0, this);
		FGameplayAbilitySpecHandle SpecHandle = GameState_ASC->GiveAbilityAndActivateOnce(PhaseSpec);
		FGameplayAbilitySpec* FoundSpec = GameState_ASC->FindAbilitySpecFromHandle(SpecHandle);

		if (FoundSpec && FoundSpec->IsActive())
		{
			FIMGGamePhaseEntry& Entry = ActivePhaseMap.FindOrAdd(SpecHandle);
			Entry.PhaseEndedCallback = PhaseEndedCallback;
		}
		else
		{
			PhaseEndedCallback.ExecuteIfBound(nullptr);
		}
	}
}

void UIMGGamePhaseSubsystem::K2_StartPhase(TSubclassOf<UIMGGamePhaseAbility> PhaseAbility, const FIMGGamePhaseDynamicDelegate& PhaseEndedDelegate)
{
	const FIMGGamePhaseDelegate EndedDelegate = FIMGGamePhaseDelegate::CreateWeakLambda(const_cast<UObject*>(PhaseEndedDelegate.GetUObject()), [PhaseEndedDelegate](const UIMGGamePhaseAbility* PhaseAbility) {
		PhaseEndedDelegate.ExecuteIfBound(PhaseAbility);
	});

	StartPhase(PhaseAbility, EndedDelegate);
}

void UIMGGamePhaseSubsystem::K2_WhenPhaseStartsOrIsActive(FGameplayTag PhaseTag, EPhaseTagMatchType MatchType, FIMGGamePhaseTagDynamicDelegate WhenPhaseActive)
{
	const FIMGGamePhaseTagDelegate ActiveDelegate = FIMGGamePhaseTagDelegate::CreateWeakLambda(WhenPhaseActive.GetUObject(), [WhenPhaseActive](const FGameplayTag& PhaseTag) {
		WhenPhaseActive.ExecuteIfBound(PhaseTag);
	});

	WhenPhaseStartsOrIsActive(PhaseTag, MatchType, ActiveDelegate);
}

void UIMGGamePhaseSubsystem::K2_WhenPhaseEnds(FGameplayTag PhaseTag, EPhaseTagMatchType MatchType, FIMGGamePhaseTagDynamicDelegate WhenPhaseEnd)
{
	const FIMGGamePhaseTagDelegate EndedDelegate = FIMGGamePhaseTagDelegate::CreateWeakLambda(WhenPhaseEnd.GetUObject(), [WhenPhaseEnd](const FGameplayTag& PhaseTag) {
		WhenPhaseEnd.ExecuteIfBound(PhaseTag);
	});

	WhenPhaseEnds(PhaseTag, MatchType, EndedDelegate);
}

void UIMGGamePhaseSubsystem::WhenPhaseStartsOrIsActive(FGameplayTag PhaseTag, EPhaseTagMatchType MatchType, const FIMGGamePhaseTagDelegate& WhenPhaseActive)
{
	FPhaseObserver Observer;
	Observer.PhaseTag = PhaseTag;
	Observer.MatchType = MatchType;
	Observer.PhaseCallback = WhenPhaseActive;
	PhaseStartObservers.Add(Observer);

	if (IsPhaseActive(PhaseTag))
	{
		WhenPhaseActive.ExecuteIfBound(PhaseTag);
	}
}

void UIMGGamePhaseSubsystem::WhenPhaseEnds(FGameplayTag PhaseTag, EPhaseTagMatchType MatchType, const FIMGGamePhaseTagDelegate& WhenPhaseEnd)
{
	FPhaseObserver Observer;
	Observer.PhaseTag = PhaseTag;
	Observer.MatchType = MatchType;
	Observer.PhaseCallback = WhenPhaseEnd;
	PhaseEndObservers.Add(Observer);
}

bool UIMGGamePhaseSubsystem::IsPhaseActive(const FGameplayTag& PhaseTag) const
{
	for (const auto& KVP : ActivePhaseMap)
	{
		const FIMGGamePhaseEntry& PhaseEntry = KVP.Value;
		if (PhaseEntry.PhaseTag.MatchesTag(PhaseTag))
		{
			return true;
		}
	}

	return false;
}

void UIMGGamePhaseSubsystem::OnBeginPhase(const UIMGGamePhaseAbility* PhaseAbility, const FGameplayAbilitySpecHandle PhaseAbilityHandle)
{
	const FGameplayTag IncomingPhaseTag = PhaseAbility->GetGamePhaseTag();

	UE_LOG(LogIMGGamePhase, Log, TEXT("Beginning Phase '%s' (%s)"), *IncomingPhaseTag.ToString(), *GetNameSafe(PhaseAbility));

	const UWorld* World = GetWorld();
	UIMGAbilitySystemComponent* GameState_ASC = World->GetGameState()->FindComponentByClass<UIMGAbilitySystemComponent>();
	if (ensure(GameState_ASC))
	{
		TArray<FGameplayAbilitySpec*> ActivePhases;
		for (const auto& KVP : ActivePhaseMap)
		{
			const FGameplayAbilitySpecHandle ActiveAbilityHandle = KVP.Key;
			if (FGameplayAbilitySpec* Spec = GameState_ASC->FindAbilitySpecFromHandle(ActiveAbilityHandle))
			{
				ActivePhases.Add(Spec);
			}
		}

		for (const FGameplayAbilitySpec* ActivePhase : ActivePhases)
		{
			const UIMGGamePhaseAbility* ActivePhaseAbility = CastChecked<UIMGGamePhaseAbility>(ActivePhase->Ability);
			const FGameplayTag ActivePhaseTag = ActivePhaseAbility->GetGamePhaseTag();

			// So if the active phase currently matches the incoming phase tag, we allow it.
			// i.e. multiple gameplay abilities can all be associated with the same phase tag.
			// For example,
			// You can be in the, Game.Playing, phase, and then start a sub-phase, like Game.Playing.SuddenDeath
			// Game.Playing phase will still be active, and if someone were to push another one, like,
			// Game.Playing.ActualSuddenDeath, it would end Game.Playing.SuddenDeath phase, but Game.Playing would
			// continue.  Similarly if we activated Game.GameOver, all the Game.Playing* phases would end.
			if (!IncomingPhaseTag.MatchesTag(ActivePhaseTag))
			{
				UE_LOG(LogIMGGamePhase, Log, TEXT("\tEnding Phase '%s' (%s)"), *ActivePhaseTag.ToString(), *GetNameSafe(ActivePhaseAbility));

				FGameplayAbilitySpecHandle HandleToEnd = ActivePhase->Handle;
				GameState_ASC->CancelAbilitiesByFunc([HandleToEnd](const UIMGGameplayAbility* IMGAbility, FGameplayAbilitySpecHandle Handle) {
					return Handle == HandleToEnd;
				}, true);
			}
		}

		FIMGGamePhaseEntry& Entry = ActivePhaseMap.FindOrAdd(PhaseAbilityHandle);
		Entry.PhaseTag = IncomingPhaseTag;

		// Notify all observers of this phase that it has started.
		for (const FPhaseObserver& Observer : PhaseStartObservers)
		{
			if (Observer.IsMatch(IncomingPhaseTag))
			{
				Observer.PhaseCallback.ExecuteIfBound(IncomingPhaseTag);
			}
		}
	}
}

void UIMGGamePhaseSubsystem::OnEndPhase(const UIMGGamePhaseAbility* PhaseAbility, const FGameplayAbilitySpecHandle PhaseAbilityHandle)
{
	const FGameplayTag EndedPhaseTag = PhaseAbility->GetGamePhaseTag();
	UE_LOG(LogIMGGamePhase, Log, TEXT("Ended Phase '%s' (%s)"), *EndedPhaseTag.ToString(), *GetNameSafe(PhaseAbility));

	const FIMGGamePhaseEntry& Entry = ActivePhaseMap.FindChecked(PhaseAbilityHandle);
	Entry.PhaseEndedCallback.ExecuteIfBound(PhaseAbility);

	ActivePhaseMap.Remove(PhaseAbilityHandle);

	// Notify all observers of this phase that it has ended.
	for (const FPhaseObserver& Observer : PhaseEndObservers)
	{
		if (Observer.IsMatch(EndedPhaseTag))
		{
			Observer.PhaseCallback.ExecuteIfBound(EndedPhaseTag);
		}
	}
}

bool UIMGGamePhaseSubsystem::FPhaseObserver::IsMatch(const FGameplayTag& ComparePhaseTag) const
{
	switch(MatchType)
	{
	case EPhaseTagMatchType::ExactMatch:
		return ComparePhaseTag == PhaseTag;
	case EPhaseTagMatchType::PartialMatch:
		return ComparePhaseTag.MatchesTag(PhaseTag);
	}

	return false;
}
