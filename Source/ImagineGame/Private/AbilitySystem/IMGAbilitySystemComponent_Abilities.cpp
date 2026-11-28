#include "AbilitySystem/IMGAbilitySystemComponent.h"
#include "AbilitySystem/IMGAbilityTagRelationshipMapping.h"
#include "GameFramework/Pawn.h"
#include "IMGLogChannels.h"

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
					IMGAbilityInstance->CancelAbility(AbilitySpec.Handle, AbilityActorInfo.Get(), IMGAbilityInstance->GetCurrentActivationInfo(), bReplicateCancelAbility);
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

bool UIMGAbilitySystemComponent::TryActivateGrantedAbilityByClass(
	const TSubclassOf<UIMGGameplayAbility> AbilityClass,
	const bool bAllowRemoteActivation,
	const bool bCancelIfAlreadyActive,
	TSubclassOf<UIMGGameplayAbility>* OutActivatedAbilityClass,
	FGameplayAbilitySpecHandle* OutActivatedSpecHandle)
{
	return TryActivateGrantedAbility(
		[AbilityClass](const FGameplayAbilitySpec& Spec)
		{
			return AbilityClass && Spec.Ability && Spec.Ability->GetClass()->IsChildOf(AbilityClass);
		},
		bAllowRemoteActivation, bCancelIfAlreadyActive, OutActivatedAbilityClass, OutActivatedSpecHandle);
}

bool UIMGAbilitySystemComponent::TryActivateGrantedAbilityByInputTag(
	const FGameplayTag InputTag,
	const bool bAllowRemoteActivation,
	const bool bCancelIfAlreadyActive,
	TSubclassOf<UIMGGameplayAbility>* OutActivatedAbilityClass,
	FGameplayAbilitySpecHandle* OutActivatedSpecHandle)
{
	return TryActivateGrantedAbility(
		[InputTag](const FGameplayAbilitySpec& Spec)
		{
			return InputTag.IsValid() && Spec.Ability && Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag);
		},
		bAllowRemoteActivation, bCancelIfAlreadyActive, OutActivatedAbilityClass, OutActivatedSpecHandle);
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
			if (UIMGGameplayAbility* IMGAbility = Cast<UIMGGameplayAbility>(AbilityInstance); IMGAbility && IMGAbility->IsActive())
			{
				OutAbilityInstance = IMGAbility;
				return true;
			}
		}
	}

	return false;
}

bool UIMGAbilitySystemComponent::IsAbilityInstanceActive(const UIMGGameplayAbility* AbilityInstance)
{
	if (!IsValid(AbilityInstance) || !AbilityInstance->IsActive())
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

void UIMGAbilitySystemComponent::ClientActivateAbilityFailed_Implementation(FGameplayAbilitySpecHandle AbilityToActivate, int16 PredictionKey)
{
	const FGameplayAbilitySpec* AbilitySpec = FindAbilitySpecFromHandle(AbilityToActivate);
	// Super broadcasts rejection and may synchronously end the predicted montage/ability.
	UE_LOG(LogIMGAbilitySystem, Warning,
		TEXT("[PredictionRejected] Ability=%s Owner=%s Avatar=%s Role=%d Spec=%s PredKey=%d; server rejected activation (details in authority failure log)"),
		*GetNameSafe(AbilitySpec ? AbilitySpec->Ability.Get() : nullptr), *GetNameSafe(GetOwnerActor()),
		*GetNameSafe(GetAvatarActor()), GetAvatarActor() ? static_cast<int32>(GetAvatarActor()->GetLocalRole()) : -1,
		*AbilityToActivate.ToString(), PredictionKey);

	Super::ClientActivateAbilityFailed_Implementation(AbilityToActivate, PredictionKey);
	AbilityNetworkTermination.Broadcast(AbilityToActivate, PredictionKey, true);
}

void UIMGAbilitySystemComponent::ClientEndAbility_Implementation(
	FGameplayAbilitySpecHandle AbilityToEnd, FGameplayAbilityActivationInfo ActivationInfo)
{
	Super::ClientEndAbility_Implementation(AbilityToEnd, ActivationInfo);
	AbilityNetworkTermination.Broadcast(AbilityToEnd, ActivationInfo.GetActivationPredictionKey().Current, false);
}

void UIMGAbilitySystemComponent::ClientCancelAbility_Implementation(
	FGameplayAbilitySpecHandle AbilityToCancel, FGameplayAbilityActivationInfo ActivationInfo)
{
	Super::ClientCancelAbility_Implementation(AbilityToCancel, ActivationInfo);
	AbilityNetworkTermination.Broadcast(AbilityToCancel, ActivationInfo.GetActivationPredictionKey().Current, false);
}

void UIMGAbilitySystemComponent::NotifyAbilityActivated(const FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability)
{
	Super::NotifyAbilityActivated(Handle, Ability);

	if (UIMGGameplayAbility* IMGAbility = Cast<UIMGGameplayAbility>(Ability))
	{
		AddAbilityToActivationGroup(IMGAbility->GetActivationGroup(), IMGAbility);
	}
}

void UIMGAbilitySystemComponent::NotifyAbilityFailed(const FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability, const FGameplayTagContainer& FailureReason)
{
	UE_LOG(LogIMGAbilitySystem, Verbose,
		TEXT("[ActivationFailed] Ability=%s Owner=%s Avatar=%s Role=%d Auth=%d Spec=%s ScopedPredKey=%d FailureTags=%s"),
		*GetNameSafe(Ability), *GetNameSafe(GetOwnerActor()), *GetNameSafe(GetAvatarActor()),
		GetAvatarActor() ? static_cast<int32>(GetAvatarActor()->GetLocalRole()) : -1,
		IsOwnerActorAuthoritative(), *Handle.ToString(), ScopedPredictionKey.Current, *FailureReason.ToStringSimple());
	Super::NotifyAbilityFailed(Handle, Ability, FailureReason);

	if (APawn* Avatar = Cast<APawn>(GetAvatarActor()))
	{
		if (!Avatar->IsLocallyControlled() && Ability->IsSupportedForNetworking())
		{
			ClientNotifyAbilityFailed(Ability, FailureReason);
			return;
		}
	}

	HandleAbilityFailed(Ability, FailureReason);
}

void UIMGAbilitySystemComponent::NotifyAbilityEnded(FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability, bool bWasCancelled)
{
	Super::NotifyAbilityEnded(Handle, Ability, bWasCancelled);

	if (UIMGGameplayAbility* IMGAbility = Cast<UIMGGameplayAbility>(Ability))
	{
		RemoveAbilityFromActivationGroup(IMGAbility->GetActivationGroup(), IMGAbility);
	}
}

void UIMGAbilitySystemComponent::ClientNotifyAbilityFailed_Implementation(const UGameplayAbility* Ability, const FGameplayTagContainer& FailureReason)
{
	HandleAbilityFailed(Ability, FailureReason);
}

void UIMGAbilitySystemComponent::HandleAbilityFailed(const UGameplayAbility* Ability, const FGameplayTagContainer& FailureReason)
{
	//UE_LOG(LogIMGAbilitySystem, Warning, TEXT("Ability %s failed to activate (tags: %s)"), *GetPathNameSafe(Ability), *FailureReason.ToString());

	if (const UIMGGameplayAbility* IMGAbility = Cast<const UIMGGameplayAbility>(Ability))
	{
		IMGAbility->OnAbilityFailedToActivate(FailureReason);
	}
}

void UIMGAbilitySystemComponent::AddAbilityToActivationGroup(EIMGAbilityActivationGroup Group, UIMGGameplayAbility* IMGAbility)
{
	check(IMGAbility);
	check(ActivationGroupCounts[(uint8)Group] < INT32_MAX);

	ActivationGroupCounts[(uint8)Group]++;

	const bool bReplicateCancelAbility = false;

	switch (Group)
	{
	case EIMGAbilityActivationGroup::Independent:
		// Independent abilities do not cancel any other abilities.
		break;

	case EIMGAbilityActivationGroup::Exclusive_Replaceable:
	case EIMGAbilityActivationGroup::Exclusive_Blocking:
		CancelActivationGroupAbilities(EIMGAbilityActivationGroup::Exclusive_Replaceable, IMGAbility, bReplicateCancelAbility);
		break;

	default:
		checkf(false, TEXT("AddAbilityToActivationGroup: Invalid ActivationGroup [%d]\n"), (uint8)Group);
		break;
	}

	const int32 ExclusiveCount = ActivationGroupCounts[(uint8)EIMGAbilityActivationGroup::Exclusive_Replaceable] + ActivationGroupCounts[(uint8)EIMGAbilityActivationGroup::Exclusive_Blocking];
	if (!ensure(ExclusiveCount <= 1))
	{
		UE_LOG(LogIMGAbilitySystem, Error, TEXT("AddAbilityToActivationGroup: Multiple exclusive abilities are running."));
	}
}

void UIMGAbilitySystemComponent::RemoveAbilityFromActivationGroup(EIMGAbilityActivationGroup Group, UIMGGameplayAbility* IMGAbility)
{
	check(IMGAbility);
	check(ActivationGroupCounts[(uint8)Group] > 0);

	ActivationGroupCounts[(uint8)Group]--;
}

void UIMGAbilitySystemComponent::CancelActivationGroupAbilities(EIMGAbilityActivationGroup Group, UIMGGameplayAbility* IgnoreIMGAbility, bool bReplicateCancelAbility)
{
	auto ShouldCancelFunc = [this, Group, IgnoreIMGAbility](const UIMGGameplayAbility* IMGAbility, FGameplayAbilitySpecHandle Handle)
	{
		return ((IMGAbility->GetActivationGroup() == Group) && (IMGAbility != IgnoreIMGAbility));
	};

	CancelAbilitiesByFunc(ShouldCancelFunc, bReplicateCancelAbility);
}

bool UIMGAbilitySystemComponent::TryActivateGrantedAbility(
	TFunctionRef<bool(const FGameplayAbilitySpec&)> Matches, bool bAllowRemoteActivation,
	bool bCancelIfAlreadyActive, TSubclassOf<UIMGGameplayAbility>* OutActivatedAbilityClass,
	FGameplayAbilitySpecHandle* OutActivatedSpecHandle)
{
	if (OutActivatedAbilityClass)
	{
		*OutActivatedAbilityClass = nullptr;
	}
	if (OutActivatedSpecHandle)
	{
		*OutActivatedSpecHandle = FGameplayAbilitySpecHandle();
	}

	ABILITYLIST_SCOPE_LOCK();
	const uint32 InputGeneration = AbilityInputGeneration;
	const TWeakObjectPtr<AActor> Avatar = GetAvatarActor();
	for (const FGameplayAbilitySpec& Spec : ActivatableAbilities.Items)
	{
		if (Spec.PendingRemove || Spec.RemoveAfterActivation || !Matches(Spec))
		{
			continue;
		}

		const FGameplayAbilitySpecHandle Handle = Spec.Handle;
		const TSubclassOf<UIMGGameplayAbility> AbilityClass = Spec.Ability->GetClass();
		if (bCancelIfAlreadyActive && Spec.IsActive())
		{
			CancelAbilityHandle(Handle);
			if (InputGeneration != AbilityInputGeneration || Avatar.Get() != GetAvatarActor())
			{
				return false;
			}
		}

		// A successful request can already have ended, or still be awaiting remote execution.
		const bool bRequested = TryActivateAbility(Handle, bAllowRemoteActivation);
		if (bRequested)
		{
			if (OutActivatedAbilityClass)
			{
				*OutActivatedAbilityClass = AbilityClass;
			}
			if (OutActivatedSpecHandle)
			{
				*OutActivatedSpecHandle = Handle;
			}
			return true;
		}
		if (InputGeneration != AbilityInputGeneration || Avatar.Get() != GetAvatarActor())
		{
			return false;
		}
	}
	return false;
}
