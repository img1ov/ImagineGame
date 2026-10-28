

#include "AbilitySystem/IMGAbilitySystemComponent.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"
#include "AbilitySystem/IMGGlobalAbilitySystem.h"
#include "System/IMGAssetManager.h"
#include "System/IMGGameData.h"

#include "IMGLogChannels.h"
#include "AbilitySystem/IMGAbilityTagRelationshipMapping.h"
#include "AbilitySystem/Abilities/IMGGameplayAbility.h"
#include "Abilities/GameplayAbility.h"
#include "Abilities/GameplayAbilityRepAnimMontage.h"
#include "Animation/AnimMontage.h"
#include "Animation/IMGAnimInstance.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGAbilitySystemComponent)

UE_DEFINE_GAMEPLAY_TAG(TAG_Gameplay_AbilityInputBlocked, "Gameplay.AbilityInputBlocked");

UIMGAbilitySystemComponent::UIMGAbilitySystemComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	FMemory::Memset(ActivationGroupCounts, 0, sizeof(ActivationGroupCounts));
	SetMontageRepAnimPositionMethod(ERepAnimPositionMethod::Position);
}

UIMGAbilitySystemComponent::~UIMGAbilitySystemComponent() = default;

void UIMGAbilitySystemComponent::OnRegister()
{
	Super::OnRegister();
	bSuspendingReplicatedMontage = false;
	RetryReplicatedMontage();
}

void UIMGAbilitySystemComponent::OnUnregister()
{
	bSuspendingReplicatedMontage = true;
	++ReplicatedMontageAvatarGeneration;
	++ReplicatedMontagePrerequisiteRevision;
	ReadyReplicatedMontage = FReplicatedMontageInstance();
	ReadyReplicatedMontageLogicalStartTime = -1.f;
	ClearReplicatedMontageWait();
	if (UWorld* World = GetWorld()) { World->GetTimerManager().ClearTimer(ReplicatedMontageRetryTimer); }
	ReplicatedMontageRetryTimer.Invalidate();
	Super::OnUnregister();
}

void UIMGAbilitySystemComponent::InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor)
{
	FGameplayAbilityActorInfo* ActorInfo = AbilityActorInfo.Get();
	check(ActorInfo);
	check(InOwnerActor);

	const bool bHasNewPawnAvatar = Cast<APawn>(InAvatarActor) && (InAvatarActor != ActorInfo->AvatarActor);
	if (InAvatarActor != ActorInfo->AvatarActor)
	{
		const bool bLeavingAvatar = !ActorInfo->AvatarActor.IsExplicitlyNull();
		ResetReplicatedMontagePrerequisites();
		// Keep data received before the first avatar, but never carry a previous avatar's pending playback across the handoff.
		if (bLeavingAvatar) { bPendingMontageRep = false; }
	}
	const bool bShouldDeferReplicatedMontage = InAvatarActor
		&& InAvatarActor->GetLocalRole() == ROLE_SimulatedProxy;

	{
		TGuardValue<bool> DeferReplicatedMontage(
			bDeferringReplicatedMontageForActorInfoInitialization, bShouldDeferReplicatedMontage);
		Super::InitAbilityActorInfo(InOwnerActor, InAvatarActor);
	}
	SetMontageRepAnimPositionMethod(ERepAnimPositionMethod::Position);
	if (bShouldDeferReplicatedMontage && bPendingMontageRep)
	{
		RetryReplicatedMontage();
	}

	if (bHasNewPawnAvatar)
	{
		// Notify all abilities that a new pawn avatar has been set
		for (const FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
		{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
			ensureMsgf(AbilitySpec.Ability && AbilitySpec.Ability->GetInstancingPolicy() != EGameplayAbilityInstancingPolicy::NonInstanced, TEXT("InitAbilityActorInfo: All Abilities should be Instanced (NonInstanced is being deprecated due to usability issues)."));
PRAGMA_ENABLE_DEPRECATION_WARNINGS

			TArray<UGameplayAbility*> Instances = AbilitySpec.GetAbilityInstances();
			for (UGameplayAbility* AbilityInstance : Instances)
			{
				UIMGGameplayAbility* IMGAbilityInstance = Cast<UIMGGameplayAbility>(AbilityInstance);
				if (IMGAbilityInstance)
				{
					// Ability instances may be missing for replays
					IMGAbilityInstance->OnPawnAvatarSet();
				}
			}
		}

		// Register with the global system once we actually have a pawn avatar. We wait until this time since some globally-applied effects may require an avatar.
		if (UIMGGlobalAbilitySystem* GlobalAbilitySystem = UWorld::GetSubsystem<UIMGGlobalAbilitySystem>(GetWorld()))
		{
			GlobalAbilitySystem->RegisterASC(this);
		}

		if (UIMGAnimInstance* IMGAnimInst = Cast<UIMGAnimInstance>(ActorInfo->GetAnimInstance()))
		{
			IMGAnimInst->InitializeWithAbilitySystem(this);
		}

		TryActivateAbilitiesOnSpawn();
	}
}

bool UIMGAbilitySystemComponent::FReplicatedMontageInstance::Matches(
	const AActor* InAvatar, const UAnimMontage* InMontage, uint8 InPlayInstanceId) const
{
	return InAvatar && InMontage && Avatar.Get() == InAvatar && Montage.Get() == InMontage
		&& PlayInstanceId == InPlayInstanceId;
}

void UIMGAbilitySystemComponent::ClearActorInfo()
{
	ResetReplicatedMontagePrerequisites();
	bPendingMontageRep = false;
	Super::ClearActorInfo();
}

FDelegateHandle UIMGAbilitySystemComponent::RegisterReplicatedMontagePrerequisite(
	AActor* Avatar, FName Name, FIMGReplicatedMontagePrerequisite Prerequisite)
{
	if (bEndingReplicatedMontage || !IsValid(Avatar) || Avatar->IsActorBeingDestroyed() || Avatar != GetAvatarActor()
		|| Name.IsNone() || !Prerequisite.IsBound() || !IsValid(Prerequisite.GetUObject()))
	{
		return FDelegateHandle();
	}
	for (FReplicatedMontagePrerequisiteEntry& Entry : ReplicatedMontagePrerequisites)
	{
		if (Entry.Avatar == Avatar && Entry.Name == Name && Entry.Callback.GetUObject() == Prerequisite.GetUObject())
		{
			Entry.Callback = MoveTemp(Prerequisite);
			++ReplicatedMontagePrerequisiteRevision;
			return Entry.Handle;
		}
	}
	FReplicatedMontagePrerequisiteEntry& Entry = ReplicatedMontagePrerequisites.AddDefaulted_GetRef();
	Entry.Handle = FDelegateHandle(FDelegateHandle::GenerateNewHandle);
	Entry.Avatar = Avatar;
	Entry.Name = Name;
	Entry.Callback = MoveTemp(Prerequisite);
	++ReplicatedMontagePrerequisiteRevision;
	return Entry.Handle;
}

void UIMGAbilitySystemComponent::UnregisterReplicatedMontagePrerequisite(FDelegateHandle Handle, bool bDiscardPendingInstance)
{
	const int32 Index = ReplicatedMontagePrerequisites.IndexOfByPredicate(
		[Handle](const FReplicatedMontagePrerequisiteEntry& Entry) { return Entry.Handle == Handle; });
	if (Index == INDEX_NONE) { return; }
	const FReplicatedMontagePrerequisiteEntry Removed = ReplicatedMontagePrerequisites[Index];
	ReplicatedMontagePrerequisites.RemoveAt(Index);
	++ReplicatedMontagePrerequisiteRevision;
	const FGameplayAbilityRepAnimMontage& RepMontage = GetRepAnimMontageInfo();
	const FReplicatedMontageInstance Instance{GetAvatarActor(), RepMontage.GetAnimMontage(), RepMontage.PlayInstanceId};
	const uint32 Generation = ReplicatedMontageAvatarGeneration;
	if (bDiscardPendingInstance && !bEndingReplicatedMontage && IsValid(GetAvatarActor())
		&& (bPendingMontageRep || bProcessingReplicatedMontage) && Removed.Avatar == GetAvatarActor())
	{
		bool bApplicable = Removed.LastApplicableInstance.Matches(GetAvatarActor(), Instance.Montage.Get(), Instance.PlayInstanceId);
		if (!bApplicable && Removed.Callback.IsBound() && Instance.Montage.IsValid())
		{
			// Native OnRep does not query readiness until the AnimInstance exists. Classify before releasing that pending start.
			TGuardValue<bool> Processing(bProcessingReplicatedMontage, true);
			bApplicable = Removed.Callback.Execute(Instance.Montage.Get(), Instance.PlayInstanceId).Readiness
				!= EIMGReplicatedMontageReadiness::NotApplicable;
		}
		if (bApplicable && Generation == ReplicatedMontageAvatarGeneration
			&& Instance.Matches(GetAvatarActor(), GetRepAnimMontageInfo().GetAnimMontage(), GetRepAnimMontageInfo().PlayInstanceId))
		{
			DiscardedReplicatedMontage = Instance;
		}
	}
	RetryReplicatedMontage();
}

bool UIMGAbilitySystemComponent::CanRetryReplicatedMontage() const
{
	return !bEndingReplicatedMontage && !bSuspendingReplicatedMontage && IsValid(GetAvatarActor())
		&& !GetAvatarActor()->IsActorBeingDestroyed() && !IsOwnerActorAuthoritative()
		&& AbilityActorInfo.IsValid() && !AbilityActorInfo->IsLocallyControlled();
}

void UIMGAbilitySystemComponent::RetryReplicatedMontage()
{
	if ((!bPendingMontageRep && !bProcessingReplicatedMontage) || !CanRetryReplicatedMontage()
		|| ReplicatedMontageRetryTimer.IsValid()) { return; }
	if (UWorld* World = GetWorld())
	{
		const uint32 Generation = ReplicatedMontageAvatarGeneration;
		ReplicatedMontageRetryTimer = World->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateWeakLambda(this, [this, Generation]()
			{
				if (Generation != ReplicatedMontageAvatarGeneration) { return; }
				ReplicatedMontageRetryTimer.Invalidate();
				if (bPendingMontageRep && CanRetryReplicatedMontage()) { OnRep_ReplicatedAnimMontage(); }
			}));
	}
}

void UIMGAbilitySystemComponent::ClearReplicatedMontageWait()
{
	WaitingReplicatedMontage = FReplicatedMontageInstance();
	ReplicatedMontageWaitReason = NAME_None;
	if (UWorld* World = GetWorld()) { World->GetTimerManager().ClearTimer(ReplicatedMontageWaitTimer); }
	ReplicatedMontageWaitTimer.Invalidate();
}

void UIMGAbilitySystemComponent::WaitForReplicatedMontage(const FReplicatedMontageInstance& Instance, FName Reason)
{
	if (WaitingReplicatedMontage.Matches(Instance.Avatar.Get(), Instance.Montage.Get(), Instance.PlayInstanceId)
		&& ReplicatedMontageWaitReason == Reason) { return; }
	ClearReplicatedMontageWait();
	WaitingReplicatedMontage = Instance;
	ReplicatedMontageWaitReason = Reason;
	UE_LOG(LogIMGAbilitySystem, Verbose, TEXT("Montage prerequisite waiting: Avatar=[%s] Montage=[%s] Instance=%u Reason=%s"),
		*GetNameSafe(Instance.Avatar.Get()), *GetNameSafe(Instance.Montage.Get()), Instance.PlayInstanceId, *Reason.ToString());
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(ReplicatedMontageWaitTimer, this,
			&ThisClass::ReportReplicatedMontageWait, 5.f, false);
	}
}

void UIMGAbilitySystemComponent::ReportReplicatedMontageWait()
{
	const FGameplayAbilityRepAnimMontage& RepMontage = GetRepAnimMontageInfo();
	if (bPendingMontageRep && CanRetryReplicatedMontage() && WaitingReplicatedMontage.Matches(
		GetAvatarActor(), RepMontage.GetAnimMontage(), RepMontage.PlayInstanceId))
	{
		UE_LOG(LogIMGAbilitySystem, Warning, TEXT("Montage prerequisite still waiting after 5s: Avatar=[%s] Montage=[%s] Instance=%u Reason=%s"),
			*GetNameSafe(GetAvatarActor()), *GetNameSafe(RepMontage.GetAnimMontage()),
			RepMontage.PlayInstanceId, *ReplicatedMontageWaitReason.ToString());
		RetryReplicatedMontage();
	}
}

void UIMGAbilitySystemComponent::ResetReplicatedMontagePrerequisites()
{
	++ReplicatedMontageAvatarGeneration;
	++ReplicatedMontagePrerequisiteRevision;
	ReplicatedMontagePrerequisites.Reset();
	ReadyReplicatedMontage = FReplicatedMontageInstance();
	DiscardedReplicatedMontage = FReplicatedMontageInstance();
	ReadyReplicatedMontageLogicalStartTime = -1.f;
	ClearReplicatedMontageWait();
	if (UWorld* World = GetWorld()) { World->GetTimerManager().ClearTimer(ReplicatedMontageRetryTimer); }
	ReplicatedMontageRetryTimer.Invalidate();
}

void UIMGAbilitySystemComponent::OnRep_ReplicatedAnimMontage()
{
	if (bEndingReplicatedMontage) { return; }
	if (bSuspendingReplicatedMontage)
	{
		bPendingMontageRep = true;
		return;
	}
	if (bProcessingReplicatedMontage)
	{
		bPendingMontageRep = true;
		RetryReplicatedMontage();
		return;
	}
	TGuardValue<bool> Processing(bProcessingReplicatedMontage, true);
	const FGameplayAbilityRepAnimMontage& RepMontage = GetRepAnimMontageInfo();
	const UAnimMontage* Montage = RepMontage.GetAnimMontage();
	if (!DiscardedReplicatedMontage.Matches(GetAvatarActor(), Montage, RepMontage.PlayInstanceId))
	{
		DiscardedReplicatedMontage = FReplicatedMontageInstance();
	}
	if (CanRetryReplicatedMontage() && Montage &&
		(DiscardedReplicatedMontage.Matches(GetAvatarActor(), Montage, RepMontage.PlayInstanceId)
		|| (RepMontage.IsStopped && (LocalAnimMontageInfo.AnimMontage != Montage
			|| LocalAnimMontageInfo.PlayInstanceId != RepMontage.PlayInstanceId))))
	{
		// Native OnRep can start a new instance before processing its stop bit. Never start a terminal instance.
		DiscardedReplicatedMontage = {GetAvatarActor(), Montage, RepMontage.PlayInstanceId};
		bPendingMontageRep = false;
		ClearReplicatedMontageWait();
		ReadyReplicatedMontage = FReplicatedMontageInstance();
		ReadyReplicatedMontageLogicalStartTime = -1.f;
		if (RepMontage.IsStopped) { CurrentMontageStop(RepMontage.BlendTime); }
		return;
	}
	const uint32 Generation = ReplicatedMontageAvatarGeneration;
	Super::OnRep_ReplicatedAnimMontage();
	if (bEndingReplicatedMontage || Generation != ReplicatedMontageAvatarGeneration)
	{
		return;
	}
	if (DiscardedReplicatedMontage.Matches(GetAvatarActor(), RepMontage.GetAnimMontage(), RepMontage.PlayInstanceId))
	{
		bPendingMontageRep = false;
		ClearReplicatedMontageWait();
		if (LocalAnimMontageInfo.AnimMontage == RepMontage.GetAnimMontage()
			&& LocalAnimMontageInfo.PlayInstanceId == RepMontage.PlayInstanceId)
		{
			CurrentMontageStop(RepMontage.BlendTime);
		}
	}
	if (!bPendingMontageRep) { ClearReplicatedMontageWait(); }
	ReadyReplicatedMontage = FReplicatedMontageInstance();
	ReadyReplicatedMontageLogicalStartTime = -1.f;
}

bool UIMGAbilitySystemComponent::GetActiveReplicatedMontagePlayInstanceId(
	const UAnimMontage* Montage, uint8& OutPlayInstanceId) const
{
	const FGameplayAbilityRepAnimMontage& RepMontage = GetRepAnimMontageInfo();
	if (!Montage || RepMontage.IsStopped != 0 || RepMontage.GetAnimMontage() != Montage)
	{
		return false;
	}

	OutPlayInstanceId = RepMontage.PlayInstanceId;
	return true;
}

bool UIMGAbilitySystemComponent::IsReadyForReplicatedMontage()
{
	ReadyReplicatedMontage = FReplicatedMontageInstance();
	ReadyReplicatedMontageLogicalStartTime = -1.f;
	if (!Super::IsReadyForReplicatedMontage())
	{
		return false;
	}
	if (bDeferringReplicatedMontageForActorInfoInitialization || bSuspendingReplicatedMontage)
	{
		return false;
	}
	if (IsOwnerActorAuthoritative() || !AbilityActorInfo.IsValid() || AbilityActorInfo->IsLocallyControlled())
	{
		return true;
	}

	const FGameplayAbilityRepAnimMontage& RepMontage = GetRepAnimMontageInfo();
	const UAnimMontage* Montage = RepMontage.GetAnimMontage();
	if (!Montage || RepMontage.IsStopped) { return true; }
	const FReplicatedMontageInstance Instance{GetAvatarActor(), Montage, RepMontage.PlayInstanceId};
	if (!IsValid(GetAvatarActor()) || GetAvatarActor()->IsActorBeingDestroyed() || bEndingReplicatedMontage) { return false; }
	if (DiscardedReplicatedMontage.Matches(Instance.Avatar.Get(), Montage, Instance.PlayInstanceId)) { return false; }

	for (int32 Index = ReplicatedMontagePrerequisites.Num() - 1; Index >= 0; --Index)
	{
		const FReplicatedMontagePrerequisiteEntry& Entry = ReplicatedMontagePrerequisites[Index];
		if (Entry.Avatar != GetAvatarActor() || !Entry.Callback.IsBound())
		{
			if (Entry.LastApplicableInstance.Matches(GetAvatarActor(), Montage, Instance.PlayInstanceId))
			{
				DiscardedReplicatedMontage = Instance;
			}
			ReplicatedMontagePrerequisites.RemoveAt(Index);
			++ReplicatedMontagePrerequisiteRevision;
		}
	}
	if (DiscardedReplicatedMontage.Matches(GetAvatarActor(), Montage, Instance.PlayInstanceId)) { return false; }

	const uint32 Revision = ReplicatedMontagePrerequisiteRevision;
	const TArray<FReplicatedMontagePrerequisiteEntry> Snapshot = ReplicatedMontagePrerequisites;
	FName WaitingReason;
	FName StartTimeProvider;
	FName ConflictingProvider;
	bool bInvalidStartTime = false;
	float LogicalStartTime = -1.f;
	for (const FReplicatedMontagePrerequisiteEntry& Entry : Snapshot)
	{
		if (!Entry.Callback.IsBound())
		{
			RetryReplicatedMontage();
			return false;
		}
		// Teardown from inside a callback abandons this evaluation unless the caller explicitly hands it off.
		if (FReplicatedMontagePrerequisiteEntry* Registered = ReplicatedMontagePrerequisites.FindByPredicate(
			[&Entry](const FReplicatedMontagePrerequisiteEntry& Item) { return Item.Handle == Entry.Handle; }))
		{
			Registered->LastApplicableInstance = Instance;
		}
		const FIMGReplicatedMontagePrerequisiteResult Result = Entry.Callback.Execute(Montage, Instance.PlayInstanceId);
		if (!CanRetryReplicatedMontage() || Revision != ReplicatedMontagePrerequisiteRevision
			|| !Instance.Matches(GetAvatarActor(), GetRepAnimMontageInfo().GetAnimMontage(), GetRepAnimMontageInfo().PlayInstanceId)
			|| GetRepAnimMontageInfo().IsStopped)
		{
			RetryReplicatedMontage();
			return false;
		}
		if (FReplicatedMontagePrerequisiteEntry* Registered = ReplicatedMontagePrerequisites.FindByPredicate(
			[&Entry](const FReplicatedMontagePrerequisiteEntry& Item) { return Item.Handle == Entry.Handle; }))
		{
			Registered->LastApplicableInstance = Result.Readiness == EIMGReplicatedMontageReadiness::NotApplicable
				? FReplicatedMontageInstance() : Instance;
		}
		if (Result.Readiness == EIMGReplicatedMontageReadiness::NotApplicable) { continue; }
		if (Result.Readiness == EIMGReplicatedMontageReadiness::Discard)
		{
			UE_LOG(LogIMGAbilitySystem, Verbose, TEXT("Montage prerequisite discarded: Provider=%s Avatar=[%s] Montage=[%s] Instance=%u Reason=%s"),
				*Entry.Name.ToString(), *GetNameSafe(GetAvatarActor()), *GetNameSafe(Montage), Instance.PlayInstanceId, *Result.Reason.ToString());
			DiscardedReplicatedMontage = Instance;
			return false;
		}
		if (Result.Readiness != EIMGReplicatedMontageReadiness::Ready)
		{
			if (WaitingReason.IsNone()) { WaitingReason = Result.Reason.IsNone() ? Entry.Name : Result.Reason; }
			continue;
		}
		if (Result.LogicalStartTime == -1.f) { continue; }
		if (!FMath::IsFinite(Result.LogicalStartTime) || Result.LogicalStartTime < 0.f
			|| Result.LogicalStartTime >= Montage->GetPlayLength())
		{
			bInvalidStartTime = true;
			continue;
		}
		if (!StartTimeProvider.IsNone())
		{
			ConflictingProvider = Entry.Name;
			continue;
		}
		StartTimeProvider = Entry.Name;
		LogicalStartTime = Result.LogicalStartTime;
	}
	if (!ConflictingProvider.IsNone())
	{
		if (!WaitingReplicatedMontage.Matches(GetAvatarActor(), Montage, Instance.PlayInstanceId)
			|| ReplicatedMontageWaitReason != TEXT("ConflictingStartTimeProviders"))
		{
			UE_LOG(LogIMGAbilitySystem, Error, TEXT("Conflicting montage start-time providers: %s and %s. Avatar=[%s] Montage=[%s] Instance=%u"),
				*StartTimeProvider.ToString(), *ConflictingProvider.ToString(), *GetNameSafe(GetAvatarActor()), *GetNameSafe(Montage), Instance.PlayInstanceId);
		}
		WaitingReason = TEXT("ConflictingStartTimeProviders");
	}
	else if (bInvalidStartTime)
	{
		WaitingReason = TEXT("InvalidLogicalStartTime");
	}
	if (!WaitingReason.IsNone())
	{
		WaitForReplicatedMontage(Instance, WaitingReason);
		return false;
	}
	ClearReplicatedMontageWait();
	ReadyReplicatedMontage = Instance;
	ReadyReplicatedMontageLogicalStartTime = LogicalStartTime;
	return true;
}

bool UIMGAbilitySystemComponent::MatchesReplicatedMontageInstance(
	const UAnimMontage* Montage, uint8 PlayInstanceId) const
{
	const FGameplayAbilityRepAnimMontage& RepMontage = GetRepAnimMontageInfo();
	return Montage && RepMontage.GetAnimMontage() == Montage && RepMontage.PlayInstanceId == PlayInstanceId
		&& LocalAnimMontageInfo.AnimMontage == Montage && LocalAnimMontageInfo.PlayInstanceId == PlayInstanceId;
}

float UIMGAbilitySystemComponent::PlayMontageSimulated(
	UAnimMontage* Montage, const float InPlayRate, const FName StartSectionName)
{
	if (bEndingReplicatedMontage || bSuspendingReplicatedMontage) { return -1.f; }
	const float LogicalStartTime = ReadyReplicatedMontageLogicalStartTime;
	const bool bMatchingInstance = ReadyReplicatedMontage.Matches(
		GetAvatarActor(), Montage, GetRepAnimMontageInfo().PlayInstanceId);
	ReadyReplicatedMontage = FReplicatedMontageInstance();
	ReadyReplicatedMontageLogicalStartTime = -1.f;
	if (!bMatchingInstance || !FMath::IsFinite(LogicalStartTime) || LogicalStartTime < 0.f)
	{
		return Super::PlayMontageSimulated(Montage, InPlayRate, StartSectionName);
	}

	UAnimInstance* AnimInstance = AbilityActorInfo.IsValid() ? AbilityActorInfo->GetAnimInstance() : nullptr;
	if (!AnimInstance || !Montage)
	{
		return -1.f;
	}

	float PresentationStartTime = LogicalStartTime;
	const FGameplayAbilityRepAnimMontage& RepMontage = GetRepAnimMontageInfo();
	if (RepMontage.GetAnimMontage() == Montage && RepMontage.IsStopped == 0
		&& !RepMontage.SkipPositionCorrection && FMath::IsFinite(RepMontage.Position)
		&& RepMontage.Position >= LogicalStartTime)
	{
		PresentationStartTime = RepMontage.Position;
	}

	const uint32 Generation = ReplicatedMontageAvatarGeneration;
	const float Duration = AnimInstance->Montage_Play(Montage, InPlayRate,
		EMontagePlayReturnType::MontageLength, PresentationStartTime);
	if (Duration > 0.f && Generation == ReplicatedMontageAvatarGeneration && !bEndingReplicatedMontage)
	{
		LocalAnimMontageInfo.AnimMontage = Montage;
	}
	return Duration;
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
			if (UIMGGameplayAbility* IMGAbility = Cast<UIMGGameplayAbility>(AbilityInstance))
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
					const UIMGGameplayAbility* IMGAbilityCDO = Cast<UIMGGameplayAbility>(AbilitySpec->Ability);
					if (IMGAbilityCDO &&
						IMGAbilityCDO->GetActivationPolicy() == EIMGAbilityActivationPolicy::OnInputTriggered)
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
	// Super broadcasts rejection and may synchronously end the predicted montage/ability.
	UE_LOG(LogIMGAbilitySystem, Warning,
		TEXT("[PredictionRejected] Ability=%s Owner=%s Avatar=%s Role=%d Spec=%s PredKey=%d; server rejected activation (details in authority failure log)"),
		*GetNameSafe(AbilitySpec ? AbilitySpec->Ability.Get() : nullptr), *GetNameSafe(GetOwnerActor()),
		*GetNameSafe(GetAvatarActor()), GetAvatarActor() ? static_cast<int32>(GetAvatarActor()->GetLocalRole()) : -1,
		*AbilityToActivate.ToString(), PredictionKey);

	Super::ClientActivateAbilityFailed_Implementation(AbilityToActivate, PredictionKey);
}





void UIMGAbilitySystemComponent::NotifyAbilityCommit(UGameplayAbility* Ability)
{
	Super::NotifyAbilityCommit(Ability);
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






void UIMGAbilitySystemComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndingReplicatedMontage = true;
	ResetReplicatedMontagePrerequisites();
	bPendingMontageRep = false;
	bDeferringReplicatedMontageForActorInfoInitialization = false;
	if (UIMGGlobalAbilitySystem* GlobalAbilitySystem = UWorld::GetSubsystem<UIMGGlobalAbilitySystem>(GetWorld()))
	{
		GlobalAbilitySystem->UnregisterASC(this);
	}

	Super::EndPlay(EndPlayReason);
}

void UIMGAbilitySystemComponent::NotifyAbilityEnded(FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability, bool bWasCancelled)
{
	Super::NotifyAbilityEnded(Handle, Ability, bWasCancelled);

	if (UIMGGameplayAbility* IMGAbility = Cast<UIMGGameplayAbility>(Ability))
	{
		RemoveAbilityFromActivationGroup(IMGAbility->GetActivationGroup(), IMGAbility);
	}
}

void UIMGAbilitySystemComponent::HandleChangeAbilityCanBeCanceled(const FGameplayTagContainer& AbilityTags, UGameplayAbility* RequestingAbility, bool bCanBeCanceled)
{
	Super::HandleChangeAbilityCanBeCanceled(AbilityTags, RequestingAbility, bCanBeCanceled);

	//@TODO: Apply any special logic like blocking input or movement
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

void UIMGAbilitySystemComponent::AddDynamicTagGameplayEffect(const FGameplayTag& Tag)
{
	const TSubclassOf<UGameplayEffect> DynamicTagGE = UIMGAssetManager::GetSubclass(UIMGGameData::Get().DynamicTagGameplayEffect);
	if (!DynamicTagGE)
	{
		UE_LOG(LogIMGAbilitySystem, Warning, TEXT("AddDynamicTagGameplayEffect: Unable to find DynamicTagGameplayEffect [%s]."), *UIMGGameData::Get().DynamicTagGameplayEffect.GetAssetName());
		return;
	}

	const FGameplayEffectSpecHandle SpecHandle = MakeOutgoingSpec(DynamicTagGE, 1.0f, MakeEffectContext());
	FGameplayEffectSpec* Spec = SpecHandle.Data.Get();

	if (!Spec)
	{
		UE_LOG(LogIMGAbilitySystem, Warning, TEXT("AddDynamicTagGameplayEffect: Unable to make outgoing spec for [%s]."), *GetNameSafe(DynamicTagGE));
		return;
	}

	Spec->DynamicGrantedTags.AddTag(Tag);

	ApplyGameplayEffectSpecToSelf(*Spec);
}

void UIMGAbilitySystemComponent::RemoveDynamicTagGameplayEffect(const FGameplayTag& Tag)
{
	const TSubclassOf<UGameplayEffect> DynamicTagGE = UIMGAssetManager::GetSubclass(UIMGGameData::Get().DynamicTagGameplayEffect);
	if (!DynamicTagGE)
	{
		UE_LOG(LogIMGAbilitySystem, Warning, TEXT("RemoveDynamicTagGameplayEffect: Unable to find gameplay effect [%s]."), *UIMGGameData::Get().DynamicTagGameplayEffect.GetAssetName());
		return;
	}

	FGameplayEffectQuery Query = FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Tag));
	Query.EffectDefinition = DynamicTagGE;

	RemoveActiveEffects(Query);
}

void UIMGAbilitySystemComponent::GetAbilityTargetData(const FGameplayAbilitySpecHandle AbilityHandle, FGameplayAbilityActivationInfo ActivationInfo, FGameplayAbilityTargetDataHandle& OutTargetDataHandle)
{
	TSharedPtr<FAbilityReplicatedDataCache> ReplicatedData = AbilityTargetDataMap.Find(FGameplayAbilitySpecHandleAndPredictionKey(AbilityHandle, ActivationInfo.GetActivationPredictionKey()));
	if (ReplicatedData.IsValid())
	{
		OutTargetDataHandle = ReplicatedData->TargetData;
	}
}
