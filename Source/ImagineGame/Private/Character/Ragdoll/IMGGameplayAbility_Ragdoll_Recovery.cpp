#include "Character/Ragdoll/IMGGameplayAbility_Ragdoll.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "AbilitySystem/IMGAbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/Skeleton.h"
#include "AnimNotifyState_MotionWarping.h"
#include "Character/IMGCharacterMovementComponent.h"
#include "Character/Ragdoll/IMGRagdollComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "GameplayPrediction.h"
#include "IMGLogChannels.h"
#include "Templates/UnrealTemplate.h"


namespace
{
bool CanPlayRecoveryMontage(const UAnimMontage* Montage, const USkeletalMeshComponent* Mesh)
{
	if (!Montage || !Mesh || !Montage->GetSkeleton() ||
		!Montage->GetSkeleton()->IsCompatibleMesh(Mesh->GetSkeletalMeshAsset()))
	{
		return false;
	}
	const auto HasWarpWindow = [](const UAnimSequenceBase* Animation)
	{
		return Animation && Animation->Notifies.ContainsByPredicate([](const FAnimNotifyEvent& Notify)
		{
			return Notify.NotifyStateClass && Notify.NotifyStateClass->IsA<UAnimNotifyState_MotionWarping>();
		});
	};
	// Recovery currently has no warp-target contract; never consume another action's targets.
	if (HasWarpWindow(Montage))
	{
		return false;
	}
	for (const FSlotAnimationTrack& Track : Montage->SlotAnimTracks)
	{
		for (const FAnimSegment& Segment : Track.AnimTrack.AnimSegments)
		{
			if (HasWarpWindow(Segment.GetAnimReference()))
			{
				return false;
			}
		}
	}
	return true;
}
}

FIMGRagdollRecoverySelection UIMGGameplayAbility_Ragdoll::SelectRecoveryAnimation_Implementation(
	const FIMGRagdollRecoveryContext& Context) const
{
	if (const UIMGRagdollComponent* Body = Action.Body.Get())
	{
		for (const FIMGRagdollRecoveryAnimation& Entry : Body->GetRecoveryAnimations())
		{
			FIMGRagdollRecoverySelection Selection;
			Selection.Montage = Entry.Montage;
			Selection.StartTime = Entry.MinStartTime;
			Selection.PlayRate = FMath::Clamp(1.f, Entry.MinPlayRate, Entry.MaxPlayRate);
			const ACharacter* Character = Cast<ACharacter>(Action.Avatar.Get());
			if (Entry.Allows(Selection, Context.bFaceUp) &&
				CanPlayRecoveryMontage(Selection.Montage, Character ? Character->GetMesh() : nullptr))
			{
				return Selection;
			}
		}
	}
	return {};
}

void UIMGGameplayAbility_Ragdoll::CheckSettling()
{
	if (!IsActionCurrent(Action.Session) || Action.Body->GetRagdollPhase() != EIMGRagdollPhase::Simulating)
	{
		AbortAction(TEXT("Simulating session lost"));
		return;
	}
	const double ServerNow = Action.Body->GetRagdollServerTime();
	FVector Linear;
	FVector Angular;
	const bool bMeasured = Action.Body->GetRagdollBodyMotion(Linear, Angular);
	if (!bMeasured || !IsActionCurrent(Action.Session) || Linear.ContainsNaN() || Angular.ContainsNaN() ||
		ServerNow < Action.Body->GetRecoveryApproval().EarliestRecoveryServerTime ||
		Linear.SizeSquared() > FMath::Square(MaxStableLinearSpeed) ||
		Angular.SizeSquared() > FMath::Square(MaxStableAngularSpeedRadians))
	{
		Action.StableStartTime = -1.0;
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (Action.StableStartTime < 0.0 || Now < Action.StableStartTime)
	{
		// Only intervals following a confirmed stable sample count toward continuous settling.
		Action.StableStartTime = Now;
	}
	const float StableSeconds = static_cast<float>(Now - Action.StableStartTime);
	if (StableSeconds + KINDA_SMALL_NUMBER < RequiredStableTime ||
		Now < Action.NextSelectionTime)
	{
		return;
	}
	Action.NextSelectionTime = Now + SelectionRetryInterval;
	FIMGRagdollRecoveryTargetData Request;
	if (!BuildRecoveryRequest(Request, StableSeconds))
	{
		return;
	}
	if (CurrentActorInfo->IsNetAuthority())
	{
		ResolveRecoveryRequest(Request, false);
	}
	else if (IsActionCurrent(Action.Session))
	{
		FGameplayAbilityTargetDataHandle Data(new FIMGRagdollRecoveryTargetData(Request));
		UIMGAbilitySystemComponent* ASC = Action.ASC.Get();
		FScopedPredictionWindow Prediction(ASC, true);
		ASC->CallServerSetReplicatedTargetData(Action.Spec, Action.ActivationKey, Data, FGameplayTag(), ASC->ScopedPredictionKey);
	}
}

bool UIMGGameplayAbility_Ragdoll::BuildRecoveryRequest(FIMGRagdollRecoveryTargetData& OutRequest, float StableSeconds)
{
	const uint16 Session = Action.Session;
	if (!IsActionCurrent(Session))
	{
		return false;
	}
	FIMGRagdollRecoveryContext Context;
	Context.Session = Session;
	Context.bFaceUp = Action.Body->IsFaceUp();
	Context.StableSeconds = StableSeconds;
	EIMGRagdollIssue Issue;
	if (!Action.Body->GetRagdollBodyMotion(Context.LinearVelocity, Context.AngularVelocityRadians) ||
		!IsActionCurrent(Session) || !Action.Body->QueryRecovery(Session, Context.Placement, Issue) || !IsActionCurrent(Session))
	{
		return false;
	}
	const ACharacter* Character = Cast<ACharacter>(Action.Avatar.Get());
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	if (!Mesh)
	{
		return false;
	}
	const TWeakObjectPtr<USkeletalMeshComponent> SelectedMesh = Mesh;
	const TWeakObjectPtr<UAnimInstance> SelectedAnim = Mesh->GetAnimInstance();
	const USkeletalMesh* MeshAsset = Mesh->GetSkeletalMeshAsset();
	const UPhysicsAsset* PhysicsAsset = Mesh->GetPhysicsAsset();
	const FTransform EntryTransform = Character->GetActorTransform();
	const uint32 ImpactRevision = Action.Body->GetRecoveryApproval().ImpactRevision;
	Action.Stage = EActionStage::SelectingRecovery;
	PreSelectRecoveryAnimation(Context);
	if (!IsActionCurrent(Session))
	{
		return false;
	}
	const FIMGRagdollRecoverySelection Selection = SelectRecoveryAnimation(Context);
	if (!IsActionCurrent(Session) || !SelectedMesh.IsValid() || Character->GetMesh() != SelectedMesh.Get() ||
		Mesh->GetAnimInstance() != SelectedAnim.Get() || Mesh->GetSkeletalMeshAsset() != MeshAsset ||
		Mesh->GetPhysicsAsset() != PhysicsAsset || !EntryTransform.Equals(Character->GetActorTransform(), 0.01f) ||
		ImpactRevision != Action.Body->GetRecoveryApproval().ImpactRevision)
	{
		if (IsActionCurrent(Session))
		{
			Action.Stage = EActionStage::Settling;
			Action.StableStartTime = -1.0;
		}
		return false;
	}
	Action.Stage = EActionStage::Settling;
	OutRequest.Session = Session;
	OutRequest.Attempt = ++Action.NextAttempt;
	if (!OutRequest.Attempt)
	{
		OutRequest.Attempt = ++Action.NextAttempt;
	}
	OutRequest.ImpactRevision = ImpactRevision;
	OutRequest.StableSeconds = StableSeconds;
	OutRequest.LinearVelocity = Context.LinearVelocity;
	OutRequest.AngularVelocityRadians = Context.AngularVelocityRadians;
	OutRequest.Selection = Selection;
	if (!CurrentActorInfo->IsNetAuthority())
	{
		FIMGRagdollBodyReference Reference;
		if (!Action.Movement->GetRagdollBodyReference(Session, Reference))
		{
			return false;
		}
		OutRequest.BodyReferenceSequence = Reference.Sequence;
		OutRequest.SampleServerTime = Reference.ServerTime;
	}
	return true;
}

bool UIMGGameplayAbility_Ragdoll::ResolveRecoveryRequest(const FIMGRagdollRecoveryTargetData& Request, bool bRemote)
{
	if (!CurrentActorInfo || !CurrentActorInfo->IsNetAuthority() || !IsActionCurrent(Request.Session) ||
		Action.Stage == EActionStage::Recovering || Request.Attempt == 0 ||
		Request.ImpactRevision != Action.Body->GetRecoveryApproval().ImpactRevision ||
		Action.Body->GetRagdollPhase() != EIMGRagdollPhase::Simulating)
	{
		return false;
	}
	if (bRemote)
	{
		if (static_cast<int32>(Request.Attempt - Action.LastRemoteAttempt) <= 0)
		{
			return false;
		}
		Action.LastRemoteAttempt = Request.Attempt;
	}
	const double ServerNow = Action.Body->GetRagdollServerTime();
	const double Earliest = Action.Body->GetRecoveryApproval().EarliestRecoveryServerTime;
	if (ServerNow < Earliest + RequiredStableTime || !FMath::IsFinite(Request.StableSeconds) ||
		Request.StableSeconds + KINDA_SMALL_NUMBER < RequiredStableTime || Request.StableSeconds > MaxRagdollTime ||
		FVector(Request.LinearVelocity).ContainsNaN() || FVector(Request.AngularVelocityRadians).ContainsNaN() ||
		FVector(Request.LinearVelocity).SizeSquared() > FMath::Square(MaxStableLinearSpeed + 0.1f) ||
		FVector(Request.AngularVelocityRadians).SizeSquared() > FMath::Square(MaxStableAngularSpeedRadians + 0.1f))
	{
		return false;
	}
	if (bRemote)
	{
		FIMGRagdollBodyReference Reference;
		// Body motion is advisory; authority still owns accepted movement, timing, assets and placement.
		const float MaxAge = Action.Movement->GetRagdollMovementState().Settings.MaxTargetAge + CheckInterval * 2.f;
		if (!Action.Movement->GetRagdollBodyReference(Request.Session, Reference) ||
			Request.BodyReferenceSequence == 0 || !FMath::IsFinite(Request.SampleServerTime) ||
			Request.SampleServerTime > ServerNow || ServerNow - Request.SampleServerTime > MaxAge ||
			static_cast<int32>(Reference.Sequence - Request.BodyReferenceSequence) < 0 ||
			static_cast<int32>(Reference.Sequence - Request.BodyReferenceSequence) > 64)
		{
			return false;
		}
	}
	else
	{
		FVector Linear;
		FVector Angular;
		if (!Action.Body->GetRagdollBodyMotion(Linear, Angular) || !IsActionCurrent(Request.Session) ||
			Linear.ContainsNaN() || Angular.ContainsNaN() ||
			Linear.SizeSquared() > FMath::Square(MaxStableLinearSpeed) ||
			Angular.SizeSquared() > FMath::Square(MaxStableAngularSpeedRadians))
		{
			Action.StableStartTime = -1.0;
			return false;
		}
	}
	const bool bSkipMontage = Request.Selection.Montage == nullptr;
	FVector BodyLocalOffset = FVector::ZeroVector;
	if (bSkipMontage)
	{
		if (!bAllowRecoveryWithoutMontage)
		{
			return false;
		}
	}
	else
	{
		const FIMGRagdollRecoveryAnimation* Entry = Action.Body->GetRecoveryAnimations().FindByPredicate(
			[&Request, this](const FIMGRagdollRecoveryAnimation& Candidate)
			{
				return Candidate.Allows(Request.Selection, Action.Body->IsFaceUp());
			});
		const ACharacter* Character = Cast<ACharacter>(Action.Avatar.Get());
		const USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
		if (!Entry || !CanPlayRecoveryMontage(Request.Selection.Montage, Mesh))
		{
			return false;
		}
		BodyLocalOffset = Entry->BodyLocalOffset;
	}
	FTransform Placement;
	EIMGRagdollIssue Issue;
	if (!Action.Body->QueryRecovery(Request.Session, Placement, Issue, BodyLocalOffset) || !IsActionCurrent(Request.Session))
	{
		return false;
	}
	Action.Stage = EActionStage::Recovering;
	Action.PlayingAttempt = Request.Attempt;
	Action.RecoveryDeadline = GetWorld()->GetTimeSeconds() + RecoveryStartTimeout;
	if (!Action.Body->BeginRecoveryAt(Request.Session, Placement, BodyLocalOffset))
	{
		if (IsActionCurrent(Request.Session) && Action.Body->GetRagdollPhase() == EIMGRagdollPhase::Simulating)
		{
			Action.Stage = EActionStage::Settling;
			return false;
		}
		AbortAction(TEXT("Recovery placement invalidated during handoff"));
		return false;
	}
	const FIMGRagdollRecoverySelection ApprovedSelection = bSkipMontage ? FIMGRagdollRecoverySelection() : Request.Selection;
	if (!IsActionCurrent(Request.Session) || !Action.Body->PublishApprovedRecovery(
		Request.Session, Request.Attempt, ApprovedSelection))
	{
		AbortAction(TEXT("Recovery approval failed"));
		return false;
	}
	UE_LOG(LogIMGAbilitySystem, Verbose, TEXT("[RagdollGA] Recovery approved Avatar=%s Session=%u Attempt=%u Remote=%d SkipMontage=%d Montage=%s Start=%.3f Rate=%.3f"),
		*GetNameSafe(Action.Avatar.Get()), Request.Session, Request.Attempt, bRemote, bSkipMontage,
		*GetNameSafe(ApprovedSelection.Montage), ApprovedSelection.StartTime, ApprovedSelection.PlayRate);
	CheckRecovery();
	return true;
}

void UIMGGameplayAbility_Ragdoll::OnRecoveryTargetData(const FGameplayAbilityTargetDataHandle& Data, FGameplayTag)
{
	const FGameplayAbilityTargetDataHandle Retained = Data;
	if (Action.ASC.IsValid())
	{
		Action.ASC->ConsumeClientReplicatedTargetData(Action.Spec, Action.ActivationKey);
	}
	if (bProcessingAction || !IsActionCurrent() || Retained.Num() != 1 || !Retained.Get(0) ||
		Retained.Get(0)->GetScriptStruct() != FIMGRagdollRecoveryTargetData::StaticStruct())
	{
		return;
	}
	TGuardValue<bool> Processing(bProcessingAction, true);
	ResolveRecoveryRequest(static_cast<const FIMGRagdollRecoveryTargetData&>(*Retained.Get(0)), true);
}

void UIMGGameplayAbility_Ragdoll::OnRecoveryTargetDataCancelled()
{
	if (IsActionCurrent() && Action.Stage == EActionStage::Recovering)
	{
		AbortAction(TEXT("Owning client reported recovery playback failure"));
	}
}

void UIMGGameplayAbility_Ragdoll::CheckRecovery()
{
	if (!IsActionCurrent(Action.Session))
	{
		return;
	}
	const FIMGRagdollRecoveryRepState& Approval = Action.Body->GetRecoveryApproval();
	if (!Approval.bApproved || Approval.bEnded)
	{
		return;
	}
	if (!Action.PlayingAttempt)
	{
		Action.PlayingAttempt = Approval.Attempt;
		Action.RecoveryDeadline = GetWorld()->GetTimeSeconds() + RecoveryStartTimeout;
	}
	if (Action.PlayingAttempt != Approval.Attempt)
	{
		AbortAction(TEXT("Recovery attempt replaced"));
		return;
	}
	if (Approval.bSkipMontage)
	{
		if (CurrentActorInfo->IsNetAuthority() &&
			Action.Body->PrepareRecoveryPresentation(Action.Session, Approval.Attempt))
		{
			Action.bCompleted = true;
			Action.Body->CompleteRecovery(Action.Session);
			EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
		}
		else if (GetWorld()->GetTimeSeconds() > Action.RecoveryDeadline)
		{
			AbortAction(TEXT("Recovery pose release timed out"));
		}
		return;
	}
	if (CurrentActorInfo->IsNetAuthority() && Action.bMontageFinished && GFrameCounter > Action.CompletionFrame)
	{
		Action.bCompleted = true;
		Action.Body->CompleteRecovery(Action.Session);
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
		return;
	}
	if (GetWorld()->GetTimeSeconds() > Action.RecoveryDeadline)
	{
		AbortAction(TEXT("Recovery playback or resources timed out"));
		return;
	}
	if (!MontageTask && !Action.bMontageFinished)
	{
		StartRecoveryMontage();
	}
}

void UIMGGameplayAbility_Ragdoll::StartRecoveryMontage()
{
	const uint16 Session = Action.Session;
	const FIMGRagdollRecoveryRepState Approval = Action.Body->GetRecoveryApproval();
	const bool bAuthority = CurrentActorInfo->IsNetAuthority();
	if ((!bAuthority && !Approval.bHasMontageInstance) || !Approval.Selection.Montage ||
		!Action.Body->PrepareRecoveryPresentation(Session, Approval.Attempt))
	{
		return;
	}
	float StartPosition = -1.f;
	float Remaining = 0.f;
	const double ServerNow = Action.Body->GetRagdollServerTime();
	if (!bAuthority && (!FMath::IsFinite(ServerNow) || ServerNow < 0.0))
	{
		return;
	}
	const double Elapsed = bAuthority ? 0.0 : FMath::Max(
		0.0, ServerNow - Approval.PlaybackServerTime);
	if (!Approval.Selection.GetPlaybackPosition(Elapsed, StartPosition, Remaining))
	{
		if (!bAuthority && Elapsed > 0.0)
		{
			Action.bMontageFinished = true;
			return;
		}
		AbortAction(TEXT("Invalid recovery montage path"));
		return;
	}
	Action.RecoveryDeadline = GetWorld()->GetTimeSeconds() + Remaining +
		Approval.Selection.Montage->BlendOut.GetBlendTime() + RecoveryStartTimeout;
	MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, Approval.Selection.Montage, Approval.Selection.PlayRate, NAME_None, true, 1.f, StartPosition);
	MontageTask->OnCompleted.AddDynamic(this, &ThisClass::OnRecoveryCompleted);
	MontageTask->OnInterrupted.AddDynamic(this, &ThisClass::OnRecoveryInterrupted);
	MontageTask->OnCancelled.AddDynamic(this, &ThisClass::OnRecoveryInterrupted);
	MontageTask->ReadyForActivation();
	if (!IsActionCurrent(Session))
	{
		return;
	}
	const ACharacter* Character = Cast<ACharacter>(Action.Avatar.Get());
	const UAnimInstance* Anim = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
	if (!Anim || !Anim->GetActiveInstanceForMontage(Approval.Selection.Montage))
	{
		AbortAction(TEXT("Recovery montage did not start"));
		return;
	}
	if (bAuthority)
	{
		uint8 PlayInstanceId = 0;
		if (!Action.ASC->GetActiveReplicatedMontagePlayInstanceId(Approval.Selection.Montage, PlayInstanceId) ||
			!Action.Body->PublishRecoveryMontageInstance(Session, Approval.Attempt, PlayInstanceId))
		{
			AbortAction(TEXT("Recovery montage instance could not be published"));
		}
	}
}

void UIMGGameplayAbility_Ragdoll::OnRecoveryCompleted()
{
	if (IsActionCurrent(Action.Session))
	{
		Action.bMontageFinished = true;
		Action.CompletionFrame = GFrameCounter;
	}
}

void UIMGGameplayAbility_Ragdoll::OnRecoveryInterrupted()
{
	if (IsActionCurrent())
	{
		AbortAction(TEXT("Recovery montage interrupted or failed"));
	}
}
