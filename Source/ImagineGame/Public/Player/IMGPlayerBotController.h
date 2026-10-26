

#pragma once

#include "ModularAIController.h"
#include "Teams/IMGTeamAgentInterface.h"

#include "IMGPlayerBotController.generated.h"

namespace ETeamAttitude { enum Type : int; }
struct FGenericTeamId;

class APlayerState;
class UIMGPawnData;
class UAIPerceptionComponent;
class UObject;
struct FFrame;

/**
 * AIMGPlayerBotController
 *
 *	The controller class used by player bots in this project.
 */
UCLASS(Blueprintable)
class IMAGINEGAME_API AIMGPlayerBotController : public AModularAIController, public IIMGTeamAgentInterface
{
	GENERATED_BODY()

public:
	AIMGPlayerBotController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/**
	 * Synchronous server query; may run before BeginPlay, PlayerState assignment and possession.
	 * Keep the selection stable and free of side effects until the PlayerState claims it.
	 * Return null to use the experience default. The PlayerState retains the first resolved data.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "IMG|Pawn")
	const UIMGPawnData* GetPawnData() const;
	virtual const UIMGPawnData* GetPawnData_Implementation() const;

	//~IIMGTeamAgentInterface interface
	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamID) override;
	virtual FGenericTeamId GetGenericTeamId() const override;
	virtual FOnIMGTeamIndexChangedDelegate* GetOnTeamIndexChangedDelegate() override;
	virtual ETeamAttitude::Type GetTeamAttitudeTowards(const AActor& Other) const override;
	//~End of IIMGTeamAgentInterface interface

	// Attempts to restart this controller (e.g., to respawn it)
	void ServerRestartController();

	//Update Team Attitude for the AI
	UFUNCTION(BlueprintCallable, Category = "IMG AI Player Controller")
	void UpdateTeamAttitude(UAIPerceptionComponent* AIPerception);

	virtual void OnUnPossess() override;


private:
	UFUNCTION()
	void OnPlayerStateChangedTeam(UObject* TeamAgent, int32 OldTeam, int32 NewTeam);

protected:
	// Called when the player state is set or cleared
	virtual void OnPlayerStateChanged();

private:
	void BroadcastOnPlayerStateChanged();

protected:	
	//~AController interface
	virtual void InitPlayerState() override;
	virtual void CleanupPlayerState() override;
	virtual void OnRep_PlayerState() override;
	//~End of AController interface

private:
	UPROPERTY()
	FOnIMGTeamIndexChangedDelegate OnTeamChangedDelegate;

	UPROPERTY()
	TObjectPtr<APlayerState> LastSeenPlayerState;
};
