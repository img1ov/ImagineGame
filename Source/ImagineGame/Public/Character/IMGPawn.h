#pragma once

#include "ModularPawn.h"
#include "Teams/IMGTeamAgentInterface.h"

#include "IMGPawn.generated.h"

class AController;

/** Base modular pawn with replicated team membership. */
UCLASS(MinimalAPI)
class AIMGPawn : public AModularPawn, public IIMGTeamAgentInterface
{
	GENERATED_BODY()

public:
	IMAGINEGAME_API AIMGPawn(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	IMAGINEGAME_API virtual void PreInitializeComponents() override;
	IMAGINEGAME_API virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	IMAGINEGAME_API virtual void PossessedBy(AController* NewController) override;
	IMAGINEGAME_API virtual void UnPossessed() override;

	IMAGINEGAME_API virtual void SetGenericTeamId(const FGenericTeamId& NewTeamID) override;
	IMAGINEGAME_API virtual FGenericTeamId GetGenericTeamId() const override;
	IMAGINEGAME_API virtual FOnIMGTeamIndexChangedDelegate* GetOnTeamIndexChangedDelegate() override;

protected:
	virtual FGenericTeamId DetermineNewTeamAfterPossessionEnds(FGenericTeamId OldTeamID) const
	{
		return FGenericTeamId::NoTeam;
	}

private:
	UFUNCTION()
	IMAGINEGAME_API void OnControllerChangedTeam(UObject* TeamAgent, int32 OldTeam, int32 NewTeam);

	UPROPERTY(ReplicatedUsing = OnRep_MyTeamID)
	FGenericTeamId MyTeamID;

	UPROPERTY()
	FOnIMGTeamIndexChangedDelegate OnTeamChangedDelegate;

	UFUNCTION()
	IMAGINEGAME_API void OnRep_MyTeamID(FGenericTeamId OldTeamID);
};
