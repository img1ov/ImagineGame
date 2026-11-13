#pragma once

#include "Components/PawnComponent.h"
#include "RootMotionModifier.h"
#include "Templates/Function.h"
#include "Character/Traversal/IMGTraversalTypes.h"

#include "IMGTraversalComponent.generated.h"

class UMotionWarpingComponent;
class UAnimMontage;
class UIMGAbilitySystemComponent;
class UIMGPawnExtensionComponent;
class UIMGCharacterMovementComponent;
struct FIMGReplicatedMontagePrerequisiteResult;
class FSavedMove_Character;

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
	const FIMGTraversalAnimationSettings* FindAnimationSettings(
	    EIMGTraversalAction Action, const UAnimMontage* Montage) const;
	FVector GetMovementIntent(EIMGTraversalRequestSource Source) const;
	bool CanInitiate(const FIMGTraversalCandidate& Candidate, const FVector& MovementIntent, bool bRemoteRequest = false) const;
	void GetWarpWindows(const UAnimMontage* Montage, TArray<FIMGTraversalWarpWindow>& OutWindows) const;
	static const FIMGTraversalWarpWindow* FindTranslationWindow(TConstArrayView<FIMGTraversalWarpWindow> Windows,
	    FName TargetName, int32 WindowIndex, float MinEndTime, float MaxEndTime);
	float GetTargetValidationInterval() const
	{
		return TargetValidationInterval;
	}
	/** Acquires targets/collision ignores and enters Flying; EndTraversal releases them before restoring movement. */
	bool BeginTraversal(const FIMGTraversalCandidate& Candidate, const FIMGTraversalAnimationSelection& Selection,
	    const TArray<FIMGTraversalWarpTarget>& WarpTargets, FGameplayAbilitySpecHandle AbilitySpec, int16 PredictionKey);
	bool PublishTraversalState(uint8 MontagePlayInstanceId);
	void EndTraversal();
	void RetirePredictedReplay(FGameplayAbilitySpecHandle AbilitySpec, int16 PredictionKey);
	bool IsTraversalActive() const
	{
		return ActiveState.bActive;
	}
	const FIMGTraversalRepState& GetActiveTraversalState() const { return ActiveState; }
	uint32 GetTraversalTargetsRevision() const { return TraversalTargetsRevision; }
	bool IsTraversalAvailable() const
	{
		return IsRegistered() && IsActive() && !bReleasingBindings && !IsBeingDestroyed();
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
	    EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Traversal|Animation", meta = (TitleProperty = "Action"))
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
	    EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Traversal|Animation", meta = (TitleProperty = "Montage"))
	TArray<FIMGTraversalMontageSettings> MontageSettings;

	/** Nonempty: restrict selection and replication ownership to this list plus MontageSettings, without target-name inference. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Traversal|Networking")
	TArray<TObjectPtr<UAnimMontage>> ReplicatedMontages;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Traversal|Execution", meta = (ClampMin = "0.01"))
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

	void RefreshWarpTargets();
	void ReconcilePredictedTargets();
	void CaptureMovementReplay(const FSavedMove_Character& Move, TArray<TFunction<TFunction<void()>()>>& OutSetups);
	void InitializeBindings();
	void ReleaseBindings();
	void RemoveWarpTargets();
	void RefreshMovementIgnores();
	void RestoreMovementIgnores();
	void RestoreMovementMode();
	bool ShouldDeferSimulatedEnd() const;
	void SetTargetTickPrerequisites(bool bAdd);
	void HandleAbilitySystemInitialized();
	void HandleAbilitySystemUninitialized();
	bool IsListedReplicatedMontage(const UAnimMontage* Montage) const;
	FIMGReplicatedMontagePrerequisiteResult CheckReplicatedMontagePrerequisite(
	    const UAnimMontage* Montage, uint8 MontagePlayInstanceId) const;

	UPROPERTY(ReplicatedUsing = OnRep_TraversalState)
	FIMGTraversalRepState ReplicatedState;

	UPROPERTY(Transient)
	FIMGTraversalRepState ActiveState;

	TArray<TWeakObjectPtr<UPrimitiveComponent>> AddedMovementIgnores;
	TWeakObjectPtr<UMotionWarpingComponent> BoundMotionWarpingComponent;

	TSet<TWeakObjectPtr<UPrimitiveComponent>> TargetTickPrerequisites;

	TWeakObjectPtr<UIMGAbilitySystemComponent> BoundAbilitySystemComponent;
	FDelegateHandle MontagePrerequisiteHandle;
	TWeakObjectPtr<UIMGPawnExtensionComponent> BoundPawnExtension;
	TWeakObjectPtr<UIMGCharacterMovementComponent> BoundMovement;
	FDelegateHandle MovementReplayCaptureHandle;
	TFunction<void()> RecordWarpContext;
	TSharedPtr<bool> ReplaySessionValid;
	bool bReplayingTraversalMove = false;
	bool bCaptureTraversalExit = false;
	uint32 TraversalTargetsRevision = 0;
	uint16 AppliedAuthoritySequence = 0;
	bool bLocallyPredictedTraversal = false;
	bool bAuthorityTargetsApplied = false;
	bool bReleasingBindings = false;
};
