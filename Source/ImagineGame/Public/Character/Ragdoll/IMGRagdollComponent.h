#pragma once

#include "Components/ActorComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Character/Ragdoll/IMGRagdollTypes.h"
#include "IMGRagdollComponent.generated.h"

class ACharacter;
class UIMGCharacterMovementComponent;
class USkeletalMeshComponent;
class USceneComponent;
class USkeletalMesh;
class UPhysicsAsset;
class UIMGPawnComponent_CharacterParts;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FIMGRagdollEvent, int32, Session);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FIMGRagdollEndedEvent, int32, Session, EIMGRagdollEndReason, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FIMGRagdollTransitionEvent, int32, Session, int32, TransitionRevision);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FIMGRagdollBodyUnavailableEvent, int32, Session, EIMGRagdollBodyIssue, Issue);

/** Local body execution and movement adapter. Abilities own the action sequence and animation selection. */
UCLASS(BlueprintType, Blueprintable, ClassGroup = (Movement), meta = (BlueprintSpawnableComponent))
class IMAGINEGAME_API UIMGRagdollComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UIMGRagdollComponent(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintPure, Category = "IMG|Ragdoll", meta = (DefaultToSelf = "Actor"))
	static UIMGRagdollComponent* FindRagdollComponent(const AActor* Actor);

	//~ Authority Lifecycle Actions
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "IMG|Ragdoll")
	bool StartRagdoll(FVector AdditionalVelocity = FVector::ZeroVector);

	/** Approve capsule placement and release the custom mode; does not play or select animation. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "IMG|Ragdoll")
	bool BeginRecovery(int32 ExpectedSession);

	/** BodyLocalOffset locates the matched follow point in the proposed capsule's unit-scale space. */
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "IMG|Ragdoll")
	bool QueryRecovery(int32 ExpectedSession, FTransform& OutTransform, EIMGRagdollIssue& OutIssue, FVector BodyLocalOffset = FVector::ZeroVector) const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "IMG|Ragdoll")
	bool BeginRecoveryAt(int32 ExpectedSession, const FTransform& Transform, FVector BodyLocalOffset = FVector::ZeroVector);

	/** The receiving GA prepares/admits its action first; this commits authority movement without sending a client RPC. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "IMG|Ragdoll")
	bool BeginTransitionAt(int32 ExpectedSession, const FIMGRagdollTransitionRequest& Request, int32& OutTransitionRevision);

	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "IMG|Ragdoll")
	bool QueryTransition(int32 ExpectedSession, const FIMGRagdollTransitionRequest& Request, EIMGRagdollIssue& OutIssue) const;

	/** The ability must complete or abort its session; there is no automatic recovery timer. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "IMG|Ragdoll")
	void CompleteRecovery(int32 ExpectedSession, bool bInterrupted = false);

	/** Forced cleanup ends the session even if safe placement fails, leaving movement disabled. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "IMG|Ragdoll")
	bool AbortRagdoll(int32 ExpectedSession, bool bForce = false);

	//~ Transition & Recovery Pose Handoff
	UFUNCTION(BlueprintPure, Category = "IMG|Ragdoll")
	int32 GetTransitionRevision() const { return ActiveState.bExternalTransition ? ActiveState.Revision : 0; }

	UFUNCTION(BlueprintPure, Category = "IMG|Ragdoll")
	bool IsTransitionReady(int32 ExpectedSession, int32 ExpectedRevision) const;

	/** Approved facts may arrive before native movement. IsTransitionReady still gates execution. */
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "IMG|Ragdoll")
	bool GetTransitionMovement(
		int32 ExpectedSession,
		int32 ExpectedRevision,
		TEnumAsByte<EMovementMode>& OutMode,
		uint8& OutCustomMode,
		FVector& OutVelocity) const;

	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "IMG|Ragdoll")
	bool GetTransitionPose(int32 ExpectedSession, int32 ExpectedRevision, FIMGRagdollPoseSnapshot& OutSnapshot) const;

	/** Release after installing the pose; discard also works when the receiving action was cancelled. */
	UFUNCTION(BlueprintCallable, Category = "IMG|Ragdoll")
	bool ReleaseTransitionPose(int32 ExpectedSession, int32 ExpectedRevision, bool bDiscard = false);

	/** Local pose; animation must install it before releasing the recovery hold. */
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "IMG|Ragdoll")
	bool GetRecoveryPose(int32 ExpectedSession, FIMGRagdollPoseSnapshot& OutSnapshot) const;

	UFUNCTION(BlueprintCallable, Category = "IMG|Ragdoll")
	bool ReleaseRecoveryHold(int32 ExpectedSession);

	/** Transfers the current local body to another owner without restoring its simulation/attachment. */
	UFUNCTION(BlueprintCallable, Category = "IMG|Ragdoll")
	bool HandOffRagdollBody(int32 ExpectedSession);

	/** Explicit local retry after fixing a setup failure; no automatic business retry timer. */
	UFUNCTION(BlueprintCallable, Category = "IMG|Ragdoll")
	void RetryBodyExecution(int32 ExpectedSession);

	//~ Queries & Runtime Facts
	UFUNCTION(BlueprintPure, Category = "IMG|Ragdoll")
	bool IsRagdollAvailable() const;

	/** Checks execution facilities without starting a movement session. */
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "IMG|Ragdoll")
	bool IsRagdollReady() const;

	UFUNCTION(BlueprintPure, Category = "IMG|Ragdoll")
	bool IsRagdollBodyActive(int32 ExpectedSession) const;

	UFUNCTION(BlueprintPure, Category = "IMG|Ragdoll")
	EIMGRagdollBodyIssue GetRagdollBodyIssue() const { return BodyIssue; }

	/** Add this local offset when matching body facts to the accepted movement frame. */
	UFUNCTION(BlueprintPure, Category = "IMG|Ragdoll")
	FVector GetBodyMovementOffset() const
	{
		return BodySessionState.OwnerCorrectionRemaining + BodySessionState.PendingPrePhysicsTranslation;
	}

	UFUNCTION(BlueprintPure, Category = "IMG|Ragdoll")
	EIMGRagdollPhase GetRagdollPhase() const { return ActiveState.Phase; }

	UFUNCTION(BlueprintPure, Category = "IMG|Ragdoll")
	bool IsFaceUp() const;

	UFUNCTION(BlueprintPure, Category = "IMG|Ragdoll")
	int32 GetRagdollSession() const { return ActiveState.Session; }

	UFUNCTION(BlueprintPure, Category = "IMG|Ragdoll")
	FVector GetRagdollInitialVelocity() const { return ActiveState.InitialVelocity; }

	UFUNCTION(BlueprintPure, Category = "IMG|Ragdoll")
	EIMGRagdollIssue GetRagdollMovementIssue() const;

	/** Read-only sampling seam for a custom target. The native implementation reads the simulated pelvis. */
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "IMG|Ragdoll")
	bool GetRagdollFollowTarget(FTransform& OutTarget, bool& bFaceUp) const;
	virtual bool GetRagdollFollowTarget_Implementation(FTransform& OutTarget, bool& bFaceUp) const;

	/** Optional body facts only. GetUp thresholds and settled time belong to the caller. */
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "IMG|Ragdoll")
	bool GetRagdollBodyMotion(FVector& OutLinearVelocity, FVector& OutAngularVelocityRadians) const;
	virtual bool GetRagdollBodyMotion_Implementation(FVector& OutLinearVelocity, FVector& OutAngularVelocityRadians) const;

	//~ Events
	UPROPERTY(BlueprintAssignable, Category = "IMG|Ragdoll")
	FIMGRagdollEvent OnRagdollStarted;

	UPROPERTY(BlueprintAssignable, Category = "IMG|Ragdoll")
	FIMGRagdollEvent OnRecoveryStarted;

	UPROPERTY(BlueprintAssignable, Category = "IMG|Ragdoll")
	FIMGRagdollEndedEvent OnRagdollEnded;

	/** Emitted after old-session end notification. Listeners match both identifiers to their receiving execution. */
	UPROPERTY(BlueprintAssignable, Category = "IMG|Ragdoll")
	FIMGRagdollTransitionEvent OnTransitionReady;

	/** A local execution resource was lost. The ability decides whether to cancel or wait. */
	UPROPERTY(BlueprintAssignable, Category = "IMG|Ragdoll")
	FIMGRagdollBodyUnavailableEvent OnRagdollBodyUnavailable;

	//~ UActorComponent Interface
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void Activate(bool bReset = false) override;
	virtual void Deactivate() override;
	virtual void OnRep_IsActive() override;

protected:
	//~ Configuration
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Ragdoll|Movement")
	FIMGRagdollSettings Settings;

	UPROPERTY(EditDefaultsOnly, Replicated, BlueprintReadOnly, Category = "IMG|Ragdoll|Target")
	FName PelvisBone = TEXT("pelvis");

	UPROPERTY(EditDefaultsOnly, Replicated, BlueprintReadOnly, Category = "IMG|Ragdoll|Target")
	FName OrientationBone = TEXT("spine_05");

	/** Local axis on OrientationBone pointing out of the character's chest. */
	UPROPERTY(EditDefaultsOnly, Replicated, BlueprintReadOnly, Category = "IMG|Ragdoll|Target")
	FVector FaceUpAxis = FVector::YAxisVector;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Ragdoll|Body")
	FName BodyCollisionProfile = TEXT("Ragdoll");

	/** Remote player bodies on dedicated servers are optional consumers, never input producers. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Ragdoll|Body")
	bool bSimulateRemoteBodiesOnDedicatedServer = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Ragdoll|Body")
	bool bEnableDedicatedServerPhysics = true;

	/** MOVE_None can be a death/external takeover; retain a free body rather than restore locomotion. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Ragdoll|Body")
	bool bKeepBodyWhenMovementDisabled = true;

	/** Older/late-bound sessions use the current approved velocity instead of repeating the launch. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Ragdoll|Body", meta = (ClampMin = "0", ForceUnits = "s"))
	float MaxInitialVelocityAge = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Ragdoll|Correction", meta = (ClampMin = "0.01", ForceUnits = "s"))
	float OwnerCorrectionTime = 0.12f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Ragdoll|Correction", meta = (ClampMin = "1", ForceUnits = "cm/s"))
	float OwnerMaxCorrectionSpeed = 600.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Ragdoll|Correction", meta = (ClampMin = "1", ForceUnits = "cm"))
	float HardCorrectionDistance = 300.f;

	/** Outer-loop position error time constant for simulated proxies. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Ragdoll|Correction", meta = (ClampMin = "0.01", ForceUnits = "s"))
	float RemoteCorrectionTime = 0.2f;

	/** Inner-loop velocity matching time constant (keep <= RemoteCorrectionTime / 4 for critical damping). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Ragdoll|Correction", meta = (ClampMin = "0.01", ForceUnits = "s"))
	float RemoteVelocityCorrectionTime = 0.05f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Ragdoll|Correction", meta = (ClampMin = "0", ForceUnits = "cm/s"))
	float RemoteMaxVelocityCorrection = 500.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Ragdoll|Correction", meta = (ClampMin = "0", ForceUnits = "cm/s"))
	float RemoteMaxVerticalVelocityCorrection = 400.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Ragdoll|Correction", meta = (ClampMin = "0", ForceUnits = "cm/s"))
	float RemoteMaxBackwardCorrection = 200.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Ragdoll|Correction", meta = (ClampMin = "0", ForceUnits = "cm/s^2"))
	float RemoteMaxAcceleration = 6000.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Ragdoll|Correction", meta = (ClampMin = "0", ForceUnits = "cm/s^2"))
	float RemoteMaxVerticalAcceleration = 4000.f;

	//~ Backend Physical Execution Seams
	virtual bool CanExecuteBody(USkeletalMeshComponent& Mesh) const;
	virtual bool StartBodySimulation(USkeletalMeshComponent& Mesh);
	virtual bool IsBodySimulationValid() const;
	virtual void UpdateBodySimulation(float DeltaTime);
	virtual void StopBodySimulation(USkeletalMeshComponent& Mesh);
	virtual void ReleaseBodyResources();
	virtual void TranslateBody(USkeletalMeshComponent& Mesh, const FVector& Delta);
	virtual void AccelerateBody(USkeletalMeshComponent& Mesh, const FVector& Acceleration);

	USkeletalMeshComponent* GetExecutionMesh() const;
	bool IsBodyInputProducer() const;
	void SetBodyIssue(EIMGRagdollBodyIssue Issue);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void RegisterComponentTickFunctions(bool bRegister) override;

private:
	struct FSavedBodyInstanceState
	{
		FName BoneName;
		bool bSimulatePhysics = false;
		float PhysicsBlendWeight = 0.0f;
		bool bUpdateKinematicFromSimulation = false;
		TArray<ECollisionEnabled::Type, TInlineAllocator<4>> ShapeCollisions;
	};

	struct FSavedMeshBodyState
	{
		TWeakObjectPtr<USceneComponent> AttachParent;
		FName AttachSocket;
		FTransform RelativeTransform = FTransform::Identity;
		FVector CharacterMeshOffset = FVector::ZeroVector;
		FName CollisionProfile;
		FCollisionResponseContainer CollisionResponses;
		TEnumAsByte<ECollisionEnabled::Type> CollisionEnabled = ECollisionEnabled::NoCollision;
		TEnumAsByte<ECollisionChannel> CollisionObjectType = ECC_Pawn;
		EVisibilityBasedAnimTickOption VisibilityTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
		bool bMeshTickEnabled = false;
		bool bPauseAnims = false;
		bool bBlendPhysics = false;
		bool bEnableUpdateRateOptimizations = false;
		bool bDedicatedServerPhysics = false;
		bool bNoSkeletonUpdate = false;
		TArray<FSavedBodyInstanceState> Bodies;

		void Capture(USkeletalMeshComponent& Mesh, const ACharacter& OwnerCharacter);
		void Restore(
			USkeletalMeshComponent& Mesh,
			const ACharacter* OwnerCharacter,
			FName ExpectedCollisionProfile,
			bool bRestoreBodies) const;
		void Reset();
	};

	struct FBodyExecutionSession
	{
		TWeakObjectPtr<USkeletalMeshComponent> Mesh;
		TWeakObjectPtr<USkeletalMesh> MeshAsset;
		TWeakObjectPtr<UPhysicsAsset> PhysicsAsset;
		FVector OwnerCorrectionRemaining = FVector::ZeroVector;
		FVector PendingPrePhysicsTranslation = FVector::ZeroVector;
		FVector ApprovedPelvisOffset = FVector::ZeroVector;
		uint16 Session = 0;
		uint16 LastSession = 0;
		uint16 HandedOffSession = 0;
		bool bInvalidated = false;
		bool bRecovering = false;
		bool bInputRejected = false;

		void ResetActiveBody();
	};

	struct FRecoveryHoldSnapshot
	{
		FIMGRagdollPoseSnapshot Snapshot;
		TWeakObjectPtr<USkeletalMesh> MeshAsset;

		void Reset()
		{
			Snapshot = FIMGRagdollPoseSnapshot();
			MeshAsset.Reset();
		}
	};

	struct FRestartPoseCache
	{
		TWeakObjectPtr<USkeletalMeshComponent> Mesh;
		TWeakObjectPtr<UPhysicsAsset> PhysicsAsset;
		TWeakObjectPtr<USkeletalMesh> MeshAsset;
		FVector PelvisLocation = FVector::ZeroVector;
		TMap<FName, FTransform> BodyTransforms;

		bool Matches(const USkeletalMeshComponent& InMesh) const;
		void Reset();
	};

	struct FBodyStartFailureRecord
	{
		uint16 Session = 0;
		TWeakObjectPtr<USkeletalMeshComponent> Mesh;
		TWeakObjectPtr<USkeletalMesh> MeshAsset;
		TWeakObjectPtr<UPhysicsAsset> PhysicsAsset;

		bool Matches(uint16 InSession, const USkeletalMeshComponent& InMesh) const;
		void Record(uint16 InSession, USkeletalMeshComponent& InMesh);
		void Reset();
	};

	struct FExternalTransitionTracker
	{
		uint16 ObservedSession = 0;
		uint16 ReadyRevision = 0;
		uint16 ReleasedRevision = 0;

		void ObserveSession(uint16 Session);
		void Reset();
	};

	bool Initialize();
	void TearDown();
	UFUNCTION()
	void HandleMovementUpdated();
	void ReconcileState();
	void ReconcileExternalTransition();
	void TickPrePhysics(float DeltaTime);
	void TickPostPhysics();
	void SampleTarget();
	void EndLocalSession(EIMGRagdollEndReason Reason);

	bool ShouldExecuteBody() const;
	bool EnsureBody();
	void ReleaseBody(bool bKeepPhysics = false, bool bResumeHeldBody = false);
	void PrepareBodyRecovery();
	void UpdateBodyCorrection(float DeltaTime);
	void UpdateOwnerBodyCorrection(USkeletalMeshComponent& Mesh, float DeltaTime);
	void UpdateRemoteBodyCorrection(USkeletalMeshComponent& Mesh, float DeltaTime);
	void HandleCapsuleCorrection(int32 Session, const FVector& Delta);
	void HandlePhysicsCreated();
	void RefreshMeshObservation(bool bForcePartsLookup = false);
	UFUNCTION()
	void HandleCharacterPartsChanged(UIMGPawnComponent_CharacterParts* Parts);
	void SetExecutionTickEnabled(bool bEnabled);
	bool HasCurrentBody() const;
	void CaptureRestartPose();
	void ComputePoseFacing(const USkeletalMeshComponent& Mesh, float& OutFacingYaw, bool& bOutFaceUp) const;

	//~ Observed Movement State (CMC owns the single replicated contract)
	UPROPERTY(Transient)
	FIMGRagdollRepState ActiveState;

	//~ Infrastructure Bindings
	TWeakObjectPtr<ACharacter> Character;
	TWeakObjectPtr<UIMGCharacterMovementComponent> Movement;
	TWeakObjectPtr<USkeletalMeshComponent> SampledMesh;
	TWeakObjectPtr<UIMGPawnComponent_CharacterParts> ObservedParts;
	FDelegateHandle PhysicsCreatedHandle;
	FDelegateHandle CapsuleCorrectionHandle;

	UPROPERTY(Transient)
	FActorComponentTickFunction BodyPrePhysicsTick;

	//~ Grouped Runtime State
	FSavedMeshBodyState SavedMeshState;
	FBodyExecutionSession BodySessionState;
	FRecoveryHoldSnapshot RecoveryHold;
	FRestartPoseCache RestartPose;
	FBodyStartFailureRecord BodyStartFailure;
	FExternalTransitionTracker TransitionTracker;

	//~ Local Session & Reentrancy Guards
	EIMGRagdollBodyIssue BodyIssue = EIMGRagdollBodyIssue::None;
	uint16 BodyIssueSession = 0;
	uint16 LocalSession = 0;
	uint16 OwnedSession = 0;
	bool bStartedNotified = false;
	bool bRecoveryNotified = false;
	bool bReleasingBody = false;
	bool bStartingBody = false;
	bool bTransitioning = false;
	bool bReconciling = false;
	bool bTearingDown = false;
};
