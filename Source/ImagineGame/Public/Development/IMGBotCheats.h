#pragma once

#include "GameFramework/CheatManager.h"

#include "IMGBotCheats.generated.h"

class UIMGBotCreationComponent;
class UObject;
struct FFrame;

/** Cheats related to bots */
UCLASS(NotBlueprintable)
class UIMGBotCheats final : public UCheatManagerExtension
{
	GENERATED_BODY()

public:
	UIMGBotCheats();

	// Adds a bot player
	UFUNCTION(Exec, BlueprintAuthorityOnly)
	void AddPlayerBot();

	// Removes a random bot player
	UFUNCTION(Exec, BlueprintAuthorityOnly)
	void RemovePlayerBot();

private:
	UIMGBotCreationComponent* GetBotComponent() const;
};
