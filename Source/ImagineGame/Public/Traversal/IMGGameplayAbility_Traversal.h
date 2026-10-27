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
	 * 选择本次玩家请求使用的检测与发起规则；GA 的激活时机、尝试频率和重试由外部逻辑负责。
	 * 每次激活只处理一次翻越尝试，失败即结束；本配置与 ActivationPolicy 独立，不开启持续检测。
	 * Manual：使用通用几何检测和手动发起条件，默认允许站立发起。
	 * Automatic：增加自动专属几何限制；InitiationSettings 启用时还要求移动意图、接近速度、方向和距离等条件。
	 * 两者均先检测并检查发起条件，再交给 SelectTraversalAnimation/CHT 选动画，选中后仍需通过执行校验。
	 * 案例：外部收到翻越按键后激活 Manual GA，可尝试站立或低速翻越。
	 * 案例：外部移动逻辑决定尝试自动翻越时激活 Automatic GA；默认站立或接近速度不足 190 cm/s 会被拒绝。
	 * 服务端控制的 AI 会强制使用 AI 来源；远端玩家请求的来源必须与服务端 GA 的此配置一致。
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
