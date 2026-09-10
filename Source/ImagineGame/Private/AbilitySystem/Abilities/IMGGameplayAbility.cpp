// Fill out your copyright notice in the Description page of Project Settings.

#include "AbilitySystem/Abilities/IMGGameplayAbility.h"

#include "AbilitySystemLog.h"
#include "AbilitySystem/Abilities/IMGAbilityCost.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystem/Abilities/IMGAbilitySimpleFailureMessage.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "AbilitySystem/IMGAbilitySourceInterface.h"
#include "AbilitySystem/IMGGameplayEffectContext.h"
#include "Physics/PhysicalMaterialWithTags.h"
#include "GameFramework/PlayerState.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "IMGGameplayTags.h"
#include "IMGLogChannels.h"
#include "AbilitySystem/IMGAbilitySystemComponent.h"
#include "Character/IMGCharacter.h"
#include "Character/IMGHeroComponent.h"
#include "Camera/IMGCameraMode.h"
#include "Player/IMGPlayerController.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGGameplayAbility)

#define ENSURE_ABILITY_IS_INSTANTIATED_OR_RETURN(FunctionName, ReturnValue)																				\
{																																						\
	if (!ensure(IsInstantiated()))																														\
	{																																					\
		ABILITY_LOG(Error, TEXT("%s: " #FunctionName " cannot be called on a non-instanced ability. Check the instancing policy."), *GetPathName());	\
		return ReturnValue;																																\
	}																																					\
}

UE_DEFINE_GAMEPLAY_TAG(TAG_ABILITY_SIMPLE_FAILURE_MESSAGE, "Ability.UserFacingSimpleActivateFail.Message");
UE_DEFINE_GAMEPLAY_TAG(TAG_ABILITY_PLAY_MONTAGE_FAILURE_MESSAGE, "Ability.PlayMontageOnActivateFail.Message");

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
	bLogCancelation = false;
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

UIMGHeroComponent* UIMGGameplayAbility::GetHeroComponentFromActorInfo() const
{
	return (CurrentActorInfo ? UIMGHeroComponent::FindHeroComponent(CurrentActorInfo->AvatarActor.Get()) : nullptr);
}

void UIMGGameplayAbility::SetCameraMode(TSubclassOf<UIMGCameraMode> CameraMode)
{
	if (!ensure(IsInstantiated()))
	{
		return;
	}

	if (UIMGHeroComponent* HeroComponent = GetHeroComponentFromActorInfo())
	{
		HeroComponent->SetAbilityCameraMode(CameraMode, CurrentSpecHandle);
		ActiveCameraMode = CameraMode;
	}
}

void UIMGGameplayAbility::ClearCameraMode()
{
	if (!ensure(IsInstantiated()))
	{
		return;
	}

	if (ActiveCameraMode)
	{
		if (UIMGHeroComponent* HeroComponent = GetHeroComponentFromActorInfo())
		{
			HeroComponent->ClearAbilityCameraMode(CurrentSpecHandle);
		}
		ActiveCameraMode = nullptr;
	}
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
		UE_LOG(LogIMGAbilitySystem, Verbose, TEXT("[BattleAbility] CanActivate rejected. Ability=%s Owner=%s Reason=InvalidActorInfo Time=%.3f"),
			*GetNameSafe(this),
			*GetAbilityOwnerName(ActorInfo),
			GetAbilityLogTimeSeconds(ActorInfo));
		return false;
	}

	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		UE_LOG(LogIMGAbilitySystem, Verbose, TEXT("[BattleAbility] CanActivate rejected. Ability=%s Owner=%s Reason=Super Time=%.3f RelevantTags=%s"),
			*GetNameSafe(this),
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
		UE_LOG(LogIMGAbilitySystem, Verbose, TEXT("[BattleAbility] CanActivate rejected. Ability=%s Owner=%s Reason=ActivationGroupBlocked Group=%d Time=%.3f"),
			*GetNameSafe(this),
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
	UE_LOG(LogIMGAbilitySystem, Verbose, TEXT("[BattleAbility] Activate. Ability=%s Owner=%s Policy=%d NetPolicy=%d Local=%d Auth=%d PredKey=%d Time=%.3f Trigger=%s"),
		*GetNameSafe(this),
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
	UE_LOG(LogIMGAbilitySystem, Verbose, TEXT("[BattleAbility] Cancel. Ability=%s Owner=%s Local=%d Auth=%d PredKey=%d Replicate=%d Time=%.3f"),
		*GetNameSafe(this),
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
	UE_LOG(LogIMGAbilitySystem, Verbose, TEXT("[BattleAbility] End. Ability=%s Owner=%s Local=%d Auth=%d PredKey=%d Replicate=%d Cancelled=%d Time=%.3f"),
		*GetNameSafe(this),
		*GetAbilityOwnerName(ActorInfo),
		ActorInfo ? ActorInfo->IsLocallyControlled() : 0,
		ActorInfo ? ActorInfo->IsNetAuthority() : 0,
		ActivationInfo.GetActivationPredictionKey().Current,
		bReplicateEndAbility,
		bWasCancelled,
		GetAbilityLogTimeSeconds(ActorInfo));

	ClearCameraMode();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UIMGGameplayAbility::OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	Super::OnGiveAbility(ActorInfo, Spec);

	K2_OnAbilityAdded();

	TryActivateAbilityOnSpawn(ActorInfo, Spec);
}


void UIMGGameplayAbility::NativeOnAbilityFailedToActivate(const FGameplayTagContainer& FailedReason) const
{
	bool bSimpleFailureFound = false;
	for (FGameplayTag Reason : FailedReason)
	{
		if (!bSimpleFailureFound)
		{
			if (const FText* pUserFacingMessage = FailureTagToUserFacingMessages.Find(Reason))
			{
				FIMGAbilitySimpleFailureMessage Message;
				Message.PlayerController = GetActorInfo().PlayerController.Get();
				Message.FailureTags = FailedReason;
				Message.UserFacingReason = *pUserFacingMessage;

				UGameplayMessageSubsystem& MessageSystem = UGameplayMessageSubsystem::Get(GetWorld());
				MessageSystem.BroadcastMessage(TAG_ABILITY_SIMPLE_FAILURE_MESSAGE, Message);
				bSimpleFailureFound = true;
			}
		}

		if (UAnimMontage* pMontage = FailureTagToAnimMontage.FindRef(Reason))
		{
			FIMGAbilityMontageFailureMessage Message;
			Message.PlayerController = GetActorInfo().PlayerController.Get();
			Message.AvatarActor = GetActorInfo().AvatarActor.Get();
			Message.FailureTags = FailedReason;
			Message.FailureMontage = pMontage;

			UGameplayMessageSubsystem& MessageSystem = UGameplayMessageSubsystem::Get(GetWorld());
			MessageSystem.BroadcastMessage(TAG_ABILITY_PLAY_MONTAGE_FAILURE_MESSAGE, Message);
		}
	}
}

void UIMGGameplayAbility::SetCanBeCanceled(bool bCanBeCanceled)
{
	// The ability can not block canceling if it's replaceable.
	if (!bCanBeCanceled && (ActivationGroup == EIMGAbilityActivationGroup::Exclusive_Replaceable))
	{
		UE_LOG(LogIMGAbilitySystem, Error, TEXT("SetCanBeCanceled: Ability [%s] can not block canceling because its activation group is replaceable."), *GetName());
		return;
	}

	Super::SetCanBeCanceled(bCanBeCanceled);
}

void UIMGGameplayAbility::OnRemoveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	K2_OnAbilityRemoved();

	Super::OnRemoveAbility(ActorInfo, Spec);
}

bool UIMGGameplayAbility::CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, OUT FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CheckCost(Handle, ActorInfo, OptionalRelevantTags) || !ActorInfo)
	{
		return false;
	}

	// Verify we can afford any additional costs
	for (const TObjectPtr<UIMGAbilityCost>& AdditionalCost : AdditionalCosts)
	{
		if (AdditionalCost != nullptr)
		{
			if (!AdditionalCost->CheckCost(this, Handle, ActorInfo, /*inout*/ OptionalRelevantTags))
			{
				return false;
			}
		}
	}

	return true;
}

void UIMGGameplayAbility::ApplyCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const
{
	Super::ApplyCost(Handle, ActorInfo, ActivationInfo);

	check(ActorInfo);

	// Used to determine if the ability actually hit a target (as some costs are only spent on successful attempts)
	auto DetermineIfAbilityHitTarget = [&]()
	{
		if (ActorInfo->IsNetAuthority())
		{
			if (UIMGAbilitySystemComponent* ASC = Cast<UIMGAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get()))
			{
				FGameplayAbilityTargetDataHandle TargetData;
				ASC->GetAbilityTargetData(Handle, ActivationInfo, TargetData);
				for (int32 TargetDataIdx = 0; TargetDataIdx < TargetData.Data.Num(); ++TargetDataIdx)
				{
					if (UAbilitySystemBlueprintLibrary::TargetDataHasHitResult(TargetData, TargetDataIdx))
					{
						return true;
					}
				}
			}
		}

		return false;
	};

	// Pay any additional costs
	bool bAbilityHitTarget = false;
	bool bHasDeterminedIfAbilityHitTarget = false;
	for (const TObjectPtr<UIMGAbilityCost>& AdditionalCost : AdditionalCosts)
	{
		if (AdditionalCost != nullptr)
		{
			if (AdditionalCost->ShouldOnlyApplyCostOnHit())
			{
				if (!bHasDeterminedIfAbilityHitTarget)
				{
					bAbilityHitTarget = DetermineIfAbilityHitTarget();
					bHasDeterminedIfAbilityHitTarget = true;
				}

				if (!bAbilityHitTarget)
				{
					continue;
				}
			}

			AdditionalCost->ApplyCost(this, Handle, ActorInfo, ActivationInfo);
		}
	}
}

FGameplayEffectContextHandle UIMGGameplayAbility::MakeEffectContext(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo) const
{
	FGameplayEffectContextHandle ContextHandle = Super::MakeEffectContext(Handle, ActorInfo);

	FIMGGameplayEffectContext* EffectContext = FIMGGameplayEffectContext::ExtractEffectContext(ContextHandle);
	check(EffectContext);

	check(ActorInfo);

	AActor* EffectCauser = nullptr;
	const IIMGAbilitySourceInterface* AbilitySource = nullptr;
	float SourceLevel = 0.0f;
	GetAbilitySource(Handle, ActorInfo, /*out*/ SourceLevel, /*out*/ AbilitySource, /*out*/ EffectCauser);

	UObject* SourceObject = GetSourceObject(Handle, ActorInfo);

	AActor* Instigator = ActorInfo ? ActorInfo->OwnerActor.Get() : nullptr;

	EffectContext->SetAbilitySource(AbilitySource, SourceLevel);
	EffectContext->AddInstigator(Instigator, EffectCauser);
	EffectContext->AddSourceObject(SourceObject);

	return ContextHandle;
}

void UIMGGameplayAbility::ApplyAbilityTagsToGameplayEffectSpec(FGameplayEffectSpec& Spec, FGameplayAbilitySpec* AbilitySpec) const
{
	Super::ApplyAbilityTagsToGameplayEffectSpec(Spec, AbilitySpec);

	if (const FHitResult* HitResult = Spec.GetContext().GetHitResult())
	{
		if (const UPhysicalMaterialWithTags* PhysMatWithTags = Cast<const UPhysicalMaterialWithTags>(HitResult->PhysMaterial.Get()))
		{
			Spec.CapturedTargetTags.GetSpecTags().AppendTags(PhysMatWithTags->Tags);
		}
	}
}

void UIMGGameplayAbility::OnPawnAvatarSet()
{
	K2_OnPawnAvatarSet();
}

void UIMGGameplayAbility::GetAbilitySource(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, float& OutSourceLevel, const IIMGAbilitySourceInterface*& OutAbilitySource, AActor*& OutEffectCauser) const
{
	OutSourceLevel = 0.0f;
	OutAbilitySource = nullptr;
	OutEffectCauser = nullptr;

	OutEffectCauser = ActorInfo->AvatarActor.Get();

	// If we were added by something that's an ability info source, use it
	UObject* SourceObject = GetSourceObject(Handle, ActorInfo);

	OutAbilitySource = Cast<IIMGAbilitySourceInterface>(SourceObject);
}

bool UIMGGameplayAbility::CanChangeActivationGroup(EIMGAbilityActivationGroup NewGroup) const
{
	if (!IsInstantiated() || !IsActive())
	{
		return false;
	}

	if (ActivationGroup == NewGroup)
	{
		return true;
	}

	UIMGAbilitySystemComponent* IMGASC = GetIMGAbilitySystemComponentFromActorInfo();
	check(IMGASC);

	if ((ActivationGroup != EIMGAbilityActivationGroup::Exclusive_Blocking) && IMGASC->IsActivationGroupBlocked(NewGroup))
	{
		// This ability can't change groups if it's blocked (unless it is the one doing the blocking).
		return false;
	}

	if ((NewGroup == EIMGAbilityActivationGroup::Exclusive_Replaceable) && !CanBeCanceled())
	{
		// This ability can't become replaceable if it can't be canceled.
		return false;
	}

	return true;
}

bool UIMGGameplayAbility::ChangeActivationGroup(EIMGAbilityActivationGroup NewGroup)
{
	ENSURE_ABILITY_IS_INSTANTIATED_OR_RETURN(ChangeActivationGroup, false);

	if (!CanChangeActivationGroup(NewGroup))
	{
		return false;
	}

	if (ActivationGroup != NewGroup)
	{
		UIMGAbilitySystemComponent* IMGASC = GetIMGAbilitySystemComponentFromActorInfo();
		check(IMGASC);

		IMGASC->RemoveAbilityFromActivationGroup(ActivationGroup, this);
		IMGASC->AddAbilityToActivationGroup(NewGroup, this);

		ActivationGroup = NewGroup;
	}

	return true;
}
