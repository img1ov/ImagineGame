#pragma once

#include "Components/PawnComponent.h"
#include "Templates/Function.h"
#include "Character/Traversal/IMGTraversalTypes.h"
#include "Character/Traversal/Execution/IMGTraversalPlaybackPath.h"

#include "IMGTraversalComponent.generated.h"

class ACharacter;
class UMotionWarpingComponent;
class UAnimMontage;
class UIMGAbilitySystemComponent;
class UIMGPawnExtensionComponent;
class UIMGCharacterMovementComponent;
struct FIMGReplicatedMontagePrerequisiteResult;
class FSavedMove_Character;
struct FCharacterMoveResponseDataContainer;

DECLARE_MULTICAST_DELEGATE(FIMGTraversalUnavailable);
DECLARE_MULTICAST_DELEGATE(FIMGTraversalGroundHandoffEvent);

/** Configuration, network snapshot and warp-target bridge for a traversal ability. */
UCLASS(BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class IMAGINEGAME_API UIMGTraversalComponent : public UPawnComponent
{
	GENERATED_BODY()

public:
	UIMGTraversalComponent(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintPure, Category = "IMG|Traversal", meta = (DefaultToSelf = "Actor"))
	static UIMGTraversalComponent* Get(const AActor* Actor);

	/** Zero direction uses actor facing for Manual/Automatic, or input/acceleration/active path intent for AI. */
	UFUNCTION(BlueprintCallable, Category = "IMG|Traversal")
	bool Detect(FIMGTraversalCandidate& OutCandidate, EIMGTraversalFailure& OutFailure, FVector Direction,
	    EIMGTraversalRequestSource Source = EIMGTraversalRequestSource::Manual) const;

	UFUNCTION(BlueprintPure, Category = "IMG|Traversal")
	UMotionWarpingComponent* GetMotionWarpingComponent() const;

	const FIMGTraversalDetectorSettings& GetDetectorSettings() const
	{
		return DetectorSettings;
	}
	const FIMGTraversalExecutionSettings* FindExecutionSettings(
	    EIMGTraversalAction Action, const UAnimMontage* Montage) const;
	FVector GetMovementIntent(EIMGTraversalRequestSource Source) const;
	bool CanInitiate(const FIMGTraversalCandidate& Candidate, const FVector& MovementIntent, bool bRemoteRequest = false) const;
	float GetTargetValidationInterval() const
	{
		return TargetValidationInterval;
	}
	/** Acquires movement until the prepared Mantle handoff; GAS retains ownership of the montage tail. */
	bool BeginTraversal(const FIMGTraversalCandidate& Candidate, const FIMGTraversalAnimationSelection& Selection,
	    const TArray<FIMGTraversalWarpTarget>& WarpTargets, FGameplayAbilitySpecHandle AbilitySpec, int16 PredictionKey,
	    uint32& OutExecutionId, float GroundHandoffTime);
	bool PublishTraversalState(uint32 ExpectedExecutionId, uint8 MontagePlayInstanceId);
	void EndTraversal(uint32 ExpectedExecutionId);
	bool OwnsExecution(uint32 ExpectedExecutionId) const
	{
		return ExpectedExecutionId != 0 && ExpectedExecutionId == ExecutionId && ActiveState.bActive;
	}
	bool IsTraversalReady() const;
	/** Reports loss of infrastructure; the owning GA decides how its activation ends. */
	FIMGTraversalUnavailable OnTraversalUnavailable;
	FIMGTraversalGroundHandoffEvent OnGroundHandoffFailed;
	FIMGTraversalGroundHandoffEvent OnGroundHandoffSucceeded;
	bool DeferMontageCompletion(uint32 ExpectedExecutionId);
	bool IsMovementExecutionValid() const;
	void RetirePredictedReplay(FGameplayAbilitySpecHandle AbilitySpec, int16 PredictionKey);
	bool IsTraversalActive() const
	{
		return ActiveState.bActive;
	}
	const FIMGTraversalRepState& GetActiveTraversalState() const { return ActiveState; }
	/** Explicit names override anchor lookup; otherwise exactly one resolved anchor binding is required. */
	const FIMGTraversalWarpTarget* FindWarpTarget(EIMGTraversalAnchor Anchor, FName TargetName = NAME_None) const;
	uint32 GetTraversalTargetsRevision() const { return TraversalTargetsRevision; }
	bool IsTraversalAvailable() const
	{
		return IsRegistered() && IsActive() && !bReleasingBindings && !bInvalidatingTraversal && !IsBeingDestroyed();
	}

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(
	    float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void Activate(bool bReset = false) override;
	virtual void Deactivate() override;
	virtual void OnRep_IsActive() override;

protected:
	/** Geometry search and safety limits. The request source selects Automatic-only parameters; this component does not trigger the GA. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Traversal|Detection")
	FIMGTraversalDetectorSettings DetectorSettings;

	/** Initiation checks after detection, before animation selection and during server revalidation. Callers control triggering and retries. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Traversal|Initiation")
	FIMGTraversalInitiationSettings InitiationSettings;

	/**
	 * Common execution rules per Action, shared by most montages for that action.
	 * CHT/SelectTraversalAnimation selects one Montage per attempt; this array does not select animations.
	 * If no MontageSettings entry matches the selection, use the corresponding Action's entire Settings.
	 */
	UPROPERTY(
	    EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Traversal|Execution", meta = (TitleProperty = "Action"))
	TArray<FIMGTraversalActionSettings> ActionSettings;

	/**
	 * Optional execution overrides for exceptional montages. Usually empty; add entries only when execution rules differ.
	 * Flow: CHT selects one Montage -> find an exact Action + Montage match here -> fall back to ActionSettings if absent.
	 * This array does not select another animation or provide a fallback when SelectTraversalAnimation returns no Montage or Action=None.
	 * A matching entry replaces the entire action Settings without merging fields. New entries do not copy ActionSettings automatically.
	 * Copy the action's complete Settings first, then change specific fields while preserving other Warp, Landing, clearance and PlayRate constraints.
	 *
	 * Example 1: common Hurdle settings calculate landing from DistanceCurve + Distance_From_Ledge. An exceptional
	 * animation may lack that curve and instead warp directly to the detected BackFloor. Set Landing.Mode=DetectedFloor
	 * only for that Montage, preserving the required BackFloor binding and landing translation window. Other Hurdle
	 * montages continue to use the curve rules in ActionSettings.
	 * Example 2: a montage's front-ledge Notify uses ContactFront instead of the common FrontLedge name. For that Montage,
	 * change the front WarpBinding.TargetName and corresponding RequiredTargets entry to ContactFront; keep the geometric
	 * Anchor as FrontLedge. If Landing uses this front contact, update AnchorTargetName/FallbackAnchorTargetName as well.
	 *
	 * Each attempt handles only the selected Montage. Prediction and the server resolve settings with the same key.
	 * Do not configure duplicate Action/Montage pairs.
	 */
	UPROPERTY(
	    EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Traversal|Execution", meta = (TitleProperty = "{Action}: {Montage}"))
	TArray<FIMGTraversalMontageSettings> MontageSettings;

	/**
	 * Optional exact montage ownership list. Empty keeps selection driven by SelectTraversalAnimation and action defaults.
	 * Nonempty restricts ownership to this list plus MontageSettings; empty uses warp-window contracts for proxy startup.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Traversal|Networking")
	TArray<TObjectPtr<UAnimMontage>> ReplicatedMontages;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Traversal|Execution", meta = (ClampMin = "0.01", ForceUnits = "s"))
	float TargetValidationInterval = 0.05f;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

private:
	UFUNCTION()
	void OnRep_TraversalState();

	UFUNCTION()
	void HandleMotionWarpingPreUpdate(UMotionWarpingComponent* Warping);

	UFUNCTION()
	void HandleMovementUpdated(float DeltaSeconds, FVector OldLocation, FVector OldVelocity);
	void HandleClientCorrectionApplied(const FCharacterMoveResponseDataContainer& Response, const FSavedMove_Character& Move);
	bool BuildGroundPlayback();
	void RefreshWarpTargets();
	void ReconcilePredictedTargets();
	void CaptureMovementReplay(const FSavedMove_Character& Move, TArray<TFunction<TFunction<void()>()>>& OutSetups);
	void InitializeBindings();
	void ReleaseBindings();
	void InvalidateTraversal();
	void ReleaseExecution();
	void RemoveWarpTargets();
	void RefreshMovementIgnores();
	void RestoreMovementIgnores();
	void RestoreMovementMode();
	bool ShouldDeferSimulatedEnd() const;
	void SetTargetTickPrerequisites(bool bAdd);
	void HandleAbilitySystemInitialized();
	void HandleAbilitySystemUninitialized();
	bool HasReplicatedMontageBinding() const;
	bool IsListedReplicatedMontage(const UAnimMontage* Montage) const;
	FIMGReplicatedMontagePrerequisiteResult CheckReplicatedMontagePrerequisite(
	    const UAnimMontage* Montage, uint8 MontagePlayInstanceId) const;

	UPROPERTY(ReplicatedUsing = OnRep_TraversalState)
	FIMGTraversalRepState ReplicatedState;

	UPROPERTY(Transient)
	FIMGTraversalRepState ActiveState;

	TArray<TWeakObjectPtr<UPrimitiveComponent>> AddedMovementIgnores;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> GroundHandoffIgnores;
	TWeakObjectPtr<UMotionWarpingComponent> BoundMotionWarpingComponent;

	TSet<TWeakObjectPtr<UPrimitiveComponent>> TargetTickPrerequisites;

	TWeakObjectPtr<UIMGAbilitySystemComponent> BoundAbilitySystemComponent;
	FDelegateHandle MontagePrerequisiteHandle;
	TWeakObjectPtr<UIMGPawnExtensionComponent> BoundPawnExtension;
	TWeakObjectPtr<UIMGCharacterMovementComponent> BoundMovement;
	FDelegateHandle MovementReplayCaptureHandle;
	FDelegateHandle ClientCorrectionAppliedHandle;
	TWeakObjectPtr<ACharacter> MovementEventCharacter;
	FIMGTraversalPlaybackPath GroundPlayback;
	TSharedPtr<bool> ReplayGroundReleased;
	TMap<const FSavedMove_Character*, uint32> ReplayMoveExecutions;
	TFunction<void()> ReplayPostMovement;
	TFunction<void()> RecordWarpContext;
	TFunction<void()> RecordGroundHandoff;
	bool bMontageCompletionPending = false;
	uint32 GroundHandoffExecutionId = 0;
	TSharedPtr<bool> ReplaySessionValid;
	bool bReplayingTraversalMove = false;
	bool bCaptureTraversalExit = false;
	// Local resource ownership is independent of replicated sequence and target revisions.
	uint32 ExecutionId = 0;
	uint32 TraversalTargetsRevision = 0;
	uint16 AppliedAuthoritySequence = 0;
	bool bLocallyPredictedTraversal = false;
	bool bAuthorityTargetsApplied = false;
	bool bReleasingBindings = false;
	bool bInvalidatingTraversal = false;
};
