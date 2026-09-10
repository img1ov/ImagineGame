// Fill out your copyright notice in the Description page of Project Settings.

#include "Player/IMGPlayerController.h"

#include "Player/IMGCheatManager.h"
#include "UI/IMGHUD.h"
#include "EngineUtils.h"
#include "IMGGameplayTags.h"
#include "GameFramework/Pawn.h"
#include "Engine/GameInstance.h"
#include "GameModes/IMGGameState.h"
#include "Settings/IMGSettingsLocal.h"
#include "Replays/IMGReplaySubsystem.h"
#include "ReplaySubsystem.h"
#include "Development/IMGDeveloperSettings.h"
#include "GameMapsSettings.h"
#include "Camera/IMGPlayerCameraManager.h"
#include "Components/PrimitiveComponent.h"

#include "CommonInputSubsystem.h"
#include "CommonInputTypeEnum.h"
#include "GenericPlatform/IInputInterface.h"
#include "HAL/IConsoleManager.h"
#include "Player/IMGLocalPlayer.h"
#include "Settings/IMGSettingsShared.h"
#include "AbilitySystemGlobals.h"
#include "IMGLogChannels.h"
#include "AbilitySystem/IMGAbilitySystemComponent.h"
#include "Net/UnrealNetwork.h"
#include "Player/IMGPlayerState.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGPlayerController)

AIMGPlayerController::AIMGPlayerController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PlayerCameraManagerClass = AIMGPlayerCameraManager::StaticClass();
#if USING_CHEAT_MANAGER
	CheatClass = UIMGCheatManager::StaticClass();
#endif
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

#if WITH_SERVER_CODE && WITH_EDITOR
	if (GIsEditor && (InPawn != nullptr) && (GetPawn() == InPawn))
	{
		for (const FIMGCheatToRun& CheatRow : GetDefault<UIMGDeveloperSettings>()->CheatsToRun)
		{
			if (CheatRow.Phase == ECheatExecutionTime::OnPlayerPawnPossession)
			{
				ConsoleCommand(CheatRow.Cheat, /*bWriteToLog=*/ true);
			}
		}
	}
#endif

	SetIsAutoRunning(false);
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

	// If we are auto running then add some player input
	if (GetIsAutoRunning())
	{
		if (APawn* CurrentPawn = GetPawn())
		{
			const FRotator MovementRotation(0.0f, GetControlRotation().Yaw, 0.0f);
			const FVector MovementDirection = MovementRotation.RotateVector(FVector::ForwardVector);
			CurrentPawn->AddMovementInput(MovementDirection, 1.0f);
		}
	}

	AIMGPlayerState* IMGPlayerState = GetIMGPlayerState();

	if (PlayerCameraManager && IMGPlayerState)
	{
		APawn* TargetPawn = PlayerCameraManager->GetViewTargetPawn();

		if (TargetPawn)
		{
			// Update view rotation on the server so it replicates
			if (HasAuthority() || TargetPawn->IsLocallyControlled())
			{
				IMGPlayerState->SetReplicatedViewRotation(TargetPawn->GetViewRotation());
			}

			// Update the target view rotation if the pawn isn't locally controlled
			if (!TargetPawn->IsLocallyControlled())
			{
				IMGPlayerState = TargetPawn->GetPlayerState<AIMGPlayerState>();
				if (IMGPlayerState)
				{
					// Get it from the spectated pawn's player state, which may not be the same as the PC's playerstate
					TargetViewRotation = IMGPlayerState->GetReplicatedViewRotation();
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

void AIMGPlayerController::OnCameraPenetratingTarget()
{
	bHideViewTargetPawnNextFrame = true;
}

void AIMGPlayerController::UpdateHiddenComponents(const FVector& ViewLocation, TSet<FPrimitiveComponentId>& OutHiddenComponents)
{
	Super::UpdateHiddenComponents(ViewLocation, OutHiddenComponents);

	if (bHideViewTargetPawnNextFrame)
	{
		AActor* const ViewTargetPawn = PlayerCameraManager ? Cast<AActor>(PlayerCameraManager->GetViewTarget()) : nullptr;
		if (ViewTargetPawn)
		{
			// internal helper func to hide all the components
			auto AddToHiddenComponents = [&OutHiddenComponents](const TInlineComponentArray<UPrimitiveComponent*>& InComponents)
			{
				// add every component and all attached children
				for (UPrimitiveComponent* Comp : InComponents)
				{
					if (Comp->IsRegistered())
					{
						OutHiddenComponents.Add(Comp->GetPrimitiveSceneId());

						for (USceneComponent* AttachedChild : Comp->GetAttachChildren())
						{
							static FName NAME_NoParentAutoHide(TEXT("NoParentAutoHide"));
							UPrimitiveComponent* AttachChildPC = Cast<UPrimitiveComponent>(AttachedChild);
							if (AttachChildPC && AttachChildPC->IsRegistered() && !AttachChildPC->ComponentTags.Contains(NAME_NoParentAutoHide))
							{
								OutHiddenComponents.Add(AttachChildPC->GetPrimitiveSceneId());
							}
						}
					}
				}
			};

			//TODO Solve with an interface.  Gather hidden components or something.
			//TODO Hiding isn't awesome, sometimes you want the effect of a fade out over a proximity, needs to bubble up to designers.

			// hide pawn's components
			TInlineComponentArray<UPrimitiveComponent*> PawnComponents;
			ViewTargetPawn->GetComponents(PawnComponents);
			AddToHiddenComponents(PawnComponents);

			//// hide weapon too
			//if (ViewTargetPawn->CurrentWeapon)
			//{
			//	TInlineComponentArray<UPrimitiveComponent*> WeaponComponents;
			//	ViewTargetPawn->CurrentWeapon->GetComponents(WeaponComponents);
			//	AddToHiddenComponents(WeaponComponents);
			//}
		}

		// we consumed it, reset for next frame
		bHideViewTargetPawnNextFrame = false;
	}
}


void AIMGPlayerController::SetPlayer(UPlayer* InPlayer)
{
	Super::SetPlayer(InPlayer);

	if (const UIMGLocalPlayer* IMGLocalPlayer = Cast<UIMGLocalPlayer>(InPlayer))
	{
		UIMGSettingsShared* UserSettings = IMGLocalPlayer->GetSharedSettings();
		UserSettings->OnSettingChanged.AddUObject(this, &ThisClass::OnSettingsChanged);

		OnSettingsChanged(UserSettings);
	}
}

void AIMGPlayerController::OnSettingsChanged(UIMGSettingsShared* InSettings)
{
	bForceFeedbackEnabled = InSettings->GetForceFeedbackEnabled();
}

void AIMGPlayerController::UpdateForceFeedback(IInputInterface* InputInterface, const int32 ControllerId)
{
	if (bForceFeedbackEnabled)
	{
		if (const UCommonInputSubsystem* CommonInputSubsystem = UCommonInputSubsystem::Get(GetLocalPlayer()))
		{
			const ECommonInputType CurrentInputType = CommonInputSubsystem->GetCurrentInputType();
			const IConsoleVariable* ForceFeedbackOverride = IConsoleManager::Get().FindConsoleVariable(TEXT("IMGPC.ShouldAlwaysPlayForceFeedback"));
			if ((ForceFeedbackOverride && ForceFeedbackOverride->GetBool()) || CurrentInputType == ECommonInputType::Gamepad || CurrentInputType == ECommonInputType::Touch)
			{
				InputInterface->SetForceFeedbackChannelValues(ControllerId, ForceFeedbackValues);
				return;
			}
		}
	}

	InputInterface->SetForceFeedbackChannelValues(ControllerId, FForceFeedbackValues());
}


void AIMGPlayerController::ReceivedPlayer()
{
	Super::ReceivedPlayer();
}

AIMGHUD* AIMGPlayerController::GetIMGHUD() const
{
	return CastChecked<AIMGHUD>(GetHUD(), ECastCheckedType::NullAllowed);
}

bool AIMGPlayerController::TryToRecordClientReplay()
{
	// See if we should record a replay
	if (ShouldRecordClientReplay())
	{
		if (UIMGReplaySubsystem* ReplaySubsystem = GetGameInstance()->GetSubsystem<UIMGReplaySubsystem>())
		{
			APlayerController* FirstLocalPlayerController = GetGameInstance()->GetFirstLocalPlayerController();
			if (FirstLocalPlayerController == this)
			{
				// If this is the first player, update the spectator player for local replays and then record
				if (AIMGGameState* GameState = Cast<AIMGGameState>(GetWorld()->GetGameState()))
				{
					GameState->SetRecorderPlayerState(PlayerState);

					ReplaySubsystem->RecordClientReplay(this);
					return true;
				}
			}
		}
	}
	return false;
}

bool AIMGPlayerController::ShouldRecordClientReplay()
{
	UWorld* World = GetWorld();
	UGameInstance* GameInstance = GetGameInstance();
	if (GameInstance != nullptr &&
		World != nullptr &&
		!World->IsPlayingReplay() &&
		!World->IsRecordingClientReplay() &&
		NM_DedicatedServer != GetNetMode() &&
		IsLocalPlayerController())
	{
		FString DefaultMap = UGameMapsSettings::GetGameDefaultMap();
		FString CurrentMap = World->URL.Map;

#if WITH_EDITOR
		CurrentMap = UWorld::StripPIEPrefixFromPackageName(CurrentMap, World->StreamingLevelsPrefix);
#endif
		if (CurrentMap == DefaultMap)
		{
			// Never record demos on the default frontend map, this could be replaced with a better check for being in the main menu
			return false;
		}

		if (UReplaySubsystem* ReplaySubsystem = GameInstance->GetSubsystem<UReplaySubsystem>())
		{
			if (ReplaySubsystem->IsRecording() || ReplaySubsystem->IsPlaying())
			{
				// Only one at a time
				return false;
			}
		}

		// If this is possible, now check the settings
		if (const UIMGLocalPlayer* IMGLocalPlayer = Cast<UIMGLocalPlayer>(GetLocalPlayer()))
		{
			if (IMGLocalPlayer->GetLocalSettings()->ShouldAutoRecordReplays())
			{
				return true;
			}
		}
	}
	return false;
}

void AIMGPlayerController::AddCheats(bool bForce)
{
#if USING_CHEAT_MANAGER
	Super::AddCheats(true);
#else //#if USING_CHEAT_MANAGER
	Super::AddCheats(bForce);
#endif // #else //#if USING_CHEAT_MANAGER
}

void AIMGPlayerController::ServerCheat_Implementation(const FString& Msg)
{
#if USING_CHEAT_MANAGER
	if (CheatManager)
	{
		UE_LOG(LogIMG, Warning, TEXT("ServerCheat: %s"), *Msg);
		ClientMessage(ConsoleCommand(Msg));
	}
#endif // #if USING_CHEAT_MANAGER
}

bool AIMGPlayerController::ServerCheat_Validate(const FString& Msg)
{
	return true;
}

void AIMGPlayerController::ServerCheatAll_Implementation(const FString& Msg)
{
#if USING_CHEAT_MANAGER
	if (CheatManager)
	{
		UE_LOG(LogIMG, Warning, TEXT("ServerCheatAll: %s"), *Msg);
		for (TActorIterator<AIMGPlayerController> It(GetWorld()); It; ++It)
		{
			AIMGPlayerController* IMGPC = (*It);
			if (IMGPC)
			{
				IMGPC->ClientMessage(IMGPC->ConsoleCommand(Msg));
			}
		}
	}
#endif // #if USING_CHEAT_MANAGER
}

bool AIMGPlayerController::ServerCheatAll_Validate(const FString& Msg)
{
	return true;
}

void AIMGPlayerController::SetIsAutoRunning(const bool bEnabled)
{
	const bool bIsAutoRunning = GetIsAutoRunning();
	if (bEnabled != bIsAutoRunning)
	{
		if (!bEnabled)
		{
			OnEndAutoRun();
		}
		else
		{
			OnStartAutoRun();
		}
	}
}

bool AIMGPlayerController::GetIsAutoRunning() const
{
	bool bIsAutoRunning = false;
	if (const UIMGAbilitySystemComponent* IMGASC = GetIMGAbilitySystemComponent())
	{
		bIsAutoRunning = IMGASC->GetTagCount(IMGGameplayTags::Status_AutoRunning) > 0;
	}
	return bIsAutoRunning;
}

void AIMGPlayerController::OnStartAutoRun()
{
	if (UIMGAbilitySystemComponent* IMGASC = GetIMGAbilitySystemComponent())
	{
		IMGASC->SetLooseGameplayTagCount(IMGGameplayTags::Status_AutoRunning, 1);
		K2_OnStartAutoRun();
	}
}

void AIMGPlayerController::OnEndAutoRun()
{
	if (UIMGAbilitySystemComponent* IMGASC = GetIMGAbilitySystemComponent())
	{
		IMGASC->SetLooseGameplayTagCount(IMGGameplayTags::Status_AutoRunning, 0);
		K2_OnEndAutoRun();
	}
}

void AIMGReplayPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// The state may go invalid at any time due to scrubbing during a replay
	if (!IsValid(FollowedPlayerState))
	{
		UWorld* World = GetWorld();

		// Listen for changes for both recording and playback
		if (AIMGGameState* GameState = Cast<AIMGGameState>(World->GetGameState()))
		{
			if (!GameState->OnRecorderPlayerStateChangedEvent.IsBoundToObject(this))
			{
				GameState->OnRecorderPlayerStateChangedEvent.AddUObject(this, &ThisClass::RecorderPlayerStateUpdated);
			}
			if (APlayerState* RecorderState = GameState->GetRecorderPlayerState())
			{
				RecorderPlayerStateUpdated(RecorderState);
			}
		}
	}
}

void AIMGReplayPlayerController::SmoothTargetViewRotation(APawn* TargetPawn, float DeltaSeconds)
{
	// Default behavior is to interpolate to TargetViewRotation which is set from APlayerController::TickActor but it's not very smooth

	Super::SmoothTargetViewRotation(TargetPawn, DeltaSeconds);
}

bool AIMGReplayPlayerController::ShouldRecordClientReplay()
{
	return false;
}

void AIMGReplayPlayerController::RecorderPlayerStateUpdated(APlayerState* NewRecorderPlayerState)
{
	if (NewRecorderPlayerState)
	{
		FollowedPlayerState = NewRecorderPlayerState;

		// Bind to when pawn changes and call now
		NewRecorderPlayerState->OnPawnSet.AddUniqueDynamic(this, &AIMGReplayPlayerController::OnPlayerStatePawnSet);
		OnPlayerStatePawnSet(NewRecorderPlayerState, NewRecorderPlayerState->GetPawn(), nullptr);
	}
}

void AIMGReplayPlayerController::OnPlayerStatePawnSet(APlayerState* ChangedPlayerState, APawn* NewPlayerPawn, APawn* OldPlayerPawn)
{
	if (ChangedPlayerState == FollowedPlayerState)
	{
		SetViewTarget(NewPlayerPawn);
	}
}
