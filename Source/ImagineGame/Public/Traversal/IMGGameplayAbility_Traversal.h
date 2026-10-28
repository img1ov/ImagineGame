#pragma once

#include "AbilitySystem/Abilities/IMGGameplayAbility.h"
#include "Traversal/IMGTraversalTypes.h"
#include "TimerManager.h"

#include "IMGGameplayAbility_Traversal.generated.h"

class UIMGTraversalComponent;
class UPrimitiveComponent;

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

	/**

	 * Selects detection and initiation rules for player requests. External logic controls activation timing, attempt

	 * frequency and retries. Each activation makes one attempt and ends on failure; this setting is independent of

	 * ActivationPolicy and does not enable continuous detection. Manual uses common geometry and manual initiation

	 * rules, allowing stationary initiation by default. Automatic adds automatic-only geometry limits and, when

	 * InitiationSettings is enabled, checks movement intent, approach speed, direction and distance. Both modes

	 * check detection and initiation before SelectTraversalAnimation/CHT selects an animation, then validate

	 * execution. For example, input logic may activate Manual for stationary or low-speed traversal; movement logic

	 * may activate Automatic, which requires movement intent but permits blocked/zero-speed and falling attempts by
	 * default. AutomaticMinSpeed optionally restores an approach-speed gate; speed also scales allowed distance.

	 * Server-controlled AI always uses the AI source; remote player requests must match this setting on the server

	 * GA.

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

	UFUNCTION()
	void OnMontageCompleted();

	UFUNCTION()
	void OnMontageInterrupted();

	FDelegateHandle TargetDataDelegateHandle;
	FDelegateHandle TargetDataCancelledDelegateHandle;
	FTimerHandle TargetCheckTimer;
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
