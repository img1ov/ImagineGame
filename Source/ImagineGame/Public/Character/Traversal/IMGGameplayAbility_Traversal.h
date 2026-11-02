#pragma once

#include "AbilitySystem/Abilities/IMGGameplayAbility.h"
#include "Character/Traversal/IMGTraversalTypes.h"
#include "TimerManager.h"

#include "IMGGameplayAbility_Traversal.generated.h"

class UIMGTraversalComponent;
class UIMGCharacterMovementComponent;
class UIMGAbilitySystemComponent;
class UPrimitiveComponent;
struct FCharacterMoveResponseDataContainer;

/** Single-montage traversal. Blueprint selects the action/montage; native code owns geometry and lifetime. */
UCLASS(Abstract, Blueprintable)
class IMAGINEGAME_API UIMGGameplayAbility_Traversal : public UIMGGameplayAbility
{
	GENERATED_BODY()

public:
	UIMGGameplayAbility_Traversal(const FObjectInitializer& ObjectInitializer);

protected:
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	    const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
	    FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	    const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	    const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	virtual void OnRemoveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override;
	virtual void OnPawnAvatarSet() override;

	/** Use CHT and PoseMatch here. None/empty montage rejects this attempt. */
	UFUNCTION(BlueprintImplementableEvent, Category = "IMG|Traversal")
	FIMGTraversalAnimationSelection SelectTraversalAnimation(const FIMGTraversalCandidate& Candidate) const;

	/** Local geometry is ready; use this hook to forward notifications before CHT/PoseMatch selection. */
	UFUNCTION(BlueprintImplementableEvent, Category = "IMG|Traversal")
	void PreSelectTraversalAnimation(const FIMGTraversalCandidate& Candidate);

	/**
	 * Optional remote-client security whitelist. An empty array accepts any montage that passes the native
	 * geometry, action, timing and Motion Warping contract checks; a non-empty array enforces exact pairs.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "IMG|Traversal|Network")
	TArray<FIMGTraversalAllowedMontage> AllowedMontages;

	/** Minimum local delay after an attempt ends or authority rejects it; held input remains eligible afterwards. */
	UPROPERTY(EditDefaultsOnly, Category = "IMG|Traversal|Network", meta = (ClampMin = "0.01"))
	float RetryInterval = 0.25f;

	/** Releases a remote authority instance whose target data has not arrived. */
	UPROPERTY(EditDefaultsOnly, Category = "IMG|Traversal|Network", meta = (ClampMin = "0.1"))
	float TargetDataTimeout = 2.f;

	/**
	 * Selects player detection/initiation rules, independently of activation timing and retries.
	 * Manual allows standing by default; Automatic requires movement intent and a fixed distance limit,
	 * with falling allowed by default. Character speed scales search length, not initiation acceptance.
	 * Server-controlled AI uses AI rules; remote player requests must match this source on the authority GA.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "IMG|Traversal|Detection")
	EIMGTraversalRequestSource RequestSource = EIMGTraversalRequestSource::Manual;

	/** Optional graph hook for extra transition effects; core cleanup must remain native. */
	UFUNCTION(BlueprintImplementableEvent, Category = "IMG|Traversal")
	void OnTraversalStarted(const FIMGTraversalCandidate& Candidate, EIMGTraversalAction Action);

	UFUNCTION(BlueprintImplementableEvent, Category = "IMG|Traversal")
	void OnTraversalEnded(bool bCompleted);

private:
	void LogTraversalEvent(const TCHAR* Event, const FString& Detail, bool bWarning = false) const;
	void OnTargetDataReady(const FGameplayAbilityTargetDataHandle& Data, FGameplayTag ApplicationTag);
	void OnTargetDataCancelled();
	void SendPendingTargetDataCancellation();
	void CancelPendingTargetData();
	bool IsSelectionValid(const FIMGTraversalAnimationSelection& Selection, const FIMGTraversalCandidate& Candidate,
	    bool bRemoteRequest) const;
	bool PrepareWarpTargets(FIMGTraversalCandidate& Candidate, const FIMGTraversalAnimationSelection& Selection,
	    TArray<FIMGTraversalWarpTarget>& OutTargets) const;
	void CheckActiveTarget();
	void OnTargetDataTimedOut();
	void FilterClientCorrection(const FCharacterMoveResponseDataContainer& Response, bool& bDiscardCorrection);
	void ClearCorrectionFilter();
	void OnNetworkTermination(FGameplayAbilitySpecHandle Handle, int16 PredictionKey, bool bRejected);
	void ClearNetworkTracking();
	void DelayLocalRetry();

	UFUNCTION()
	void OnMontageCompleted();

	UFUNCTION()
	void OnMontageInterrupted();

	FDelegateHandle TargetDataDelegateHandle;
	FDelegateHandle TargetDataCancelledDelegateHandle;
	FDelegateHandle CorrectionFilterHandle;
	FDelegateHandle NetworkTerminationHandle;
	FTimerHandle TargetCheckTimer;
	TWeakObjectPtr<UIMGCharacterMovementComponent> FilteredMovement;
	TWeakObjectPtr<UIMGAbilitySystemComponent> NetworkASC;
	TWeakObjectPtr<AActor> NetworkRequestAvatar;
	// Retained through local EndAbility until the matching authority termination arrives.
	FGameplayAbilitySpecHandle PendingAuthoritySpec;
	int16 PendingAuthorityPredictionKey = 0;
	double NextLocalRetryTime = 0.0;
	double CorrectionFilterStartTime = 0.0;
	TArray<float> PreTraversalMoveTimeStamps;
	FIMGTraversalCandidate LocalCandidate;
	TWeakObjectPtr<UIMGTraversalComponent> ActiveComponent;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> ActiveObstacleComponents;
	TArray<FTransform> PreviousObstacleTransforms;
	float PreviousTargetCheckTime = 0.f;
	bool bLocalRequest = false;
	bool bTraversalStarted = false;
	bool bMontageCompleted = false;
	// These persist through EndAbility until the next activation because montage startup can end synchronously.
	bool bTargetDataSent = false;
	bool bTargetDataCancellationSent = false;
};
