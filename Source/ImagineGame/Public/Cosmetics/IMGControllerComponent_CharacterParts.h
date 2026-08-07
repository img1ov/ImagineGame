

#pragma once

#include "Components/ControllerComponent.h"
#include "IMGCharacterPartTypes.h"

#include "IMGControllerComponent_CharacterParts.generated.h"

class APawn;
class UIMGPawnComponent_CharacterParts;
class UObject;
struct FFrame;

enum class ECharacterPartSource : uint8
{
	Natural,

	NaturalSuppressedViaCheat,

	AppliedViaDeveloperSettingsCheat,

	AppliedViaCheatManager
};

//////////////////////////////////////////////////////////////////////

// A character part requested on a controller component
USTRUCT()
struct FIMGControllerCharacterPartEntry
{
	GENERATED_BODY()

	FIMGControllerCharacterPartEntry()
	{}

public:
	// The character part being represented
	UPROPERTY(EditAnywhere, meta=(ShowOnlyInnerProperties))
	FIMGCharacterPart Part;

	// The handle if already applied to a pawn
	FIMGCharacterPartHandle Handle;

	// The source of this part
	ECharacterPartSource Source = ECharacterPartSource::Natural;
};

//////////////////////////////////////////////////////////////////////

// A component that configure what cosmetic actors to spawn for the owning controller when it possesses a pawn
UCLASS(meta = (BlueprintSpawnableComponent))
class UIMGControllerComponent_CharacterParts : public UControllerComponent
{
	GENERATED_BODY()

public:
	UIMGControllerComponent_CharacterParts(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~UActorComponent interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~End of UActorComponent interface

	// Adds a character part to the actor that owns this customization component, should be called on the authority only
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category=Cosmetics)
	void AddCharacterPart(const FIMGCharacterPart& NewPart);

	// Removes a previously added character part from the actor that owns this customization component, should be called on the authority only
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category=Cosmetics)
	void RemoveCharacterPart(const FIMGCharacterPart& PartToRemove);

	// Removes all added character parts, should be called on the authority only
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category=Cosmetics)
	void RemoveAllCharacterParts();

	// Applies relevant developer settings if in PIE
	void ApplyDeveloperSettings();

protected:
	UPROPERTY(EditAnywhere, Category=Cosmetics)
	TArray<FIMGControllerCharacterPartEntry> CharacterParts;

private:
	UIMGPawnComponent_CharacterParts* GetPawnCustomizer() const;

	UFUNCTION()
	void OnPossessedPawnChanged(APawn* OldPawn, APawn* NewPawn);

	void AddCharacterPartInternal(const FIMGCharacterPart& NewPart, ECharacterPartSource Source);

	void AddCheatPart(const FIMGCharacterPart& NewPart, bool bSuppressNaturalParts);
	void ClearCheatParts();

	void SetSuppressionOnNaturalParts(bool bSuppressed);

	friend class UIMGCosmeticCheats;
};
