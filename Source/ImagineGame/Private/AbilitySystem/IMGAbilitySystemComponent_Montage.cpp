#include "AbilitySystem/IMGAbilitySystemComponent.h"
#include "Abilities/GameplayAbilityRepAnimMontage.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "IMGLogChannels.h"
#include "Templates/UnrealTemplate.h"
#include "TimerManager.h"

bool UIMGAbilitySystemComponent::FReplicatedMontageInstance::Matches(
	const AActor* InAvatar, const UAnimMontage* InMontage, uint8 InPlayInstanceId) const
{
	return InAvatar && InMontage && Avatar.Get() == InAvatar && Montage.Get() == InMontage
		&& PlayInstanceId == InPlayInstanceId;
}

FDelegateHandle UIMGAbilitySystemComponent::RegisterReplicatedMontagePrerequisite(
	AActor* Avatar, FName Name, FIMGReplicatedMontagePrerequisite Prerequisite)
{
	if (MontageGate.bEnding || !IsValid(Avatar) || Avatar->IsActorBeingDestroyed() || Avatar != GetAvatarActor()
		|| Name.IsNone() || !Prerequisite.IsBound() || !IsValid(Prerequisite.GetUObject()))
	{
		return FDelegateHandle();
	}
	for (FReplicatedMontagePrerequisiteEntry& Entry : MontageGate.Prerequisites)
	{
		if (Entry.Avatar == Avatar && Entry.Name == Name && Entry.Callback.GetUObject() == Prerequisite.GetUObject())
		{
			Entry.Callback = MoveTemp(Prerequisite);
			++MontageGate.Revision;
			return Entry.Handle;
		}
	}
	FReplicatedMontagePrerequisiteEntry& Entry = MontageGate.Prerequisites.AddDefaulted_GetRef();
	Entry.Handle = FDelegateHandle(FDelegateHandle::GenerateNewHandle);
	Entry.Avatar = Avatar;
	Entry.Name = Name;
	Entry.Callback = MoveTemp(Prerequisite);
	++MontageGate.Revision;
	return Entry.Handle;
}

void UIMGAbilitySystemComponent::UnregisterReplicatedMontagePrerequisite(FDelegateHandle Handle, bool bDiscardPendingInstance)
{
	const int32 Index = MontageGate.Prerequisites.IndexOfByPredicate(
		[Handle](const FReplicatedMontagePrerequisiteEntry& Entry) { return Entry.Handle == Handle; });
	if (Index == INDEX_NONE)
	{
		return;
	}
	const FReplicatedMontagePrerequisiteEntry Removed = MontageGate.Prerequisites[Index];
	MontageGate.Prerequisites.RemoveAt(Index);
	++MontageGate.Revision;
	const FGameplayAbilityRepAnimMontage& RepMontage = GetRepAnimMontageInfo();
	const FReplicatedMontageInstance Instance{GetAvatarActor(), RepMontage.GetAnimMontage(), RepMontage.PlayInstanceId};
	const uint32 Generation = MontageGate.AvatarGeneration;
	if (bDiscardPendingInstance && !MontageGate.bEnding && IsValid(GetAvatarActor())
		&& (bPendingMontageRep || MontageGate.bProcessing) && Removed.Avatar == GetAvatarActor())
	{
		bool bApplicable = Removed.LastApplicableInstance.Matches(GetAvatarActor(), Instance.Montage.Get(), Instance.PlayInstanceId);
		if (!bApplicable && Removed.Callback.IsBound() && Instance.Montage.IsValid())
		{
			// Native OnRep does not query readiness until the AnimInstance exists. Classify before releasing that pending start.
			TGuardValue<bool> Processing(MontageGate.bProcessing, true);
			bApplicable = Removed.Callback.Execute(Instance.Montage.Get(), Instance.PlayInstanceId).Readiness
				!= EIMGReplicatedMontageReadiness::NotApplicable;
		}
		if (bApplicable && Generation == MontageGate.AvatarGeneration
			&& Instance.Matches(GetAvatarActor(), GetRepAnimMontageInfo().GetAnimMontage(), GetRepAnimMontageInfo().PlayInstanceId))
		{
			MontageGate.Discarded = Instance;
		}
	}
	RetryReplicatedMontage();
}

bool UIMGAbilitySystemComponent::CanRetryReplicatedMontage() const
{
	return !MontageGate.bEnding && !MontageGate.bSuspending && IsValid(GetAvatarActor())
		&& !GetAvatarActor()->IsActorBeingDestroyed() && !IsOwnerActorAuthoritative()
		&& AbilityActorInfo.IsValid() && !AbilityActorInfo->IsLocallyControlled();
}

void UIMGAbilitySystemComponent::RetryReplicatedMontage()
{
	if ((!bPendingMontageRep && !MontageGate.bProcessing) || !CanRetryReplicatedMontage()
		|| MontageGate.RetryTimer.IsValid())
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		const uint32 Generation = MontageGate.AvatarGeneration;
		MontageGate.RetryTimer = World->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateWeakLambda(this, [this, Generation]()
			{
				if (Generation != MontageGate.AvatarGeneration)
				{
					return;
				}
				MontageGate.RetryTimer.Invalidate();
				if (bPendingMontageRep && CanRetryReplicatedMontage())
				{
					OnRep_ReplicatedAnimMontage();
				}
			}));
	}
}

void UIMGAbilitySystemComponent::ClearReplicatedMontageWait()
{
	MontageGate.Waiting = FReplicatedMontageInstance();
	MontageGate.WaitReason = NAME_None;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(MontageGate.WaitTimer);
	}
	MontageGate.WaitTimer.Invalidate();
}

void UIMGAbilitySystemComponent::WaitForReplicatedMontage(const FReplicatedMontageInstance& Instance, FName Reason)
{
	if (MontageGate.Waiting.Matches(Instance.Avatar.Get(), Instance.Montage.Get(), Instance.PlayInstanceId)
		&& MontageGate.WaitReason == Reason)
	{
		return;
	}
	ClearReplicatedMontageWait();
	MontageGate.Waiting = Instance;
	MontageGate.WaitReason = Reason;
	UE_LOG(LogIMGAbilitySystem, Verbose, TEXT("Montage prerequisite waiting: Avatar=[%s] Montage=[%s] Instance=%u Reason=%s"),
		*GetNameSafe(Instance.Avatar.Get()), *GetNameSafe(Instance.Montage.Get()), Instance.PlayInstanceId, *Reason.ToString());
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(MontageGate.WaitTimer, this,
			&ThisClass::ReportReplicatedMontageWait, 5.f, false);
	}
}

void UIMGAbilitySystemComponent::ReportReplicatedMontageWait()
{
	const FGameplayAbilityRepAnimMontage& RepMontage = GetRepAnimMontageInfo();
	if (bPendingMontageRep && CanRetryReplicatedMontage() && MontageGate.Waiting.Matches(
		GetAvatarActor(), RepMontage.GetAnimMontage(), RepMontage.PlayInstanceId))
	{
		UE_LOG(LogIMGAbilitySystem, Warning, TEXT("Montage prerequisite still waiting after 5s: Avatar=[%s] Montage=[%s] Instance=%u Reason=%s"),
			*GetNameSafe(GetAvatarActor()), *GetNameSafe(RepMontage.GetAnimMontage()),
			RepMontage.PlayInstanceId, *MontageGate.WaitReason.ToString());
		RetryReplicatedMontage();
	}
}

void UIMGAbilitySystemComponent::ResetReplicatedMontagePrerequisites()
{
	++MontageGate.AvatarGeneration;
	++MontageGate.Revision;
	MontageGate.Prerequisites.Reset();
	MontageGate.ResetReady();
	MontageGate.Discarded = FReplicatedMontageInstance();
	ClearReplicatedMontageWait();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(MontageGate.RetryTimer);
	}
	MontageGate.RetryTimer.Invalidate();
}

void UIMGAbilitySystemComponent::OnRep_ReplicatedAnimMontage()
{
	if (MontageGate.bEnding)
	{
		return;
	}
	if (MontageGate.bSuspending)
	{
		bPendingMontageRep = true;
		return;
	}
	if (MontageGate.bProcessing)
	{
		bPendingMontageRep = true;
		RetryReplicatedMontage();
		return;
	}
	TGuardValue<bool> Processing(MontageGate.bProcessing, true);
	const FGameplayAbilityRepAnimMontage& RepMontage = GetRepAnimMontageInfo();
	const UAnimMontage* Montage = RepMontage.GetAnimMontage();
	if (!MontageGate.Discarded.Matches(GetAvatarActor(), Montage, RepMontage.PlayInstanceId))
	{
		MontageGate.Discarded = FReplicatedMontageInstance();
	}
	if (CanRetryReplicatedMontage() && Montage &&
		(MontageGate.Discarded.Matches(GetAvatarActor(), Montage, RepMontage.PlayInstanceId)
		|| (RepMontage.IsStopped && (LocalAnimMontageInfo.AnimMontage != Montage
			|| LocalAnimMontageInfo.PlayInstanceId != RepMontage.PlayInstanceId))))
	{
		// Native OnRep can start a new instance before processing its stop bit. Never start a terminal instance.
		MontageGate.Discarded = {GetAvatarActor(), Montage, RepMontage.PlayInstanceId};
		bPendingMontageRep = false;
		ClearReplicatedMontageWait();
		MontageGate.ResetReady();
		if (RepMontage.IsStopped)
		{
			CurrentMontageStop(RepMontage.BlendTime);
		}
		return;
	}
	const uint32 Generation = MontageGate.AvatarGeneration;
	Super::OnRep_ReplicatedAnimMontage();
	if (MontageGate.bEnding || Generation != MontageGate.AvatarGeneration)
	{
		return;
	}
	if (MontageGate.Discarded.Matches(GetAvatarActor(), RepMontage.GetAnimMontage(), RepMontage.PlayInstanceId))
	{
		bPendingMontageRep = false;
		ClearReplicatedMontageWait();
		if (LocalAnimMontageInfo.AnimMontage == RepMontage.GetAnimMontage()
			&& LocalAnimMontageInfo.PlayInstanceId == RepMontage.PlayInstanceId)
		{
			CurrentMontageStop(RepMontage.BlendTime);
		}
	}
	if (!bPendingMontageRep)
	{
		ClearReplicatedMontageWait();
	}
	MontageGate.ResetReady();
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

bool UIMGAbilitySystemComponent::GetReplicatedMontagePosition(
	const UAnimMontage* Montage, uint8 PlayInstanceId, float& OutPosition) const
{
	OutPosition = -1.f;
	const FGameplayAbilityRepAnimMontage& RepMontage = GetRepAnimMontageInfo();
	if (!Montage || RepMontage.GetAnimMontage() != Montage || RepMontage.PlayInstanceId != PlayInstanceId ||
		RepMontage.IsStopped != 0 || !RepMontage.bRepPosition || RepMontage.SkipPositionCorrection ||
		!FMath::IsFinite(RepMontage.Position) ||
		RepMontage.Position < 0.f || RepMontage.Position >= Montage->GetPlayLength())
	{
		return false;
	}
	OutPosition = RepMontage.Position;
	return true;
}

bool UIMGAbilitySystemComponent::IsReadyForReplicatedMontage()
{
	MontageGate.ResetReady();
	if (!Super::IsReadyForReplicatedMontage())
	{
		return false;
	}
	if (MontageGate.bDeferringForActorInfoInitialization || MontageGate.bSuspending)
	{
		return false;
	}
	if (IsOwnerActorAuthoritative() || !AbilityActorInfo.IsValid() || AbilityActorInfo->IsLocallyControlled())
	{
		return true;
	}

	const FGameplayAbilityRepAnimMontage& RepMontage = GetRepAnimMontageInfo();
	const UAnimMontage* Montage = RepMontage.GetAnimMontage();
	if (!Montage || RepMontage.IsStopped)
	{
		return true;
	}
	const FReplicatedMontageInstance Instance{GetAvatarActor(), Montage, RepMontage.PlayInstanceId};
	if (!IsValid(GetAvatarActor()) || GetAvatarActor()->IsActorBeingDestroyed() || MontageGate.bEnding)
	{
		return false;
	}
	if (MontageGate.Discarded.Matches(Instance.Avatar.Get(), Montage, Instance.PlayInstanceId))
	{
		return false;
	}

	for (int32 Index = MontageGate.Prerequisites.Num() - 1; Index >= 0; --Index)
	{
		const FReplicatedMontagePrerequisiteEntry& Entry = MontageGate.Prerequisites[Index];
		if (Entry.Avatar != GetAvatarActor() || !Entry.Callback.IsBound())
		{
			if (Entry.LastApplicableInstance.Matches(GetAvatarActor(), Montage, Instance.PlayInstanceId))
			{
				MontageGate.Discarded = Instance;
			}
			MontageGate.Prerequisites.RemoveAt(Index);
			++MontageGate.Revision;
		}
	}
	if (MontageGate.Discarded.Matches(GetAvatarActor(), Montage, Instance.PlayInstanceId))
	{
		return false;
	}

	return EvaluateReplicatedMontagePrerequisites(Instance);
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
	if (MontageGate.bEnding || MontageGate.bSuspending)
	{
		return -1.f;
	}
	const float LogicalStartTime = MontageGate.LogicalStartTime;
	const bool bMatchingInstance = MontageGate.Ready.Matches(
		GetAvatarActor(), Montage, GetRepAnimMontageInfo().PlayInstanceId);
	MontageGate.ResetReady();
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
		&& RepMontage.bRepPosition && !RepMontage.SkipPositionCorrection && FMath::IsFinite(RepMontage.Position)
		&& RepMontage.Position < Montage->GetPlayLength()
		&& RepMontage.Position >= LogicalStartTime)
	{
		PresentationStartTime = RepMontage.Position;
	}

	const uint32 Generation = MontageGate.AvatarGeneration;
	const float Duration = AnimInstance->Montage_Play(Montage, InPlayRate,
		EMontagePlayReturnType::MontageLength, PresentationStartTime);
	if (Duration > 0.f && Generation == MontageGate.AvatarGeneration && !MontageGate.bEnding)
	{
		LocalAnimMontageInfo.AnimMontage = Montage;
	}
	return Duration;
}

bool UIMGAbilitySystemComponent::EvaluateReplicatedMontagePrerequisites(const FReplicatedMontageInstance& Instance)
{
	const UAnimMontage* Montage = Instance.Montage.Get();
	const uint32 Revision = MontageGate.Revision;
	const auto Snapshot = MontageGate.Prerequisites;
	FName WaitingReason;
	FName StartTimeProvider;
	FName ConflictingProvider;
	bool bInvalidStartTime = false;
	float LogicalStartTime = -1.f;
	for (int32 Index = 0; Index < Snapshot.Num(); ++Index)
	{
		const FReplicatedMontagePrerequisiteEntry& Entry = Snapshot[Index];
		if (!Entry.Callback.IsBound())
		{
			RetryReplicatedMontage();
			return false;
		}
		// Teardown from inside a callback abandons this evaluation unless the caller explicitly hands it off.
		MontageGate.Prerequisites[Index].LastApplicableInstance = Instance;
		const FIMGReplicatedMontagePrerequisiteResult Result = Entry.Callback.Execute(Montage, Instance.PlayInstanceId);
		if (!CanRetryReplicatedMontage() || Revision != MontageGate.Revision
			|| !Instance.Matches(GetAvatarActor(), GetRepAnimMontageInfo().GetAnimMontage(), GetRepAnimMontageInfo().PlayInstanceId)
			|| GetRepAnimMontageInfo().IsStopped)
		{
			RetryReplicatedMontage();
			return false;
		}
		// The unchanged revision guarantees that the original index still identifies this registration.
		MontageGate.Prerequisites[Index].LastApplicableInstance =
			Result.Readiness == EIMGReplicatedMontageReadiness::NotApplicable ? FReplicatedMontageInstance() : Instance;
		if (Result.Readiness == EIMGReplicatedMontageReadiness::NotApplicable)
		{
			continue;
		}
		if (Result.Readiness == EIMGReplicatedMontageReadiness::Discard)
		{
			UE_LOG(LogIMGAbilitySystem, Verbose, TEXT("Montage prerequisite discarded: Provider=%s Avatar=[%s] Montage=[%s] Instance=%u Reason=%s"),
				*Entry.Name.ToString(), *GetNameSafe(GetAvatarActor()), *GetNameSafe(Montage), Instance.PlayInstanceId, *Result.Reason.ToString());
			MontageGate.Discarded = Instance;
			return false;
		}
		if (Result.Readiness != EIMGReplicatedMontageReadiness::Ready)
		{
			if (WaitingReason.IsNone())
			{
				WaitingReason = Result.Reason.IsNone() ? Entry.Name : Result.Reason;
			}
			continue;
		}
		if (Result.LogicalStartTime == -1.f)
		{
			continue;
		}
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
		if (!MontageGate.Waiting.Matches(GetAvatarActor(), Montage, Instance.PlayInstanceId)
			|| MontageGate.WaitReason != TEXT("ConflictingStartTimeProviders"))
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
	MontageGate.Ready = Instance;
	MontageGate.LogicalStartTime = LogicalStartTime;
	return true;
}
