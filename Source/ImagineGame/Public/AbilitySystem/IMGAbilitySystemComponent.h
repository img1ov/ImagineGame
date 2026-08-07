// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Abilities/IMGGameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "NativeGameplayTags.h"

#include "IMGAbilitySystemComponent.generated.h"

#define UE_API IMAGINEGAME_API

class AActor;
class UGameplayAbility;
class UIMGAbilityTagRelationshipMapping;
class UObject;
struct FFrame;
struct FGameplayAbilityTargetDataHandle;

UE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Gameplay_AbilityInputBlocked);

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

	UE_API virtual void InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor) override;

	typedef TFunctionRef<bool(const UIMGGameplayAbility* ActAbility, FGameplayAbilitySpecHandle Handle)> TShouldCancelAbilityFunc;
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

	/** Rebuilds the AbilityId -> SpecHandle cache from currently granted abilities. */
	UE_API void RebuildAbilityIdCache();

	/** Returns the cached spec handle for an AbilityId, or invalid if missing. */
	UE_API FGameplayAbilitySpecHandle GetAbilitySpecHandleById(FName AbilityId) const;

	/** Tries to activate one granted ability by AbilityId. */
	UE_API bool TryActivateAbilityById(
		FName AbilityId,
		bool bAllowRemoteActivation = true,
		bool bCancelIfAlreadyActive = false,
		FGameplayAbilitySpecHandle* OutActivatedSpecHandle = nullptr);
	
	UE_API void ProcessAbilityInput(float DeltaTime, bool bGamePaused);
	UE_API void ClearAbilityInput();
	
	UE_API bool IsActivationGroupBlocked(EIMGAbilityActivationGroup Group) const;
	
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

	UE_API virtual void ClientTryActivateAbility_Implementation(FGameplayAbilitySpecHandle AbilityToActivate) override;
	UE_API virtual void ClientActivateAbilityFailed_Implementation(FGameplayAbilitySpecHandle AbilityToActivate, int16 PredictionKey) override;
	UE_API virtual void OnGiveAbility(FGameplayAbilitySpec& AbilitySpec) override;
	UE_API virtual void OnRemoveAbility(FGameplayAbilitySpec& AbilitySpec) override;
	UE_API virtual void OnRep_ActivateAbilities() override;
	UE_API virtual void NotifyAbilityCommit(UGameplayAbility* Ability) override;
	UE_API virtual void NotifyAbilityActivated(const FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability) override;
	UE_API virtual void NotifyAbilityFailed(const FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability, const FGameplayTagContainer& FailureReason) override;
	
	UE_API virtual void AbilitySpecInputPressed(FGameplayAbilitySpec& Spec) override;
	UE_API virtual void AbilitySpecInputReleased(FGameplayAbilitySpec& Spec) override;

	UE_API virtual void ApplyAbilityBlockAndCancelTags(const FGameplayTagContainer& AbilityTags, UGameplayAbility* RequestingAbility, bool bEnableBlockTags, const FGameplayTagContainer& BlockTags, bool bExecuteCancelTags, const FGameplayTagContainer& CancelTags) override;

protected:

	// If set, this table is used to look up tag relationships for activate and cancel
	UPROPERTY()
	TObjectPtr<UIMGAbilityTagRelationshipMapping> TagRelationshipMapping;

	/** Cached AbilityId -> SpecHandle map for Id-based activation. */
	TMap<FName, FGameplayAbilitySpecHandle> AbilityIdToSpecHandle;

	// Handles to abilities that had their input pressed this frame.
	TArray<FGameplayAbilitySpecHandle> InputPressedSpecHandles;

	// Handles to abilities that had their input released this frame.
	TArray<FGameplayAbilitySpecHandle> InputReleasedSpecHandles;

	// Handles to abilities that have their input held.
	TArray<FGameplayAbilitySpecHandle> InputHeldSpecHandles;

	// Number of abilities running in each activation group.
	int32 ActivationGroupCounts[(uint8)EIMGAbilityActivationGroup::MAX];
};

#undef UE_API
