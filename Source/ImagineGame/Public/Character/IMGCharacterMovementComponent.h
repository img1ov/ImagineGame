
#pragma once

#include "NativeGameplayTags.h"
#include "GameFramework/CharacterMovementComponent.h"

#include "IMGCharacterMovementComponent.generated.h"

#define UE_API IMAGINEGAME_API

class UObject;
struct FFrame;

IMAGINEGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Gameplay_MovementStopped);

UENUM(BlueprintType)
enum class EIMGStance : uint8
{
	Stand,
	Crouch,
	Crawl
};

class UIMGCharacterMovementComponent;
struct FIMGStanceStateBase;

/**
 * Lightweight native stance state machine.
 *
 * The movement component owns this live state. AIMGCharacter only keeps the
 * replicated snapshot used to synchronize simulated proxies.
 */
struct IMAGINEGAME_API FIMGStanceStateMachine
{
	EIMGStance GetCurrentStance() const { return CurrentStance; }
	EIMGStance GetDesiredStance() const { return DesiredStance; }

	bool RequestTransition(UIMGCharacterMovementComponent& Movement, EIMGStance NewStance);
	void SetDesiredStance(EIMGStance NewStance);
	bool ReconcileDesiredStance(UIMGCharacterMovementComponent& Movement);
	bool ApplyReplicatedStance(UIMGCharacterMovementComponent& Movement, EIMGStance ReplicatedStance);
	void UpdateCurrentState(UIMGCharacterMovementComponent& Movement, float DeltaSeconds);
	float GetMaxSpeed(const UIMGCharacterMovementComponent& Movement, float DefaultSpeed) const;

private:
	friend class UIMGCharacterMovementComponent;
	static const FIMGStanceStateBase& ResolveState(EIMGStance Stance);
	bool TransitionTo(UIMGCharacterMovementComponent& Movement, EIMGStance NewStance, bool bClientSimulation = false);

	EIMGStance CurrentStance = EIMGStance::Stand;
	EIMGStance DesiredStance = EIMGStance::Stand;
};

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
	UE_API virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;
	UE_API virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	UE_API virtual void UpdateCharacterStateAfterMovement(float DeltaSeconds) override;
	UE_API virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	UE_API virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;

	UE_API bool RequestStance(EIMGStance NewStance);
	UE_API EIMGStance GetDesiredStance() const { return StanceMachine.GetDesiredStance(); }
	UE_API EIMGStance GetStance() const;
	UE_API bool IsCrawling() const { return GetStance() == EIMGStance::Crawl; }
	UE_API void ApplyReplicatedStance(EIMGStance ReplicatedStance);

	UE_API virtual void Crouch(bool bClientSimulation = false) override;
	UE_API virtual void UnCrouch(bool bClientSimulation = false) override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Stance")
	bool bCanCrawl = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Stance", meta = (ClampMin = "0"))
	float CrawlHalfHeight = 44.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Stance", meta = (ClampMin = "0"))
	float MaxCrawlSpeed = 120.0f;
	
	// TODO : Fix: OffsetRootBone one-frame “flick” on Listen Server (CMC timing bug)
	UE_API virtual void TickCharacterPose(float DeltaTime) override;
	
	//~End of UMovementComponent interface

	UFUNCTION(BlueprintCallable, Category = "IMG|CharacterMovement")
	void SetStrafeEnabled(const bool bEnable);

protected:
	FIMGStanceStateMachine StanceMachine;
	bool ResizeForStance(EIMGStance NewStance, bool bClientSimulation);
	float GetStanceHalfHeight(EIMGStance Stance) const;
	void SetDesiredStanceFromMove(EIMGStance NewStance);

	friend class FSavedMove_IMG;
	friend struct FIMGStanceStateMachine;
	friend struct FIMGStandStance;
	friend struct FIMGCrouchStance;
	friend struct FIMGCrawlStance;
	
	// Cached ground info for the character.  Do not access this directly!  It's only updated when accessed via GetGroundInfo().
	FIMGCharacterGroundInfo CachedGroundInfo;
	
	UPROPERTY(Transient)
	bool bHasReplicatedAcceleration = false;
};

#undef UE_API
