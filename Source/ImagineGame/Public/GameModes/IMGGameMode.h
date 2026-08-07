// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "ModularGameMode.h"

#include "IMGGameMode.generated.h"

#define UE_API IMAGINEGAME_API

class AActor;
class AController;
class UIMGPawnData;
class UIMGExperienceDefinition;

/**
 * Post login event, triggered when a player or bot joins the game as well as after seamless and non seamless travel
 *
 * This is called after the player has finished initialization
 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnGameModePlayerInitialized, AGameModeBase* /*GameMode*/, AController* /*NewPlayer*/);

/**
 * AIMGGameMode
 *
 *	The base game mode class used by this project.
 */
UCLASS(MinimalAPI, Config = Game, Meta = (ShortTooltip = "The base game mode class used by this project."))
class AIMGGameMode : public AModularGameModeBase
{
	GENERATED_BODY()

public:
	
	UE_API AIMGGameMode(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintCallable, Category = "IMG|Pawn")
	UE_API const UIMGPawnData* GetPawnDataForController(const AController* InController) const;
	
	//~AGameModeBase interface
	UE_API virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	UE_API virtual UClass* GetDefaultPawnClassForController_Implementation(AController* InController) override;
	UE_API virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform) override;
	UE_API virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	UE_API virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	UE_API virtual void GenericPlayerInitialization(AController* NewPlayer) override;
	UE_API virtual void InitGameState() override;
	//~End of AGameModeBase interface
	
	// Agnostic version of PlayerCanRestart that can be used for both player bots and players
	UE_API virtual bool ControllerCanRestart(AController* Controller);
	
	// Delegate called on player initialization, described above 
	FOnGameModePlayerInitialized OnGameModePlayerInitialized;


protected:
	
	UE_API void OnExperienceLoaded(const UIMGExperienceDefinition* CurrentExperience);
	UE_API bool IsExperienceLoaded() const;
	
	UE_API void HandleMatchAssignmentIfNotExpectingOne();

	UE_API void OnMatchAssignmentGiven(const FPrimaryAssetId& ExperienceId, const FString& ExperienceIdSource) const;
	
	UE_API bool TryDedicatedServerLogin();
};

#undef UE_API