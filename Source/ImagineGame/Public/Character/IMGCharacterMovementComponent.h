
#pragma once

#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/CharacterMovementReplication.h"
#include "Character/Ragdoll/IMGCustomMovementMode_Ragdoll.h"

#include "IMGCharacterMovementComponent.generated.h"

#define UE_API IMAGINEGAME_API

class UObject;
struct FFrame;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FIMGRagdollMovementEvent);

UENUM(BlueprintType)
enum class EIMGCustomMovementMode : uint8
{
	None,
	Ragdoll
};

struct IMAGINEGAME_API FIMGCharacterNetworkMoveData : FCharacterNetworkMoveData
{
	FIMGRagdollMovementInput RagdollInput;
	virtual void ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType) override;
	virtual bool Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType) override;
};

struct IMAGINEGAME_API FIMGCharacterNetworkMoveDataContainer : FCharacterNetworkMoveDataContainer
{
	FIMGCharacterNetworkMoveDataContainer();
	FIMGCharacterNetworkMoveData Moves[3];
};

struct IMAGINEGAME_API FIMGCharacterMoveResponseDataContainer : FCharacterMoveResponseDataContainer
{
	bool bHasRagdollState = false;
	/** Correlates the native result with placement, even if the snapshot changed before packing. */
	bool bRagdollPlacementReflected = false;
	FIMGRagdollRepState RagdollState;
	virtual void ServerFillResponseData(const UCharacterMovementComponent& CharacterMovement, const FClientAdjustment& PendingAdjustment) override;
	virtual bool Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap) override;
};

UENUM(BlueprintType)
enum class EIMGStance : uint8
{
	Stand,
	Crouch,
	Crawl
};

class UIMGCharacterMovementComponent;

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
	void UpdateCurrentState(UIMGCharacterMovementComponent& Movement);

private:
	friend class UIMGCharacterMovementComponent;
	bool TransitionTo(UIMGCharacterMovementComponent& Movement, EIMGStance NewStance, bool bClientSimulation = false);

	EIMGStance CurrentStance = EIMGStance::Stand;
	EIMGStance DesiredStance = EIMGStance::Stand;
	bool bTransitionInProgress = false;
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
	
	UE_API virtual bool CanAttemptJump() const override;
	UE_API virtual bool DoJump(bool bReplayingMoves, float DeltaTime) override;
	
	UFUNCTION(BlueprintCallable, Category = "IMG|CharacterMovement")
	UE_API const FIMGCharacterGroundInfo& GetGroundInfo();
	
	UE_API void SetReplicatedAcceleration(const FVector& InAcceleration);
	
	//~UMovementComponent interface
	UE_API virtual void SimulateMovement(float DeltaTime) override;
	UE_API virtual void SimulatedTick(float DeltaSeconds) override;
	
	UE_API virtual float GetMaxSpeed() const override;
	UE_API virtual void Crouch(bool bClientSimulation = false) override;
	UE_API virtual void UnCrouch(bool bClientSimulation = false) override;
	UE_API virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	UE_API virtual void UpdateCharacterStateAfterMovement(float DeltaSeconds) override;
	UE_API virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	UE_API virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	UE_API virtual void PhysCustom(float DeltaTime, int32 Iterations) override;
	UE_API virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;
	UE_API virtual void PhysicsRotation(float DeltaTime) override;
	UE_API virtual void MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags, const FVector& NewAccel) override;
	UE_API virtual void ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& MoveResponse) override;
	using Super::ServerMoveHandleClientError;
	UE_API virtual void ServerMoveHandleClientError(float ClientTimeStamp, float DeltaTime, const FVector& Accel, const FVector& RelativeClientLocation,
		FMovementBaseInterfaceData* ClientMovementBaseInterfaceData, FName ClientBaseBoneName, uint8 ClientMovementMode) override;

	UFUNCTION(BlueprintPure, Category = "IMG|CharacterMovement")
	UE_API bool IsRagdollMovementMode() const;
	UE_API bool HasRagdollNetworkMovementMode() const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "IMG|CharacterMovement|Ragdoll")
	UE_API bool StartRagdollMovement(const FIMGRagdollSettings& Settings, FVector AdditionalVelocity = FVector::ZeroVector);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "IMG|CharacterMovement|Ragdoll")
	UE_API bool RecoverRagdollMovement(int32 ExpectedSession);
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "IMG|CharacterMovement|Ragdoll")
	UE_API bool QueryRagdollRecovery(int32 ExpectedSession, FTransform& OutTransform, EIMGRagdollIssue& OutIssue, FVector BodyLocalOffset = FVector::ZeroVector) const;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "IMG|CharacterMovement|Ragdoll")
	UE_API bool RecoverRagdollMovementAt(int32 ExpectedSession, const FTransform& Transform, FVector BodyLocalOffset = FVector::ZeroVector);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "IMG|CharacterMovement|Ragdoll")
	UE_API bool StopRagdollMovement(EIMGRagdollEndReason Reason = EIMGRagdollEndReason::Interrupted, int32 ExpectedSession = 0);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "IMG|CharacterMovement|Ragdoll")
	UE_API void CompleteRagdollRecovery(int32 ExpectedSession, bool bInterrupted = false);
	UFUNCTION(BlueprintCallable, Category = "IMG|CharacterMovement|Ragdoll", meta = (DefaultToSelf = "InputSource"))
	UE_API bool SubmitRagdollFollowTarget(const FTransform& Target, bool bFaceUp, UObject* InputSource, int32 ExpectedSession);
	UFUNCTION(BlueprintCallable, Category = "IMG|CharacterMovement|Ragdoll")
	UE_API void ClearRagdollFollowTarget(const UObject* InputSource, int32 ExpectedSession);
	UE_API bool IsRagdollRecovering() const;
	UE_API bool IsRagdollRecoveryReady() const;
	UE_API const FIMGRagdollRepState& GetRagdollMovementState() const;
	UE_API void ReceiveRagdollMovementState(const FIMGRagdollRepState& State);
	UFUNCTION(BlueprintPure, Category = "IMG|CharacterMovement|Ragdoll")
	EIMGRagdollPhase GetRagdollMovementPhase() const { return ActiveRagdollState.Phase; }
	UFUNCTION(BlueprintPure, Category = "IMG|CharacterMovement|Ragdoll")
	int32 GetRagdollMovementSession() const { return ActiveRagdollState.Session; }
	UFUNCTION(BlueprintPure, Category = "IMG|CharacterMovement|Ragdoll")
	FVector GetRagdollInitialVelocity() const { return ActiveRagdollState.InitialVelocity; }
	UFUNCTION(BlueprintPure, Category = "IMG|CharacterMovement|Ragdoll")
	EIMGRagdollIssue GetRagdollMovementIssue() const { return RagdollMovementIssue; }
	UPROPERTY(BlueprintAssignable, Category = "IMG|CharacterMovement|Ragdoll")
	FIMGRagdollMovementEvent OnRagdollMovementUpdated;
	UE_API FIMGRagdollMovementInput CaptureRagdollInput() const;
	/** Cached live input, or the saved historical input while replaying. Never samples the body. */
	UE_API FIMGRagdollMovementInput GetRagdollMovementInput() const;
	UE_API bool FindRagdollRecoveryTransform(FTransform& OutTransform) const;
	UE_API virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UE_API virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	UE_API virtual void Deactivate() override;
	UE_API virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	UE_API virtual void OnRep_IsActive() override;
	UE_API virtual void OnUnregister() override;

	UE_API bool RequestStance(EIMGStance NewStance);
	UE_API EIMGStance GetDesiredStance() const;
	UE_API EIMGStance GetStance() const;
	UE_API bool IsCrawling() const { return GetStance() == EIMGStance::Crawl; }
	UE_API void ApplyReplicatedStance(EIMGStance ReplicatedStance);

	/** Whether this movement component can enter the Crawl stance. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Character Movement: Stance")
	bool bCanCrawl = true;

	/** Collision capsule half-height used while crawling. It cannot be smaller than the capsule radius at runtime. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Character Movement: Stance", meta = (ClampMin = "0", UIMin = "0", ForceUnits = "cm"))
	float CrawlHalfHeight = 44.0f;

	/** Maximum ground speed while crawling. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Character Movement: Stance", meta = (ClampMin = "0", UIMin = "0", ForceUnits = "cm/s"))
	float MaxCrawlSpeed = 120.0f;

	// TODO : Fix: OffsetRootBone one-frame “flick” on Listen Server (CMC timing bug)
	UE_API virtual void TickCharacterPose(float DeltaTime) override;
	
	//~End of UMovementComponent interface

	UFUNCTION(BlueprintCallable, Category = "IMG|CharacterMovement")
	void SetStrafeEnabled(const bool bEnable);

protected:
	FIMGStanceStateMachine StanceMachine;
	bool CanEnterStance(EIMGStance NewStance) const;
	bool ApplyStanceTransition(EIMGStance PreviousStance, EIMGStance NewStance, bool bClientSimulation);
	bool ResizeForCrawlTransition(EIMGStance PreviousStance, EIMGStance NewStance, bool bClientSimulation);
	float GetStanceHalfHeight(EIMGStance Stance) const;
	void SetDesiredStanceFromMove(EIMGStance NewStance);

	friend class FSavedMove_IMG;
	friend struct FIMGCharacterMoveResponseDataContainer;
	friend struct FIMGStanceStateMachine;
	friend class FIMGCustomMovementMode_Ragdoll;

	FIMGCustomMovementMode_Ragdoll RagdollMode;
	FIMGCharacterNetworkMoveDataContainer IMGNetworkMoveData;
	FIMGCharacterMoveResponseDataContainer IMGMoveResponseData;
	UPROPERTY(ReplicatedUsing = OnRep_RagdollMovementState)
	FIMGRagdollRepState ReplicatedRagdollState;
	UPROPERTY(Transient)
	FIMGRagdollRepState ActiveRagdollState;
	FIMGRagdollMovementInput SubmittedRagdollInput;
	TWeakObjectPtr<UObject> RagdollInputSource;
	double RagdollInputSampleTime = -1.0;
	double RagdollRemoteInputTime = -1.0;
	float RagdollAdjustmentTimeStamp = -1.0f;
	uint16 RagdollAdjustmentPlacementSession = 0;
	EIMGRagdollIssue RagdollMovementIssue = EIMGRagdollIssue::None;
	bool bRagdollTransitioning = false;
	uint16 RagdollRecoveryAppliedSession = 0;
	UFUNCTION()
	void OnRep_RagdollMovementState();
	void PublishRagdollMovementState();
	void FinishRagdollMovement(EIMGRagdollEndReason Reason, bool bLeaveMode, bool bKeepPlacement = false);
	void SetRagdollMovementIssue(EIMGRagdollIssue Issue);
	void ClearRagdollMovementResources();
	FTransform GetRagdollPlacementTransform() const;
	void ApplyRagdollRecoveryTransform(const FTransform& Transform);
	void UpdateRagdollMovementState();
	
	// Cached ground info for the character.  Do not access this directly!  It's only updated when accessed via GetGroundInfo().
	FIMGCharacterGroundInfo CachedGroundInfo;
	
	UPROPERTY(Transient)
	bool bHasReplicatedAcceleration = false;
};

#undef UE_API
