#pragma once

#include "AbilitySystem/Abilities/IMGGameplayAbility.h"
#include "Traversal/IMGTraversalTypes.h"
#include "TimerManager.h"

#include "IMGGameplayAbility_Traversal.generated.h"

class UIMGTraversalComponent;
class UPrimitiveComponent;

/** One-montage GASP traversal. Blueprint selects the action/montage; native code owns geometry and lifetime. */
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

	/** Action/montage pairs that a remote client's CHT may choose. Required for networked player traversal. */
	UPROPERTY(EditDefaultsOnly, Category = "IMG|Traversal|Network")
	TArray<FIMGTraversalAllowedMontage> AllowedMontages;

	UPROPERTY(EditDefaultsOnly, Category = "IMG|Traversal|Animation")
	FName DistanceFromLedgeCurveName = TEXT("Distance_From_Ledge");

	/** Optional graph hook for extra transition effects; core cleanup must remain native. */
	UFUNCTION(BlueprintImplementableEvent, Category = "IMG|Traversal")
	void OnTraversalStarted(const FIMGTraversalCandidate& Candidate, EIMGTraversalAction Action);

	UFUNCTION(BlueprintImplementableEvent, Category = "IMG|Traversal")
	void OnTraversalEnded(bool bCompleted);

private:
	void OnTargetDataReady(const FGameplayAbilityTargetDataHandle& Data, FGameplayTag ApplicationTag);
	bool IsSelectionValid(const FIMGTraversalAnimationSelection& Selection, const FIMGTraversalCandidate& Candidate,
		bool bRemoteRequest) const;
	bool PrepareWarpTargets(FIMGTraversalCandidate& Candidate, const FIMGTraversalAnimationSelection& Selection) const;
	void CheckActiveTarget();

	UFUNCTION()
	void OnMontageCompleted();

	UFUNCTION()
	void OnMontageInterrupted();

	FDelegateHandle TargetDataDelegateHandle;
	FTimerHandle TargetCheckTimer;
	FIMGTraversalCandidate LocalCandidate;
	TWeakObjectPtr<UIMGTraversalComponent> ActiveComponent;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> ActiveObstacleComponents;
	TWeakObjectPtr<UPrimitiveComponent> ActiveBackFloorComponent;
	TArray<FTransform> PreviousObstacleTransforms;
	FTransform PreviousBackFloorTransform;
	float PreviousTargetCheckTime = 0.f;
	bool bLocalRequest = false;
	bool bTraversalStarted = false;
	bool bMontageCompleted = false;
	EIMGTraversalAction ActiveAction = EIMGTraversalAction::None;
};
