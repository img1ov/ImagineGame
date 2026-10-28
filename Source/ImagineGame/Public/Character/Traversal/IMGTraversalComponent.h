#pragma once

#include "Components/PawnComponent.h"
#include "RootMotionModifier.h"
#include "Character/Traversal/IMGTraversalTypes.h"

#include "IMGTraversalComponent.generated.h"

class UMotionWarpingComponent;
class UAnimMontage;
class UIMGAbilitySystemComponent;
class UIMGPawnExtensionComponent;
class UIMGCharacterMovementComponent;

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
	bool CanInitiate(const FIMGTraversalCandidate& Candidate, bool bRemoteRequest = false) const;
	void GetWarpWindows(const UAnimMontage* Montage, TArray<FIMGTraversalWarpWindow>& OutWindows) const;
	static const FIMGTraversalWarpWindow* FindTranslationWindow(TConstArrayView<FIMGTraversalWarpWindow> Windows,
	    FName TargetName, int32 WindowIndex, float MinEndTime, float MaxEndTime);
	float GetTargetValidationInterval() const
	{
		return TargetValidationInterval;
	}
	void BeginTraversal(const FIMGTraversalCandidate& Candidate, const FIMGTraversalAnimationSelection& Selection,
	    const TArray<FIMGTraversalWarpTarget>& WarpTargets);
	bool PublishTraversalState(uint8 MontagePlayInstanceId);
	void EndTraversal();
	bool IsTraversalActive() const
	{
		return ActiveState.bActive;
	}
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
	/** 几何搜索与安全限制；Automatic 专属参数由本次请求来源选择，组件不自行触发 GA。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Traversal|Detection")
	FIMGTraversalDetectorSettings DetectorSettings;

	/** 检测成功后的发起条件，在选动画前及服务端重检时消费；触发时机和重试由外部处理。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Traversal|Initiation")
	FIMGTraversalInitiationSettings InitiationSettings;

	/**
	 * 按 Action 配置通用执行规则；同类动作的大多数蒙太奇共用此处设置。
	 * CHT/SelectTraversalAnimation 仍负责每次选出一个 Montage，本数组不参与动画选择。
	 * 选中结果没有匹配的 MontageSettings 时，使用对应 Action 的这一整份 Settings。
	 */
	UPROPERTY(
	    EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Traversal|Animation", meta = (TitleProperty = "Action"))
	TArray<FIMGTraversalActionSettings> ActionSettings;

	/**
	 * 少数特殊蒙太奇的可选执行配置；通常保持空数组，只在执行规则确有差异时增加条目。
	 * 流程：CHT 选出一个 Montage -> 按选中的 Action + Montage 精确查找本数组 -> 未匹配则使用 ActionSettings。
	 * 这里不会再次选动画；SelectTraversalAnimation 返回空 Montage 或 Action=None 时，本数组不提供兜底动画。
	 * 匹配条目的 Settings 会整份替换动作配置，不逐字段合并；新增条目也不会自动复制对应 ActionSettings。
	 * 配置时应先复制该动作的完整 Settings，再修改特殊项，保留其余 Warp、Landing、净空和 PlayRate 约束。
	 *
	 * 案例一：通用 Hurdle 配置使用 DistanceCurve + Distance_From_Ledge 推算落点；假设某个特殊动画没有这条曲线，
	 * 而是直接 Warp 到检测出的 BackFloor。只为该 Montage 配置 Landing.Mode=DetectedFloor，保留必要的
	 * BackFloor 绑定和落地平移窗口；其他 Hurdle 继续使用 ActionSettings 的曲线规则。
	 * 案例二：某个动画的前沿 Notify 使用 ContactFront，而通用名字是 FrontLedge。只为该 Montage 将
	 * 前沿 WarpBinding.TargetName 及 RequiredTargets 中对应项改为 ContactFront，几何 Anchor 仍为 FrontLedge；
	 * 若 Landing 使用该前沿作为锚点，其 AnchorTargetName/FallbackAnchorTargetName 也应同步修改。
	 *
	 * 每次只处理最终选中的一个 Montage；预测端与服务端用同一键解析配置。不要重复配置同一 Action/Montage。
	 */
	UPROPERTY(
	    EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Traversal|Animation", meta = (TitleProperty = "Montage"))
	TArray<FIMGTraversalMontageSettings> MontageSettings;

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
	void InitializeBindings();
	void ReleaseBindings();
	void RemoveWarpTargets();
	void RefreshMovementIgnores();
	void RestoreMovementIgnores();
	bool ShouldDeferSimulatedEnd() const;
	void SetTargetTickPrerequisites(bool bAdd);
	void HandleAbilitySystemInitialized();
	void HandleAbilitySystemUninitialized();
	bool IsReplicatedMontageReady(
	    const UAnimMontage* Montage, uint8 MontagePlayInstanceId, bool bIsStopped, float& OutLogicalStartTime) const;

	UPROPERTY(ReplicatedUsing = OnRep_TraversalState)
	FIMGTraversalRepState ReplicatedState;

	UPROPERTY(Transient)
	FIMGTraversalRepState ActiveState;

	TArray<TWeakObjectPtr<UPrimitiveComponent>> AddedMovementIgnores;
	TWeakObjectPtr<UMotionWarpingComponent> BoundMotionWarpingComponent;

	TSet<TWeakObjectPtr<UPrimitiveComponent>> TargetTickPrerequisites;

	TWeakObjectPtr<UIMGAbilitySystemComponent> BoundAbilitySystemComponent;
	TWeakObjectPtr<UIMGPawnExtensionComponent> BoundPawnExtension;
	TWeakObjectPtr<UIMGCharacterMovementComponent> BoundMovement;
	bool bReleasingBindings = false;
};
