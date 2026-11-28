#include "Character/Ragdoll/IMGRagdollComponent.h"

#include "AbilitySystem/IMGAbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Character/IMGCharacterMovementComponent.h"
#include "Character/IMGPawnExtensionComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameStateBase.h"
#include "IMGLogChannels.h"
#include "Templates/UnrealTemplate.h"

double UIMGRagdollComponent::GetRagdollServerTime() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return -1.0;
	}
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		return World->GetTimeSeconds();
	}
	const AGameStateBase* GameState = World->GetGameState();
	return GameState ? GameState->GetServerWorldTimeSeconds() : -1.0;
}

void UIMGRagdollComponent::InitializeRecoveryPresentation()
{
	if (bTearingDown || RecoveryPresentation.bReleasing)
	{
		return;
	}
	UIMGPawnExtensionComponent* Extension = UIMGPawnExtensionComponent::FindPawnExtensionComponent(GetOwner());
	if (RecoveryPresentation.PawnExtension.Get() != Extension)
	{
		ReleaseRecoveryPresentation();
		RecoveryPresentation.PawnExtension = Extension;
		if (Extension)
		{
			Extension->OnAbilitySystemUninitialized_Register(
				FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &ThisClass::HandleRecoveryASCUninitialized));
			Extension->OnAbilitySystemInitialized_RegisterAndCall(
				FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &ThisClass::HandleRecoveryASCInitialized));
		}
	}
}

void UIMGRagdollComponent::HandleRecoveryASCInitialized()
{
	if (const UIMGPawnExtensionComponent* Extension = RecoveryPresentation.PawnExtension.Get())
	{
		BindRecoveryAbilitySystem(Extension->GetIMGAbilitySystemComponent());
	}
}

bool UIMGRagdollComponent::BindRecoveryAbilitySystem(UIMGAbilitySystemComponent* ASC)
{
	if (bTearingDown || RecoveryPresentation.bReleasing || !IsRegistered() || !IsActive() ||
		!IsValid(ASC) || ASC->GetAvatarActor() != GetOwner())
	{
		return false;
	}
	if (RecoveryPresentation.ASC.Get() != ASC)
	{
		HandleRecoveryASCUninitialized();
		RecoveryPresentation.ASC = ASC;
	}
	if (GetOwner()->GetLocalRole() == ROLE_SimulatedProxy && !RecoveryPresentation.MontagePrerequisite.IsValid())
	{
		RecoveryPresentation.MontagePrerequisite = ASC->RegisterReplicatedMontagePrerequisite(
			GetOwner(), TEXT("Ragdoll"), FIMGReplicatedMontagePrerequisite::CreateUObject(
				this, &ThisClass::CheckRecoveryMontagePrerequisite));
	}
	ASC->RetryReplicatedMontage();
	return GetOwner()->GetLocalRole() != ROLE_SimulatedProxy || RecoveryPresentation.MontagePrerequisite.IsValid();
}

void UIMGRagdollComponent::HandleRecoveryASCUninitialized()
{
	if (RecoveryPresentation.bReleasing)
	{
		return;
	}
	TGuardValue<bool> Releasing(RecoveryPresentation.bReleasing, true);
	if (UIMGAbilitySystemComponent* ASC = RecoveryPresentation.ASC.Get())
	{
		ASC->UnregisterReplicatedMontagePrerequisite(RecoveryPresentation.MontagePrerequisite);
	}
	RecoveryPresentation.MontagePrerequisite.Reset();
	RecoveryPresentation.ASC.Reset();
	ClearRecoveryPose();
}

void UIMGRagdollComponent::ReleaseRecoveryPresentation()
{
	HandleRecoveryASCUninitialized();
	if (UIMGPawnExtensionComponent* Extension = RecoveryPresentation.PawnExtension.Get())
	{
		Extension->UnregisterAbilitySystemDelegates(this);
	}
	RecoveryPresentation.PawnExtension.Reset();
}

void UIMGRagdollComponent::ClearRecoveryPose()
{
	if (UAnimInstance* Anim = RecoveryPresentation.AnimInstance.Get(); Anim && RecoveryPresentation.bHasPose)
	{
		Anim->RemovePoseSnapshot(RecoveryPoseName);
	}
	if (USkeletalMeshComponent* Mesh = RecoveryPresentation.Mesh.Get(); Mesh && Movement.IsValid() &&
		Movement->GetRagdollMovementSession() == RecoveryPresentation.PreparedSession &&
		Movement->GetRagdollMovementPhase() != EIMGRagdollPhase::Simulating && Character.IsValid() &&
		Character->GetMesh() == Mesh && Mesh->GetAnimInstance() == RecoveryPresentation.AnimInstance.Get())
	{
		if (Mesh->VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones)
		{
			Mesh->VisibilityBasedAnimTickOption = RecoveryPresentation.SavedVisibilityTick;
		}
		if (!RecoveryPresentation.bSavedMeshTick && Mesh->IsComponentTickEnabled())
		{
			Mesh->SetComponentTickEnabled(false);
		}
	}
	RecoveryPresentation.Mesh.Reset();
	RecoveryPresentation.AnimInstance.Reset();
	RecoveryPresentation.PreparedSession = 0;
	RecoveryPresentation.PreparedAttempt = 0;
	RecoveryPresentation.PoseBlendStartTime = -1.0;
	RecoveryPresentation.bHasPose = false;
}

void UIMGRagdollComponent::BeginRecoverySession(uint16 Session, int16 ActivationKey, double EarliestServerTime)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !IsRagdollAvailable() ||
		Session == 0 || Session != GetRagdollSession() || !FMath::IsFinite(EarliestServerTime))
	{
		return;
	}
	ClearRecoveryPose();
	RecoveryApproval = FIMGRagdollRecoveryRepState();
	RecoveryApproval.Session = Session;
	RecoveryApproval.ActivationKey = ActivationKey;
	RecoveryApproval.EarliestRecoveryServerTime = EarliestServerTime;
	GetOwner()->ForceNetUpdate();
}

void UIMGRagdollComponent::DelayRecoverySession(uint16 Session, double EarliestServerTime)
{
	if (GetOwner() && GetOwner()->HasAuthority() && Session == RecoveryApproval.Session &&
		!RecoveryApproval.bApproved && !RecoveryApproval.bEnded && FMath::IsFinite(EarliestServerTime))
	{
		++RecoveryApproval.ImpactRevision;
		RecoveryApproval.EarliestRecoveryServerTime = FMath::Max(
			RecoveryApproval.EarliestRecoveryServerTime, EarliestServerTime);
		GetOwner()->ForceNetUpdate();
	}
}

bool UIMGRagdollComponent::PublishApprovedRecovery(
	uint16 Session, uint32 Attempt, const FIMGRagdollRecoverySelection& Selection)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Session == 0 ||
		Session != RecoveryApproval.Session || RecoveryApproval.bEnded || RecoveryApproval.bApproved || Attempt == 0 ||
		GetRagdollSession() != Session || GetRagdollPhase() != EIMGRagdollPhase::Recovering)
	{
		return false;
	}
	RecoveryApproval.Attempt = Attempt;
	RecoveryApproval.Selection = Selection;
	RecoveryApproval.bSkipMontage = Selection.Montage == nullptr;
	RecoveryApproval.bApproved = true;
	RecoveryApproval.PlaybackServerTime = GetRagdollServerTime();
	GetOwner()->ForceNetUpdate();
	return true;
}

bool UIMGRagdollComponent::PublishRecoveryMontageInstance(uint16 Session, uint32 Attempt, uint8 PlayInstanceId)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Session != RecoveryApproval.Session ||
		Attempt != RecoveryApproval.Attempt || !RecoveryApproval.bApproved || RecoveryApproval.bEnded ||
		RecoveryApproval.bSkipMontage)
	{
		return false;
	}
	RecoveryApproval.MontagePlayInstanceId = PlayInstanceId;
	RecoveryApproval.bHasMontageInstance = true;
	RecoveryApproval.PlaybackServerTime = GetRagdollServerTime();
	GetOwner()->ForceNetUpdate();
	return true;
}

void UIMGRagdollComponent::EndRecoverySession(uint16 Session)
{
	if (Session == 0 || Session != RecoveryApproval.Session)
	{
		return;
	}
	if (GetOwner() && GetOwner()->HasAuthority() && Session != 0 && Session == RecoveryApproval.Session)
	{
		RecoveryApproval.bEnded = true;
		GetOwner()->ForceNetUpdate();
	}
	if (RecoveryPresentation.PreparedSession == Session)
	{
		ClearRecoveryPose();
	}
}

bool UIMGRagdollComponent::PrepareRecoveryPresentation(uint16 Session, uint32 Attempt)
{
	if (RecoveryPresentation.bPreparing || RecoveryPresentation.bReleasing || !IsRagdollAvailable() ||
		!Movement.IsValid() || !Movement->IsRagdollRecoveryReady() ||
		Session != RecoveryApproval.Session || Session != GetRagdollSession() ||
		Session != Movement->GetRagdollMovementSession() ||
		Attempt != RecoveryApproval.Attempt || !RecoveryApproval.bApproved || RecoveryApproval.bEnded)
	{
		return false;
	}
	if (RecoveryApproval.bSkipMontage)
	{
		return ReleaseRecoveryHold(Session);
	}
	if (RecoveryPresentation.PreparedSession == Session && RecoveryPresentation.PreparedAttempt == Attempt &&
		RecoveryPresentation.AnimInstance.IsValid() && Character->GetMesh() == RecoveryPresentation.Mesh.Get() &&
		Character->GetMesh()->GetAnimInstance() == RecoveryPresentation.AnimInstance.Get())
	{
		return true;
	}
	TGuardValue<bool> Preparing(RecoveryPresentation.bPreparing, true);
	USkeletalMeshComponent* Mesh = Character->GetMesh();
	UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!Anim || RecoveryPoseName.IsNone() || !FMath::IsFinite(RecoveryPoseBlendTime) || RecoveryPoseBlendTime <= 0.f)
	{
		return false;
	}
	ClearRecoveryPose();
	FIMGRagdollPoseSnapshot Pose;
	const bool bHasPose = GetRecoveryPose(Session, Pose);
	if (!bHasPose && (Mesh->IsAnySimulatingPhysics() || Mesh->bPauseAnims || !Mesh->GetAttachParent()))
	{
		return false;
	}
	// A late join or a body-free dedicated-server consumer has no physical pose to install.
	if (bHasPose)
	{
		FPoseSnapshot& Installed = Anim->AddPoseSnapshot(RecoveryPoseName);
		Installed = Pose.Pose;
		Installed.SnapshotName = RecoveryPoseName;
	}
	if (!ReleaseRecoveryHold(Session))
	{
		if (bHasPose)
		{
			Anim->RemovePoseSnapshot(RecoveryPoseName);
		}
		return false;
	}
	if (!IsRagdollAvailable() || GetRagdollSession() != Session || RecoveryApproval.bEnded ||
		Character->GetMesh() != Mesh || Mesh->GetAnimInstance() != Anim)
	{
		if (bHasPose)
		{
			Anim->RemovePoseSnapshot(RecoveryPoseName);
		}
		return false;
	}
	if (bHasPose && !Pose.Pose.LocalTransforms.IsEmpty())
	{
		// SnapshotPose stores the root in the detached mesh frame; preserve its world pose after reattachment.
		Anim->AddPoseSnapshot(RecoveryPoseName).LocalTransforms[0] =
			(Pose.Pose.LocalTransforms[0] * Pose.MeshWorldTransform).GetRelativeTransform(Mesh->GetComponentTransform());
	}
	RecoveryPresentation.PreparedSession = Session;
	RecoveryPresentation.PreparedAttempt = Attempt;
	RecoveryPresentation.Mesh = Mesh;
	RecoveryPresentation.AnimInstance = Anim;
	RecoveryPresentation.bHasPose = bHasPose;
	RecoveryPresentation.PoseBlendStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0;
	RecoveryPresentation.SavedVisibilityTick = Mesh->VisibilityBasedAnimTickOption;
	RecoveryPresentation.bSavedMeshTick = Mesh->IsComponentTickEnabled();
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Mesh->SetComponentTickEnabled(true);
	return true;
}

float UIMGRagdollComponent::GetRecoveryPoseWeight() const
{
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : -1.0;
	if (!RecoveryPresentation.bHasPose || !RecoveryApproval.bApproved || RecoveryApproval.bEnded ||
		RecoveryApproval.bSkipMontage || RecoveryPresentation.PoseBlendStartTime < 0.0 ||
		Now < RecoveryPresentation.PoseBlendStartTime || RecoveryPoseBlendTime <= 0.f)
	{
		return 0.f;
	}
	return FMath::Clamp(1.f - static_cast<float>(Now - RecoveryPresentation.PoseBlendStartTime) / RecoveryPoseBlendTime, 0.f, 1.f);
}

void UIMGRagdollComponent::OnRep_RecoveryApproval()
{
	if (RecoveryApproval.bEnded || (RecoveryPresentation.PreparedSession != 0 &&
		(RecoveryPresentation.PreparedSession != RecoveryApproval.Session ||
			RecoveryPresentation.PreparedAttempt != RecoveryApproval.Attempt)))
	{
		ClearRecoveryPose();
	}
	if (UIMGAbilitySystemComponent* ASC = RecoveryPresentation.ASC.Get())
	{
		ASC->RetryReplicatedMontage();
	}
}

void UIMGRagdollComponent::UpdateRecoveryPresentation()
{
	if (RecoveryApproval.bEnded)
	{
		if (RecoveryPresentation.PreparedSession != 0)
		{
			ClearRecoveryPose();
		}
		return;
	}
	if (UIMGAbilitySystemComponent* ASC = RecoveryPresentation.ASC.Get(); ASC && RecoveryApproval.bApproved &&
		!RecoveryApproval.bSkipMontage && !ASC->MatchesReplicatedMontageInstance(
			RecoveryApproval.Selection.Montage, RecoveryApproval.MontagePlayInstanceId))
	{
		ASC->RetryReplicatedMontage();
	}
}

FIMGReplicatedMontagePrerequisiteResult UIMGRagdollComponent::CheckRecoveryMontagePrerequisite(
	const UAnimMontage* Montage, uint8 PlayInstanceId)
{
	const bool bListed = RecoveryAnimations.ContainsByPredicate(
		[Montage](const FIMGRagdollRecoveryAnimation& Entry) { return Montage && Entry.Montage == Montage; });
	const bool bMatchingAsset = Montage && RecoveryApproval.Selection.Montage == Montage;
	if (!bListed && !bMatchingAsset)
	{
		return {};
	}
	const bool bMatchingInstance = bMatchingAsset && RecoveryApproval.bHasMontageInstance &&
		RecoveryApproval.MontagePlayInstanceId == PlayInstanceId;
	if (bMatchingInstance && (RecoveryApproval.bEnded ||
		(GetRagdollSession() == RecoveryApproval.Session && GetRagdollPhase() == EIMGRagdollPhase::Inactive)))
	{
		return {EIMGReplicatedMontageReadiness::Discard, -1.f, TEXT("RagdollEnded")};
	}
	if (!bMatchingInstance || !RecoveryApproval.bApproved || RecoveryApproval.bSkipMontage)
	{
		return {EIMGReplicatedMontageReadiness::Waiting, -1.f, TEXT("RagdollApproval")};
	}
	if (!PrepareRecoveryPresentation(RecoveryApproval.Session, RecoveryApproval.Attempt))
	{
		return {EIMGReplicatedMontageReadiness::Waiting, -1.f, TEXT("RagdollPoseOrMovement")};
	}
	return {EIMGReplicatedMontageReadiness::Ready, RecoveryApproval.Selection.StartTime, NAME_None};
}
