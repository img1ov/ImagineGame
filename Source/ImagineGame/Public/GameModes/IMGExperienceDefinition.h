
#pragma once

#include "Engine/DataAsset.h"
#include "IMGExperienceDefinition.generated.h"

class UIMGExperienceActionSet;
class UGameFeatureAction;
class UIMGPawnData;

/**
 * Definition of an experience
 */
UCLASS(BlueprintType, Const)
class IMAGINEGAME_API UIMGExperienceDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UIMGExperienceDefinition();

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

#if WITH_EDITORONLY_DATA
	virtual void UpdateAssetBundleData() override;
#endif

public:
	// List of Game Feature Plugins this experience wants to have active
	UPROPERTY(EditDefaultsOnly, Category = Gameplay)
	TArray<FString> GameFeaturesToEnable;

	/** The default pawn class to spawn for players */
	//@TODO: Make soft?
	UPROPERTY(EditDefaultsOnly, Category = Gameplay)
	TObjectPtr<const UIMGPawnData> DefaultPawnData;

	// List of actions to perform as this experience is loaded/activated/deactivated/unloaded
	UPROPERTY(EditDefaultsOnly, Instanced, Category="Actions")
	TArray<TObjectPtr<UGameFeatureAction>> Actions;

	// List of additional action sets to compose into this experience
	UPROPERTY(EditDefaultsOnly, Category=Gameplay)
	TArray<TObjectPtr<UIMGExperienceActionSet>> ActionSets;
};
