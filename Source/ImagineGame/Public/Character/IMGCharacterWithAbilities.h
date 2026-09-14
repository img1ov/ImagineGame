#pragma once

#include "Character/IMGCharacter.h"

#include "IMGCharacterWithAbilities.generated.h"

class UAbilitySystemComponent;
class UIMGAbilitySystemComponent;

// AIMGCharacter normally gets its ability system component from the possessing player state.
// This variant owns a self-contained ability system component.
UCLASS(MinimalAPI, Blueprintable)
class AIMGCharacterWithAbilities : public AIMGCharacter
{
	GENERATED_BODY()

public:
	IMAGINEGAME_API AIMGCharacterWithAbilities(const FObjectInitializer& ObjectInitializer);

	IMAGINEGAME_API virtual void PostInitializeComponents() override;
	IMAGINEGAME_API virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

private:
	UPROPERTY(VisibleAnywhere, Category = "IMG|PlayerState")
	TObjectPtr<UIMGAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY()
	TObjectPtr<const class UIMGHealthSet> HealthSet;

	UPROPERTY()
	TObjectPtr<const class UIMGCombatSet> CombatSet;
};
