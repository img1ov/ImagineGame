// Fill out your copyright notice in the Description page of Project Settings.


#include "Player/IMGPlayerState.h"

#include "IMGLogChannels.h"
#include "AbilitySystem/IMGAbilitySet.h"
#include "AbilitySystem/IMGAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/IMGCombatSet.h"
#include "AbilitySystem/Attributes/IMGHealthSet.h"
#include "Player/IMGPlayerController.h"
#include "Character/IMGPawnData.h"
#include "GameModes/IMGExperienceManagerComponent.h"
#include "GameModes/IMGGameMode.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"

class AController;
class APlayerState;
class FLifetimeProperty;

AIMGPlayerState::AIMGPlayerState(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	AbilitySystemComponent = ObjectInitializer.CreateDefaultSubobject<UIMGAbilitySystemComponent>(this, TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	// These attribute sets will be detected by AbilitySystemComponent::InitializeComponent. Keeping a reference so that the sets don't get garbage collected before that.
	HealthSet = CreateDefaultSubobject<UIMGHealthSet>(TEXT("HealthSet"));
	CombatSet = CreateDefaultSubobject<UIMGCombatSet>(TEXT("CombatSet"));

	// AbilitySystemComponent needs to be updated at a high frequency.
	SetNetUpdateFrequency(100.0f);
	
	MyTeamID = FGenericTeamId::NoTeam;
	MySquadID = INDEX_NONE;
}

AIMGPlayerController* AIMGPlayerState::GetIMGPlayerController() const
{
	return Cast<AIMGPlayerController>(GetOwningController());
}

UAbilitySystemComponent* AIMGPlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AIMGPlayerState::SetPawnData(const UIMGPawnData* InPawnData)
{
	check(InPawnData);

	if (GetLocalRole() != ROLE_Authority)
	{
		return;
	}

	if (PawnData)
	{
		UE_LOG(LogIMG, Error, TEXT("Trying to set PawnData [%s] on player state [%s] that already has valid PawnData [%s]."), *GetNameSafe(InPawnData), *GetNameSafe(this), *GetNameSafe(PawnData));
		return;
	}

	MARK_PROPERTY_DIRTY_FROM_NAME(ThisClass, PawnData, this);
	PawnData = InPawnData;

	for (const UIMGAbilitySet* AbilitySet : PawnData->AbilitySets)
	{
		if (AbilitySet)
		{
			AbilitySet->GiveToAbilitySystem(AbilitySystemComponent, nullptr);
		}
	}
	
	ForceNetUpdate();
}

void AIMGPlayerState::PreInitializeComponents()
{
	Super::PreInitializeComponents();
}

void AIMGPlayerState::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	check(AbilitySystemComponent);
	AbilitySystemComponent->InitAbilityActorInfo(this, GetPawn());

	UWorld* World = GetWorld();
	if (World && World->IsGameWorld() && World->GetNetMode() != NM_Client)
	{
		AGameStateBase* GameState = GetWorld()->GetGameState();
		check(GameState);
		UIMGExperienceManagerComponent* ExperienceComponent = GameState->FindComponentByClass<UIMGExperienceManagerComponent>();
		check(ExperienceComponent);

		ExperienceComponent->CallOrRegister_OnExperienceLoaded(FOnIMGExperienceLoaded::FDelegate::CreateUObject(this, &ThisClass::OnExperienceLoaded));
	}
}

void AIMGPlayerState::SetGenericTeamId(const FGenericTeamId& NewTeamID)
{
	if (HasAuthority())
	{
		const FGenericTeamId OldTeamID = MyTeamID;

		MARK_PROPERTY_DIRTY_FROM_NAME(ThisClass, MyTeamID, this);
		MyTeamID = NewTeamID;
		ConditionalBroadcastTeamChanged(this, OldTeamID, NewTeamID);
		ForceNetUpdate();
	}
	else
	{
		UE_LOG(LogIMGTeams, Error, TEXT("Cannot set team for %s on non-authority"), *GetPathName(this));
	}
}

FGenericTeamId AIMGPlayerState::GetGenericTeamId() const
{
	return MyTeamID;
}

FOnIMGTeamIndexChangedDelegate* AIMGPlayerState::GetOnTeamIndexChangedDelegate()
{
	return &OnTeamChangedDelegate;
}

void AIMGPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams SharedParams;
	SharedParams.bIsPushBased = true;

	DOREPLIFETIME_WITH_PARAMS_FAST(ThisClass, PawnData, SharedParams);
	DOREPLIFETIME_WITH_PARAMS_FAST(ThisClass, MyTeamID, SharedParams);
	DOREPLIFETIME_WITH_PARAMS_FAST(ThisClass, MySquadID, SharedParams);
	
	SharedParams.Condition = ELifetimeCondition::COND_SkipOwner;
	DOREPLIFETIME_WITH_PARAMS_FAST(ThisClass, ReplicatedViewRotation, SharedParams);
}

void AIMGPlayerState::SetSquadID(int32 NewSquadID)
{
	if (HasAuthority())
	{
		MARK_PROPERTY_DIRTY_FROM_NAME(ThisClass, MySquadID, this);

		MySquadID = NewSquadID;
	}
}

FRotator AIMGPlayerState::GetReplicatedViewRotation() const
{
	// Could replace this with custom replication
	return ReplicatedViewRotation;
}

void AIMGPlayerState::SetReplicatedViewRotation(const FRotator& NewRotation)
{
	if (NewRotation != ReplicatedViewRotation)
	{
		MARK_PROPERTY_DIRTY_FROM_NAME(ThisClass, ReplicatedViewRotation, this);
		ReplicatedViewRotation = NewRotation;
	}
}

void AIMGPlayerState::OnRep_PawnData()
{
}

void AIMGPlayerState::OnExperienceLoaded(const UIMGExperienceDefinition* CurrentExperience)
{
	if (AIMGGameMode* IMGGameMode = GetWorld()->GetAuthGameMode<AIMGGameMode>())
	{
		if (const UIMGPawnData* NewPawnData = IMGGameMode->GetPawnDataForController(GetOwningController()))
		{
			SetPawnData(NewPawnData);
		}
		else
		{
			UE_LOG(LogIMG, Error, TEXT("AIMGPlayerState::OnExperienceLoaded(): Unable to find PawnData to initialize player state [%s]!"), *GetNameSafe(this));
		}
	}
}

void AIMGPlayerState::OnRep_MyTeamID(FGenericTeamId OldTeamID)
{
	ConditionalBroadcastTeamChanged(this, OldTeamID, MyTeamID);
}

void AIMGPlayerState::OnRep_MySquadID()
{
	//@TODO: Let the squad subsystem know (once that exists)
}
