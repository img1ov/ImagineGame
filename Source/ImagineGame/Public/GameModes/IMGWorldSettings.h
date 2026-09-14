
#pragma once

#include "GameFramework/WorldSettings.h"
#include "IMGWorldSettings.generated.h"

class UIMGExperienceDefinition;
/**
 * 
 */
UCLASS()
class IMAGINEGAME_API AIMGWorldSettings : public AWorldSettings
{
	GENERATED_BODY()

public:
	AIMGWorldSettings(const FObjectInitializer& ObjectInitializer);

#if WITH_EDITOR
	virtual void CheckForErrors() override;
#endif

public:
	FPrimaryAssetId GetDefaultGameplayExperience() const;

protected:
	UPROPERTY(EditDefaultsOnly, Category = GameMode)
	TSoftClassPtr<UIMGExperienceDefinition> DefaultGameplayExperience;

public:
#if WITH_EDITORONLY_DATA
	UPROPERTY(EditDefaultsOnly, Category = PIE)
	bool ForceStandaloneNetMode = false;
#endif
};
