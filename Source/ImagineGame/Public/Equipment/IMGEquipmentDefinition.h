
#pragma once

#include "Templates/SubclassOf.h"
#include "GameplayTagContainer.h"

#include "IMGEquipmentDefinition.generated.h"

class AActor;
class UIMGAbilitySet;
class UIMGEquipmentInstance;

USTRUCT()
struct FIMGEquipmentActorToSpawn
{
	GENERATED_BODY()

	FIMGEquipmentActorToSpawn()
	{}

	UPROPERTY(EditAnywhere, Category=Equipment)
	TSubclassOf<AActor> ActorToSpawn;

	// A valid tag opts this entry into non-replicated presentation on each non-dedicated peer.
	// Matches the spawned Part Actor's GameplayTags, never the combined cosmetic tags.
	UPROPERTY(EditAnywhere, Category=Equipment, meta=(Categories="Cosmetic"))
	FGameplayTag AttachToPartTag;

	// Optional ComponentTags name on the selected Part. If absent or unmatched, use its first skeletal mesh.
	UPROPERTY(EditAnywhere, Category=Equipment)
	FName AttachComponentTag;

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
