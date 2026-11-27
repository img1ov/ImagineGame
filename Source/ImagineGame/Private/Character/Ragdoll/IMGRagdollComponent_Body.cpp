#include "Character/Ragdoll/IMGRagdollComponent.h"

#include "Character/IMGCharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Cosmetics/IMGPawnComponent_CharacterParts.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameStateBase.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "IMGLogChannels.h"

USkeletalMeshComponent* UIMGRagdollComponent::GetExecutionMesh() const { return BodyMesh.Get(); }

bool UIMGRagdollComponent::IsBodyInputProducer() const
{
	return Character.IsValid() && (Character->GetLocalRole() == ROLE_AutonomousProxy || (Character->HasAuthority()
		&& (Character->IsLocallyControlled() || Character->GetRemoteRole() != ROLE_AutonomousProxy)));
}

bool UIMGRagdollComponent::ShouldExecuteBody() const
{
	return Character.IsValid() && (!Character->IsNetMode(NM_DedicatedServer)
		|| IsBodyInputProducer() || bSimulateRemoteBodiesOnDedicatedServer);
}

bool UIMGRagdollComponent::CanExecuteBody(USkeletalMeshComponent& Mesh) const
{
	FCollisionResponseTemplate Profile;
	return UCollisionProfile::Get()->GetProfileTemplate(BodyCollisionProfile, Profile) && CollisionEnabledHasPhysics(Profile.CollisionEnabled)
		&& FMath::IsFinite(MaxInitialVelocityAge) && MaxInitialVelocityAge >= 0.f
		&& FMath::IsFinite(OwnerCorrectionTime) && OwnerCorrectionTime > 0.f
		&& FMath::IsFinite(OwnerMaxCorrectionSpeed) && OwnerMaxCorrectionSpeed > 0.f
		&& FMath::IsFinite(HardCorrectionDistance) && HardCorrectionDistance > 0.f
		&& FMath::IsFinite(RemoteCorrectionTime) && RemoteCorrectionTime > 0.f
		&& FMath::IsFinite(RemoteMaxVelocityCorrection) && RemoteMaxVelocityCorrection >= 0.f
		&& FMath::IsFinite(RemoteMaxBackwardCorrection) && RemoteMaxBackwardCorrection >= 0.f
		&& FMath::IsFinite(RemoteMaxAcceleration) && RemoteMaxAcceleration >= 0.f
		&& Mesh.IsRegistered() && !Mesh.IsBeingDestroyed() && Mesh.GetSkeletalMeshAsset() && Mesh.GetPhysicsAsset()
		&& Mesh.GetBoneIndex(PelvisBone) != INDEX_NONE && Mesh.GetPhysicsAsset()->FindBodyIndex(PelvisBone) != INDEX_NONE
		&& (bEnableDedicatedServerPhysics || !Mesh.IsNetMode(NM_DedicatedServer) || Mesh.bEnablePhysicsOnDedicatedServer);
}

bool UIMGRagdollComponent::IsRagdollReady() const
{
	if (!IsRagdollAvailable()) { return false; }
	USkeletalMeshComponent* Mesh = Character->GetMesh();
	return !ShouldExecuteBody() || (IsValid(Mesh) && CanExecuteBody(*Mesh));
}

bool UIMGRagdollComponent::HasCurrentBody() const
{
	return BodySession != 0 && !bBodyInvalidated && BodyMesh.IsValid() && BodyMesh->IsRegistered()
		&& !BodyMesh->IsBeingDestroyed() && Character.IsValid() && Character->GetMesh() == BodyMesh.Get()
		&& BodyMesh->GetSkeletalMeshAsset() == BodyMeshAsset.Get() && BodyMesh->GetPhysicsAsset() == BodyPhysicsAsset.Get();
}

bool UIMGRagdollComponent::IsRagdollBodyActive(int32 ExpectedSession) const
{
	return ExpectedSession != 0 && ExpectedSession == BodySession && ExpectedSession == ActiveState.Session
		&& ActiveState.Phase == EIMGRagdollPhase::Simulating && !bBodyRecovering && !bReleasingBody
		&& IsRagdollAvailable() && Movement->IsRagdollMovementMode() && HasCurrentBody() && IsBodySimulationValid();
}

void UIMGRagdollComponent::SetBodyIssue(FName Issue)
{
	if (BodyIssue == Issue && BodyIssueSession == ActiveState.Session) { return; }
	BodyIssue = Issue;
	BodyIssueSession = ActiveState.Session;
	if (!Issue.IsNone() && !bStartingBody && !bReleasingBody)
	{
		const bool bWaiting = Issue == TEXT("WaitingForBodyReference") || Issue == TEXT("BodyNotReady");
		if (!bWaiting)
		{
			UE_LOG(LogIMG, Log, TEXT("[Ragdoll][Body] %s Session=%u Issue=%s"), *GetNameSafe(GetOwner()), ActiveState.Session, *Issue.ToString());
			OnRagdollBodyUnavailable.Broadcast(ActiveState.Session);
		}
	}
}

void UIMGRagdollComponent::RefreshMeshObservation()
{
	if (!Character.IsValid()) { return; }
	UIMGPawnComponent_CharacterParts* Parts = Character->FindComponentByClass<UIMGPawnComponent_CharacterParts>();
	if (ObservedParts.Get() != Parts)
	{
		if (ObservedParts.IsValid()) { ObservedParts->OnCharacterPartsChanged.RemoveDynamic(this, &ThisClass::HandleCharacterPartsChanged); }
		ObservedParts = Parts;
		if (Parts) { Parts->OnCharacterPartsChanged.AddUniqueDynamic(this, &ThisClass::HandleCharacterPartsChanged); }
	}
	USkeletalMeshComponent* Mesh = Character->GetMesh();
	if (SampledMesh.Get() == Mesh) { return; }
	ReleaseBody();
	if (SampledMesh.IsValid())
	{
		SampledMesh->UnregisterOnPhysicsCreatedDelegate(PhysicsCreatedHandle);
		RemoveTickPrerequisiteComponent(SampledMesh.Get());
		BodyPrePhysicsTick.RemovePrerequisite(SampledMesh.Get(), SampledMesh->PrimaryComponentTick);
	}
	PhysicsCreatedHandle.Reset();
	SampledMesh = Mesh;
	if (IsValid(Mesh))
	{
		PhysicsCreatedHandle = Mesh->RegisterOnPhysicsCreatedDelegate(FOnSkelMeshPhysicsCreated::CreateUObject(this, &ThisClass::HandlePhysicsCreated));
		AddTickPrerequisiteComponent(Mesh);
		BodyPrePhysicsTick.AddPrerequisite(Mesh, Mesh->PrimaryComponentTick);
	}
}

void UIMGRagdollComponent::HandleCharacterPartsChanged(UIMGPawnComponent_CharacterParts* Parts)
{
	if (Parts == ObservedParts.Get())
	{
		// Asset replacement may recreate bodies on the same component. Never restore old body settings onto them.
		if (BodyMesh.IsValid() && (BodyMesh->GetSkeletalMeshAsset() != BodyMeshAsset.Get()
			|| BodyMesh->GetPhysicsAsset() != BodyPhysicsAsset.Get())) { bBodyInvalidated = true; }
		if (ActiveState.Phase != EIMGRagdollPhase::Inactive) { SetExecutionTickEnabled(true); }
	}
}

void UIMGRagdollComponent::HandlePhysicsCreated()
{
	if (BodySession != 0) { bBodyInvalidated = true; }
}

void UIMGRagdollComponent::CaptureRestartPose()
{
	if (!HasCurrentBody()) { return; }
	RestartMesh = BodyMesh;
	RestartPhysicsAsset = BodyPhysicsAsset;
	RestartMeshAsset = BodyMeshAsset;
	RestartPelvisLocation = BodyMesh->GetBoneTransform(BodyMesh->GetBoneIndex(PelvisBone)).GetLocation();
	for (FBodyInstance* Body : BodyMesh->Bodies)
	{
		if (Body && Body->IsValidBodyInstance() && Body->GetBodySetup())
		{
			RestartBodyTransforms.Add(Body->GetBodySetup()->BoneName, Body->GetUnrealWorldTransform());
		}
	}
}

void UIMGRagdollComponent::RecordBodyStartFailure(USkeletalMeshComponent& Mesh, uint16 Session)
{
	FailedBodySession = Session;
	FailedBodyMesh = &Mesh;
	FailedBodyMeshAsset = Mesh.GetSkeletalMeshAsset();
	FailedBodyPhysicsAsset = Mesh.GetPhysicsAsset();
}

void UIMGRagdollComponent::RetryBodyExecution(int32 ExpectedSession)
{
	if (ExpectedSession != 0 && ExpectedSession == ActiveState.Session && IsRagdollAvailable()
		&& ActiveState.Phase == EIMGRagdollPhase::Simulating && HandedOffSession != ExpectedSession)
	{
		FailedBodySession = 0;
		SetExecutionTickEnabled(true);
	}
}

bool UIMGRagdollComponent::EnsureBody()
{
	if (HasCurrentBody()) { return true; }
	if (bReleasingBody || bTearingDown || HandedOffSession == ActiveState.Session) { return false; }
	USkeletalMeshComponent* Mesh = Character.IsValid() ? Character->GetMesh() : nullptr;
	if (!IsValid(Mesh) || !CanExecuteBody(*Mesh)) { SetBodyIssue(TEXT("BodyNotReady")); return false; }
	if (FailedBodySession == ActiveState.Session && FailedBodyMesh.Get() == Mesh
		&& FailedBodyMeshAsset.Get() == Mesh->GetSkeletalMeshAsset() && FailedBodyPhysicsAsset.Get() == Mesh->GetPhysicsAsset()) { return false; }
	const uint16 Session = ActiveState.Session;
	const bool bFirstEntry = LastBodySession != Session;
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	const double ServerNow = Character->HasAuthority() && World ? World->GetTimeSeconds()
		: GameState ? GameState->GetServerWorldTimeSeconds() : -1.0;
	const bool bFreshLaunch = bFirstEntry && ServerNow >= 0.0 && FMath::IsFinite(ServerNow)
		&& FMath::Abs(ServerNow - ActiveState.StartServerTime) <= MaxInitialVelocityAge;
	FIMGRagdollBodyReference Reference;
	const bool bHasReference = Movement->GetRagdollBodyReference(Session, Reference);
	// Consumers join a current result. Rebuilt producers use current movement velocity, never the original launch.
	if (!IsBodyInputProducer() && !bHasReference)
	{
		SetBodyIssue(TEXT("WaitingForBodyReference"));
		return false;
	}
	bSavedDedicatedServerPhysics = Mesh->bEnablePhysicsOnDedicatedServer;
	if (Mesh->IsNetMode(NM_DedicatedServer) && !Mesh->bEnablePhysicsOnDedicatedServer)
	{
		Mesh->bEnablePhysicsOnDedicatedServer = true;
		Mesh->RecreatePhysicsState();
	}
	BodyMesh = Mesh;
	BodyMeshAsset = Mesh->GetSkeletalMeshAsset();
	BodyPhysicsAsset = Mesh->GetPhysicsAsset();
	BodySession = Session;
	bBodyInvalidated = false;
	bBodyRecovering = false;
	SavedAttachParent = Mesh->GetAttachParent();
	SavedAttachSocket = Mesh->GetAttachSocketName();
	SavedMeshRelativeTransform = Mesh->GetRelativeTransform();
	SavedCharacterMeshOffset = Character->GetBaseTranslationOffset();
	SavedCollisionProfile = Mesh->GetCollisionProfileName();
	SavedCollisionResponses = Mesh->GetCollisionResponseToChannels();
	SavedCollisionEnabled = Mesh->GetCollisionEnabled();
	SavedCollisionObjectType = Mesh->GetCollisionObjectType();
	SavedVisibilityTickOption = Mesh->VisibilityBasedAnimTickOption;
	bSavedMeshTickEnabled = Mesh->IsComponentTickEnabled();
	bSavedPauseAnims = Mesh->bPauseAnims;
	bSavedNoSkeletonUpdate = Mesh->bNoSkeletonUpdate;
	bSavedBlendPhysics = Mesh->bBlendPhysics;
	bSavedEnableUpdateRateOptimizations = Mesh->bEnableUpdateRateOptimizations;
	for (FBodyInstance* Body : Mesh->Bodies)
	{
		if (!Body || !Body->IsValidBodyInstance() || !Body->GetBodySetup()) { continue; }
		const FName Bone = Body->GetBodySetup()->BoneName;
		SavedBodySimulation.Add(Bone, Body->IsInstanceSimulatingPhysics());
		SavedBodyBlend.Add(Bone, Body->PhysicsBlendWeight);
		SavedUpdateKinematic.Add(Bone, Body->bUpdateKinematicFromSimulation);
		TArray<ECollisionEnabled::Type>& Shapes = SavedShapeCollision.Add(Bone);
		for (int32 Index = 0; Index < Body->GetBodySetup()->AggGeom.GetElementCount(); ++Index) { Shapes.Add(Body->GetShapeCollisionEnabled(Index)); }
	}
	Mesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	Mesh->SetCollisionProfileName(BodyCollisionProfile, false);
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Mesh->bEnableUpdateRateOptimizations = false;
	Mesh->bNoSkeletonUpdate = false;
	Mesh->SetComponentTickEnabled(true);
	RecoveryPose = FPoseSnapshot();
	OwnerCorrectionRemaining = FVector::ZeroVector;
	BodyIssue = NAME_None;
	FBodyInstance* PelvisBody = Mesh->GetBodyInstance(PelvisBone);
	if (!PelvisBody || !PelvisBody->IsValidBodyInstance())
	{
		Mesh->RecreatePhysicsState();
		PelvisBody = Mesh->GetBodyInstance(PelvisBone);
	}
	if (!PelvisBody || !PelvisBody->IsValidBodyInstance() || Mesh->GetSkeletalMeshAsset() != BodyMeshAsset.Get()
		|| Mesh->GetPhysicsAsset() != BodyPhysicsAsset.Get())
	{
		RecordBodyStartFailure(*Mesh, Session);
		ReleaseBody();
		SetBodyIssue(TEXT("MissingPelvisBody"));
		return false;
	}
	// Any recreation here was requested by this entry, after capturing the pre-entry settings.
	bBodyInvalidated = false;
	bool bStarted = false;
	{
		TGuardValue<bool> StartingGuard(bStartingBody, true);
		bStarted = StartBodySimulation(*Mesh);
	}
	if (!bStarted || !HasCurrentBody() || !IsRagdollAvailable() || ActiveState.Session != Session
		|| Movement->GetRagdollMovementState().Session != Session || ActiveState.Phase != EIMGRagdollPhase::Simulating
		|| !Mesh->IsSimulatingPhysics(PelvisBone))
	{
		const FName Failure = BodyIssue.IsNone() ? FName(TEXT("BodyStartFailed")) : BodyIssue;
		if (IsValid(Mesh) && !Mesh->IsBeingDestroyed()) { RecordBodyStartFailure(*Mesh, Session); }
		ReleaseBody();
		BodyIssue = NAME_None;
		if (IsRegistered() && IsActive()) { SetBodyIssue(Failure); }
		return false;
	}
	FVector InitialPelvis = Mesh->GetBoneTransform(Mesh->GetBoneIndex(PelvisBone)).GetLocation();
	if (!RestartBodyTransforms.IsEmpty() && RestartMesh.Get() == Mesh && RestartPhysicsAsset.Get() == Mesh->GetPhysicsAsset()
		&& RestartMeshAsset.Get() == Mesh->GetSkeletalMeshAsset())
	{
		for (const auto& Saved : RestartBodyTransforms)
		{
			if (FBodyInstance* Body = Mesh->GetBodyInstance(Saved.Key))
			{
				if (Body->IsValidBodyInstance() && !Saved.Value.ContainsNaN()) { Body->SetBodyTransform(Saved.Value, ETeleportType::TeleportPhysics); }
			}
		}
		InitialPelvis = RestartPelvisLocation;
	}
	RestartBodyTransforms.Reset();
	RestartMesh.Reset();
	RestartPhysicsAsset.Reset();
	RestartMeshAsset.Reset();
	ApprovedPelvisOffset = bHasReference ? FVector(Reference.PelvisLocation) - FVector(Reference.CapsuleLocation)
		: InitialPelvis - Movement->UpdatedComponent->GetComponentLocation();
	bBodyInputRejected = false;
	if (bHasReference)
	{
		// Use the body's offset from the same sampled capsule, on the current CMC capsule timeline.
		const FVector Target = Movement->UpdatedComponent->GetComponentLocation() + FVector(Reference.PelvisLocation) - FVector(Reference.CapsuleLocation);
		TranslateBody(*Mesh, Target - InitialPelvis);
	}
	Mesh->SetAllPhysicsLinearVelocity(bHasReference ? FVector(Reference.Velocity)
		: bFreshLaunch ? FVector(ActiveState.InitialVelocity) : Movement->Velocity);
	LastBodySession = Session;
	FailedBodySession = 0;
	SetBodyIssue(NAME_None);
	return true;
}

bool UIMGRagdollComponent::StartBodySimulation(USkeletalMeshComponent& Mesh)
{
	Mesh.SetAllBodiesSimulatePhysics(true);
	Mesh.SetAllBodiesPhysicsBlendWeight(1.f);
	return true;
}

bool UIMGRagdollComponent::IsBodySimulationValid() const
{
	return BodyMesh.IsValid() && BodyMesh->IsSimulatingPhysics(PelvisBone);
}

void UIMGRagdollComponent::UpdateBodySimulation(float) {}
void UIMGRagdollComponent::StopBodySimulation(USkeletalMeshComponent&) {}
void UIMGRagdollComponent::ReleaseBodyResources() {}

void UIMGRagdollComponent::TranslateBody(USkeletalMeshComponent& Mesh, const FVector& Delta)
{
	if (Delta.ContainsNaN() || Delta.IsNearlyZero()) { return; }
	// Move the whole articulated body; single-bone teleports would stretch constraints.
	for (FBodyInstance* Body : Mesh.Bodies)
	{
		if (!Body || !Body->IsValidBodyInstance()) { continue; }
		FTransform Transform = Body->GetUnrealWorldTransform();
		if (Transform.ContainsNaN()) { continue; }
		Transform.AddToTranslation(Delta);
		Body->SetBodyTransform(Transform, ETeleportType::TeleportPhysics);
	}
}

void UIMGRagdollComponent::AccelerateBody(USkeletalMeshComponent& Mesh, const FVector& Acceleration)
{
	Mesh.AddForceToAllBodiesBelow(Acceleration, NAME_None, true, true);
}

void UIMGRagdollComponent::HandleCapsuleCorrection(int32 Session, const FVector& Delta)
{
	if (IsRagdollBodyActive(Session) && IsBodyInputProducer() && !Delta.ContainsNaN())
	{
		OwnerCorrectionRemaining += Delta;
		// Refresh only live input. SavedMove history remains untouched during reconciliation.
		SampleTarget();
	}
}

void UIMGRagdollComponent::UpdateBodyCorrection(float DeltaTime)
{
	if (!HasCurrentBody() || DeltaTime <= SMALL_NUMBER || !FMath::IsFinite(DeltaTime) || !Movement.IsValid()) { return; }
	USkeletalMeshComponent& Mesh = *BodyMesh.Get();
	if (IsBodyInputProducer())
	{
		FIMGRagdollBodyReference Reference;
		if (Movement->GetRagdollBodyReference(BodySession, Reference))
		{
			ApprovedPelvisOffset = FVector(Reference.PelvisLocation) - FVector(Reference.CapsuleLocation);
		}
		const auto IsRejectedTarget = [](EIMGRagdollIssue Issue)
		{
			return Issue == EIMGRagdollIssue::TargetTooFar || Issue == EIMGRagdollIssue::TargetPathBlocked;
		};
		const bool bRejected = IsRejectedTarget(Movement->GetRagdollMovementIssue())
			|| (Reference.Session == BodySession && IsRejectedTarget(Reference.Issue));
		if (bRejected && !bBodyInputRejected)
		{
			const int32 PelvisIndex = Mesh.GetBoneIndex(PelvisBone);
			if (PelvisIndex != INDEX_NONE)
			{
				OwnerCorrectionRemaining = Movement->UpdatedComponent->GetComponentLocation() + ApprovedPelvisOffset - Mesh.GetBoneTransform(PelvisIndex).GetLocation();
				Mesh.SetAllPhysicsLinearVelocity(Movement->Velocity);
				SampleTarget();
			}
		}
		bBodyInputRejected = bRejected;
		if (OwnerCorrectionRemaining.ContainsNaN()) { OwnerCorrectionRemaining = FVector::ZeroVector; return; }
		FVector Step = OwnerCorrectionRemaining;
		if (Step.SizeSquared() < FMath::Square(FMath::Max(1.f, HardCorrectionDistance)))
		{
			Step *= 1.f - FMath::Exp(-DeltaTime / FMath::Max(0.01f, OwnerCorrectionTime));
			Step = Step.GetClampedToMaxSize(FMath::Max(1.f, OwnerMaxCorrectionSpeed) * DeltaTime);
		}
		TranslateBody(Mesh, Step);
		OwnerCorrectionRemaining -= Step;
		return;
	}
	FIMGRagdollBodyReference Reference;
	if (!Movement->GetRagdollBodyReference(BodySession, Reference)) { return; }
	const int32 PelvisIndex = Mesh.GetBoneIndex(PelvisBone);
	if (PelvisIndex == INDEX_NONE) { return; }
	const FVector Pelvis = Mesh.GetBoneTransform(PelvisIndex).GetLocation();
	const FVector Target = Movement->UpdatedComponent->GetComponentLocation() + FVector(Reference.PelvisLocation) - FVector(Reference.CapsuleLocation);
	const FVector Error = Target - Pelvis;
	if (Error.ContainsNaN()) { return; }
	if (Error.SizeSquared() > FMath::Square(FMath::Max(1.f, HardCorrectionDistance)))
	{
		TranslateBody(Mesh, Error);
		Mesh.SetAllPhysicsLinearVelocity(Reference.Velocity);
		return;
	}
	const FVector Up = -Movement->GetGravityDirection();
	const auto ClampAxes = [&Up](const FVector& Value, float Limit)
	{
		return FVector::VectorPlaneProject(Value, Up).GetClampedToMaxSize(Limit) + Up * FMath::Clamp(FVector::DotProduct(Value, Up), -Limit, Limit);
	};
	// The capsule already supplies its own interpolation/extrapolation. Keep the body offset on that same timeline.
	const FVector FeedVelocity = Movement->Velocity;
	FVector VelocityCorrection = ClampAxes(Error / FMath::Max(0.01f, RemoteCorrectionTime), FMath::Max(0.f, RemoteMaxVelocityCorrection));
	const FVector TravelDirection = FeedVelocity.GetSafeNormal();
	const float AlongTravel = FVector::DotProduct(VelocityCorrection, TravelDirection);
	const float BackwardLimit = FMath::Max(0.f, RemoteMaxBackwardCorrection);
	if (AlongTravel < -BackwardLimit) { VelocityCorrection += TravelDirection * (-BackwardLimit - AlongTravel); }
	const FVector CurrentVelocity = Mesh.GetPhysicsLinearVelocity(PelvisBone);
	const FVector Acceleration = ClampAxes((FeedVelocity + VelocityCorrection - CurrentVelocity) / FMath::Max(0.01f, RemoteCorrectionTime), FMath::Max(0.f, RemoteMaxAcceleration));
	if (!Acceleration.ContainsNaN()) { AccelerateBody(Mesh, Acceleration); }
}

void UIMGRagdollComponent::PrepareBodyRecovery()
{
	if (!HasCurrentBody() || bBodyRecovering) { return; }
	USkeletalMeshComponent& Mesh = *BodyMesh.Get();
	Mesh.SnapshotPose(RecoveryPose);
	RecoveryPoseMeshAsset = Mesh.GetSkeletalMeshAsset();
	RecoveryMeshTransform = Mesh.GetComponentTransform();
	StopBodySimulation(Mesh);
	Mesh.SetAllBodiesSimulatePhysics(false);
	Mesh.SetComponentTickEnabled(false);
	Mesh.bPauseAnims = true;
	bBodyRecovering = true;
	OwnerCorrectionRemaining = FVector::ZeroVector;
	// Keep the detached component's final rendered pose until the animation adapter takes the snapshot.
}

bool UIMGRagdollComponent::GetRecoveryPose(int32 ExpectedSession, FPoseSnapshot& OutPose, FTransform& OutMeshTransform) const
{
	OutPose = FPoseSnapshot();
	OutMeshTransform = FTransform::Identity;
	if (ExpectedSession == 0 || ExpectedSession != ActiveState.Session || ActiveState.Phase != EIMGRagdollPhase::Recovering || !RecoveryPose.bIsValid
		|| !Character.IsValid() || !Character->GetMesh() || Character->GetMesh()->GetSkeletalMeshAsset() != RecoveryPoseMeshAsset.Get()) { return false; }
	OutPose = RecoveryPose;
	OutMeshTransform = RecoveryMeshTransform;
	return true;
}

bool UIMGRagdollComponent::ReleaseRecoveryHold(int32 ExpectedSession)
{
	if (!IsRagdollAvailable() || ExpectedSession == 0 || ExpectedSession != ActiveState.Session || ActiveState.Phase != EIMGRagdollPhase::Recovering
		|| !Movement.IsValid() || !Movement->IsRagdollRecoveryReady()) { return false; }
	if (BodySession != 0 && (!HasCurrentBody() || !bBodyRecovering)) { return false; }
	ReleaseBody();
	return true;
}

bool UIMGRagdollComponent::HandOffRagdollBody(int32 ExpectedSession)
{
	if (ActiveState.bExternalTransition || ExpectedSession == 0 || ExpectedSession != ActiveState.Session || !HasCurrentBody()) { return false; }
	HandedOffSession = BodySession;
	if (Movement.IsValid()) { Movement->ClearRagdollFollowTarget(this, BodySession); }
	ReleaseBody(true);
	SetBodyIssue(TEXT("BodyHandedOff"));
	return true;
}

bool UIMGRagdollComponent::GetTransitionPose(int32 ExpectedSession, int32 ExpectedRevision, FPoseSnapshot& OutPose, FTransform& OutMeshTransform) const
{
	OutPose = FPoseSnapshot();
	OutMeshTransform = FTransform::Identity;
	if (!IsTransitionReady(ExpectedSession, ExpectedRevision) || ReadyTransitionRevision != ExpectedRevision || !RecoveryPose.bIsValid
		|| !Character->GetMesh() || Character->GetMesh()->GetSkeletalMeshAsset() != RecoveryPoseMeshAsset.Get()) { return false; }
	OutPose = RecoveryPose;
	OutMeshTransform = RecoveryMeshTransform;
	return true;
}

bool UIMGRagdollComponent::ReleaseTransitionPose(int32 ExpectedSession, int32 ExpectedRevision, bool bDiscard)
{
	if (!IsRagdollAvailable() || ExpectedSession == 0 || ExpectedSession != ActiveState.Session || ExpectedRevision == 0
		|| ExpectedRevision != ActiveState.Revision || !ActiveState.bExternalTransition
		|| (BodySession != 0 && BodySession != ExpectedSession)
		|| (!bDiscard && (ReadyTransitionRevision != ExpectedRevision || !IsTransitionReady(ExpectedSession, ExpectedRevision)))) { return false; }
	ReleasedTransitionRevision = static_cast<uint16>(ExpectedRevision);
	if (bDiscard)
	{
		ReadyTransitionRevision = static_cast<uint16>(ExpectedRevision);
		EndLocalSession(EIMGRagdollEndReason::Transferred);
	}
	else { ReleaseBody(); }
	if (ActiveState.Session == ExpectedSession && ActiveState.Revision == ExpectedRevision) { SetExecutionTickEnabled(false); }
	return true;
}

void UIMGRagdollComponent::ReleaseBody(bool bKeepPhysics, bool bResumeHeldBody)
{
	if (bReleasingBody || BodySession == 0) { return; }
	TGuardValue<bool> ReleaseGuard(bReleasingBody, true);
	USkeletalMeshComponent* Mesh = BodyMesh.Get();
	const uint16 Session = BodySession;
	const bool bRestoreBodies = HasCurrentBody();
	BodySession = 0;
	if (Movement.IsValid()) { Movement->ClearRagdollFollowTarget(this, Session); }
	if (IsValid(Mesh) && !Mesh->IsBeingDestroyed())
	{
		StopBodySimulation(*Mesh);
		if (bKeepPhysics && bResumeHeldBody && bBodyRecovering && bRestoreBodies)
		{
			Mesh->bPauseAnims = bSavedPauseAnims;
			Mesh->SetComponentTickEnabled(true);
			Mesh->SetAllBodiesSimulatePhysics(true);
			Mesh->SetAllBodiesPhysicsBlendWeight(1.f);
		}
		if (!bKeepPhysics)
		{
			if (bRestoreBodies)
			{
				// Bodies created from an initially non-physical mesh have no saved runtime state.
				Mesh->SetAllBodiesSimulatePhysics(false);
				for (const TPair<FName, bool>& Saved : SavedBodySimulation)
				{
					FBodyInstance* Body = Mesh->GetBodyInstance(Saved.Key);
					if (!Body || !Body->IsValidBodyInstance()) { continue; }
					Body->SetInstanceSimulatePhysics(Saved.Value, true, true);
					Body->PhysicsBlendWeight = SavedBodyBlend.FindChecked(Saved.Key);
					Body->SetUpdateKinematicFromSimulation(SavedUpdateKinematic.FindChecked(Saved.Key));
					const TArray<ECollisionEnabled::Type>& Shapes = SavedShapeCollision.FindChecked(Saved.Key);
					for (int32 Index = 0; Index < Shapes.Num(); ++Index) { Body->SetShapeCollisionEnabled(Index, Shapes[Index]); }
				}
			}
			Mesh->bBlendPhysics = bSavedBlendPhysics;
			Mesh->bPauseAnims = bSavedPauseAnims;
			Mesh->bNoSkeletonUpdate = bSavedNoSkeletonUpdate;
			Mesh->VisibilityBasedAnimTickOption = SavedVisibilityTickOption;
			Mesh->bEnableUpdateRateOptimizations = bSavedEnableUpdateRateOptimizations;
			Mesh->SetComponentTickEnabled(bSavedMeshTickEnabled);
			// An external attachment is a takeover; do not overwrite it with our old parent.
			if (!Mesh->GetAttachParent())
			{
				if (SavedAttachParent.IsValid())
				{
					Mesh->AttachToComponent(SavedAttachParent.Get(), FAttachmentTransformRules::KeepRelativeTransform, SavedAttachSocket);
					FTransform Relative = SavedMeshRelativeTransform;
					if (Character.IsValid() && SavedAttachParent.Get() == Character->GetRootComponent())
					{
						Relative.AddToTranslation(Character->GetBaseTranslationOffset() - SavedCharacterMeshOffset);
					}
					Mesh->SetRelativeTransform(Relative, false, nullptr, ETeleportType::TeleportPhysics);
				}
			}
			if (Mesh->GetCollisionProfileName() == BodyCollisionProfile)
			{
				Mesh->SetCollisionProfileName(SavedCollisionProfile);
				Mesh->SetCollisionObjectType(SavedCollisionObjectType);
				Mesh->SetCollisionResponseToChannels(SavedCollisionResponses);
				Mesh->SetCollisionEnabled(SavedCollisionEnabled);
			}
			if (Mesh->IsNetMode(NM_DedicatedServer) && Mesh->bEnablePhysicsOnDedicatedServer != bSavedDedicatedServerPhysics)
			{
				Mesh->bEnablePhysicsOnDedicatedServer = bSavedDedicatedServerPhysics;
				Mesh->RecreatePhysicsState();
			}
		}
	}
	BodyMesh.Reset();
	BodyMeshAsset.Reset();
	BodyPhysicsAsset.Reset();
	SavedBodySimulation.Reset();
	SavedBodyBlend.Reset();
	SavedUpdateKinematic.Reset();
	SavedShapeCollision.Reset();
	OwnerCorrectionRemaining = FVector::ZeroVector;
	bBodyRecovering = false;
	bBodyInputRejected = false;
	bBodyInvalidated = false;
}
