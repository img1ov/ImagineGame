// Fill out your copyright notice in the Description page of Project Settings.

#include "AbilitySystem/Abilities/IMGGameplayAbility.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "IMGGameplayTags.h"
#include "IMGLogChannels.h"
#include "AbilitySystem/IMGAbilitySystemComponent.h"
#include "Character/IMGCharacter.h"
#include "Player/IMGPlayerController.h"

namespace
{
double GetAbilityLogTimeSeconds(const FGameplayAbilityActorInfo* ActorInfo)
{
	const UWorld* World = ActorInfo && ActorInfo->AvatarActor.IsValid() ? ActorInfo->AvatarActor->GetWorld() : nullptr;
	return World ? World->GetTimeSeconds() : 0.0;
}

FString GetAbilityOwnerName(const FGameplayAbilityActorInfo* ActorInfo)
{
	return GetNameSafe(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr);
}
}

UIMGGameplayAbility::UIMGGameplayAbility(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ReplicationPolicy = EGameplayAbilityReplicationPolicy::ReplicateNo;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	NetSecurityPolicy = EGameplayAbilityNetSecurityPolicy::ClientOrServer;

	ActivationPolicy = EIMGAbilityActivationPolicy::OnInputTriggered;
	ActivationGroup = EIMGAbilityActivationGroup::Independent;
}

UIMGAbilitySystemComponent* UIMGGameplayAbility::GetIMGAbilitySystemComponentFromActorInfo() const
{
	return (CurrentActorInfo ? Cast<UIMGAbilitySystemComponent>(CurrentActorInfo->AbilitySystemComponent.Get()) : nullptr);
}

AIMGPlayerController* UIMGGameplayAbility::GetIMGPlayerControllerFromActorInfo() const
{
	return (CurrentActorInfo ? Cast<AIMGPlayerController>(CurrentActorInfo->PlayerController.Get()) : nullptr);
}

AController* UIMGGameplayAbility::GetControllerFromActorInfo() const
{
	if (CurrentActorInfo)
	{
		if (AController* PC = CurrentActorInfo->PlayerController.Get())
		{
			return PC;
		}

		// Look for a player controller or pawn in the owner chain.
		AActor* TestActor = CurrentActorInfo->OwnerActor.Get();
		while (TestActor)
		{
			if (AController* C = Cast<AController>(TestActor))
			{
				return C;
			}

			if (APawn* Pawn = Cast<APawn>(TestActor))
			{
				return Pawn->GetController();
			}

			TestActor = TestActor->GetOwner();
		}
	}

	return nullptr;
}

AIMGCharacter* UIMGGameplayAbility::GetIMGCharacterFromActorInfo() const
{
	return (CurrentActorInfo ? Cast<AIMGCharacter>(CurrentActorInfo->AvatarActor.Get()) : nullptr);
}

void UIMGGameplayAbility::TryActivateAbilityOnSpawn(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) const
{
	if (ActorInfo && !Spec.IsActive() && (ActivationPolicy == EIMGAbilityActivationPolicy::OnSpawn))
	{
		UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
		const AActor* AvatarActor = ActorInfo->AvatarActor.Get();

		if (ASC && AvatarActor && !AvatarActor->GetTearOff() && (AvatarActor->GetLifeSpan() <= 0.0f))
		{
			const bool bIsLocalExecution = (NetExecutionPolicy == EGameplayAbilityNetExecutionPolicy::LocalPredicted) || (NetExecutionPolicy == EGameplayAbilityNetExecutionPolicy::LocalOnly);
			const bool bIsServerExecution = (NetExecutionPolicy == EGameplayAbilityNetExecutionPolicy::ServerOnly) || (NetExecutionPolicy == EGameplayAbilityNetExecutionPolicy::ServerInitiated);

			const bool bClientShouldActivate = ActorInfo->IsLocallyControlled() && bIsLocalExecution;
			const bool bServerShouldActivate = ActorInfo->IsNetAuthority() && bIsServerExecution;

			if (bClientShouldActivate || bServerShouldActivate)
			{
				ASC->TryActivateAbility(Spec.Handle);
			}
		}
	}
}

bool UIMGGameplayAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!ActorInfo || !ActorInfo->AbilitySystemComponent.IsValid())
	{
		UE_LOG(LogIMGAbilitySystem, Verbose, TEXT("[BattleAbility] CanActivate rejected. Ability=%s AbilityId=%s Owner=%s Reason=InvalidActorInfo Time=%.3f"),
			*GetNameSafe(this),
			*AbilityId.ToString(),
			*GetAbilityOwnerName(ActorInfo),
			GetAbilityLogTimeSeconds(ActorInfo));
		return false;
	}

	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		UE_LOG(LogIMGAbilitySystem, Verbose, TEXT("[BattleAbility] CanActivate rejected. Ability=%s AbilityId=%s Owner=%s Reason=Super Time=%.3f RelevantTags=%s"),
			*GetNameSafe(this),
			*AbilityId.ToString(),
			*GetAbilityOwnerName(ActorInfo),
			GetAbilityLogTimeSeconds(ActorInfo),
			OptionalRelevantTags ? *OptionalRelevantTags->ToString() : TEXT("None"));
		return false;
	}

	UIMGAbilitySystemComponent* IMGASC = CastChecked<UIMGAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get());
	if (IMGASC->IsActivationGroupBlocked(ActivationGroup))
	{
		if (OptionalRelevantTags)
		{
			OptionalRelevantTags->AddTag(IMGGameplayTags::Ability_ActivateFail_ActivationGroup);
		}
		UE_LOG(LogIMGAbilitySystem, Verbose, TEXT("[BattleAbility] CanActivate rejected. Ability=%s AbilityId=%s Owner=%s Reason=ActivationGroupBlocked Group=%d Time=%.3f"),
			*GetNameSafe(this),
			*AbilityId.ToString(),
			*GetAbilityOwnerName(ActorInfo),
			static_cast<int32>(ActivationGroup),
			GetAbilityLogTimeSeconds(ActorInfo));
		return false;
	}

	return true;
}

bool UIMGGameplayAbility::DoesAbilitySatisfyTagRequirements(const UAbilitySystemComponent& AbilitySystemComponent, const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	bool bBlocked = false;
	bool bMissing = false;

	const UAbilitySystemGlobals& AbilitySystemGlobals = UAbilitySystemGlobals::Get();
	const FGameplayTag& BlockedTag = AbilitySystemGlobals.ActivateFailTagsBlockedTag;
	const FGameplayTag& MissingTag = AbilitySystemGlobals.ActivateFailTagsMissingTag;

	if (AbilitySystemComponent.AreAbilityTagsBlocked(GetAssetTags()))
	{
		bBlocked = true;
	}

	const UIMGAbilitySystemComponent* IMGASC = Cast<UIMGAbilitySystemComponent>(&AbilitySystemComponent);
	static FGameplayTagContainer AllRequiredTags;
	static FGameplayTagContainer AllBlockedTags;

	AllRequiredTags = ActivationRequiredTags;
	AllBlockedTags = ActivationBlockedTags;

	if (IMGASC)
	{
		IMGASC->GetAdditionalActivationTagRequirements(GetAssetTags(), AllRequiredTags, AllBlockedTags);
	}

	if (AllBlockedTags.Num() || AllRequiredTags.Num())
	{
		static FGameplayTagContainer AbilitySystemComponentTags;

		AbilitySystemComponentTags.Reset();
		AbilitySystemComponent.GetOwnedGameplayTags(AbilitySystemComponentTags);

		if (AbilitySystemComponentTags.HasAny(AllBlockedTags))
		{
			if (OptionalRelevantTags && AbilitySystemComponentTags.HasTag(IMGGameplayTags::Status_Death))
			{
				OptionalRelevantTags->AddTag(IMGGameplayTags::Ability_ActivateFail_IsDead);
			}

			bBlocked = true;
		}

		if (!AbilitySystemComponentTags.HasAll(AllRequiredTags))
		{
			bMissing = true;
		}
	}

	if (SourceTags != nullptr)
	{
		if (SourceBlockedTags.Num() || SourceRequiredTags.Num())
		{
			if (SourceTags->HasAny(SourceBlockedTags))
			{
				bBlocked = true;
			}

			if (!SourceTags->HasAll(SourceRequiredTags))
			{
				bMissing = true;
			}
		}
	}

	if (TargetTags != nullptr)
	{
		if (TargetBlockedTags.Num() || TargetRequiredTags.Num())
		{
			if (TargetTags->HasAny(TargetBlockedTags))
			{
				bBlocked = true;
			}

			if (!TargetTags->HasAll(TargetRequiredTags))
			{
				bMissing = true;
			}
		}
	}

	if (bBlocked)
	{
		if (OptionalRelevantTags && BlockedTag.IsValid())
		{
			OptionalRelevantTags->AddTag(BlockedTag);
		}
		return false;
	}
	if (bMissing)
	{
		if (OptionalRelevantTags && MissingTag.IsValid())
		{
			OptionalRelevantTags->AddTag(MissingTag);
		}
		return false;
	}

	return true;
}

void UIMGGameplayAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	UE_LOG(LogIMGAbilitySystem, Verbose, TEXT("[BattleAbility] Activate. Ability=%s AbilityId=%s Owner=%s Policy=%d NetPolicy=%d Local=%d Auth=%d PredKey=%d Time=%.3f Trigger=%s"),
		*GetNameSafe(this),
		*AbilityId.ToString(),
		*GetAbilityOwnerName(ActorInfo),
		static_cast<int32>(ActivationPolicy),
		static_cast<int32>(NetExecutionPolicy),
		ActorInfo ? ActorInfo->IsLocallyControlled() : 0,
		ActorInfo ? ActorInfo->IsNetAuthority() : 0,
		ActivationInfo.GetActivationPredictionKey().Current,
		GetAbilityLogTimeSeconds(ActorInfo),
		TriggerEventData ? *TriggerEventData->EventTag.ToString() : TEXT("None"));

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

void UIMGGameplayAbility::CancelAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateCancelAbility)
{
	UE_LOG(LogIMGAbilitySystem, Verbose, TEXT("[BattleAbility] Cancel. Ability=%s AbilityId=%s Owner=%s Local=%d Auth=%d PredKey=%d Replicate=%d Time=%.3f"),
		*GetNameSafe(this),
		*AbilityId.ToString(),
		*GetAbilityOwnerName(ActorInfo),
		ActorInfo ? ActorInfo->IsLocallyControlled() : 0,
		ActorInfo ? ActorInfo->IsNetAuthority() : 0,
		ActivationInfo.GetActivationPredictionKey().Current,
		bReplicateCancelAbility,
		GetAbilityLogTimeSeconds(ActorInfo));

	Super::CancelAbility(Handle, ActorInfo, ActivationInfo, bReplicateCancelAbility);
}

void UIMGGameplayAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	UE_LOG(LogIMGAbilitySystem, Verbose, TEXT("[BattleAbility] End. Ability=%s AbilityId=%s Owner=%s Local=%d Auth=%d PredKey=%d Replicate=%d Cancelled=%d Time=%.3f"),
		*GetNameSafe(this),
		*AbilityId.ToString(),
		*GetAbilityOwnerName(ActorInfo),
		ActorInfo ? ActorInfo->IsLocallyControlled() : 0,
		ActorInfo ? ActorInfo->IsNetAuthority() : 0,
		ActivationInfo.GetActivationPredictionKey().Current,
		bReplicateEndAbility,
		bWasCancelled,
		GetAbilityLogTimeSeconds(ActorInfo));

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UIMGGameplayAbility::OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	Super::OnGiveAbility(ActorInfo, Spec);

	TryActivateAbilityOnSpawn(ActorInfo, Spec);
}
