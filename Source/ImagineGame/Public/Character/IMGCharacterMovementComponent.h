#pragma once

#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/CharacterMovementReplication.h"
#include "Character/IMGMovementSimulationContext.h"
#include "Character/Ragdoll/IMGCustomMovementMode_Ragdoll.h"
#include "Misc/Optional.h"
#include "Templates/Function.h"

#include "IMGCharacterMovementComponent.generated.h"

#define UE_API IMAGINEGAME_API

UE_API DECLARE_LOG_CATEGORY_EXTERN(LogIMGMovementNet, Log, All);

class UObject;
class AController;
class USkeletalMeshComponent;
struct FFrame;

DECLARE_DELEGATE_RetVal_TwoParams(bool, FMovementRequestCapture, uint8&, FMovementSimulationContext&);
DECLARE_DELEGATE_RetVal_FourParams(bool, FMovementRequestApply, uint8, const FMovementSimulationContext&, const FVector&, bool);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FIMGRagdollMovementEvent);
DECLARE_MULTICAST_DELEGATE_TwoParams(FIMGRagdollCapsuleCorrection, int32 /* Session */, const FVector& /* PostReplayDelta */);
DECLARE_MULTICAST_DELEGATE_TwoParams(
	FIMGClientCorrectionFilter,
	const FCharacterMoveResponseDataContainer&,
	bool& /* bDiscardCorrection */);

DECLARE_MULTICAST_DELEGATE_TwoParams(
	FIMGClientCorrectionApplied,
	const FCharacterMoveResponseDataContainer&,
	const FSavedMove_Character&);

using FMovementReplayRestore = TFunction<void()>;
using FMovementReplaySetup = TFunction<FMovementReplayRestore()>;
DECLARE_MULTICAST_DELEGATE_TwoParams(FMovementReplayCapture, const FSavedMove_Character&, TArray<FMovementReplaySetup>&);

UENUM(BlueprintType)
enum class EIMGCustomMovementMode : uint8
{
	None,
	Ragdoll
};

struct IMAGINEGAME_API FIMGCharacterNetworkMoveData : FCharacterNetworkMoveData
{
	uint8 InputMagnitude = 0;
	bool bHasMovementRequest = false;
	uint8 MovementRequest = 0;
	uint32 MovementContextRevision = 0;
	uint32 MovementContextGeneration = 0;
	uint32 MovementContextHash = 0;
	uint32 MovementParameterHash = 0;
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
	bool bHasMovementContext = false;
	uint32 MovementContextGeneration = 0;
	FMovementSimulationContext MovementContext;

	bool bHasRagdollState = false;
	/** Correlates the native result with placement, even if the snapshot changed before packing. */
	bool bRagdollPlacementReflected = false;
	FIMGRagdollRepState RagdollState;

	bool bHasRagdollBodyReference = false;
	FIMGRagdollBodyReference RagdollBodyReference;

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
 *	Information about the ground under the character. It only gets updated as needed.
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

/** Transient runtime state for the opaque Locomotion request provider and fingerprint sync. */
struct FIMGMovementRequestRuntime
{
	TWeakObjectPtr<UObject> Source;
	FMovementRequestCapture CaptureDelegate;
	FMovementRequestApply ApplyDelegate;
	uint32 Generation = 0;
	uint8 Request = 0;
	bool bHasRequest = false;
	FVector RequestInput = FVector::ZeroVector;
	uint8 InputMagnitude = 0;
	bool bHasRequestInput = false;
	FMovementSimulationContext Context;
	FMovementSimulationContext AuthorityContext;
	TArray<uint32, TInlineAllocator<8>> AuthorityParameterRevisions;
	uint32 AuthorityContextGeneration = 0;
	bool bContextApplied = false;
	uint32 ParameterHash = 0;
	float ContextTimeStamp = 0.f;
	FMovementSimulationContext AdjustmentContext;
	uint32 AdjustmentGeneration = 0;
	float AdjustmentTimeStamp = 0.f;

	void ResetHistory();
};

/** Transient runtime state for autonomous proxy small-correction mesh smoothing. */
struct FIMGAutonomousMeshSmoothingState
{
	TWeakObjectPtr<USkeletalMeshComponent> Mesh;
	TOptional<FVector> PreCorrectionLocation;
	FVector WorldOffset = FVector::ZeroVector;
	FVector AppliedRelativeOffset = FVector::ZeroVector;
	FVector AppliedRelativeLocation = FVector::ZeroVector;

	void ResetTracking();
};

/** Transient non-replicated runtime state for Ragdoll movement coordination. */
struct FIMGRagdollMovementRuntime
{
	FIMGRagdollBodyReference ActiveBodyReference;
	uint32 BodyReferenceSequence = 0;
	double BodyReferencePublishTime = -1.0;

	FIMGRagdollMovementInput SubmittedInput;
	TWeakObjectPtr<UObject> InputSource;
	double InputSampleTime = -1.0;
	double RemoteInputTime = -1.0;
	float InputMoveTimeStamp = -1.f;

	TOptional<FVector> PreCorrectionLocation;
	uint16 CorrectionSession = 0;
	float AdjustmentTimeStamp = -1.0f;
	uint16 AdjustmentPlacementSession = 0;
	uint16 RecoveryAppliedSession = 0;
	uint16 LocalPlacementSession = 0;

	TWeakObjectPtr<AController> OwningController;
	EIMGRagdollIssue Issue = EIMGRagdollIssue::None;
	bool bTransitioning = false;
	bool bAwaitingInitialInput = false;
	FVector LocalLaunchVelocity = FVector::ZeroVector;
	uint16 LocalLaunchSession = 0;

	void ClearInput();
	void ClearCorrection();
	void ClearAll();
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

	//~ Prediction & Replay Hooks
	/** Listeners may only set discard to true. Discarded corrections still ACK through the native path. Packed responses only. */
	FIMGClientCorrectionFilter ClientCorrectionFilter;
	/** Accepted timestamp-matched corrections only, after native mode/root-motion application. */
	FIMGClientCorrectionApplied ClientCorrectionApplied;
	/** Capture local execution context per move; setup/restore bracket each correction replay. */
	FMovementReplayCapture MovementReplayCapture;
	/** Accepted owner corrections, once after replay. Body drivers may smooth this delta without modifying move history. */
	FIMGRagdollCapsuleCorrection RagdollCapsuleCorrection;

	//~ Locomotion Request Provider
	/** One weak UObject provider; movement records opaque rule inputs and returns timestamp-matched authority context. */
	UE_API bool RegisterMovementRequestProvider(UObject* Source, FMovementRequestCapture Capture, FMovementRequestApply Apply);
	UE_API void UnregisterMovementRequestProvider(const UObject* Source);
	bool IsMovementRequestProvider(const UObject* Source) const { return Source && MovementRequestRuntime.Source.Get() == Source; }
	UE_API bool HasMovementRequestProvider() const;

	//~ Core Movement & UMovementComponent Overrides
	UE_API virtual void ControlledCharacterMove(const FVector& InputVector, float DeltaSeconds) override;
	UE_API virtual void PerformMovement(float DeltaSeconds) override;
	UE_API virtual bool ForcePositionUpdate(float DeltaTime) override;
	UE_API virtual bool CanAttemptJump() const override;
	UE_API virtual bool DoJump(bool bReplayingMoves, float DeltaTime) override;
	UE_API virtual void SimulateMovement(float DeltaTime) override;
	UE_API virtual void SimulatedTick(float DeltaSeconds) override;
	UE_API virtual void SmoothClientPosition(float DeltaSeconds) override;
	UE_API virtual float GetMaxSpeed() const override;
	UE_API virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
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
	UE_API virtual bool ClientUpdatePositionAfterServerUpdate() override;
	UE_API virtual void OnTeleported() override;
	using Super::ServerMoveHandleClientError;
	UE_API virtual void ServerMoveHandleClientError(
		float ClientTimeStamp,
		float DeltaTime,
		const FVector& Accel,
		const FVector& RelativeClientLocation,
		FMovementBaseInterfaceData* ClientMovementBaseInterfaceData,
		FName ClientBaseBoneName,
		uint8 ClientMovementMode) override;
	UE_API virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UE_API virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	UE_API virtual void Deactivate() override;
	UE_API virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	UE_API virtual void OnRep_IsActive() override;
	UE_API virtual void OnUnregister() override;
	// TODO : Fix: OffsetRootBone one-frame "flick" on Listen Server (CMC timing bug)
	UE_API virtual void TickCharacterPose(float DeltaTime) override;

	UFUNCTION(BlueprintCallable, Category = "IMG|CharacterMovement")
	UE_API const FIMGCharacterGroundInfo& GetGroundInfo();

	UE_API void SetReplicatedAcceleration(const FVector& InAcceleration);

	UFUNCTION(BlueprintCallable, Category = "IMG|CharacterMovement")
	void SetStrafeEnabled(const bool bEnable);

	//~ Stance API
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

	//~ Ragdoll Movement API
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

	/** Source/ability validates its action first. This is the authority movement commit, not a client RPC. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "IMG|CharacterMovement|Ragdoll")
	UE_API bool TransitionRagdollMovementAt(int32 ExpectedSession, const FIMGRagdollTransitionRequest& Request, int32& OutTransitionRevision);

	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "IMG|CharacterMovement|Ragdoll")
	UE_API bool QueryRagdollTransition(int32 ExpectedSession, const FIMGRagdollTransitionRequest& Request, EIMGRagdollIssue& OutIssue) const;

	UE_API bool IsRagdollTransitionReady(int32 ExpectedSession, int32 ExpectedRevision) const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "IMG|CharacterMovement|Ragdoll")
	UE_API bool StopRagdollMovement(EIMGRagdollEndReason Reason = EIMGRagdollEndReason::Interrupted, int32 ExpectedSession = 0);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "IMG|CharacterMovement|Ragdoll")
	UE_API void CompleteRagdollRecovery(int32 ExpectedSession, bool bInterrupted = false);

	UFUNCTION(BlueprintCallable, Category = "IMG|CharacterMovement|Ragdoll", meta = (DefaultToSelf = "InputSource"))
	UE_API bool SubmitRagdollFollowTarget(const FTransform& Target, const FVector& PelvisVelocity, float CapsuleHeightOffset, bool bFaceUp, UObject* InputSource, int32 ExpectedSession);

	UFUNCTION(BlueprintCallable, Category = "IMG|CharacterMovement|Ragdoll")
	UE_API void ClearRagdollFollowTarget(const UObject* InputSource, int32 ExpectedSession);

	UE_API bool IsRagdollRecovering() const;
	UE_API bool IsRagdollRecoveryReady() const;
	UE_API const FIMGRagdollRepState& GetRagdollMovementState() const;

	/** Current accepted anchor in local world space. False until phase/mode/clock match, or when invalid/stale. */
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "IMG|CharacterMovement|Ragdoll")
	UE_API bool GetRagdollBodyReference(int32 ExpectedSession, FIMGRagdollBodyReference& OutReference) const;

	UE_API void ReceiveRagdollMovementState(const FIMGRagdollRepState& State);

	UFUNCTION(BlueprintPure, Category = "IMG|CharacterMovement|Ragdoll")
	EIMGRagdollPhase GetRagdollMovementPhase() const { return ActiveRagdollState.Phase; }

	UFUNCTION(BlueprintPure, Category = "IMG|CharacterMovement|Ragdoll")
	int32 GetRagdollMovementSession() const { return ActiveRagdollState.Session; }

	UFUNCTION(BlueprintPure, Category = "IMG|CharacterMovement|Ragdoll")
	FVector GetRagdollInitialVelocity() const { return ActiveRagdollState.InitialVelocity; }
	UE_API FVector GetRagdollLaunchVelocity() const;
	UE_API void ResetRagdollMeshSmoothing(int32 ExpectedSession);

	UFUNCTION(BlueprintPure, Category = "IMG|CharacterMovement|Ragdoll")
	EIMGRagdollIssue GetRagdollMovementIssue() const { return RagdollRuntime.Issue; }

	UPROPERTY(BlueprintAssignable, Category = "IMG|CharacterMovement|Ragdoll")
	FIMGRagdollMovementEvent OnRagdollMovementUpdated;

	UE_API FIMGRagdollMovementInput CaptureRagdollInput() const;
	/** Cached live input, or the saved historical input while replaying. Never samples the body. */
	UE_API FIMGRagdollMovementInput GetRagdollMovementInput() const;
	UE_API bool FindRagdollRecoveryTransform(FTransform& OutTransform) const;

protected:
	UE_API virtual float ComputeAnalogInputModifier() const override;
	using Super::OnClientCorrectionReceived;
	UE_API virtual void OnClientCorrectionReceived(
		FNetworkPredictionData_Client_Character& ClientData,
		float TimeStamp,
		FVector NewLocation,
		FVector NewVelocity,
		FMovementBaseInterfaceData* NewMovementBaseInterfaceData,
		FName NewBaseBoneName,
		bool bHasBase,
		bool bBaseRelativePosition,
		uint8 ServerMovementMode,
		FVector ServerGravityDirection) override;

	/** Extend only with movement modes implemented by this CMC; action admission belongs to the caller. */
	UE_API virtual bool SupportsRagdollTransitionMode(EMovementMode TargetMode, uint8 TargetCustomMode) const;

	//~ Stance internals
	FIMGStanceStateMachine StanceMachine;
	bool CanEnterStance(EIMGStance NewStance) const;
	bool ApplyStanceTransition(EIMGStance PreviousStance, EIMGStance NewStance, bool bClientSimulation);
	bool ResizeForCrawlTransition(EIMGStance PreviousStance, EIMGStance NewStance, bool bClientSimulation);
	float GetStanceHalfHeight(EIMGStance Stance) const;
	void SetDesiredStanceFromMove(EIMGStance NewStance);

	//~ Movement Request internals
	bool ApplyMovementRequest(const FVector& Input);
	bool CaptureMovementRequestContext(bool bCaptureRequest = true);
	void ResolveMovementContext(FMovementSimulationContext& Context) const;
	void ResetMovementRequestHistory();
	FIMGMovementRequestRuntime MovementRequestRuntime;

	//~ Ragdoll internals
	bool BuildRagdollPlacement(
		const FTransform& Transform,
		bool bRequireFloor,
		const FVector& BodyLocalOffset,
		FTransform& OutTransform,
		EIMGRagdollIssue& OutIssue) const;
	bool ValidateRagdollTransitionRequest(
		int32 ExpectedSession,
		const FIMGRagdollTransitionRequest& Request,
		FTransform& OutPlacement,
		EIMGRagdollIssue& OutIssue) const;

	UFUNCTION()
	void OnRep_RagdollMovementState();
	UFUNCTION()
	void OnRep_RagdollBodyReference();

	void ReceiveRagdollBodyReference(const FIMGRagdollBodyReference& Reference);
	void PublishRagdollBodyReference(const FVector& PelvisLocation, EIMGRagdollIssue Issue);
	void PublishRagdollMovementState();
	void FinishRagdollMovement(EIMGRagdollEndReason Reason, bool bLeaveMode, bool bKeepPlacement = false);
	void SetRagdollMovementIssue(EIMGRagdollIssue Issue);
	void ClearRagdollMovementResources();
	FTransform GetRagdollPlacementTransform() const;
	void ApplyRagdollRecoveryTransform(const FTransform& Transform);
	void UpdateRagdollMovementState();
	void TickRagdollRemoteInputTimeout();

	FIMGCustomMovementMode_Ragdoll RagdollMode;
	FIMGCharacterNetworkMoveDataContainer IMGNetworkMoveData;
	FIMGCharacterMoveResponseDataContainer IMGMoveResponseData;

	UPROPERTY(ReplicatedUsing = OnRep_RagdollMovementState)
	FIMGRagdollRepState ReplicatedRagdollState;

	UPROPERTY(Transient)
	FIMGRagdollRepState ActiveRagdollState;

	UPROPERTY(ReplicatedUsing = OnRep_RagdollBodyReference)
	FIMGRagdollBodyReference ReplicatedRagdollBodyReference;

	FIMGRagdollMovementRuntime RagdollRuntime;

	// Cached ground info for the character. Do not access this directly! It's only updated when accessed via GetGroundInfo().
	FIMGCharacterGroundInfo CachedGroundInfo;

	UPROPERTY(Transient)
	bool bHasReplicatedAcceleration = false;

	friend class FSavedMove_IMG;
	friend struct FIMGCharacterMoveResponseDataContainer;
	friend struct FIMGStanceStateMachine;
	friend class FIMGCustomMovementMode_Ragdoll;

private:
	static int32 GetMovementNetDiagnosticsLevel();
	void TeardownMovementSubsystems(bool bEndingPlay, bool bBroadcastRagdollUpdate);
	void HandleMoveResponse_MovementContext(
		const FIMGCharacterMoveResponseDataContainer& Response,
		const FSavedMove_Character& AckedMove);
	void HandleMoveResponse_Ragdoll(
		const FIMGCharacterMoveResponseDataContainer& Response,
		bool bAcceptedAckAdvance,
		const FNetworkPredictionData_Client_Character*& ClientData);

	USkeletalMeshComponent* GetAutonomousSmoothingMesh() const;
	void FinishAutonomousMeshCorrection();
	void ApplyAutonomousMeshOffset();
	void RemoveAutonomousMeshOffset();
	void ResetAutonomousMeshSmoothing();
	void DecayAutonomousMeshOffset(float DeltaTime);

	FIMGAutonomousMeshSmoothingState AutonomousMeshSmoothing;
};

#undef UE_API
