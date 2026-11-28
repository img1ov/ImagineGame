#include "Character/Ragdoll/IMGRagdollComponent.h"

#include "Character/IMGCharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Cosmetics/IMGPawnComponent_CharacterParts.h"
#include "Engine/CollisionProfile.h"
#include "Engine/ReplicatedState.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameStateBase.h"
#include "IMGLogChannels.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsAsset.h"

namespace IMGRagdollBody
{
	constexpr float MinCorrectionTime = 0.01f;
	constexpr float MinSpeedLimit = 1.0f;
	constexpr float MinDistanceLimit = 1.0f;
	// Avoid waking sleeping simulated ragdolls with negligible correction forces.
	constexpr float MinAccelerationThreshold = 1.0f;
	constexpr float SettledVelocityTolerance = 2.0f;
	constexpr int32 MaxRemoteReferences = 4;

	void RetireKinematicUpdate(USkeletalMeshComponent& Mesh)
	{
		UWorld* World = Mesh.GetWorld();
		if (FPhysScene* Scene = World ? World->GetPhysicsScene() : nullptr)
		{
			Scene->ClearPreSimKinematicUpdate(&Mesh);
		}
	}

	void SynchronizeBodiesToPose(USkeletalMeshComponent& Mesh)
	{
		// A queued animation teleport can still write simulated bodies at the next pre-sim boundary.
		RetireKinematicUpdate(Mesh);
		// Enabling simulation preserves the old physics pose, which may lag behind the displayed mesh.
		for (FBodyInstance* Body : Mesh.Bodies)
		{
			if (!Body || !Body->IsValidBodyInstance() || Body->InstanceBoneIndex < 0
				|| Body->InstanceBoneIndex >= Mesh.GetNumComponentSpaceTransforms())
			{
				continue;
			}
			const FTransform Transform = Mesh.GetBoneTransform(Body->InstanceBoneIndex);
			if (Transform.IsValid())
			{
				Body->SetBodyTransform(Transform, ETeleportType::TeleportPhysics);
			}
		}
	}

	bool IsRejectedTargetIssue(EIMGRagdollIssue Issue)
	{
		return Issue == EIMGRagdollIssue::TargetTooFar || Issue == EIMGRagdollIssue::TargetPathBlocked;
	}

	FVector ClampHorizontalAndVertical(const FVector& Value, const FVector& Up, float HorizontalLimit, float VerticalLimit)
	{
		const FVector Horizontal = FVector::VectorPlaneProject(Value, Up).GetClampedToMaxSize(FMath::Max(0.0f, HorizontalLimit));
		const float Vertical = FMath::Clamp(static_cast<float>(FVector::DotProduct(Value, Up)), -FMath::Max(0.0f, VerticalLimit), FMath::Max(0.0f, VerticalLimit));
		return Horizontal + Up * Vertical;
	}

	double GetServerClockSeconds(const ACharacter& Character, const UWorld* World)
	{
		if (!World)
		{
			return -1.0;
		}
		if (Character.HasAuthority())
		{
			return World->GetTimeSeconds();
		}
		const AGameStateBase* GameState = World->GetGameState();
		return GameState ? GameState->GetServerWorldTimeSeconds() : -1.0;
	}

	FVector PredictBodyReference(const ACharacter& Character, const UWorld* World,
		const FIMGRagdollBodyReference& Reference, float MaxAge)
	{
		const double Now = GetServerClockSeconds(Character, World);
		const double Age = FMath::IsFinite(Now) ? FMath::Clamp(Now - Reference.ServerTime, 0.0, static_cast<double>(MaxAge)) : 0.0;
		return FVector(Reference.PelvisLocation) + FVector(Reference.Velocity) * Age;
	}
}

void UIMGRagdollComponent::FSavedMeshBodyState::Capture(USkeletalMeshComponent& Mesh, const ACharacter& OwnerCharacter)
{
	Reset();
	AttachParent = Mesh.GetAttachParent();
	AttachSocket = Mesh.GetAttachSocketName();
	RelativeTransform = Mesh.GetRelativeTransform();
	CharacterMeshOffset = OwnerCharacter.GetBaseTranslationOffset();
	if (AttachParent.Get() == OwnerCharacter.GetRootComponent() && AttachSocket.IsNone())
	{
		// Preserve the visible world pose separately; network smoothing is not a permanent attachment offset.
		RelativeTransform.SetTranslation(CharacterMeshOffset);
		RelativeTransform.SetRotation(OwnerCharacter.GetBaseRotationOffset());
	}
	CollisionProfile = Mesh.GetCollisionProfileName();
	CollisionResponses = Mesh.GetCollisionResponseToChannels();
	CollisionEnabled = Mesh.GetCollisionEnabled();
	CollisionObjectType = Mesh.GetCollisionObjectType();
	VisibilityTickOption = Mesh.VisibilityBasedAnimTickOption;
	bMeshTickEnabled = Mesh.IsComponentTickEnabled();
	bPauseAnims = Mesh.bPauseAnims;
	bNoSkeletonUpdate = Mesh.bNoSkeletonUpdate;
	bBlendPhysics = Mesh.bBlendPhysics;
	bEnableUpdateRateOptimizations = Mesh.bEnableUpdateRateOptimizations;

	Bodies.Reserve(Mesh.Bodies.Num());
	for (FBodyInstance* Body : Mesh.Bodies)
	{
		if (!Body || !Body->IsValidBodyInstance() || !Body->GetBodySetup())
		{
			continue;
		}

		FSavedBodyInstanceState& Saved = Bodies.AddDefaulted_GetRef();
		Saved.BoneName = Body->GetBodySetup()->BoneName;
		Saved.bSimulatePhysics = Body->IsInstanceSimulatingPhysics();
		Saved.WorldTransform = Body->GetUnrealWorldTransform();
		if (Saved.bSimulatePhysics)
		{
			Saved.LinearVelocity = Body->GetUnrealWorldVelocity();
			Saved.AngularVelocityRadians = Body->GetUnrealWorldAngularVelocityInRadians();
		}
		Saved.PhysicsBlendWeight = Body->PhysicsBlendWeight;
		Saved.bUpdateKinematicFromSimulation = Body->bUpdateKinematicFromSimulation;
		const int32 ShapeCount = Body->GetBodySetup()->AggGeom.GetElementCount();
		Saved.ShapeCollisions.Reserve(ShapeCount);
		for (int32 Index = 0; Index < ShapeCount; ++Index)
		{
			Saved.ShapeCollisions.Add(Body->GetShapeCollisionEnabled(Index));
		}
	}
}

void UIMGRagdollComponent::FSavedMeshBodyState::Restore(
	USkeletalMeshComponent& Mesh,
	const ACharacter* OwnerCharacter,
	FName ExpectedCollisionProfile,
	bool bRestoreBodies) const
{
	if (bRestoreBodies)
	{
		// Bodies created from an initially non-physical mesh have no saved runtime state.
		Mesh.SetAllBodiesSimulatePhysics(false);
		for (const FSavedBodyInstanceState& Saved : Bodies)
		{
			FBodyInstance* Body = Mesh.GetBodyInstance(Saved.BoneName);
			if (!Body || !Body->IsValidBodyInstance())
			{
				continue;
			}
			Body->SetInstanceSimulatePhysics(Saved.bSimulatePhysics, true, true);
			Body->PhysicsBlendWeight = Saved.PhysicsBlendWeight;
			Body->SetUpdateKinematicFromSimulation(Saved.bUpdateKinematicFromSimulation);
			for (int32 Index = 0; Index < Saved.ShapeCollisions.Num(); ++Index)
			{
				Body->SetShapeCollisionEnabled(Index, Saved.ShapeCollisions[Index]);
			}
		}
	}

	if (bRestoreBodies)
	{
		for (FBodyInstance* Body : Mesh.Bodies)
		{
			if (Body && Body->IsValidBodyInstance() && !Body->IsInstanceSimulatingPhysics())
			{
				Body->SetLinearVelocity(FVector::ZeroVector, false);
				Body->SetAngularVelocityInRadians(FVector::ZeroVector, false);
			}
		}
	}

	Mesh.bBlendPhysics = bBlendPhysics;
	Mesh.bPauseAnims = bPauseAnims;
	Mesh.bNoSkeletonUpdate = bNoSkeletonUpdate;
	Mesh.VisibilityBasedAnimTickOption = VisibilityTickOption;
	Mesh.bEnableUpdateRateOptimizations = bEnableUpdateRateOptimizations;
	Mesh.SetComponentTickEnabled(bMeshTickEnabled);

	// An external attachment is a takeover; do not overwrite it with our old parent.
	if (!Mesh.GetAttachParent() && AttachParent.IsValid())
	{
		Mesh.AttachToComponent(AttachParent.Get(), FAttachmentTransformRules::KeepRelativeTransform, AttachSocket);
		FTransform Relative = RelativeTransform;
		if (OwnerCharacter && AttachParent.Get() == OwnerCharacter->GetRootComponent())
		{
			Relative.AddToTranslation(OwnerCharacter->GetBaseTranslationOffset() - CharacterMeshOffset);
			if (AttachSocket.IsNone())
			{
				Relative.SetRotation(OwnerCharacter->GetBaseRotationOffset());
			}
		}
		Mesh.SetRelativeTransform(Relative, false, nullptr, ETeleportType::TeleportPhysics);
	}

	if (Mesh.GetCollisionProfileName() == ExpectedCollisionProfile)
	{
		Mesh.SetCollisionProfileName(CollisionProfile);
		Mesh.SetCollisionObjectType(CollisionObjectType);
		Mesh.SetCollisionResponseToChannels(CollisionResponses);
		Mesh.SetCollisionEnabled(CollisionEnabled);
	}

	if (Mesh.IsNetMode(NM_DedicatedServer) && Mesh.bEnablePhysicsOnDedicatedServer != bDedicatedServerPhysics)
	{
		Mesh.bEnablePhysicsOnDedicatedServer = bDedicatedServerPhysics;
		Mesh.RecreatePhysicsState();
	}
}

void UIMGRagdollComponent::FSavedMeshBodyState::Reset()
{
	AttachParent.Reset();
	AttachSocket = NAME_None;
	RelativeTransform = FTransform::Identity;
	CharacterMeshOffset = FVector::ZeroVector;
	CollisionProfile = NAME_None;
	Bodies.Reset();
}

void UIMGRagdollComponent::FBodyExecutionSession::ResetActiveBody()
{
	Session = 0;
	Mesh.Reset();
	MeshAsset.Reset();
	PhysicsAsset.Reset();
	OwnerCorrectionRemaining = FVector::ZeroVector;
	ApprovedPelvisVelocity = FVector::ZeroVector;
	RemoteReferences.Reset();
	RemotePresentationTime = -1.0;
	bRecovering = false;
	bInputRejected = false;
	bInvalidated = false;
}

bool UIMGRagdollComponent::FRestartPoseCache::Matches(const USkeletalMeshComponent& InMesh) const
{
	return !Bodies.IsEmpty() && !PelvisLocation.ContainsNaN() && !PelvisVelocity.ContainsNaN()
		&& Mesh.Get() == &InMesh
		&& PhysicsAsset.Get() == InMesh.GetPhysicsAsset()
		&& MeshAsset.Get() == InMesh.GetSkeletalMeshAsset();
}

void UIMGRagdollComponent::FRestartPoseCache::Reset()
{
	Mesh.Reset();
	PhysicsAsset.Reset();
	MeshAsset.Reset();
	PelvisLocation = FVector::ZeroVector;
	PelvisVelocity = FVector::ZeroVector;
	CapsuleHeightOffset = 0.f;
	Bodies.Reset();
}

bool UIMGRagdollComponent::FBodyStartFailureRecord::Matches(uint16 InSession, const USkeletalMeshComponent& InMesh) const
{
	return Session == InSession
		&& Mesh.Get() == &InMesh
		&& MeshAsset.Get() == InMesh.GetSkeletalMeshAsset()
		&& PhysicsAsset.Get() == InMesh.GetPhysicsAsset();
}

void UIMGRagdollComponent::FBodyStartFailureRecord::Record(uint16 InSession, USkeletalMeshComponent& InMesh)
{
	Session = InSession;
	Mesh = &InMesh;
	MeshAsset = InMesh.GetSkeletalMeshAsset();
	PhysicsAsset = InMesh.GetPhysicsAsset();
}

void UIMGRagdollComponent::FBodyStartFailureRecord::Reset()
{
	Session = 0;
	Mesh.Reset();
	MeshAsset.Reset();
	PhysicsAsset.Reset();
}

USkeletalMeshComponent* UIMGRagdollComponent::GetExecutionMesh() const
{
	return BodySessionState.Mesh.Get();
}

bool UIMGRagdollComponent::IsBodyInputProducer() const
{
	return Character.IsValid()
		&& (Character->GetLocalRole() == ROLE_AutonomousProxy
			|| (Character->HasAuthority()
				&& (Character->IsLocallyControlled() || Character->GetRemoteRole() != ROLE_AutonomousProxy)));
}

bool UIMGRagdollComponent::ShouldExecuteBody() const
{
	return Character.IsValid()
		&& (!Character->IsNetMode(NM_DedicatedServer)
			|| IsBodyInputProducer()
			|| bSimulateRemoteBodiesOnDedicatedServer);
}

bool UIMGRagdollComponent::CanExecuteBody(USkeletalMeshComponent& Mesh) const
{
	FCollisionResponseTemplate Profile;
	return UCollisionProfile::Get()->GetProfileTemplate(BodyCollisionProfile, Profile)
		&& CollisionEnabledHasPhysics(Profile.CollisionEnabled)
		&& FMath::IsFinite(MaxInitialVelocityAge) && MaxInitialVelocityAge >= 0.f
		&& FMath::IsFinite(OwnerCorrectionTime) && OwnerCorrectionTime > 0.f
		&& FMath::IsFinite(OwnerMaxCorrectionSpeed) && OwnerMaxCorrectionSpeed > 0.f
		&& FMath::IsFinite(HardCorrectionDistance) && HardCorrectionDistance > 0.f
		&& FMath::IsFinite(RemoteCorrectionTime) && RemoteCorrectionTime > 0.f
		&& FMath::IsFinite(RemoteVelocityCorrectionTime) && RemoteVelocityCorrectionTime > 0.f
		&& FMath::IsFinite(RemoteInterpolationTime) && RemoteInterpolationTime >= 0.f
		&& FMath::IsFinite(RemotePositionTolerance) && RemotePositionTolerance >= 0.f
		&& FMath::IsFinite(RemoteMaxVelocityCorrection) && RemoteMaxVelocityCorrection >= 0.f
		&& FMath::IsFinite(RemoteMaxVerticalVelocityCorrection) && RemoteMaxVerticalVelocityCorrection >= 0.f
		&& FMath::IsFinite(RemoteMaxBackwardCorrection) && RemoteMaxBackwardCorrection >= 0.f
		&& FMath::IsFinite(RemoteMaxAcceleration) && RemoteMaxAcceleration >= 0.f
		&& FMath::IsFinite(RemoteMaxVerticalAcceleration) && RemoteMaxVerticalAcceleration >= 0.f
		&& Mesh.IsRegistered()
		&& !Mesh.IsBeingDestroyed()
		&& Mesh.GetSkeletalMeshAsset()
		&& Mesh.GetPhysicsAsset()
		&& Mesh.GetBoneIndex(PelvisBone) != INDEX_NONE
		&& Mesh.GetPhysicsAsset()->FindBodyIndex(PelvisBone) != INDEX_NONE
		&& (bEnableDedicatedServerPhysics || !Mesh.IsNetMode(NM_DedicatedServer) || Mesh.bEnablePhysicsOnDedicatedServer);
}

bool UIMGRagdollComponent::IsRagdollReady() const
{
	if (!IsRagdollAvailable())
	{
		return false;
	}
	USkeletalMeshComponent* Mesh = Character->GetMesh();
	return !ShouldExecuteBody() || (IsValid(Mesh) && CanExecuteBody(*Mesh));
}

bool UIMGRagdollComponent::HasCurrentBody() const
{
	return BodySessionState.Session != 0
		&& !BodySessionState.bInvalidated
		&& BodySessionState.Mesh.IsValid()
		&& BodySessionState.Mesh->IsRegistered()
		&& !BodySessionState.Mesh->IsBeingDestroyed()
		&& Character.IsValid()
		&& Character->GetMesh() == BodySessionState.Mesh.Get()
		&& BodySessionState.Mesh->GetSkeletalMeshAsset() == BodySessionState.MeshAsset.Get()
		&& BodySessionState.Mesh->GetPhysicsAsset() == BodySessionState.PhysicsAsset.Get();
}

bool UIMGRagdollComponent::IsRagdollBodyActive(int32 ExpectedSession) const
{
	return ExpectedSession != 0
		&& ExpectedSession == BodySessionState.Session
		&& ExpectedSession == ActiveState.Session
		&& ActiveState.Phase == EIMGRagdollPhase::Simulating
		&& !BodySessionState.bRecovering
		&& !bReleasingBody
		&& IsRagdollAvailable()
		&& Movement->IsRagdollMovementMode()
		&& HasCurrentBody()
		&& IsBodySimulationValid();
}

void UIMGRagdollComponent::SetBodyIssue(EIMGRagdollBodyIssue Issue)
{
	if (BodyIssue == Issue && BodyIssueSession == ActiveState.Session)
	{
		return;
	}
	BodyIssue = Issue;
	BodyIssueSession = ActiveState.Session;
	if (Issue != EIMGRagdollBodyIssue::None && !bStartingBody && !bReleasingBody && !IsWaitingRagdollBodyIssue(Issue))
	{
		UE_LOG(LogIMG, Log, TEXT("[Ragdoll][Body] %s Session=%u Issue=%s"),
			*GetNameSafe(GetOwner()),
			ActiveState.Session,
			*StaticEnum<EIMGRagdollBodyIssue>()->GetNameStringByValue(static_cast<int64>(Issue)));
		OnRagdollBodyUnavailable.Broadcast(ActiveState.Session, Issue);
	}
}

void UIMGRagdollComponent::RefreshMeshObservation(bool bForcePartsLookup)
{
	if (!Character.IsValid())
	{
		return;
	}

	// Avoid per-tick FindComponentByClass; look up CharacterParts on initialize or if the weak binding went stale.
	if (bForcePartsLookup || ObservedParts.IsStale())
	{
		UIMGPawnComponent_CharacterParts* Parts = Character->FindComponentByClass<UIMGPawnComponent_CharacterParts>();
		if (ObservedParts.Get() != Parts)
		{
			if (ObservedParts.IsValid())
			{
				ObservedParts->OnCharacterPartsChanged.RemoveDynamic(this, &ThisClass::HandleCharacterPartsChanged);
			}
			ObservedParts = Parts;
			if (Parts)
			{
				Parts->OnCharacterPartsChanged.AddUniqueDynamic(this, &ThisClass::HandleCharacterPartsChanged);
			}
		}
	}

	USkeletalMeshComponent* Mesh = Character->GetMesh();
	if (SampledMesh.Get() == Mesh)
	{
		return;
	}

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
		PhysicsCreatedHandle = Mesh->RegisterOnPhysicsCreatedDelegate(
			FOnSkelMeshPhysicsCreated::CreateUObject(this, &ThisClass::HandlePhysicsCreated));
		AddTickPrerequisiteComponent(Mesh);
		BodyPrePhysicsTick.AddPrerequisite(Mesh, Mesh->PrimaryComponentTick);
	}
}

void UIMGRagdollComponent::HandleCharacterPartsChanged(UIMGPawnComponent_CharacterParts* Parts)
{
	if (Parts != ObservedParts.Get())
	{
		return;
	}

	// Asset replacement may recreate bodies on the same component. Never restore old body settings onto them.
	if (BodySessionState.Mesh.IsValid()
		&& (BodySessionState.Mesh->GetSkeletalMeshAsset() != BodySessionState.MeshAsset.Get()
			|| BodySessionState.Mesh->GetPhysicsAsset() != BodySessionState.PhysicsAsset.Get()))
	{
		BodySessionState.bInvalidated = true;
	}
	if (ActiveState.Phase != EIMGRagdollPhase::Inactive)
	{
		SetExecutionTickEnabled(true);
	}
}

void UIMGRagdollComponent::HandlePhysicsCreated()
{
	if (BodySessionState.Session != 0)
	{
		BodySessionState.bInvalidated = true;
	}
}

void UIMGRagdollComponent::CaptureRestartPose()
{
	if (!HasCurrentBody())
	{
		return;
	}
	USkeletalMeshComponent* Mesh = BodySessionState.Mesh.Get();
	const FBodyInstance* PelvisBody = Mesh->GetBodyInstance(PelvisBone);
	if (!PelvisBody || !PelvisBody->IsValidBodyInstance())
	{
		return;
	}
	RestartPose.Reset();
	RestartPose.Mesh = Mesh;
	RestartPose.PhysicsAsset = BodySessionState.PhysicsAsset;
	RestartPose.MeshAsset = BodySessionState.MeshAsset;
	RestartPose.PelvisLocation = PelvisBody->GetUnrealWorldTransform().GetLocation();
	RestartPose.CapsuleHeightOffset = BodySessionState.CapsuleHeightOffset;
	if (!BodySessionState.bRecovering)
	{
		RestartPose.PelvisVelocity = PelvisBody->GetUnrealWorldVelocityAtPoint(RestartPose.PelvisLocation);
	}
	RestartPose.Bodies.Reserve(Mesh->Bodies.Num());
	for (FBodyInstance* Body : Mesh->Bodies)
	{
		if (!Body || !Body->IsValidBodyInstance() || !Body->GetBodySetup())
		{
			continue;
		}
		FSavedBodyInstanceState& Saved = RestartPose.Bodies.AddDefaulted_GetRef();
		Saved.BoneName = Body->GetBodySetup()->BoneName;
		Saved.WorldTransform = Body->GetUnrealWorldTransform();
		if (!BodySessionState.bRecovering && Body->IsInstanceSimulatingPhysics())
		{
			Saved.LinearVelocity = Body->GetUnrealWorldVelocity();
			Saved.AngularVelocityRadians = Body->GetUnrealWorldAngularVelocityInRadians();
		}
	}
}

void UIMGRagdollComponent::RetryBodyExecution(int32 ExpectedSession)
{
	if (ExpectedSession != 0
		&& ExpectedSession == ActiveState.Session
		&& IsRagdollAvailable()
		&& ActiveState.Phase == EIMGRagdollPhase::Simulating
		&& BodySessionState.HandedOffSession != ExpectedSession)
	{
		BodyStartFailure.Reset();
		SetExecutionTickEnabled(true);
	}
}

bool UIMGRagdollComponent::PrepareBodyMesh(USkeletalMeshComponent& Mesh, uint16 Session)
{
	// Enable dedicated-server physics before capturing pre-entry state so body instances exist.
	const bool bOriginalDedicatedServerPhysics = Mesh.bEnablePhysicsOnDedicatedServer;
	if (Mesh.IsNetMode(NM_DedicatedServer) && !Mesh.bEnablePhysicsOnDedicatedServer)
	{
		Mesh.bEnablePhysicsOnDedicatedServer = true;
		Mesh.RecreatePhysicsState();
	}

	BodySessionState.Mesh = &Mesh;
	BodySessionState.MeshAsset = Mesh.GetSkeletalMeshAsset();
	BodySessionState.PhysicsAsset = Mesh.GetPhysicsAsset();
	BodySessionState.Session = Session;
	BodySessionState.bInvalidated = false;
	BodySessionState.bRecovering = false;
	BodySessionState.OwnerCorrectionRemaining = FVector::ZeroVector;

	SavedMeshState.Capture(Mesh, *Character);
	if (RecoveryPresentation.PreparedSession != 0 && RecoveryPresentation.Mesh.Get() == &Mesh
		&& RecoveryPresentation.AnimInstance.Get() == Mesh.GetAnimInstance())
	{
		// A new movement session can arrive before the previous recovery approval retires.
		SavedMeshState.VisibilityTickOption = RecoveryPresentation.SavedVisibilityTick;
		SavedMeshState.bMeshTickEnabled = RecoveryPresentation.bSavedMeshTick;
		ClearRecoveryPose();
	}
	SavedMeshState.bDedicatedServerPhysics = bOriginalDedicatedServerPhysics;

	Mesh.DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	Mesh.SetCollisionProfileName(BodyCollisionProfile, false);
	Mesh.VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Mesh.bEnableUpdateRateOptimizations = false;
	Mesh.bNoSkeletonUpdate = false;
	Mesh.SetComponentTickEnabled(true);
	RecoveryHold.Reset();
	BodyIssue = EIMGRagdollBodyIssue::None;

	FBodyInstance* PelvisBody = Mesh.GetBodyInstance(PelvisBone);
	if (!PelvisBody || !PelvisBody->IsValidBodyInstance())
	{
		Mesh.RecreatePhysicsState();
		PelvisBody = Mesh.GetBodyInstance(PelvisBone);
	}
	if (!PelvisBody
		|| !PelvisBody->IsValidBodyInstance()
		|| Mesh.GetSkeletalMeshAsset() != BodySessionState.MeshAsset.Get()
		|| Mesh.GetPhysicsAsset() != BodySessionState.PhysicsAsset.Get())
	{
		BodyStartFailure.Record(Session, Mesh);
		ReleaseBody();
		SetBodyIssue(EIMGRagdollBodyIssue::MissingPelvisBody);
		return false;
	}

	// Any recreation above was requested by this entry, after capturing the pre-entry settings.
	BodySessionState.bInvalidated = false;

	return true;
}

bool UIMGRagdollComponent::EnsureBody()
{
	if (HasCurrentBody())
	{
		return true;
	}
	if (bReleasingBody || bTearingDown || BodySessionState.HandedOffSession == ActiveState.Session)
	{
		return false;
	}

	USkeletalMeshComponent* Mesh = Character.IsValid() ? Character->GetMesh() : nullptr;
	if (!IsValid(Mesh) || !CanExecuteBody(*Mesh))
	{
		SetBodyIssue(EIMGRagdollBodyIssue::BodyNotReady);
		return false;
	}

	const uint16 Session = ActiveState.Session;
	if (BodyStartFailure.Matches(Session, *Mesh))
	{
		return false;
	}

	const bool bInputProducer = IsBodyInputProducer();
	FIMGRagdollBodyReference Reference;
	const bool bHasReference = Movement->GetRagdollBodyReference(Session, Reference);
	const double ServerNow = IMGRagdollBody::GetServerClockSeconds(*Character, GetWorld());
	const bool bFreshLaunch = BodySessionState.LastSession != Session
		&& ServerNow >= 0.0
		&& FMath::IsFinite(ServerNow)
		&& FMath::Abs(ServerNow - ActiveState.StartServerTime) <= MaxInitialVelocityAge;
	// Fresh consumers can start the approved launch immediately; late binding needs a current body result.
	if (!bInputProducer && !bHasReference && !bFreshLaunch)
	{
		SetBodyIssue(EIMGRagdollBodyIssue::WaitingForBodyReference);
		return false;
	}

	Mesh->HandleExistingParallelEvaluationTask(/*bBlockOnTask=*/true, /*bPerformPostAnimEvaluation=*/true);
	if (!IsValid(Mesh) || !IsRagdollAvailable() || Character->GetMesh() != Mesh
		|| ActiveState.Session != Session || ActiveState.Phase != EIMGRagdollPhase::Simulating)
	{
		return false;
	}

	if (!PrepareBodyMesh(*Mesh, Session))
	{
		return false;
	}
	FBodyInstance* PelvisBody = Mesh->GetBodyInstance(PelvisBone);

	const int32 PelvisIndex = Mesh->GetBoneIndex(PelvisBone);
	const FVector PosePelvis = Mesh->GetBoneTransform(PelvisIndex).GetLocation();
	const double PhysicsPoseError = FVector::Distance(PelvisBody->GetUnrealWorldTransform().GetLocation(), PosePelvis);
	IMGRagdollBody::SynchronizeBodiesToPose(*Mesh);
	FVector InitialPelvis = PosePelvis;

	const bool bHasRestartPose = RestartPose.Matches(*Mesh);
	if (bHasRestartPose)
	{
		for (const FSavedBodyInstanceState& Saved : RestartPose.Bodies)
		{
			if (FBodyInstance* Body = Mesh->GetBodyInstance(Saved.BoneName))
			{
				if (Body->IsValidBodyInstance() && Saved.WorldTransform.IsValid())
				{
					Body->SetBodyTransform(Saved.WorldTransform, ETeleportType::TeleportPhysics);
				}
			}
		}
		InitialPelvis = RestartPose.PelvisLocation;
	}

	bool bStarted = false;
	{
		TGuardValue<bool> StartingGuard(bStartingBody, true);
		bStarted = StartBodySimulation(*Mesh);
	}

	if (!bStarted
		|| !HasCurrentBody()
		|| !IsRagdollAvailable()
		|| ActiveState.Session != Session
		|| Movement->GetRagdollMovementState().Session != Session
		|| ActiveState.Phase != EIMGRagdollPhase::Simulating
		|| !Mesh->IsSimulatingPhysics(PelvisBone))
	{
		const EIMGRagdollBodyIssue Failure = BodyIssue != EIMGRagdollBodyIssue::None
			? BodyIssue
			: EIMGRagdollBodyIssue::BodyStartFailed;
		if (IsValid(Mesh) && !Mesh->IsBeingDestroyed())
		{
			BodyStartFailure.Record(Session, *Mesh);
		}
		ReleaseBody();
		BodyIssue = EIMGRagdollBodyIssue::None;
		if (IsRegistered() && IsActive())
		{
			SetBodyIssue(Failure);
		}
		return false;
	}

	BodySessionState.ApprovedPelvisOffset = bHasReference
		? FVector(Reference.PelvisLocation) - FVector(Reference.CapsuleLocation)
		: InitialPelvis - Movement->UpdatedComponent->GetComponentLocation();
	BodySessionState.bInputRejected = false;
	if (bHasRestartPose)
	{
		BodySessionState.CapsuleHeightOffset = RestartPose.CapsuleHeightOffset;
	}
	else if (BodySessionState.LastSession != Session)
	{
		BodySessionState.CapsuleHeightOffset = FVector::DotProduct(
			Movement->UpdatedComponent->GetComponentLocation() - InitialPelvis, -Movement->GetGravityDirection());
	}

	FVector InitialBodyTranslation = FVector::ZeroVector;
	if (bHasReference && !bInputProducer && !bFreshLaunch)
	{
		// Late consumers join the current result; fresh entries retain the displayed pose and converge normally.
		const FVector Target = IMGRagdollBody::PredictBodyReference(
			*Character, GetWorld(), Reference, ActiveState.Settings.MaxTargetAge);
		InitialBodyTranslation = Target - InitialPelvis;
		TranslateBody(*Mesh, InitialBodyTranslation);
	}

	const FVector LocalLaunchVelocity = bHasRestartPose
		? RestartPose.PelvisVelocity + FVector(ActiveState.LaunchVelocityDelta)
		: Movement->GetRagdollLaunchVelocity();
	const FVector LaunchVelocity = bHasReference && !bInputProducer
		? FVector(Reference.Velocity)
		: (bHasRestartPose || bFreshLaunch ? LocalLaunchVelocity : Movement->Velocity);
	BodySessionState.ApprovedPelvisVelocity = bHasReference ? FVector(Reference.Velocity) : LaunchVelocity;
	InitializeBodyVelocity(*Mesh, LaunchVelocity, bHasRestartPose, bInputProducer);
	RestartPose.Reset();

	UE_LOG(LogIMG, Verbose,
		TEXT("[Ragdoll][Entry] Actor=%s Role=%d Session=%u Producer=%d Fresh=%d Reference=%d PhysicsPoseError=%.2f HeightOffset=%.2f Capsule=%s PosePelvis=%s BodyTranslation=%s LaunchVelocity=%s CapsuleYaw=%.2f MeshYaw=%.2f PelvisYaw=%.2f PelvisAngularVelocity=%s"),
		*GetNameSafe(GetOwner()), static_cast<int32>(Character->GetLocalRole()), Session,
		bInputProducer, bFreshLaunch, bHasReference, PhysicsPoseError, BodySessionState.CapsuleHeightOffset,
		*Movement->UpdatedComponent->GetComponentLocation().ToCompactString(), *PosePelvis.ToCompactString(),
		*InitialBodyTranslation.ToCompactString(), *LaunchVelocity.ToCompactString(),
		Character->GetActorRotation().Yaw, Mesh->GetComponentRotation().Yaw,
		PelvisBody->GetUnrealWorldTransform().Rotator().Yaw,
		*PelvisBody->GetUnrealWorldAngularVelocityInRadians().ToCompactString());

	BodySessionState.LastSession = Session;
	BodyStartFailure.Reset();
	SetBodyIssue(EIMGRagdollBodyIssue::None);
	return true;
}

void UIMGRagdollComponent::InitializeBodyVelocity(USkeletalMeshComponent& Mesh,
	const FVector& LaunchVelocity, bool bHasRestartPose, bool bInputProducer)
{
	Mesh.SetAllPhysicsLinearVelocity(LaunchVelocity);
	// Kinematic interpolation/network heading changes are not trusted angular momentum.
	Mesh.SetAllPhysicsAngularVelocityInRadians(FVector::ZeroVector);
	const TArray<FSavedBodyInstanceState>& Snapshots = bHasRestartPose ? RestartPose.Bodies : SavedMeshState.Bodies;
	const FVector PreviousMean = bHasRestartPose ? RestartPose.PelvisVelocity
		: BodySessionState.LastSession != ActiveState.Session
		? Movement->GetRagdollLaunchVelocity() - FVector(ActiveState.LaunchVelocityDelta) : Movement->Velocity;
	for (const FSavedBodyInstanceState& Saved : Snapshots)
	{
		if ((!bHasRestartPose && (!bInputProducer || !Saved.bSimulatePhysics))
			|| Saved.LinearVelocity.ContainsNaN() || Saved.AngularVelocityRadians.ContainsNaN())
		{
			continue;
		}
		FBodyInstance* Body = Mesh.GetBodyInstance(Saved.BoneName);
		if (!Body || !Body->IsValidBodyInstance() || !Body->IsInstanceSimulatingPhysics())
		{
			continue;
		}
		Body->SetLinearVelocity(Saved.LinearVelocity + LaunchVelocity - PreviousMean, false);
		const float MaxAngularSpeed = Body->GetMaxAngularVelocityInRadians();
		if (FMath::IsFinite(MaxAngularSpeed) && MaxAngularSpeed >= 0.f)
		{
			Body->SetAngularVelocityInRadians(Saved.AngularVelocityRadians.GetClampedToMaxSize(MaxAngularSpeed), false);
		}
	}
}

bool UIMGRagdollComponent::StartBodySimulation(USkeletalMeshComponent& Mesh)
{
	Mesh.SetAllBodiesSimulatePhysics(true);
	Mesh.SetAllBodiesPhysicsBlendWeight(1.f);
	return true;
}

bool UIMGRagdollComponent::IsBodySimulationValid() const
{
	return BodySessionState.Mesh.IsValid() && BodySessionState.Mesh->IsSimulatingPhysics(PelvisBone);
}

void UIMGRagdollComponent::UpdateBodySimulation(float) {}
void UIMGRagdollComponent::StopBodySimulation(USkeletalMeshComponent&) {}
void UIMGRagdollComponent::ReleaseBodyResources() {}

void UIMGRagdollComponent::TranslateBody(USkeletalMeshComponent& Mesh, const FVector& Delta)
{
	if (Delta.ContainsNaN() || Delta.IsNearlyZero())
	{
		return;
	}
	// Move the whole articulated body; single-bone teleports would stretch constraints.
	for (FBodyInstance* Body : Mesh.Bodies)
	{
		if (!Body || !Body->IsValidBodyInstance())
		{
			continue;
		}
		FTransform Transform = Body->GetUnrealWorldTransform();
		if (Transform.ContainsNaN())
		{
			continue;
		}
		Transform.AddToTranslation(Delta);
		Body->SetBodyTransform(Transform, ETeleportType::TeleportPhysics);
	}
}

void UIMGRagdollComponent::AccelerateBody(USkeletalMeshComponent& Mesh, const FVector& Acceleration)
{
	if (Acceleration.ContainsNaN() || Acceleration.IsNearlyZero(IMGRagdollBody::MinAccelerationThreshold))
	{
		return;
	}
	// Apply acceleration only to actively simulating bodies to avoid waking sleeping/kinematic bodies unnecessarily.
	for (FBodyInstance* Body : Mesh.Bodies)
	{
		if (Body && Body->IsValidBodyInstance() && Body->IsInstanceSimulatingPhysics())
		{
			Body->AddForce(Acceleration, /*bAllowSubstepping=*/true, /*bAccelChange=*/true);
		}
	}
}

void UIMGRagdollComponent::HandleCapsuleCorrection(int32 Session, const FVector& Delta)
{
	if (IsRagdollBodyActive(Session) && IsBodyInputProducer() && !Delta.ContainsNaN())
	{
		BodySessionState.OwnerCorrectionRemaining += Delta;
		// Refresh only live input. SavedMove history remains untouched during reconciliation.
		SampleTarget();
	}
}

void UIMGRagdollComponent::UpdateOwnerBodyCorrection(USkeletalMeshComponent& Mesh, float DeltaTime)
{
	FIMGRagdollBodyReference Reference;
	if (Movement->GetRagdollBodyReference(BodySessionState.Session, Reference))
	{
		BodySessionState.ApprovedPelvisOffset = FVector(Reference.PelvisLocation) - FVector(Reference.CapsuleLocation);
		BodySessionState.ApprovedPelvisVelocity = Reference.Velocity;
	}

	const bool bRejected = IMGRagdollBody::IsRejectedTargetIssue(Movement->GetRagdollMovementIssue())
		|| (Reference.Session == BodySessionState.Session && IMGRagdollBody::IsRejectedTargetIssue(Reference.Issue));
	if (bRejected && !BodySessionState.bInputRejected)
	{
		const FBodyInstance* PelvisBody = Mesh.GetBodyInstance(PelvisBone);
		if (PelvisBody && PelvisBody->IsValidBodyInstance())
		{
			const FVector CurrentPelvis = PelvisBody->GetUnrealWorldTransform().GetLocation();
			BodySessionState.OwnerCorrectionRemaining = Movement->UpdatedComponent->GetComponentLocation()
				+ BodySessionState.ApprovedPelvisOffset - CurrentPelvis;
			Mesh.SetAllPhysicsLinearVelocity(BodySessionState.ApprovedPelvisVelocity);
			SampleTarget();
		}
	}
	BodySessionState.bInputRejected = bRejected;

	if (BodySessionState.OwnerCorrectionRemaining.ContainsNaN())
	{
		BodySessionState.OwnerCorrectionRemaining = FVector::ZeroVector;
		return;
	}

	FVector Step = BodySessionState.OwnerCorrectionRemaining;
	if (Step.SizeSquared() < FMath::Square(FMath::Max(IMGRagdollBody::MinDistanceLimit, HardCorrectionDistance)))
	{
		const float Alpha = 1.0f - FMath::Exp(-DeltaTime / FMath::Max(IMGRagdollBody::MinCorrectionTime, OwnerCorrectionTime));
		Step *= Alpha;
		Step = Step.GetClampedToMaxSize(FMath::Max(IMGRagdollBody::MinSpeedLimit, OwnerMaxCorrectionSpeed) * DeltaTime);
	}

	if (!Step.IsNearlyZero())
	{
		TranslateBody(Mesh, Step);
		BodySessionState.OwnerCorrectionRemaining -= Step;
	}
}

bool UIMGRagdollComponent::SampleRemoteBodyReference(FVector& OutTarget, FVector& OutVelocity)
{
	FIMGRagdollBodyReference Reference;
	if (!Movement->GetRagdollBodyReference(BodySessionState.Session, Reference))
	{
		BodySessionState.RemoteReferences.Reset();
		BodySessionState.RemotePresentationTime = -1.0;
		return false;
	}

	// Keep interpolation history independent of capsule smoothing and world-origin shifts.
	Reference.PelvisLocation = FRepMovement::RebaseOntoZeroOrigin(Reference.PelvisLocation, this);
	Reference.CapsuleLocation = FRepMovement::RebaseOntoZeroOrigin(Reference.CapsuleLocation, this);
	auto& Samples = BodySessionState.RemoteReferences;
	if (!Samples.IsEmpty() && (Samples.Last().Session != Reference.Session
		|| Samples.Last().MovementRevision != Reference.MovementRevision))
	{
		Samples.Reset();
		BodySessionState.RemotePresentationTime = -1.0;
	}
	if (Samples.IsEmpty() || Samples.Last().Sequence != Reference.Sequence)
	{
		if (!Samples.IsEmpty() && Reference.ServerTime <= Samples.Last().ServerTime)
		{
			Samples.Last() = Reference;
		}
		else
		{
			if (Samples.Num() == IMGRagdollBody::MaxRemoteReferences)
			{
				Samples.RemoveAt(0, 1, EAllowShrinking::No);
			}
			Samples.Add(Reference);
		}
	}
	const double Delay = FMath::Min(static_cast<double>(RemoteInterpolationTime),
		static_cast<double>(ActiveState.Settings.MaxTargetAge));
	const double TargetTime = FMath::Max(BodySessionState.RemotePresentationTime,
		IMGRagdollBody::GetServerClockSeconds(*Character, GetWorld()) - Delay);
	BodySessionState.RemotePresentationTime = TargetTime;
	FVector Position = Samples[0].PelvisLocation;
	OutVelocity = Samples[0].Velocity;
	for (int32 Index = 1; Index < Samples.Num(); ++Index)
	{
		const FIMGRagdollBodyReference& A = Samples[Index - 1];
		const FIMGRagdollBodyReference& B = Samples[Index];
		if (TargetTime <= B.ServerTime)
		{
			const double Span = B.ServerTime - A.ServerTime;
			const double Alpha = Span > UE_DOUBLE_SMALL_NUMBER
				? FMath::Clamp((TargetTime - A.ServerTime) / Span, 0.0, 1.0) : 1.0;
			Position = FMath::Lerp(FVector(A.PelvisLocation), FVector(B.PelvisLocation), Alpha);
			OutVelocity = FMath::Lerp(FVector(A.Velocity), FVector(B.Velocity), Alpha);
			OutTarget = FRepMovement::RebaseOntoLocalOrigin(Position, this);
			return true;
		}
	}
	if (TargetTime > Samples.Last().ServerTime)
	{
		const double MaxExtrapolation = FMath::Min(static_cast<double>(ActiveState.Settings.MaxTargetAge),
			FMath::Max(Delay, static_cast<double>(IMGRagdollBody::MinCorrectionTime)));
		OutVelocity = Samples.Last().Velocity;
		Position = FVector(Samples.Last().PelvisLocation)
			+ OutVelocity * FMath::Clamp(TargetTime - Samples.Last().ServerTime, 0.0, MaxExtrapolation);
	}
	OutTarget = FRepMovement::RebaseOntoLocalOrigin(Position, this);
	return true;
}

void UIMGRagdollComponent::UpdateRemoteBodyCorrection(USkeletalMeshComponent& Mesh, float DeltaTime)
{
	FVector Target;
	FVector FeedVelocity;
	if (!SampleRemoteBodyReference(Target, FeedVelocity))
	{
		return;
	}

	const FBodyInstance* PelvisBody = Mesh.GetBodyInstance(PelvisBone);
	if (!PelvisBody || !PelvisBody->IsValidBodyInstance())
	{
		return;
	}

	const FVector Pelvis = PelvisBody->GetUnrealWorldTransform().GetLocation();
	const FVector Error = Target - Pelvis;
	if (Error.ContainsNaN())
	{
		return;
	}

	if (Error.SizeSquared() > FMath::Square(FMath::Max(IMGRagdollBody::MinDistanceLimit, HardCorrectionDistance)))
	{
		TranslateBody(Mesh, Error);
		Mesh.SetAllPhysicsLinearVelocity(FeedVelocity);
		return;
	}

	const FVector Up = -Movement->GetGravityDirection();
	// Match physical body motion; capsule floor lift/correction must never accelerate the ragdoll.
	const float PosTime = FMath::Max(IMGRagdollBody::MinCorrectionTime, RemoteCorrectionTime);
	const float VelTime = FMath::Min(FMath::Max(IMGRagdollBody::MinCorrectionTime, RemoteVelocityCorrectionTime), PosTime * 0.25f);
	const FVector CurrentVelocity = PelvisBody->GetUnrealWorldVelocityAtPoint(Pelvis);
	if (Error.IsNearlyZero(RemotePositionTolerance)
		&& FeedVelocity.IsNearlyZero(IMGRagdollBody::SettledVelocityTolerance)
		&& CurrentVelocity.IsNearlyZero(IMGRagdollBody::SettledVelocityTolerance))
	{
		return;
	}

	FVector VelocityCorrection = IMGRagdollBody::ClampHorizontalAndVertical(
		Error / PosTime, Up, RemoteMaxVelocityCorrection, RemoteMaxVerticalVelocityCorrection);

	const FVector TravelDirection = FeedVelocity.GetSafeNormal();
	const float AlongTravel = FVector::DotProduct(VelocityCorrection, TravelDirection);
	const float BackwardLimit = FMath::Max(0.0f, RemoteMaxBackwardCorrection);
	if (AlongTravel < -BackwardLimit)
	{
		VelocityCorrection += TravelDirection * (-BackwardLimit - AlongTravel);
	}

	// Implicit damping remains stable when a render frame spans several physics steps.
	const double Step = DeltaTime;
	const double DampingTime = VelTime + Step + Step * Step / PosTime;
	const FVector Acceleration = IMGRagdollBody::ClampHorizontalAndVertical(
		(VelocityCorrection + (FeedVelocity - CurrentVelocity) * (1.0 + Step / PosTime)) / DampingTime,
		Up,
		RemoteMaxAcceleration,
		RemoteMaxVerticalAcceleration);
	AccelerateBody(Mesh, Acceleration);
}

void UIMGRagdollComponent::UpdateBodyCorrection(float DeltaTime)
{
	if (!HasCurrentBody()
		|| DeltaTime <= SMALL_NUMBER
		|| !FMath::IsFinite(DeltaTime)
		|| !Movement.IsValid()
		|| !Movement->UpdatedComponent)
	{
		return;
	}

	USkeletalMeshComponent& Mesh = *BodySessionState.Mesh.Get();
	if (IsBodyInputProducer())
	{
		UpdateOwnerBodyCorrection(Mesh, DeltaTime);
	}
	else
	{
		UpdateRemoteBodyCorrection(Mesh, DeltaTime);
	}
}

void UIMGRagdollComponent::PrepareBodyRecovery()
{
	if (!HasCurrentBody() || BodySessionState.bRecovering)
	{
		return;
	}

	USkeletalMeshComponent& Mesh = *BodySessionState.Mesh.Get();
	Mesh.SnapshotPose(RecoveryHold.Snapshot.Pose);
	RecoveryHold.MeshAsset = Mesh.GetSkeletalMeshAsset();
	RecoveryHold.Snapshot.MeshWorldTransform = Mesh.GetComponentTransform();
	RecoveryHold.BodyState.Capture(Mesh, *Character);
	const int32 PelvisIndex = Mesh.GetBoneIndex(PelvisBone);
	RecoveryHold.Snapshot.PelvisWorldLocation = PelvisIndex != INDEX_NONE
		? Mesh.GetBoneTransform(PelvisIndex).GetLocation()
		: RecoveryHold.Snapshot.MeshWorldTransform.GetLocation();
	ComputePoseFacing(Mesh, RecoveryHold.Snapshot.FacingYaw, RecoveryHold.Snapshot.bFaceUp);

	StopBodySimulation(Mesh);
	Mesh.SetAllBodiesSimulatePhysics(false);
	for (FBodyInstance* Body : Mesh.Bodies)
	{
		if (!Body || !Body->IsValidBodyInstance() || !Body->GetBodySetup())
		{
			continue;
		}
		// A held pose has no contact authority after the capsule takes over.
		const int32 ShapeCount = Body->GetBodySetup()->AggGeom.GetElementCount();
		for (int32 Index = 0; Index < ShapeCount; ++Index)
		{
			Body->SetShapeCollisionEnabled(Index, ECollisionEnabled::NoCollision);
		}
	}
	Mesh.SetComponentTickEnabled(false);
	Mesh.bPauseAnims = true;
	BodySessionState.bRecovering = true;
	BodySessionState.OwnerCorrectionRemaining = FVector::ZeroVector;

	// Keep the detached component's final rendered pose until the animation adapter takes the snapshot.
}

bool UIMGRagdollComponent::GetRecoveryPose(int32 ExpectedSession, FIMGRagdollPoseSnapshot& OutSnapshot) const
{
	OutSnapshot = FIMGRagdollPoseSnapshot();
	if (ExpectedSession == 0
		|| ExpectedSession != ActiveState.Session
		|| ActiveState.Phase != EIMGRagdollPhase::Recovering
		|| !RecoveryHold.Snapshot.IsValid()
		|| !Character.IsValid()
		|| !Character->GetMesh()
		|| Character->GetMesh()->GetSkeletalMeshAsset() != RecoveryHold.MeshAsset.Get())
	{
		return false;
	}

	OutSnapshot = RecoveryHold.Snapshot;
	return true;
}

bool UIMGRagdollComponent::ReleaseRecoveryHold(int32 ExpectedSession)
{
	if (!IsRagdollAvailable()
		|| ExpectedSession == 0
		|| ExpectedSession != ActiveState.Session
		|| ActiveState.Phase != EIMGRagdollPhase::Recovering
		|| !Movement.IsValid()
		|| !Movement->IsRagdollRecoveryReady())
	{
		return false;
	}
	if (BodySessionState.Session != 0 && (!HasCurrentBody() || !BodySessionState.bRecovering))
	{
		return false;
	}

	ReleaseBody();
	return true;
}

bool UIMGRagdollComponent::HandOffRagdollBody(int32 ExpectedSession)
{
	if (ActiveState.bExternalTransition
		|| ExpectedSession == 0
		|| ExpectedSession != ActiveState.Session
		|| !HasCurrentBody())
	{
		return false;
	}

	BodySessionState.HandedOffSession = BodySessionState.Session;
	if (Movement.IsValid())
	{
		Movement->ClearRagdollFollowTarget(this, BodySessionState.Session);
	}
	ReleaseBody(/*bKeepPhysics=*/true, /*bResumeHeldBody=*/true);
	SetBodyIssue(EIMGRagdollBodyIssue::BodyHandedOff);
	return true;
}

bool UIMGRagdollComponent::GetTransitionPose(
	int32 ExpectedSession,
	int32 ExpectedRevision,
	FIMGRagdollPoseSnapshot& OutSnapshot) const
{
	OutSnapshot = FIMGRagdollPoseSnapshot();
	if (!IsTransitionReady(ExpectedSession, ExpectedRevision)
		|| TransitionTracker.ReadyRevision != ExpectedRevision
		|| !RecoveryHold.Snapshot.IsValid()
		|| !Character->GetMesh()
		|| Character->GetMesh()->GetSkeletalMeshAsset() != RecoveryHold.MeshAsset.Get())
	{
		return false;
	}

	OutSnapshot = RecoveryHold.Snapshot;
	return true;
}

bool UIMGRagdollComponent::ReleaseTransitionPose(int32 ExpectedSession, int32 ExpectedRevision, bool bDiscard)
{
	if (!IsRagdollAvailable()
		|| ExpectedSession == 0
		|| ExpectedSession != ActiveState.Session
		|| ExpectedRevision == 0
		|| ExpectedRevision != ActiveState.Revision
		|| !ActiveState.bExternalTransition
		|| (BodySessionState.Session != 0 && BodySessionState.Session != ExpectedSession)
		|| (!bDiscard
			&& (TransitionTracker.ReadyRevision != ExpectedRevision
				|| !IsTransitionReady(ExpectedSession, ExpectedRevision))))
	{
		return false;
	}

	TransitionTracker.ReleasedRevision = static_cast<uint16>(ExpectedRevision);
	if (bDiscard)
	{
		TransitionTracker.ReadyRevision = static_cast<uint16>(ExpectedRevision);
		EndLocalSession(EIMGRagdollEndReason::Transferred);
	}
	else
	{
		ReleaseBody();
	}

	if (ActiveState.Session == ExpectedSession && ActiveState.Revision == ExpectedRevision)
	{
		SetExecutionTickEnabled(false);
	}
	return true;
}

void UIMGRagdollComponent::ReleaseBody(bool bKeepPhysics, bool bResumeHeldBody)
{
	if (bReleasingBody || BodySessionState.Session == 0)
	{
		return;
	}

	TGuardValue<bool> ReleaseGuard(bReleasingBody, true);
	USkeletalMeshComponent* Mesh = BodySessionState.Mesh.Get();
	const uint16 Session = BodySessionState.Session;
	const bool bRestoreBodies = HasCurrentBody();
	const bool bWasRecovering = BodySessionState.bRecovering;
	BodySessionState.Session = 0;

	if (Movement.IsValid())
	{
		Movement->ClearRagdollFollowTarget(this, Session);
	}

	if (IsValid(Mesh) && !Mesh->IsBeingDestroyed())
	{
		StopBodySimulation(*Mesh);
		if (bKeepPhysics && bResumeHeldBody && bWasRecovering && bRestoreBodies)
		{
			IMGRagdollBody::RetireKinematicUpdate(*Mesh);
			for (const FSavedBodyInstanceState& Saved : RecoveryHold.BodyState.Bodies)
			{
				FBodyInstance* Body = Mesh->GetBodyInstance(Saved.BoneName);
				if (!Body || !Body->IsValidBodyInstance())
				{
					continue;
				}
				Body->SetBodyTransform(Saved.WorldTransform, ETeleportType::TeleportPhysics);
				for (int32 Index = 0; Index < Saved.ShapeCollisions.Num(); ++Index)
				{
					Body->SetShapeCollisionEnabled(Index, Saved.ShapeCollisions[Index]);
				}
			}
			Mesh->bPauseAnims = SavedMeshState.bPauseAnims;
			Mesh->SetComponentTickEnabled(true);
			Mesh->SetAllBodiesSimulatePhysics(true);
			Mesh->SetAllBodiesPhysicsBlendWeight(1.f);
			for (const FSavedBodyInstanceState& Saved : RecoveryHold.BodyState.Bodies)
			{
				if (FBodyInstance* Body = Mesh->GetBodyInstance(Saved.BoneName); Body && Body->IsValidBodyInstance())
				{
					Body->SetLinearVelocity(Saved.LinearVelocity, false);
					Body->SetAngularVelocityInRadians(Saved.AngularVelocityRadians, false);
				}
			}
		}
		if (!bKeepPhysics)
		{
			SavedMeshState.Restore(*Mesh, Character.Get(), BodyCollisionProfile, bRestoreBodies);
			if (Movement.IsValid())
			{
				Movement->ResetRagdollMeshSmoothing(Session);
			}
		}
	}

	BodySessionState.ResetActiveBody();
	SavedMeshState.Reset();
}
