// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "AbilitySystemInterface.h"
#include "CommonPlayerController.h"
#include "Teams/IMGTeamAgentInterface.h"

#include "IMGPlayerController.generated.h"

#define UE_API IMAGINEGAME_API

struct FGenericTeamId;

class AIMGPlayerState;
class UAbilitySystemComponent;
class UIMGAbilitySystemComponent;
class UObject;

/**
 * AIMGPlayerController
 *
 *	The base player controller class used by this project.
 */
UCLASS(MinimalAPI, Config = Game, Meta = (ShortTooltip = "The base player controller class used by this project."))
class AIMGPlayerController : public ACommonPlayerController, public IAbilitySystemInterface, public IIMGTeamAgentInterface
{
	GENERATED_BODY()

public:
	UE_API AIMGPlayerController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	UE_API virtual ~AIMGPlayerController() override;

	UFUNCTION(BlueprintCallable, Category = "IMG|PlayerController")
	UE_API AIMGPlayerState* GetIMGPlayerState() const;

	UFUNCTION(BlueprintCallable, Category = "IMG|PlayerController")
	UE_API UIMGAbilitySystemComponent* GetIMGAbilitySystemComponent() const;

	UFUNCTION(BlueprintCallable, Category = "IMG|PlayerController")
	UE_API virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	UE_API virtual void SetGenericTeamId(const FGenericTeamId& NewTeamID) override;
	UE_API virtual FGenericTeamId GetGenericTeamId() const override;
	UE_API virtual FOnIMGTeamIndexChangedDelegate* GetOnTeamIndexChangedDelegate() override;
	
	//~AActor interface
	UE_API virtual void PreInitializeComponents() override;
	UE_API virtual void BeginPlay() override;
	UE_API virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	UE_API virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~End of AActor interface

	//~AController interface
	UE_API virtual void OnPossess(APawn* InPawn) override;
	UE_API virtual void OnUnPossess() override;
	UE_API virtual void InitPlayerState() override;
	UE_API virtual void CleanupPlayerState() override;
	UE_API virtual void OnRep_PlayerState() override;
	//~End of AController interface

	//~APlayerController interface
	UE_API virtual void PreProcessInput(const float DeltaTime, const bool bGamePaused) override;
	UE_API virtual void PostProcessInput(const float DeltaTime, const bool bGamePaused) override;
	UE_API virtual void PlayerTick(float DeltaTime) override;
	//~End of APlayerController interface
	
protected:

	UE_API virtual void OnPlayerStateChanged();
	
private:
	
	void BroadcastOnPlayerStateChanged();

	UFUNCTION()
	void OnPlayerStateChangedTeam(UObject* TeamAgent, int32 OldTeam, int32 NewTeam);

private:
	
	UPROPERTY()
	FOnIMGTeamIndexChangedDelegate OnTeamChangedDelegate;

	UPROPERTY()
	TObjectPtr<APlayerState> LastSeenPlayerState;
};

#undef UE_API
