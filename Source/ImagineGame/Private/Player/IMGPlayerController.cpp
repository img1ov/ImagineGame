// Fill out your copyright notice in the Description page of Project Settings.

#include "Player/IMGPlayerController.h"

#include "AbilitySystemGlobals.h"
#include "IMGLogChannels.h"
#include "AbilitySystem/IMGAbilitySystemComponent.h"
#include "Net/UnrealNetwork.h"
#include "Player/IMGPlayerState.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGPlayerController)

AIMGPlayerController::AIMGPlayerController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

AIMGPlayerController::~AIMGPlayerController() = default;

AIMGPlayerState* AIMGPlayerController::GetIMGPlayerState() const
{
	return CastChecked<AIMGPlayerState>(PlayerState, ECastCheckedType::NullAllowed);
}

UIMGAbilitySystemComponent* AIMGPlayerController::GetIMGAbilitySystemComponent() const
{
	const AIMGPlayerState* IMGPS = GetIMGPlayerState();
	return (IMGPS ? IMGPS->GetIMGAbilitySystemComponent() : nullptr);
}

UAbilitySystemComponent* AIMGPlayerController::GetAbilitySystemComponent() const
{
	return GetIMGAbilitySystemComponent();
}

void AIMGPlayerController::PreInitializeComponents()
{
	Super::PreInitializeComponents();
}

void AIMGPlayerController::BeginPlay()
{
	Super::BeginPlay();

	SetActorHiddenInGame(false);
}

void AIMGPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void AIMGPlayerController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	
	// Disable replicating the PC target view as it doesn't work well for replays or client-side spectating.
	// The engine TargetViewRotation is only set in APlayerController::TickActor if the server knows ahead of time that 
	// a specific pawn is being spectated and it only replicates down for COND_OwnerOnly.
	// In client-saved replays, COND_OwnerOnly is never true and the target pawn is not always known at the time of recording.
	// To support client-saved replays, the replication of this was moved to ReplicatedViewRotation and updated in PlayerTick.
	DISABLE_REPLICATED_PROPERTY(APlayerController, TargetViewRotation);
}

void AIMGPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
}

void AIMGPlayerController::OnUnPossess()
{
	if (const APawn* PawnBeingUnpossessed = GetPawn())
	{
		const APlayerState* ThePlayerState = PlayerState.Get();
		if (IsValid(ThePlayerState))
		{
			if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(ThePlayerState))
			{
				if (ASC->GetAvatarActor() == PawnBeingUnpossessed)
				{
					ASC->SetAvatarActor(nullptr);
				}
			}
		}
	}

	Super::OnUnPossess();
}

void AIMGPlayerController::InitPlayerState()
{
	Super::InitPlayerState();
	BroadcastOnPlayerStateChanged();
}

void AIMGPlayerController::CleanupPlayerState()
{
	Super::CleanupPlayerState();
	BroadcastOnPlayerStateChanged();
}

void AIMGPlayerController::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	BroadcastOnPlayerStateChanged();

	if (GetWorld()->IsNetMode(NM_Client))
	{
		if (AIMGPlayerState* IMGPS = GetPlayerState<AIMGPlayerState>())
		{
			if (UIMGAbilitySystemComponent* IMGASC = IMGPS->GetIMGAbilitySystemComponent())
			{
				IMGASC->RefreshAbilityActorInfo();
				IMGASC->TryActivateAbilitiesOnSpawn();
			}
		}
	}
}

void AIMGPlayerController::PreProcessInput(const float DeltaTime, const bool bGamePaused)
{
	Super::PreProcessInput(DeltaTime, bGamePaused);
}

void AIMGPlayerController::PostProcessInput(const float DeltaTime, const bool bGamePaused)
{
	if (UIMGAbilitySystemComponent* IMGASC = GetIMGAbilitySystemComponent())
	{
		IMGASC->ProcessAbilityInput(DeltaTime, bGamePaused);
	}

	Super::PostProcessInput(DeltaTime, bGamePaused);
}

void AIMGPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	
	AIMGPlayerState* IMGPS = GetIMGPlayerState();
	
	if (PlayerCameraManager && IMGPS)
	{
		APawn* TargetPawn = PlayerCameraManager->GetViewTargetPawn();
		
		if (TargetPawn)
		{
			// Update view rotation on the server so it replicates
			if (HasAuthority() || TargetPawn->IsLocallyControlled())
			{
				IMGPS->SetReplicatedViewRotation(TargetPawn->GetViewRotation());
			}

			// Update the target view rotation if the pawn isn't locally controlled
			if (!TargetPawn->IsLocallyControlled())
			{
				IMGPS = TargetPawn->GetPlayerState<AIMGPlayerState>();
				if (IMGPS)
				{
					// Get it from the spectated pawn's player state, which may not be the same as the PC's playerstate
					TargetViewRotation = IMGPS->GetReplicatedViewRotation();
				}
			}
		}
	}
}

void AIMGPlayerController::SetGenericTeamId(const FGenericTeamId& NewTeamID)
{
	UE_LOG(LogIMGTeams, Error, TEXT("You can't set the team ID on a player controller (%s); it's driven by the associated player state"), *GetPathNameSafe(this));
}

FGenericTeamId AIMGPlayerController::GetGenericTeamId() const
{
	if (const IIMGTeamAgentInterface* PSWithTeamInterface = Cast<IIMGTeamAgentInterface>(PlayerState))
	{
		return PSWithTeamInterface->GetGenericTeamId();
	}

	return FGenericTeamId::NoTeam;
}

FOnIMGTeamIndexChangedDelegate* AIMGPlayerController::GetOnTeamIndexChangedDelegate()
{
	return &OnTeamChangedDelegate;
}

void AIMGPlayerController::BroadcastOnPlayerStateChanged()
{
	OnPlayerStateChanged();

	FGenericTeamId OldTeamID = FGenericTeamId::NoTeam;
	if (LastSeenPlayerState != nullptr)
	{
		if (IIMGTeamAgentInterface* PlayerStateTeamInterface = Cast<IIMGTeamAgentInterface>(LastSeenPlayerState))
		{
			OldTeamID = PlayerStateTeamInterface->GetGenericTeamId();
			PlayerStateTeamInterface->GetTeamChangedDelegateChecked().RemoveAll(this);
		}
	}

	FGenericTeamId NewTeamID = FGenericTeamId::NoTeam;
	if (PlayerState != nullptr)
	{
		if (IIMGTeamAgentInterface* PlayerStateTeamInterface = Cast<IIMGTeamAgentInterface>(PlayerState))
		{
			NewTeamID = PlayerStateTeamInterface->GetGenericTeamId();
			PlayerStateTeamInterface->GetTeamChangedDelegateChecked().AddDynamic(this, &ThisClass::OnPlayerStateChangedTeam);
		}
	}

	ConditionalBroadcastTeamChanged(this, OldTeamID, NewTeamID);
	LastSeenPlayerState = PlayerState;
}

void AIMGPlayerController::OnPlayerStateChangedTeam(UObject* TeamAgent, int32 OldTeam, int32 NewTeam)
{
	ConditionalBroadcastTeamChanged(this, IntegerToGenericTeamId(OldTeam), IntegerToGenericTeamId(NewTeam));
}

void AIMGPlayerController::OnPlayerStateChanged()
{
}
