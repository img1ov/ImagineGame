

#pragma once

#include "GameFramework/CheatManager.h"

#include "IMGCosmeticCheats.generated.h"

class UIMGControllerComponent_CharacterParts;
class UObject;
struct FFrame;

/** Cheats related to bots */
UCLASS(NotBlueprintable)
class UIMGCosmeticCheats final : public UCheatManagerExtension
{
	GENERATED_BODY()

public:
	UIMGCosmeticCheats();

	// Adds a character part
	UFUNCTION(Exec, BlueprintAuthorityOnly)
	void AddCharacterPart(const FString& AssetName, bool bSuppressNaturalParts = true);

	// Replaces previous cheat parts with a new one
	UFUNCTION(Exec, BlueprintAuthorityOnly)
	void ReplaceCharacterPart(const FString& AssetName, bool bSuppressNaturalParts = true);

	// Clears any existing cheats
	UFUNCTION(Exec, BlueprintAuthorityOnly)
	void ClearCharacterPartOverrides();

private:
	UIMGControllerComponent_CharacterParts* GetCosmeticComponent() const;
};
