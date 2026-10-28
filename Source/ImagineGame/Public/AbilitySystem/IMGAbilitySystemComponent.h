
#pragma once

#include "Abilities/IMGGameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "Engine/TimerHandle.h"
#include "NativeGameplayTags.h"

#include "IMGAbilitySystemComponent.generated.h"

#define UE_API IMAGINEGAME_API

class AActor;
class UAnimMontage;
class UGameplayAbility;
class UIMGAbilityTagRelationshipMapping;
class UObject;
struct FFrame;
struct FGameplayAbilityTargetDataHandle;

UE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Gameplay_AbilityInputBlocked);

enum class EIMGReplicatedMontageReadiness : uint8
{
	NotApplicable,
	Waiting,
	Ready,
	Discard
};

/** A read-only local decision; all applicable checks must be Ready. Only the presentation owner supplies a logical start time. */
struct FIMGReplicatedMontagePrerequisiteResult
{
	EIMGReplicatedMontageReadiness Readiness = EIMGReplicatedMontageReadiness::NotApplicable;
	float LogicalStartTime = -1.f;
	FName Reason;
};

DECLARE_DELEGATE_RetVal_TwoParams(FIMGReplicatedMontagePrerequisiteResult, FIMGReplicatedMontagePrerequisite,
	const UAnimMontage* /* Montage */, uint8 /* PlayInstanceId */);

/**
 * UIMGAbilitySystemComponent
 *
 *	Base ability system component class used by this project.
 */
UCLASS(MinimalAPI)
class UIMGAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	
	UE_API UIMGAbilitySystemComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	UE_API virtual ~UIMGAbilitySystemComponent() override;
	UE_API virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UE_API virtual void InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor) override;
	UE_API virtual void ClearActorInfo() override;
	UE_API virtual float PlayMontageSimulated(
		UAnimMontage* Montage, float InPlayRate, FName StartSectionName = NAME_None) override;

	/** Bind to a UObject for the current avatar. Registration never executes the callback synchronously. */
	UE_API FDelegateHandle RegisterReplicatedMontagePrerequisite(
		AActor* Avatar, FName Name, FIMGReplicatedMontagePrerequisite Prerequisite);
	/** Teardown may query once to classify a pending start, then abandons it. Pass false only for a deliberate handoff. */
	UE_API void UnregisterReplicatedMontagePrerequisite(FDelegateHandle Handle, bool bDiscardPendingInstance = true);
	/** Coalesces requests until the next tick; callers notify after changing prerequisite data. */
	UE_API void RetryReplicatedMontage();
	UE_API bool GetActiveReplicatedMontagePlayInstanceId(const UAnimMontage* Montage, uint8& OutPlayInstanceId) const;
	/** Matches the replicated instance and the instance already started locally. */
	UE_API bool MatchesReplicatedMontageInstance(const UAnimMontage* Montage, uint8 PlayInstanceId) const;

	typedef TFunctionRef<bool(const UIMGGameplayAbility* IMGAbility, FGameplayAbilitySpecHandle Handle)> TShouldCancelAbilityFunc;
	UE_API void CancelAbilitiesByFunc(const TShouldCancelAbilityFunc& ShouldCancelFunc, bool bReplicateCancelAbility);

	UE_API void CancelInputActivatedAbilities(bool bReplicateCancelAbility);
	
	UE_API void AbilityInputTagPressed(const FGameplayTag InputTag);
	UE_API void AbilityInputTagTriggered(const FGameplayTag InputTag);
	UE_API void AbilityInputTagReleased(const FGameplayTag InputTag);

	/** Tries to activate one granted ability by class. Returns true only when activation succeeds. */
	UE_API bool TryActivateGrantedAbilityByClass(
		TSubclassOf<UIMGGameplayAbility> AbilityClass,
		bool bAllowRemoteActivation = true,
		bool bCancelIfAlreadyActive = false,
		TSubclassOf<UIMGGameplayAbility>* OutActivatedAbilityClass = nullptr,
		FGameplayAbilitySpecHandle* OutActivatedSpecHandle = nullptr);

	/** Tries to activate one granted ability by dynamic input tag. */
	UE_API bool TryActivateGrantedAbilityByInputTag(
		FGameplayTag InputTag,
		bool bAllowRemoteActivation = true,
		bool bCancelIfAlreadyActive = false,
		TSubclassOf<UIMGGameplayAbility>* OutActivatedAbilityClass = nullptr,
		FGameplayAbilitySpecHandle* OutActivatedSpecHandle = nullptr);

	/** True if this ASC currently has a granted ability whose class matches (or derives from) AbilityClass. */
	UE_API bool HasGrantedAbilityByClass(TSubclassOf<UIMGGameplayAbility> AbilityClass);

	/** Finds one currently active ability instance by class. */
	UE_API bool FindActiveAbilityInstanceByClass(
		TSubclassOf<UIMGGameplayAbility> AbilityClass,
		TWeakObjectPtr<UIMGGameplayAbility>& OutAbilityInstance);

	/** True if the specified ability instance is currently active in this ASC. */
	UE_API bool IsAbilityInstanceActive(const UIMGGameplayAbility* AbilityInstance);

	/** Cancel one ability by spec handle (if active). */
	UE_API void CancelAbilityByHandle(FGameplayAbilitySpecHandle AbilityHandle);

	UE_API void ProcessAbilityInput(float DeltaTime, bool bGamePaused);
	UE_API void ClearAbilityInput();
	
	UE_API bool IsActivationGroupBlocked(EIMGAbilityActivationGroup Group) const;
	UE_API void AddAbilityToActivationGroup(EIMGAbilityActivationGroup Group, UIMGGameplayAbility* IMGAbility);
	UE_API void RemoveAbilityFromActivationGroup(EIMGAbilityActivationGroup Group, UIMGGameplayAbility* IMGAbility);
	UE_API void CancelActivationGroupAbilities(EIMGAbilityActivationGroup Group, UIMGGameplayAbility* IgnoreIMGAbility, bool bReplicateCancelAbility);
	UE_API void AddDynamicTagGameplayEffect(const FGameplayTag& Tag);
	UE_API void RemoveDynamicTagGameplayEffect(const FGameplayTag& Tag);
	UE_API void GetAbilityTargetData(const FGameplayAbilitySpecHandle AbilityHandle, FGameplayAbilityActivationInfo ActivationInfo, FGameplayAbilityTargetDataHandle& OutTargetDataHandle);
	
	/** Sets the current tag relationship mapping, if null it will clear it out */
	UE_API void SetTagRelationshipMapping(UIMGAbilityTagRelationshipMapping* NewMapping);

	/** Looks at ability tags and gathers additional required and blocking tags */
	UE_API void GetAdditionalActivationTagRequirements(const FGameplayTagContainer& AbilityTags, FGameplayTagContainer& OutActivationRequired, FGameplayTagContainer& OutActivationBlocked) const;

	UE_API void ResetGameplayTagCounts(FGameplayTagContainer TagContainer, const int32 NewCount = 0);
	
	UFUNCTION(BlueprintCallable, Category="IMG|Ability", DisplayName="SetGameplayTagCount")
	UE_API void K2_SetGameplayTagCount(const FGameplayTag GameplayTag, const int32 NewCount) { SetLooseGameplayTagCount(GameplayTag, NewCount ); }

	UFUNCTION(BlueprintCallable, Category="IMG|Ability", DisplayName="ResetGameplayTagCounts")
	UE_API void K2_ResetGameplayTagCounts(FGameplayTagContainer TagContainer, const int32 NewCount = 0) {ResetGameplayTagCounts(TagContainer, NewCount ); }
	
	UE_API void TryActivateAbilitiesOnSpawn();

protected:
	UE_API virtual void OnRegister() override;
	UE_API virtual void OnUnregister() override;

	UE_API virtual void ClientTryActivateAbility_Implementation(FGameplayAbilitySpecHandle AbilityToActivate) override;
	UE_API virtual void ClientActivateAbilityFailed_Implementation(FGameplayAbilitySpecHandle AbilityToActivate, int16 PredictionKey) override;
	UE_API virtual void NotifyAbilityCommit(UGameplayAbility* Ability) override;
	UE_API virtual void NotifyAbilityActivated(const FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability) override;
	UE_API virtual void NotifyAbilityFailed(const FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability, const FGameplayTagContainer& FailureReason) override;
	UE_API virtual void NotifyAbilityEnded(FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability, bool bWasCancelled) override;
	UE_API virtual void HandleChangeAbilityCanBeCanceled(const FGameplayTagContainer& AbilityTags, UGameplayAbility* RequestingAbility, bool bCanBeCanceled) override;
	UE_API virtual bool IsReadyForReplicatedMontage() override;
	UE_API virtual void OnRep_ReplicatedAnimMontage() override;

	UFUNCTION(Client, Unreliable)
	UE_API void ClientNotifyAbilityFailed(const UGameplayAbility* Ability, const FGameplayTagContainer& FailureReason);
	UE_API void HandleAbilityFailed(const UGameplayAbility* Ability, const FGameplayTagContainer& FailureReason);
	
	UE_API virtual void AbilitySpecInputPressed(FGameplayAbilitySpec& Spec) override;
	UE_API virtual void AbilitySpecInputReleased(FGameplayAbilitySpec& Spec) override;

	UE_API virtual void ApplyAbilityBlockAndCancelTags(const FGameplayTagContainer& AbilityTags, UGameplayAbility* RequestingAbility, bool bEnableBlockTags, const FGameplayTagContainer& BlockTags, bool bExecuteCancelTags, const FGameplayTagContainer& CancelTags) override;

protected:

	// If set, this table is used to look up tag relationships for activate and cancel
	UPROPERTY()
	TObjectPtr<UIMGAbilityTagRelationshipMapping> TagRelationshipMapping;

	// Handles to abilities that had their input pressed this frame.
	TArray<FGameplayAbilitySpecHandle> InputPressedSpecHandles;

	// Handles to abilities that had their input released this frame.
	TArray<FGameplayAbilitySpecHandle> InputReleasedSpecHandles;

	// Handles to abilities that have their input held.
	TArray<FGameplayAbilitySpecHandle> InputHeldSpecHandles;

	// Number of abilities running in each activation group.
	int32 ActivationGroupCounts[(uint8)EIMGAbilityActivationGroup::MAX];

private:
	struct FReplicatedMontageInstance
	{
		TWeakObjectPtr<AActor> Avatar;
		TWeakObjectPtr<const UAnimMontage> Montage;
		uint8 PlayInstanceId = 0;
		bool Matches(const AActor* InAvatar, const UAnimMontage* InMontage, uint8 InPlayInstanceId) const;
	};

	struct FReplicatedMontagePrerequisiteEntry
	{
		FDelegateHandle Handle;
		TWeakObjectPtr<AActor> Avatar;
		FName Name;
		FIMGReplicatedMontagePrerequisite Callback;
		FReplicatedMontageInstance LastApplicableInstance;
	};

	void ResetReplicatedMontagePrerequisites();
	void ClearReplicatedMontageWait();
	void WaitForReplicatedMontage(const FReplicatedMontageInstance& Instance, FName Reason);
	void ReportReplicatedMontageWait();
	bool CanRetryReplicatedMontage() const;

	TArray<FReplicatedMontagePrerequisiteEntry> ReplicatedMontagePrerequisites;
	FReplicatedMontageInstance ReadyReplicatedMontage;
	FReplicatedMontageInstance DiscardedReplicatedMontage;
	FReplicatedMontageInstance WaitingReplicatedMontage;
	FName ReplicatedMontageWaitReason;
	FTimerHandle ReplicatedMontageRetryTimer;
	FTimerHandle ReplicatedMontageWaitTimer;
	float ReadyReplicatedMontageLogicalStartTime = -1.f;
	uint32 ReplicatedMontagePrerequisiteRevision = 0;
	uint32 ReplicatedMontageAvatarGeneration = 0;
	bool bProcessingReplicatedMontage = false;
	bool bEndingReplicatedMontage = false;
	bool bSuspendingReplicatedMontage = false;
	bool bDeferringReplicatedMontageForActorInfoInitialization = false;
};

#undef UE_API
