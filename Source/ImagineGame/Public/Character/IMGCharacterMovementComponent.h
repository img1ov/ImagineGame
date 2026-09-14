
#pragma once

#include "NativeGameplayTags.h"
#include "GameFramework/CharacterMovementComponent.h"

#include "IMGCharacterMovementComponent.generated.h"

#define UE_API IMAGINEGAME_API

class UObject;
struct FFrame;

IMAGINEGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Gameplay_MovementStopped);

/**
 * FIMGCharacterGroundInfo
 *
 *	Information about the ground under the character.  It only gets updated as needed.
 */
USTRUCT(BlueprintType)
struct FIMGCharacterGroundInfo
{
	GENERATED_BODY()

	FIMGCharacterGroundInfo()
		: LastUpdateFrame(0)
		, GroundDistance(0.0f)
	{
	}

	uint64 LastUpdateFrame;

	UPROPERTY(BlueprintReadOnly)
	FHitResult GroundHitResult;

	UPROPERTY(BlueprintReadOnly)
	float GroundDistance;
};

/**
 * UIMGCharacterMovementComponent
 *
 *	The base character movement component class used by this project.
 */
UCLASS(MinimalAPI, Config = Game)
class UIMGCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	
	UE_API UIMGCharacterMovementComponent(const FObjectInitializer& ObjectInitializer);
	
	UE_API virtual void InitializeComponent() override;
	
	UE_API virtual bool CanAttemptJump() const override;
	
	UFUNCTION(BlueprintCallable, Category = "IMG|CharacterMovement")
	UE_API const FIMGCharacterGroundInfo& GetGroundInfo();
	
	UE_API void SetReplicatedAcceleration(const FVector& InAcceleration);
	
	UE_API void SetReplicatedRotation(const FRotator& InRotation);
	
	//~UMovementComponent interface
	UE_API virtual void SimulateMovement(float DeltaTime) override;
	UE_API virtual FRotator GetDeltaRotation(float DeltaTime) const override;
	
	UE_API virtual float GetMaxSpeed() const override;
	
	// TODO : Fix: OffsetRootBone one-frame “flick” on Listen Server (CMC timing bug)
	UE_API virtual void TickCharacterPose(float DeltaTime) override;
	
	//~End of UMovementComponent interface
	
	UFUNCTION(BlueprintCallable, Category = "IMG|CharacterMovement")
	void SetStrafeEnabled(const bool bEnable);

protected:
	
	// Cached ground info for the character.  Do not access this directly!  It's only updated when accessed via GetGroundInfo().
	FIMGCharacterGroundInfo CachedGroundInfo;
	
	UPROPERTY(Transient)
	bool bHasReplicatedAcceleration = false;
};

#undef UE_API
