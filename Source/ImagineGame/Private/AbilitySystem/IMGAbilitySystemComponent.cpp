// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/IMGAbilitySystemComponent.h"

#include "IMGLogChannels.h"
#include "AbilitySystem/IMGAbilityTagRelationshipMapping.h"
#include "AbilitySystem/Abilities/IMGGameplayAbility.h"
#include "Abilities/GameplayAbility.h"
#include "Abilities/GameplayAbilityRepAnimMontage.h"
#include "Animation/IMGAnimInstance.h"

UE_DEFINE_GAMEPLAY_TAG(TAG_Gameplay_AbilityInputBlocked, "Gameplay.AbilityInputBlocked");

UIMGAbilitySystemComponent::UIMGAbilitySystemComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetMontageRepAnimPositionMethod(ERepAnimPositionMethod::CurrentSectionId);
}

UIMGAbilitySystemComponent::~UIMGAbilitySystemComponent() = default;

void UIMGAbilitySystemComponent::InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor)
{
	FGameplayAbilityActorInfo* ActorInfo = AbilityActorInfo.Get();
	check(ActorInfo);
	check(InOwnerActor);
	
	Super::InitAbilityActorInfo(InOwnerActor, InAvatarActor);

	SetMontageRepAnimPositionMethod(ERepAnimPositionMethod::CurrentSectionId);

	if (UIMGAnimInstance* ActAnimInst = Cast<UIMGAnimInstance>(ActorInfo->GetAnimInstance()))
	{
		ActAnimInst->InitializeWithAbilitySystem(this);
	}
}

void UIMGAbilitySystemComponent::CancelAbilitiesByFunc(const TShouldCancelAbilityFunc& ShouldCancelFunc, const bool bReplicateCancelAbility)
{
	ABILITYLIST_SCOPE_LOCK();

	for (const FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
	{
		if (!AbilitySpec.IsActive())
		{
			continue;
		}

		UIMGGameplayAbility* IMGAbilityCDO = Cast<UIMGGameplayAbility>(AbilitySpec.Ability);
		if (!IMGAbilityCDO)
		{
			UE_LOG(LogIMGAbilitySystem, Error, TEXT("CancelAbilitiesByFunc: Non-UIMGGameplayAbility %s was Granted to ASC. Skipping."), *AbilitySpec.Ability.GetName());
			continue;
		}

		// NonInstanced is deprecated in newer UE versions. Enforce an instanced policy without referencing the deprecated enumerator.
		const EGameplayAbilityInstancingPolicy::Type Policy = AbilitySpec.Ability->GetInstancingPolicy();
		ensureMsgf(
			Policy == EGameplayAbilityInstancingPolicy::InstancedPerActor || Policy == EGameplayAbilityInstancingPolicy::InstancedPerExecution,
			TEXT("CancelAbilitiesByFunc: All Abilities should be instanced (InstancedPerActor/InstancedPerExecution)."));

		// Cancel all the spawned instances.
		TArray<UGameplayAbility*> Instances = AbilitySpec.GetAbilityInstances();
		for (UGameplayAbility* AbilityInstance : Instances)
		{
			UIMGGameplayAbility* IMGAbilityInstance = CastChecked<UIMGGameplayAbility>(AbilityInstance);

			if (ShouldCancelFunc(IMGAbilityInstance, AbilitySpec.Handle))
			{
				if (IMGAbilityInstance->CanBeCanceled())
				{
					CancelAbilityHandle(AbilitySpec.Handle);
				}
				else
				{
					UE_LOG(LogIMGAbilitySystem, Error, TEXT("CancelAbilitiesByFunc: Can't cancel ability [%s] because CanBeCanceled is false."), *IMGAbilityInstance->GetName());
				}
			}
		}
	}
}

void UIMGAbilitySystemComponent::CancelInputActivatedAbilities(bool bReplicateCancelAbility)
{
	auto ShouldCancelFunc = [this](const UIMGGameplayAbility* IMGAbility, FGameplayAbilitySpecHandle /*Handle*/)
	{
		const EIMGAbilityActivationPolicy ActivationPolicy = IMGAbility->GetActivationPolicy();
		return ((ActivationPolicy == EIMGAbilityActivationPolicy::OnInputTriggered) || (ActivationPolicy == EIMGAbilityActivationPolicy::WhileInputActive));
	};

	CancelAbilitiesByFunc(ShouldCancelFunc, bReplicateCancelAbility);
}

void UIMGAbilitySystemComponent::AbilityInputTagPressed(const FGameplayTag InputTag)
{
	if (InputTag.IsValid())
	{
		for (const FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
		{
			if (AbilitySpec.Ability && AbilitySpec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
			{
				InputPressedSpecHandles.AddUnique(AbilitySpec.Handle);
				InputHeldSpecHandles.AddUnique(AbilitySpec.Handle);
			}
		}
	}
}

void UIMGAbilitySystemComponent::AbilityInputTagTriggered(const FGameplayTag InputTag)
{
	if (InputTag.IsValid())
	{
		for (const FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
		{
			if (AbilitySpec.Ability && AbilitySpec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
			{
				// One-shot trigger for command outputs: do not add to held handles.
				InputPressedSpecHandles.AddUnique(AbilitySpec.Handle);
			}
		}
	}
}

void UIMGAbilitySystemComponent::AbilityInputTagReleased(const FGameplayTag InputTag)
{
	if (InputTag.IsValid())
	{
		for (const FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
		{
			if (AbilitySpec.Ability && AbilitySpec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
			{
				InputReleasedSpecHandles.AddUnique(AbilitySpec.Handle);
				InputHeldSpecHandles.Remove(AbilitySpec.Handle);
			}
		}
	}
}

bool UIMGAbilitySystemComponent::TryActivateGrantedAbilityByClass(
	const TSubclassOf<UIMGGameplayAbility> AbilityClass,
	const bool bAllowRemoteActivation,
	const bool bCancelIfAlreadyActive,
	TSubclassOf<UIMGGameplayAbility>* OutActivatedAbilityClass,
	FGameplayAbilitySpecHandle* OutActivatedSpecHandle)
{
	if (!AbilityClass)
	{
		return false;
	}

	ABILITYLIST_SCOPE_LOCK();

	for (const FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
	{
		if (!AbilitySpec.Ability)
		{
			continue;
		}

		if (!AbilitySpec.Ability->GetClass()->IsChildOf(AbilityClass))
		{
			continue;
		}

		if (bCancelIfAlreadyActive && AbilitySpec.IsActive())
		{
			CancelAbilityHandle(AbilitySpec.Handle);
		}
		
		const bool bRequested = TryActivateAbility(AbilitySpec.Handle, bAllowRemoteActivation);
		if (!bRequested)
		{
			return false;
		}

		if (const FGameplayAbilitySpec* ActivatedSpec = FindAbilitySpecFromHandle(AbilitySpec.Handle))
		{
			if (ActivatedSpec->IsActive())
			{
				if (OutActivatedAbilityClass)
				{
					*OutActivatedAbilityClass = AbilitySpec.Ability->GetClass();
				}
				if (OutActivatedSpecHandle)
				{
					*OutActivatedSpecHandle = AbilitySpec.Handle;
				}
				return true;
			}
		}

		return false;
	}

	return false;
}

bool UIMGAbilitySystemComponent::TryActivateGrantedAbilityByInputTag(
	const FGameplayTag InputTag,
	const bool bAllowRemoteActivation,
	const bool bCancelIfAlreadyActive,
	TSubclassOf<UIMGGameplayAbility>* OutActivatedAbilityClass,
	FGameplayAbilitySpecHandle* OutActivatedSpecHandle)
{
	if (!InputTag.IsValid())
	{
		return false;
	}

	ABILITYLIST_SCOPE_LOCK();

	for (const FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
	{
		if (!AbilitySpec.Ability || !AbilitySpec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
		{
			continue;
		}

		if (bCancelIfAlreadyActive && AbilitySpec.IsActive())
		{
			CancelAbilityHandle(AbilitySpec.Handle);
		}

		const bool bRequested = TryActivateAbility(AbilitySpec.Handle, bAllowRemoteActivation);
		if (!bRequested)
		{
			continue;
		}

		if (const FGameplayAbilitySpec* ActivatedSpec = FindAbilitySpecFromHandle(AbilitySpec.Handle))
		{
			if (ActivatedSpec->IsActive())
			{
				if (OutActivatedAbilityClass)
				{
					*OutActivatedAbilityClass = AbilitySpec.Ability->GetClass();
				}
				if (OutActivatedSpecHandle)
				{
					*OutActivatedSpecHandle = AbilitySpec.Handle;
				}
				return true;
			}
		}
	}

	return false;
}
bool UIMGAbilitySystemComponent::HasGrantedAbilityByClass(const TSubclassOf<UIMGGameplayAbility> AbilityClass)
{
	if (!AbilityClass)
	{
		return false;
	}

	ABILITYLIST_SCOPE_LOCK();

	for (const FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
	{
		if (AbilitySpec.Ability && AbilitySpec.Ability->GetClass()->IsChildOf(AbilityClass))
		{
			return true;
		}
	}

	return false;
}

bool UIMGAbilitySystemComponent::FindActiveAbilityInstanceByClass(
	const TSubclassOf<UIMGGameplayAbility> AbilityClass,
	TWeakObjectPtr<UIMGGameplayAbility>& OutAbilityInstance)
{
	OutAbilityInstance.Reset();

	if (!AbilityClass)
	{
		return false;
	}

	ABILITYLIST_SCOPE_LOCK();

	for (const FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
	{
		if (!AbilitySpec.IsActive() || !AbilitySpec.Ability || !AbilitySpec.Ability->GetClass()->IsChildOf(AbilityClass))
		{
			continue;
		}

		const TArray<UGameplayAbility*> Instances = AbilitySpec.GetAbilityInstances();
		for (UGameplayAbility* AbilityInstance : Instances)
		{
			if (UIMGGameplayAbility* ActAbility = Cast<UIMGGameplayAbility>(AbilityInstance))
			{
				OutAbilityInstance = ActAbility;
				return true;
			}
		}
	}

	return false;
}

bool UIMGAbilitySystemComponent::IsAbilityInstanceActive(const UIMGGameplayAbility* AbilityInstance)
{
	if (!AbilityInstance)
	{
		return false;
	}

	ABILITYLIST_SCOPE_LOCK();

	for (const FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
	{
		if (!AbilitySpec.IsActive())
		{
			continue;
		}

		const TArray<UGameplayAbility*> Instances = AbilitySpec.GetAbilityInstances();
		for (UGameplayAbility* Instance : Instances)
		{
			if (Instance == AbilityInstance)
			{
				return true;
			}
		}
	}

	return false;
}

void UIMGAbilitySystemComponent::CancelAbilityByHandle(const FGameplayAbilitySpecHandle AbilityHandle)
{
	if (AbilityHandle.IsValid())
	{
		CancelAbilityHandle(AbilityHandle);
	}
}

void UIMGAbilitySystemComponent::RebuildAbilityIdCache()
{
	AbilityIdToSpecHandle.Reset();

	ABILITYLIST_SCOPE_LOCK();

	for (const FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
	{
		const UIMGGameplayAbility* IMGAbilityCDO = Cast<UIMGGameplayAbility>(AbilitySpec.Ability);
		if (!IMGAbilityCDO)
		{
			continue;
		}

		const FName AbilityId = IMGAbilityCDO->GetAbilityId();
		if (AbilityId.IsNone())
		{
			continue;
		}

		if (AbilityIdToSpecHandle.Contains(AbilityId))
		{
			UE_LOG(LogIMGAbilitySystem, Warning, TEXT("Duplicate AbilityId [%s] detected on ASC [%s]."), *AbilityId.ToString(), *GetNameSafe(this));
			continue;
		}

		AbilityIdToSpecHandle.Add(AbilityId, AbilitySpec.Handle);
	}
}

FGameplayAbilitySpecHandle UIMGAbilitySystemComponent::GetAbilitySpecHandleById(const FName AbilityId) const
{
	if (AbilityId.IsNone())
	{
		return FGameplayAbilitySpecHandle();
	}

	if (const FGameplayAbilitySpecHandle* Handle = AbilityIdToSpecHandle.Find(AbilityId))
	{
		return *Handle;
	}

	return FGameplayAbilitySpecHandle();
}

bool UIMGAbilitySystemComponent::TryActivateAbilityById(
	const FName AbilityId,
	const bool bAllowRemoteActivation,
	const bool bCancelIfAlreadyActive,
	FGameplayAbilitySpecHandle* OutActivatedSpecHandle)
{
	if (AbilityId.IsNone())
	{
		return false;
	}

	const FGameplayAbilitySpecHandle Handle = GetAbilitySpecHandleById(AbilityId);
	if (!Handle.IsValid())
	{
		return false;
	}

	if (!FindAbilitySpecFromHandle(Handle))
	{
		AbilityIdToSpecHandle.Remove(AbilityId);
		return false;
	}

	if (bCancelIfAlreadyActive)
	{
		if (const FGameplayAbilitySpec* AbilitySpec = FindAbilitySpecFromHandle(Handle))
		{
			if (AbilitySpec->IsActive())
			{
				CancelAbilityHandle(Handle);
			}
		}
	}
	
	const bool bRequested = TryActivateAbility(Handle, bAllowRemoteActivation);
	if (!bRequested)
	{
		return false;
	}

	if (const FGameplayAbilitySpec* ActivatedSpec = FindAbilitySpecFromHandle(Handle))
	{
		if (ActivatedSpec->IsActive())
		{
			if (OutActivatedSpecHandle)
			{
				*OutActivatedSpecHandle = Handle;
			}
			return true;
		}
	}

	return false;
}

void UIMGAbilitySystemComponent::TryActivateAbilitiesOnSpawn()
{
	ABILITYLIST_SCOPE_LOCK();
	for (const FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
	{
		if (const UIMGGameplayAbility* IMGAbilityCDO = Cast<UIMGGameplayAbility>(AbilitySpec.Ability))
		{
			IMGAbilityCDO->TryActivateAbilityOnSpawn(AbilityActorInfo.Get(), AbilitySpec);
		}
	}
}

void UIMGAbilitySystemComponent::ProcessAbilityInput(float DeltaTime, bool bGamePaused)
{
	if (HasMatchingGameplayTag(TAG_Gameplay_AbilityInputBlocked))
	{
		ClearAbilityInput();
		return;
	}

	static TArray<FGameplayAbilitySpecHandle> AbilitiesToActive;
	AbilitiesToActive.Reset();

	//@TODO: See if we can use FScopedServerAbilityRPCBatcher ScopedRPCBatcher in some of these loops

	//
	// Process all abilities that activate when the input is held.
	//
	for (const FGameplayAbilitySpecHandle& SpecHandle : InputHeldSpecHandles)
	{
		if (const FGameplayAbilitySpec* AbilitySpec = FindAbilitySpecFromHandle(SpecHandle))
		{
			if (AbilitySpec->Ability && !AbilitySpec->IsActive())
			{
				const UIMGGameplayAbility* IMGAbilityCDO = Cast<UIMGGameplayAbility>(AbilitySpec->Ability);
				if (IMGAbilityCDO &&
					IMGAbilityCDO->GetActivationPolicy() == EIMGAbilityActivationPolicy::WhileInputActive)
				{
					AbilitiesToActive.AddUnique(AbilitySpec->Handle);
				}
			}
		}
	}

	//
	// Process all abilities that had their input pressed this frame.
	//
	for (const FGameplayAbilitySpecHandle& SpecHandle : InputPressedSpecHandles)
	{
		if (FGameplayAbilitySpec* AbilitySpec = FindAbilitySpecFromHandle(SpecHandle))
		{
			if (AbilitySpec->Ability)
			{
				AbilitySpec->InputPressed = true;

				if (AbilitySpec->IsActive())
				{
					// Ability is active so pass along the input event.
					AbilitySpecInputPressed(*AbilitySpec);
				}
				else
				{
					const UIMGGameplayAbility* ActAbilityCDO = Cast<UIMGGameplayAbility>(AbilitySpec->Ability);
					if (ActAbilityCDO &&
						ActAbilityCDO->GetActivationPolicy() == EIMGAbilityActivationPolicy::OnInputTriggered)
					{
						AbilitiesToActive.AddUnique(AbilitySpec->Handle);
					}
				}
			}
		}
	}

	//
	// Try to activate all the abilities that are from presses and holds.
	// We do it all at once so that held inputs don't activate the ability
	// and then also send an input event to the ability because of the press.
	//
	if (!AbilitiesToActive.IsEmpty())
	{
		for (const FGameplayAbilitySpecHandle& AbilitySpecHandle : AbilitiesToActive)
		{
			TryActivateAbility(AbilitySpecHandle);
		}
	}

	//
	// Process all abilities that had their input released this frame.
	//
	for (const FGameplayAbilitySpecHandle& SpecHandle : InputReleasedSpecHandles)
	{
		if (FGameplayAbilitySpec* AbilitySpec = FindAbilitySpecFromHandle(SpecHandle))
		{
			if (AbilitySpec->Ability)
			{
				AbilitySpec->InputPressed = false;

				if (AbilitySpec->IsActive())
				{
					// Ability is active so pass along the input event.
					AbilitySpecInputReleased(*AbilitySpec);
				}
			}
		}
	}

	//
	// Clear the cached ability handles.
	//
	InputPressedSpecHandles.Reset();
	InputReleasedSpecHandles.Reset();
}

void UIMGAbilitySystemComponent::ClearAbilityInput()
{
	InputPressedSpecHandles.Reset();
	InputReleasedSpecHandles.Reset();
	InputHeldSpecHandles.Reset();
}

bool UIMGAbilitySystemComponent::IsActivationGroupBlocked(EIMGAbilityActivationGroup Group) const
{
	bool bBlocked = false;

	switch (Group)
	{
	case EIMGAbilityActivationGroup::Independent:
		// Independent abilities are never blocked.
		bBlocked = false;
		break;

	case EIMGAbilityActivationGroup::Exclusive_Replaceable:
	case EIMGAbilityActivationGroup::Exclusive_Blocking:
		// Exclusive abilities can activate if nothing is blocking.
		bBlocked = (ActivationGroupCounts[static_cast<uint8>(EIMGAbilityActivationGroup::Exclusive_Blocking)] > 0);
		break;

	default:
		checkf(false, TEXT("IsActivationGroupBlocked: Invalid ActivationGroup [%d]\n"), (uint8)Group);
		break;
	}

	return bBlocked;
}

void UIMGAbilitySystemComponent::ApplyAbilityBlockAndCancelTags(const FGameplayTagContainer& AbilityTags, UGameplayAbility* RequestingAbility, bool bEnableBlockTags, const FGameplayTagContainer& BlockTags, bool bExecuteCancelTags, const FGameplayTagContainer& CancelTags)
{
	FGameplayTagContainer ModifiedBlockTags = BlockTags;
	FGameplayTagContainer ModifiedCancelTags = CancelTags;

	if (TagRelationshipMapping)
	{
		// Use the mapping to expand the ability tags into block and cancel tag
		TagRelationshipMapping->GetAbilityTagsToBlockAndCancel(AbilityTags, &ModifiedBlockTags, &ModifiedCancelTags);
	}

	Super::ApplyAbilityBlockAndCancelTags(AbilityTags, RequestingAbility, bEnableBlockTags, ModifiedBlockTags, bExecuteCancelTags, ModifiedCancelTags);

	//@TODO: Apply any special logic like blocking input or movement
}

void UIMGAbilitySystemComponent::SetTagRelationshipMapping(UIMGAbilityTagRelationshipMapping* NewMapping)
{
	TagRelationshipMapping = NewMapping;
}

void UIMGAbilitySystemComponent::GetAdditionalActivationTagRequirements(const FGameplayTagContainer& AbilityTags,
	FGameplayTagContainer& OutActivationRequired, FGameplayTagContainer& OutActivationBlocked) const
{
	if (TagRelationshipMapping)
	{
		TagRelationshipMapping->GetRequiredAndBlockedActivationTags(AbilityTags, &OutActivationRequired, &OutActivationBlocked);
	}
}

void UIMGAbilitySystemComponent::ResetGameplayTagCounts(FGameplayTagContainer TagContainer, const int32 NewCount)
{
	for (const FGameplayTag& Tag : TagContainer)
	{
		SetLooseGameplayTagCount(Tag, NewCount);
	}
}

void UIMGAbilitySystemComponent::ClientTryActivateAbility_Implementation(FGameplayAbilitySpecHandle AbilityToActivate)
{
	
	Super::ClientTryActivateAbility_Implementation(AbilityToActivate);
}

void UIMGAbilitySystemComponent::ClientActivateAbilityFailed_Implementation(FGameplayAbilitySpecHandle AbilityToActivate, int16 PredictionKey)
{
	const FGameplayAbilitySpec* AbilitySpec = FindAbilitySpecFromHandle(AbilityToActivate);
	
	Super::ClientActivateAbilityFailed_Implementation(AbilityToActivate, PredictionKey);
}





void UIMGAbilitySystemComponent::OnGiveAbility(FGameplayAbilitySpec& AbilitySpec)
{
	Super::OnGiveAbility(AbilitySpec);

	const UIMGGameplayAbility* IMGAbilityCDO = Cast<UIMGGameplayAbility>(AbilitySpec.Ability);
	if (!IMGAbilityCDO)
	{
		return;
	}

	const FName AbilityId = IMGAbilityCDO->GetAbilityId();
	if (AbilityId.IsNone())
	{
		return;
	}

	if (AbilityIdToSpecHandle.Contains(AbilityId))
	{
		UE_LOG(LogIMGAbilitySystem, Warning, TEXT("Duplicate AbilityId [%s] detected on ASC [%s]."), *AbilityId.ToString(), *GetNameSafe(this));
		return;
	}

	AbilityIdToSpecHandle.Add(AbilityId, AbilitySpec.Handle);
}

void UIMGAbilitySystemComponent::OnRemoveAbility(FGameplayAbilitySpec& AbilitySpec)
{
	if (const UIMGGameplayAbility* IMGAbilityCDO = Cast<UIMGGameplayAbility>(AbilitySpec.Ability))
	{
		const FName AbilityId = IMGAbilityCDO->GetAbilityId();
		if (!AbilityId.IsNone())
		{
			const FGameplayAbilitySpecHandle* Handle = AbilityIdToSpecHandle.Find(AbilityId);
			if (Handle && *Handle == AbilitySpec.Handle)
			{
				AbilityIdToSpecHandle.Remove(AbilityId);
			}
		}
	}

	Super::OnRemoveAbility(AbilitySpec);
}

void UIMGAbilitySystemComponent::OnRep_ActivateAbilities()
{
	Super::OnRep_ActivateAbilities();

	RebuildAbilityIdCache();
}

void UIMGAbilitySystemComponent::NotifyAbilityCommit(UGameplayAbility* Ability)
{
	Super::NotifyAbilityCommit(Ability);
}

void UIMGAbilitySystemComponent::NotifyAbilityActivated(const FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability)
{
	Super::NotifyAbilityActivated(Handle, Ability);
}

void UIMGAbilitySystemComponent::NotifyAbilityFailed(const FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability, const FGameplayTagContainer& FailureReason)
{
	Super::NotifyAbilityFailed(Handle, Ability, FailureReason);
}


void UIMGAbilitySystemComponent::AbilitySpecInputPressed(FGameplayAbilitySpec& Spec)
{
	Super::AbilitySpecInputPressed(Spec);

	// We don't support UGameplayAbility::bReplicateInputDirectly.
	// Use replicated events instead so that the WaitInputPress ability task works.
	if (Spec.IsActive())
	{
		const UGameplayAbility* Instance = Spec.GetPrimaryInstance();
		if (!Instance)
		{
			// InstancedPerExecution may not have a "primary" instance. Fall back to any active instance.
			const TArray<UGameplayAbility*> Instances = Spec.GetAbilityInstances();
			Instance = Instances.Num() > 0 ? Instances[0] : nullptr;
		}

		const FPredictionKey OriginalPredictionKey = Instance
			? Instance->GetCurrentActivationInfo().GetActivationPredictionKey()
			: FPredictionKey();

		// Invoke the InputPressed event. This is not replicated here. If someone is listening, they may replicate the InputPressed event to the server.
		InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputPressed, Spec.Handle, OriginalPredictionKey);
	}
}

void UIMGAbilitySystemComponent::AbilitySpecInputReleased(FGameplayAbilitySpec& Spec)
{
	Super::AbilitySpecInputReleased(Spec);

	// We don't support UGameplayAbility::bReplicateInputDirectly.
	// Use replicated events instead so that the WaitInputRelease ability task works.
	if (Spec.IsActive())
	{
		const UGameplayAbility* Instance = Spec.GetPrimaryInstance();
		if (!Instance)
		{
			const TArray<UGameplayAbility*> Instances = Spec.GetAbilityInstances();
			Instance = Instances.Num() > 0 ? Instances[0] : nullptr;
		}

		const FPredictionKey OriginalPredictionKey = Instance
			? Instance->GetCurrentActivationInfo().GetActivationPredictionKey()
			: FPredictionKey();

		// Invoke the InputReleased event. This is not replicated here. If someone is listening, they may replicate the InputReleased event to the server.
		InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputReleased, Spec.Handle, OriginalPredictionKey);
	}
}




