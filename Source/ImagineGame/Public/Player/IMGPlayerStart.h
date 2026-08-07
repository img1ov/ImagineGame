

#pragma once

#include "GameFramework/PlayerStart.h"
#include "GameplayTagContainer.h"

#include "IMGPlayerStart.generated.h"

#define UE_API IMAGINEGAME_API

class AController;
class UObject;

enum class EIPlayerStartLocationOccupancy
{
	Empty,
	Partial,
	Full
};

/**
 * AIMGPlayerStart
 * 
 * Base player starts that can be used by a lot of modes.
 */
UCLASS(MinimalAPI, Config = Game)
class AIMGPlayerStart : public APlayerStart
{
	GENERATED_BODY()

public:
	UE_API AIMGPlayerStart(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	const FGameplayTagContainer& GetGameplayTags() { return StartPointTags; }

	UE_API EIPlayerStartLocationOccupancy GetLocationOccupancy(AController* const ControllerPawnToFit) const;

	/** Did this player start get claimed by a controller already? */
	UE_API bool IsClaimed() const;

	/** If this PlayerStart was not claimed, claim it for ClaimingController */
	UE_API bool TryClaim(AController* OccupyingController);

protected:
	/** Check if this PlayerStart is still claimed */
	UE_API void CheckUnclaimed();

	/** The controller that claimed this PlayerStart */
	UPROPERTY(Transient)
	TObjectPtr<AController> ClaimingController = nullptr;

	/** Interval in which we'll check if this player start is not colliding with anyone anymore */
	UPROPERTY(EditDefaultsOnly, Category = "Player Start Claiming")
	float ExpirationCheckInterval = 1.f;

	/** Tags to identify this player start */
	UPROPERTY(EditAnywhere)
	FGameplayTagContainer StartPointTags;

	/** Handle to track expiration recurring timer */
	FTimerHandle ExpirationTimerHandle;
};

#undef UE_API
