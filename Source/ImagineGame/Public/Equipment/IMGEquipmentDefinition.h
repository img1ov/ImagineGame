
#pragma once

#include "Templates/SubclassOf.h"
#include "GameplayTagContainer.h"

#include "IMGEquipmentDefinition.generated.h"

class AActor;
class UIMGAbilitySet;
class UIMGEquipmentInstance;

USTRUCT()
struct FIMGEquipmentPartAttachment
{
	GENERATED_BODY()

	// Matches the spawned Part Actor's GameplayTags.
	UPROPERTY(EditAnywhere, Category=Equipment, meta=(Categories="Cosmetic"))
	FGameplayTag PartTag;

	// Optional ComponentTags name on the matched Part. Falls back to its first skeletal mesh.
	UPROPERTY(EditAnywhere, Category=Equipment)
	FName ComponentTag;
};

USTRUCT()
struct FIMGEquipmentActorToSpawn
{
	GENERATED_BODY()

	FIMGEquipmentActorToSpawn()
	{}

	UPROPERTY(EditAnywhere, Category=Equipment)
	TSubclassOf<AActor> ActorToSpawn;

	// Enables local presentation on non-dedicated peers, with the visible Character Mesh as fallback.
	// When disabled, the authority spawns and attaches to the Character Mesh or other Pawn's root.
	UPROPERTY(EditAnywhere, Category=Equipment)
	bool bUsePartAttachment = false;

	UPROPERTY(EditAnywhere, Category=Equipment, meta=(EditCondition="bUsePartAttachment"))
	FIMGEquipmentPartAttachment PartAttachment;

	UPROPERTY(EditAnywhere, Category=Equipment)
	FName AttachSocket;

	UPROPERTY(EditAnywhere, Category=Equipment)
	FTransform AttachTransform;
};


/**
 * UIMGEquipmentDefinition
 *
 * Definition of a piece of equipment that can be applied to a pawn
 */
UCLASS(Blueprintable, Const, Abstract, BlueprintType)
class UIMGEquipmentDefinition : public UObject
{
	GENERATED_BODY()

public:
	UIMGEquipmentDefinition(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// Class to spawn
	UPROPERTY(EditDefaultsOnly, Category=Equipment)
	TSubclassOf<UIMGEquipmentInstance> InstanceType;

	// Gameplay ability sets to grant when this is equipped
	UPROPERTY(EditDefaultsOnly, Category=Equipment)
	TArray<TObjectPtr<const UIMGAbilitySet>> AbilitySetsToGrant;

	// Actors to spawn on the pawn when this is equipped
	UPROPERTY(EditDefaultsOnly, Category=Equipment)
	TArray<FIMGEquipmentActorToSpawn> ActorsToSpawn;
};
