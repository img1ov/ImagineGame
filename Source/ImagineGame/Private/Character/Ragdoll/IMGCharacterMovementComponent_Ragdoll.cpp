#include "Character/IMGCharacterMovementComponent.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/ReplicatedState.h"
#include "Engine/ScopedMovementUpdate.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "IMGLogChannels.h"
#include "Templates/UnrealTemplate.h"

namespace IMGRagdollMovement
{
	constexpr double MaxBodyReferencePublishInterval = 0.05;
	constexpr double Quantize10Scale = 10.0;
	constexpr double Quantize100Scale = 100.0;
	// FVector_NetQuantize10 packs 24-bit signed integers scaled by 10 -> safe magnitude bound ~838,860 cm/s.
	constexpr double MaxNetQuantize10Component = 838860.0;
	constexpr float PlacementMatchTolerance = 0.1f;

	FVector QuantizeVector(const FVector& Value, double Scale)
	{
		return FVector(
			FMath::RoundToDouble(Value.X * Scale) / Scale,
			FMath::RoundToDouble(Value.Y * Scale) / Scale,
			FMath::RoundToDouble(Value.Z * Scale) / Scale);
	}

	bool QuantizeAndValidateWireVelocity(const FVector& InVelocity, FVector& OutQuantizedVelocity)
	{
		if (InVelocity.ContainsNaN())
		{
			return false;
		}
		OutQuantizedVelocity = QuantizeVector(InVelocity, Quantize10Scale);
		return !OutQuantizedVelocity.ContainsNaN()
			&& FMath::Abs(OutQuantizedVelocity.X) <= MaxNetQuantize10Component
			&& FMath::Abs(OutQuantizedVelocity.Y) <= MaxNetQuantize10Component
			&& FMath::Abs(OutQuantizedVelocity.Z) <= MaxNetQuantize10Component;
	}
}

void FIMGRagdollMovementRuntime::ClearInput()
{
	SubmittedInput = FIMGRagdollMovementInput();
	InputSource.Reset();
	InputSampleTime = -1.0;
	RemoteInputTime = -1.0;
	bAwaitingInitialInput = false;
	LocalLaunchVelocity = FVector::ZeroVector;
	LocalLaunchSession = 0;
}

void FIMGRagdollMovementRuntime::ClearCorrection()
{
	PreCorrectionLocation.Reset();
	CorrectionSession = 0;
}

void FIMGRagdollMovementRuntime::ClearAll()
{
	ClearInput();
	ClearCorrection();
	ActiveBodyReference = FIMGRagdollBodyReference();
	BodyReferencePublishTime = -1.0;
	InputMoveTimeStamp = -1.f;
	AdjustmentTimeStamp = -1.0f;
	AdjustmentPlacementSession = 0;
	OwningController.Reset();
}

bool UIMGCharacterMovementComponent::IsRagdollMovementMode() const
{
	return MovementMode == MOVE_Custom && CustomMovementMode == static_cast<uint8>(EIMGCustomMovementMode::Ragdoll);
}

bool UIMGCharacterMovementComponent::HasRagdollNetworkMovementMode() const
{
	if (IsRagdollMovementMode())
	{
		return true;
	}
	if (!CharacterOwner)
	{
		return false;
	}

	TEnumAsByte<EMovementMode> Mode;
	TEnumAsByte<EMovementMode> GroundMode;
	uint8 CustomMode = 0;
	UnpackNetworkMovementMode(CharacterOwner->GetReplicatedMovementMode(), Mode, CustomMode, GroundMode);
	return Mode == MOVE_Custom && CustomMode == static_cast<uint8>(EIMGCustomMovementMode::Ragdoll);
}

void UIMGCharacterMovementComponent::SimulatedTick(float DeltaSeconds)
{
	if (!HasRagdollNetworkMovementMode())
	{
		Super::SimulatedTick(DeltaSeconds);
		return;
	}

	if (bNetworkMovementModeChanged && CharacterOwner)
	{
		USkeletalMeshComponent* Mesh = CharacterOwner->GetMesh();
		// Keep the displayed world pose through admission, before a body reference can arrive.
		const FScopedPreventAttachedComponentMove PreserveEntryMesh(
			Mesh && Mesh->GetAttachParent() == UpdatedComponent ? Mesh : nullptr);
		ApplyNetworkMovementMode(CharacterOwner->GetReplicatedMovementMode());
		bNetworkMovementModeChanged = false;
	}

	if (!HasValidData())
	{
		return;
	}

	if (!IsRagdollMovementMode())
	{
		Super::SimulatedTick(DeltaSeconds);
		return;
	}

	// Presentation root motion cannot replace the authoritative ragdoll capsule stream.
	CharacterOwner->RootMotionRepMoves.Reset();
	CurrentRootMotion.Clear();
	RootMotionParams.Clear();
	bWasSimulatingRootMotion = false;

	if (CharacterOwner->IsReplicatingMovement() && UpdatedComponent)
	{
		USkeletalMeshComponent* Mesh = CharacterOwner->GetMesh();
		const bool bAttachedMesh = Mesh && Mesh->GetAttachParent() == UpdatedComponent;
		const FVector SavedMeshLocation = bAttachedMesh ? Mesh->GetRelativeLocation() : FVector::ZeroVector;
		const FQuat SavedRotation = UpdatedComponent->GetComponentQuat();
		const bool bPreventMeshMovement = bAttachedMesh && !bNetworkSmoothingComplete;
		{
			const FScopedPreventAttachedComponentMove PreventMeshMovement(bPreventMeshMovement ? Mesh : nullptr);
			SimulateMovement(DeltaSeconds);
		}
		if (HasValidData()
			&& bPreventMeshMovement
			&& NetworkSmoothingMode == ENetworkSmoothingMode::Linear
			&& Mesh == CharacterOwner->GetMesh()
			&& Mesh->GetAttachParent() == UpdatedComponent
			&& ClientPredictionData)
		{
			const FQuat Rotation = UpdatedComponent->GetComponentQuat();
			if (!Rotation.Equals(SavedRotation, 1e-6f))
			{
				const FQuat RotationDelta = Rotation - SavedRotation;
				ClientPredictionData->MeshRotationTarget += RotationDelta;
				ClientPredictionData->OriginalMeshRotationOffset += RotationDelta;
				ClientPredictionData->MeshRotationOffset = Rotation;
				Mesh->SetRelativeLocationAndRotation(SavedMeshLocation, CharacterOwner->GetBaseRotationOffset());
			}
		}
	}

	if (HasValidData() && !bNetworkSmoothingComplete)
	{
		SmoothClientPosition(DeltaSeconds);
	}
}

FIMGRagdollMovementInput UIMGCharacterMovementComponent::CaptureRagdollInput() const
{
	FIMGRagdollMovementInput Input;
	if (ActiveRagdollState.Phase != EIMGRagdollPhase::Simulating)
	{
		return Input;
	}

	Input.Session = ActiveRagdollState.Session;
	if (!RagdollRuntime.InputSource.IsValid() || RagdollRuntime.SubmittedInput.Session != Input.Session)
	{
		return Input;
	}

	Input = RagdollRuntime.SubmittedInput;
	if (!GetWorld()
		|| RagdollRuntime.InputSampleTime < 0.0
		|| GetWorld()->GetTimeSeconds() - RagdollRuntime.InputSampleTime > ActiveRagdollState.Settings.MaxTargetAge)
	{
		Input.Status = EIMGRagdollTargetStatus::Expired;
	}
	else if (Input.IsValid())
	{
		// Predict cached physical motion to this move boundary once; replay consumes the saved result.
		const double Age = FMath::Max(0.0, GetWorld()->GetTimeSeconds() - RagdollRuntime.InputSampleTime);
		Input.PelvisLocation = IMGRagdollMovement::QuantizeVector(
			FVector(Input.PelvisLocation) + FVector(Input.PelvisVelocity) * Age, IMGRagdollMovement::Quantize10Scale);
	}
	return Input;
}

FIMGRagdollMovementInput UIMGCharacterMovementComponent::GetRagdollMovementInput() const
{
	if (!HasValidData() || CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		return FIMGRagdollMovementInput();
	}
	if (CharacterOwner->bClientUpdating)
	{
		return RagdollMode.Input;
	}
	if (CharacterOwner->HasAuthority()
		&& !CharacterOwner->IsLocallyControlled()
		&& CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy)
	{
		FIMGRagdollMovementInput Input = RagdollMode.Input;
		if (Input.Status == EIMGRagdollTargetStatus::Ready
			&& (RagdollRuntime.RemoteInputTime < 0.0
				|| GetWorld()->GetTimeSeconds() - RagdollRuntime.RemoteInputTime > ActiveRagdollState.Settings.MaxTargetAge))
		{
			Input.Status = EIMGRagdollTargetStatus::Expired;
		}
		return Input;
	}
	return CaptureRagdollInput();
}

bool UIMGCharacterMovementComponent::FindRagdollRecoveryTransform(FTransform& OutTransform) const
{
	EIMGRagdollIssue Issue;
	return QueryRagdollRecovery(ActiveRagdollState.Session, OutTransform, Issue);
}

bool UIMGCharacterMovementComponent::QueryRagdollRecovery(
	int32 ExpectedSession,
	FTransform& OutTransform,
	EIMGRagdollIssue& OutIssue,
	FVector BodyLocalOffset) const
{
	OutTransform = FTransform::Identity;
	OutIssue = EIMGRagdollIssue::Unavailable;
	if (!HasValidData()
		|| !IsRegistered()
		|| !IsActive()
		|| IsBeingDestroyed()
		|| !IsRagdollMovementMode()
		|| CharacterOwner->IsActorBeingDestroyed()
		|| CharacterOwner->bClientUpdating
		|| CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy
		|| UpdatedComponent != CharacterOwner->GetCapsuleComponent()
		|| ActiveRagdollState.Phase != EIMGRagdollPhase::Simulating)
	{
		return false;
	}

	OutIssue = EIMGRagdollIssue::SessionMismatch;
	if (ExpectedSession == 0 || ExpectedSession != ActiveRagdollState.Session)
	{
		return false;
	}

	if (Velocity.ContainsNaN())
	{
		OutIssue = EIMGRagdollIssue::InvalidTarget;
		return false;
	}

	const float MaxSpeed = ActiveRagdollState.Settings.MaxRecoverySpeed;
	if (MaxSpeed > 0.0f && Velocity.SizeSquared() > FMath::Square(MaxSpeed))
	{
		OutIssue = EIMGRagdollIssue::RecoveryTooFast;
		return false;
	}

	return RagdollMode.FindRecoveryTransform(*this, OutTransform, OutIssue, BodyLocalOffset);
}

void UIMGCharacterMovementComponent::SetRagdollMovementIssue(EIMGRagdollIssue Issue)
{
	if (RagdollRuntime.Issue == Issue || (CharacterOwner && CharacterOwner->bClientUpdating))
	{
		return;
	}
	RagdollRuntime.Issue = Issue;
	UE_LOG(LogIMG, Verbose, TEXT("[Ragdoll] %s Role=%d Session=%u Revision=%u Phase=%d Issue=%s"),
		*GetNameSafe(GetOwner()),
		CharacterOwner ? static_cast<int32>(CharacterOwner->GetLocalRole()) : -1,
		ActiveRagdollState.Session,
		ActiveRagdollState.Revision,
		static_cast<int32>(ActiveRagdollState.Phase),
		*StaticEnum<EIMGRagdollIssue>()->GetNameStringByValue(static_cast<int64>(Issue)));
}

void UIMGCharacterMovementComponent::ApplyRagdollRecoveryTransform(const FTransform& Transform)
{
	if (!HasValidData()
		|| (!IsRagdollMovementMode() && !IsWalking() && !IsFalling())
		|| Transform.ContainsNaN())
	{
		return;
	}

	RagdollRuntime.LocalPlacementSession = ActiveRagdollState.Session;
	if (CharacterOwner->HasAuthority())
	{
		RagdollRuntime.RecoveryAppliedSession = ActiveRagdollState.Session;
	}
	RagdollMode.ApplyRecoveryTransform(*this, Transform);
}

const FIMGRagdollRepState& UIMGCharacterMovementComponent::GetRagdollMovementState() const
{
	return ActiveRagdollState;
}

bool UIMGCharacterMovementComponent::GetRagdollBodyReference(int32 ExpectedSession, FIMGRagdollBodyReference& OutReference) const
{
	OutReference = FIMGRagdollBodyReference();
	if (!HasValidData()
		|| !IsRegistered()
		|| !IsActive()
		|| IsBeingDestroyed()
		|| CharacterOwner->bClientUpdating
		|| ExpectedSession == 0
		|| ExpectedSession != ActiveRagdollState.Session
		|| ActiveRagdollState.Phase != EIMGRagdollPhase::Simulating
		|| !IsRagdollMovementMode()
		|| RagdollRuntime.ActiveBodyReference.Session != ExpectedSession
		|| RagdollRuntime.ActiveBodyReference.MovementRevision != ActiveRagdollState.Revision)
	{
		return false;
	}

	OutReference = RagdollRuntime.ActiveBodyReference;
	if (!OutReference.IsValid())
	{
		return false;
	}

	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	if (!World || (!CharacterOwner->HasAuthority() && !GameState))
	{
		return false;
	}

	const double Now = CharacterOwner->HasAuthority() ? World->GetTimeSeconds() : GameState->GetServerWorldTimeSeconds();
	if (!FMath::IsFinite(Now) || FMath::Abs(Now - OutReference.ServerTime) > ActiveRagdollState.Settings.MaxTargetAge)
	{
		OutReference.Issue = EIMGRagdollIssue::ExpiredTarget;
		return false;
	}

	OutReference.PelvisLocation = FRepMovement::RebaseOntoLocalOrigin(OutReference.PelvisLocation, this);
	OutReference.CapsuleLocation = FRepMovement::RebaseOntoLocalOrigin(OutReference.CapsuleLocation, this);
	return true;
}

void UIMGCharacterMovementComponent::PublishRagdollBodyReference(const FVector& PelvisLocation, EIMGRagdollIssue Issue)
{
	if (!HasValidData()
		|| !CharacterOwner->HasAuthority()
		|| CharacterOwner->bClientUpdating
		|| ActiveRagdollState.Session == 0
		|| ActiveRagdollState.Revision == 0
		|| !GetWorld())
	{
		return;
	}

	if (Issue == EIMGRagdollIssue::None
		&& (ActiveRagdollState.Phase != EIMGRagdollPhase::Simulating
			|| !IsRagdollMovementMode()
			|| !RagdollMode.Input.IsValid()
			|| RagdollMode.Input.Session != ActiveRagdollState.Session
			|| PelvisLocation.ContainsNaN()
			|| Velocity.ContainsNaN()))
	{
		return;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	const bool bStateChanged = RagdollRuntime.ActiveBodyReference.Session != ActiveRagdollState.Session
		|| RagdollRuntime.ActiveBodyReference.MovementRevision != ActiveRagdollState.Revision
		|| RagdollRuntime.ActiveBodyReference.Issue != Issue;

	FIMGRagdollBodyReference Reference;
	Reference.Session = ActiveRagdollState.Session;
	Reference.MovementRevision = ActiveRagdollState.Revision;
	if (++RagdollRuntime.BodyReferenceSequence == 0)
	{
		++RagdollRuntime.BodyReferenceSequence;
	}
	Reference.Sequence = RagdollRuntime.BodyReferenceSequence;
	Reference.ServerTime = Now;
	Reference.ClientTimeStamp = RagdollRuntime.InputMoveTimeStamp;
	Reference.Issue = Issue;

	if (Issue == EIMGRagdollIssue::None)
	{
		Reference.PelvisLocation = FRepMovement::RebaseOntoZeroOrigin(PelvisLocation, this);
		Reference.CapsuleLocation = FRepMovement::RebaseOntoZeroOrigin(UpdatedComponent->GetComponentLocation(), this);
		Reference.Velocity = FVector(RagdollMode.Input.PelvisVelocity).GetClampedToMaxSize(ActiveRagdollState.Settings.MaxFollowSpeed);
		Reference.FacingYaw = FRotator::DecompressAxisFromShort(RagdollMode.Input.FacingYaw);
		Reference.bFaceUp = RagdollMode.Input.bFaceUp;
	}

	RagdollRuntime.ActiveBodyReference = Reference;

	// Keep the current result for move responses; property replication has a bounded heartbeat, including at rest.
	const double PublishInterval = FMath::Min(
		IMGRagdollMovement::MaxBodyReferencePublishInterval,
		static_cast<double>(ActiveRagdollState.Settings.MaxTargetAge) * 0.5);
	if (bStateChanged
		|| RagdollRuntime.BodyReferencePublishTime < 0.0
		|| Now - RagdollRuntime.BodyReferencePublishTime >= PublishInterval)
	{
		ReplicatedRagdollBodyReference = Reference;
		RagdollRuntime.BodyReferencePublishTime = Now;
		CharacterOwner->ForceNetUpdate();
	}
}

void UIMGCharacterMovementComponent::OnRep_RagdollBodyReference()
{
	ReceiveRagdollBodyReference(ReplicatedRagdollBodyReference);
}

void UIMGCharacterMovementComponent::ReceiveRagdollBodyReference(const FIMGRagdollBodyReference& Reference)
{
	if (Reference.Session == 0
		|| Reference.MovementRevision == 0
		|| Reference.Sequence == 0
		|| !FMath::IsFinite(Reference.ServerTime)
		|| Reference.ServerTime < 0.0
		|| !FMath::IsFinite(Reference.ClientTimeStamp)
		|| static_cast<uint8>(Reference.Issue) > static_cast<uint8>(EIMGRagdollIssue::ExitBlocked)
		|| (Reference.Issue == EIMGRagdollIssue::None && !Reference.IsValid()))
	{
		return;
	}

	if (RagdollRuntime.ActiveBodyReference.Sequence != 0)
	{
		if (Reference.MovementRevision != RagdollRuntime.ActiveBodyReference.MovementRevision)
		{
			if (static_cast<int16>(Reference.MovementRevision - RagdollRuntime.ActiveBodyReference.MovementRevision) <= 0)
			{
				return;
			}
		}
		else if (static_cast<int32>(Reference.Sequence - RagdollRuntime.ActiveBodyReference.Sequence) <= 0)
		{
			return;
		}
	}

	// Cache early references without changing lifecycle or movement; the getter gates them on the matching phase.
	RagdollRuntime.ActiveBodyReference = Reference;
}

bool UIMGCharacterMovementComponent::IsRagdollRecovering() const
{
	return ActiveRagdollState.Phase == EIMGRagdollPhase::Recovering;
}

bool UIMGCharacterMovementComponent::IsRagdollRecoveryReady() const
{
	return IsRagdollRecovering()
		&& HasValidData()
		&& IsWalking()
		&& (CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy
			|| RagdollRuntime.RecoveryAppliedSession == ActiveRagdollState.Session);
}

FVector UIMGCharacterMovementComponent::GetRagdollLaunchVelocity() const
{
	if (CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_AutonomousProxy)
	{
		const FVector LocalVelocity = RagdollRuntime.LocalLaunchSession == ActiveRagdollState.Session
			? RagdollRuntime.LocalLaunchVelocity : Velocity + FVector(ActiveRagdollState.LaunchVelocityDelta);
		return LocalVelocity.ContainsNaN() ? FVector(ActiveRagdollState.InitialVelocity) : LocalVelocity;
	}
	return ActiveRagdollState.InitialVelocity;
}


bool UIMGCharacterMovementComponent::StartRagdollMovement(const FIMGRagdollSettings& Settings, FVector AdditionalVelocity)
{
	const FVector LaunchVelocity = Velocity + AdditionalVelocity
		+ (HasValidData() ? GetImpartedMovementBaseVelocity() : FVector::ZeroVector);
	if (RagdollRuntime.bTransitioning
		|| !HasValidData()
		|| !CharacterOwner->HasAuthority()
		|| !IsRegistered()
		|| !IsActive()
		|| IsBeingDestroyed()
		|| CharacterOwner->IsActorBeingDestroyed()
		|| UpdatedComponent != CharacterOwner->GetCapsuleComponent()
		|| MovementMode == MOVE_None
		|| UpdatedComponent->IsSimulatingPhysics()
		|| (!CharacterOwner->GetCapsuleComponent()->IsQueryCollisionEnabled()
			&& !(ActiveRagdollState.Phase == EIMGRagdollPhase::Simulating && IsRagdollMovementMode()))
		|| !Settings.IsValid()
		|| (!CharacterOwner->IsLocallyControlled() && CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy && !ShouldUsePackedMovementRPCs())
		|| AdditionalVelocity.ContainsNaN()
		|| LaunchVelocity.ContainsNaN())
	{
		SetRagdollMovementIssue(EIMGRagdollIssue::Unavailable);
		return false;
	}

	if (ActiveRagdollState.Phase == EIMGRagdollPhase::Simulating)
	{
		return IsRagdollMovementMode() || RagdollRuntime.bAwaitingInitialInput;
	}

	TGuardValue<bool> Transition(RagdollRuntime.bTransitioning, true);
	const uint16 Revision = ActiveRagdollState.Revision;
	uint16 Session = ActiveRagdollState.Session + 1;
	if (Session == 0)
	{
		++Session;
	}

	ActiveRagdollState = FIMGRagdollRepState();
	ActiveRagdollState.Revision = Revision;
	ActiveRagdollState.Session = Session;
	ActiveRagdollState.Settings = Settings;
	ActiveRagdollState.Phase = EIMGRagdollPhase::Simulating;
	ActiveRagdollState.AnchorLocation = FRepMovement::RebaseOntoZeroOrigin(CharacterOwner->GetActorLocation(), this);
	ActiveRagdollState.InitialVelocity = LaunchVelocity;
	ActiveRagdollState.LaunchVelocityDelta = AdditionalVelocity;
	ActiveRagdollState.StartServerTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	ActiveRagdollState.FacingYaw = FRotator::CompressAxisToShort(CharacterOwner->GetActorRotation().Yaw);

	RagdollRuntime.OwningController = CharacterOwner->GetController();
	RagdollRuntime.ClearInput();
	RagdollRuntime.RecoveryAppliedSession = 0;
	RagdollRuntime.LocalPlacementSession = 0;
	RagdollRuntime.AdjustmentTimeStamp = -1.0f;
	RagdollRuntime.AdjustmentPlacementSession = 0;

	// Old locomotion moves are still in flight until the owner has received this admission.
	RagdollRuntime.bAwaitingInitialInput = !CharacterOwner->IsLocallyControlled()
		&& CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy
		&& (IsMovingOnGround() || IsFalling());
	SetRagdollMovementIssue(EIMGRagdollIssue::None);
	PublishRagdollMovementState();
	if (!RagdollRuntime.bAwaitingInitialInput)
	{
		SetMovementMode(MOVE_Custom, static_cast<uint8>(EIMGCustomMovementMode::Ragdoll));
	}
	if (HasValidData() && !RagdollRuntime.bAwaitingInitialInput && !IsRagdollMovementMode())
	{
		FinishRagdollMovement(EIMGRagdollEndReason::MovementChanged, false);
	}
	if (HasValidData() && GetMovementNetDiagnosticsLevel() >= 1)
	{
		UE_LOG(LogIMGMovementNet, Log,
			TEXT("[RagdollAdmission] Actor=%s Session=%u WaitingForOwner=%d Capsule=%s Velocity=%s"),
			*GetNameSafe(CharacterOwner.Get()), ActiveRagdollState.Session, RagdollRuntime.bAwaitingInitialInput,
			*UpdatedComponent->GetComponentLocation().ToCompactString(), *Velocity.ToCompactString());
	}
	if (HasValidData() && !CharacterOwner->bClientUpdating)
	{
		OnRagdollMovementUpdated.Broadcast();
	}
	return HasValidData() && ActiveRagdollState.Phase == EIMGRagdollPhase::Simulating;
}

bool UIMGCharacterMovementComponent::SubmitRagdollFollowTarget(
	const FTransform& Target,
	const FVector& PelvisVelocity,
	float CapsuleHeightOffset,
	bool bFaceUp,
	UObject* InputSource,
	int32 ExpectedSession)
{
	if (!HasValidData()
		|| !IsRegistered()
		|| !IsActive()
		|| IsBeingDestroyed()
		|| CharacterOwner->bClientUpdating
		|| ActiveRagdollState.Phase != EIMGRagdollPhase::Simulating
		|| (CharacterOwner->GetLocalRole() != ROLE_AutonomousProxy
			&& !(CharacterOwner->HasAuthority()
				&& (CharacterOwner->IsLocallyControlled() || CharacterOwner->GetRemoteRole() != ROLE_AutonomousProxy))))
	{
		SetRagdollMovementIssue(EIMGRagdollIssue::Unavailable);
		return false;
	}

	if (ExpectedSession == 0 || ExpectedSession != ActiveRagdollState.Session)
	{
		SetRagdollMovementIssue(EIMGRagdollIssue::SessionMismatch);
		return false;
	}

	FVector WireVelocity;
	if (Target.ContainsNaN() || !Target.GetRotation().IsNormalized() || !IsValid(InputSource)
		|| !IMGRagdollMovement::QuantizeAndValidateWireVelocity(PelvisVelocity, WireVelocity)
		|| !FMath::IsFinite(CapsuleHeightOffset)
		|| FMath::Abs(CapsuleHeightOffset) > CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight())
	{
		SetRagdollMovementIssue(EIMGRagdollIssue::InvalidTarget);
		return false;
	}

	if (FVector::DistSquared(Target.GetLocation(), UpdatedComponent->GetComponentLocation())
		> FMath::Square(ActiveRagdollState.Settings.MaxTargetDistance))
	{
		SetRagdollMovementIssue(EIMGRagdollIssue::TargetTooFar);
		return false;
	}

	if (RagdollRuntime.InputSource.IsValid() && RagdollRuntime.InputSource.Get() != InputSource)
	{
		SetRagdollMovementIssue(EIMGRagdollIssue::InputSourceBusy);
		return false;
	}

	const FVector Position = FRepMovement::RebaseOntoZeroOrigin(Target.GetLocation(), this);
	RagdollRuntime.SubmittedInput.Session = ActiveRagdollState.Session;
	RagdollRuntime.SubmittedInput.Status = EIMGRagdollTargetStatus::Ready;
	RagdollRuntime.SubmittedInput.PelvisLocation = IMGRagdollMovement::QuantizeVector(Position, IMGRagdollMovement::Quantize10Scale);
	RagdollRuntime.SubmittedInput.PelvisVelocity = WireVelocity;
	RagdollRuntime.SubmittedInput.CapsuleHeightOffset = CapsuleHeightOffset;
	RagdollRuntime.SubmittedInput.FacingYaw = FRotator::CompressAxisToShort(Target.Rotator().Yaw);
	RagdollRuntime.SubmittedInput.bFaceUp = bFaceUp;
	RagdollRuntime.InputSource = InputSource;
	RagdollRuntime.InputSampleTime = GetWorld()->GetTimeSeconds();
	return true;
}

void UIMGCharacterMovementComponent::ClearRagdollFollowTarget(const UObject* InputSource, int32 ExpectedSession)
{
	if (ExpectedSession != 0
		&& ExpectedSession == ActiveRagdollState.Session
		&& RagdollRuntime.InputSource.Get() == InputSource)
	{
		RagdollRuntime.SubmittedInput = FIMGRagdollMovementInput();
		RagdollRuntime.InputSource.Reset();
		RagdollRuntime.InputSampleTime = -1.0;
	}
}

bool UIMGCharacterMovementComponent::RecoverRagdollMovement(int32 ExpectedSession)
{
	FTransform RecoveryTransform;
	EIMGRagdollIssue Issue;
	if (!QueryRagdollRecovery(ExpectedSession, RecoveryTransform, Issue))
	{
		SetRagdollMovementIssue(Issue);
		return false;
	}
	return RecoverRagdollMovementAt(ExpectedSession, RecoveryTransform);
}

bool UIMGCharacterMovementComponent::RecoverRagdollMovementAt(
	int32 ExpectedSession,
	const FTransform& Transform,
	FVector BodyLocalOffset)
{
	if (RagdollRuntime.bTransitioning
		|| !HasValidData()
		|| !CharacterOwner->HasAuthority()
		|| !IsRegistered()
		|| !IsActive()
		|| IsBeingDestroyed()
		|| CharacterOwner->IsActorBeingDestroyed()
		|| !IsRagdollMovementMode()
		|| CharacterOwner->bClientUpdating
		|| UpdatedComponent != CharacterOwner->GetCapsuleComponent()
		|| ActiveRagdollState.Phase != EIMGRagdollPhase::Simulating)
	{
		SetRagdollMovementIssue(EIMGRagdollIssue::Unavailable);
		return false;
	}

	if (ExpectedSession == 0 || ExpectedSession != ActiveRagdollState.Session)
	{
		SetRagdollMovementIssue(EIMGRagdollIssue::SessionMismatch);
		return false;
	}

	if (Velocity.ContainsNaN())
	{
		SetRagdollMovementIssue(EIMGRagdollIssue::InvalidTarget);
		return false;
	}

	if (ActiveRagdollState.Settings.MaxRecoverySpeed > 0.0f
		&& Velocity.SizeSquared() > FMath::Square(ActiveRagdollState.Settings.MaxRecoverySpeed))
	{
		SetRagdollMovementIssue(EIMGRagdollIssue::RecoveryTooFast);
		return false;
	}

	EIMGRagdollIssue Issue;
	FTransform RecoveryTransform;
	if (!BuildRagdollPlacement(Transform, true, BodyLocalOffset, RecoveryTransform, Issue))
	{
		SetRagdollMovementIssue(Issue);
		return false;
	}

	const FVector QuantizedPosition = FRepMovement::RebaseOntoZeroOrigin(RecoveryTransform.GetLocation(), this);
	const uint16 FacingYaw = FRotator::CompressAxisToShort(Transform.Rotator().Yaw);
	const bool bFaceUp = GetRagdollMovementInput().bFaceUp;

	TGuardValue<bool> Transition(RagdollRuntime.bTransitioning, true);
	ActiveRagdollState.Phase = EIMGRagdollPhase::Recovering;
	ActiveRagdollState.bHasRecovery = true;
	ActiveRagdollState.bFaceUp = bFaceUp;
	ActiveRagdollState.AnchorLocation = QuantizedPosition;
	ActiveRagdollState.FacingYaw = FacingYaw;
	RagdollRuntime.ClearInput();

	SetRagdollMovementIssue(EIMGRagdollIssue::None);
	PublishRagdollMovementState();
	const uint16 RecoveryRevision = ActiveRagdollState.Revision;
	// Let the body consumer freeze its world pose before capsule collision/placement changes.
	OnRagdollMovementUpdated.Broadcast();
	if (!HasValidData() || ActiveRagdollState.Revision != RecoveryRevision
		|| ActiveRagdollState.Phase != EIMGRagdollPhase::Recovering)
	{
		return false;
	}
	ApplyRagdollRecoveryTransform(RecoveryTransform);
	if (HasValidData() && MovementMode != MOVE_Walking)
	{
		FinishRagdollMovement(EIMGRagdollEndReason::MovementChanged, false);
	}
	if (HasValidData() && !CharacterOwner->bClientUpdating)
	{
		OnRagdollMovementUpdated.Broadcast();
	}
	return HasValidData()
		&& ActiveRagdollState.Session == ExpectedSession
		&& (ActiveRagdollState.Phase == EIMGRagdollPhase::Recovering
			|| (ActiveRagdollState.Phase == EIMGRagdollPhase::Inactive && ActiveRagdollState.EndReason == EIMGRagdollEndReason::Recovered));
}

bool UIMGCharacterMovementComponent::SupportsRagdollTransitionMode(EMovementMode TargetMode, uint8 TargetCustomMode) const
{
	return TargetCustomMode == 0
		&& (TargetMode == MOVE_Walking || TargetMode == MOVE_Falling || TargetMode == MOVE_Flying || TargetMode == MOVE_Swimming);
}

bool UIMGCharacterMovementComponent::BuildRagdollPlacement(
	const FTransform& Transform,
	bool bRequireFloor,
	const FVector& BodyLocalOffset,
	FTransform& OutTransform,
	EIMGRagdollIssue& OutIssue) const
{
	OutTransform = FTransform::Identity;
	if (!RagdollMode.ValidateTransitionTransform(*this, Transform, bRequireFloor, OutIssue, BodyLocalOffset))
	{
		return false;
	}

	const FVector Position = FRepMovement::RebaseOntoZeroOrigin(Transform.GetLocation(), this);
	const FVector QuantizedPosition = IMGRagdollMovement::QuantizeVector(Position, IMGRagdollMovement::Quantize100Scale);
	const uint16 Yaw = FRotator::CompressAxisToShort(Transform.Rotator().Yaw);
	const FVector Up = -GetGravityDirection();
	const FVector Facing = FVector::VectorPlaneProject(
		FRotator(0.f, FRotator::DecompressAxisFromShort(Yaw), 0.f).Vector(), Up).GetSafeNormal();
	if (Facing.IsNearlyZero())
	{
		OutIssue = EIMGRagdollIssue::InvalidTarget;
		return false;
	}

	OutTransform = FTransform(
		FRotationMatrix::MakeFromXZ(Facing, Up).ToQuat(),
		FRepMovement::RebaseOntoLocalOrigin(QuantizedPosition, this));
	return RagdollMode.ValidateTransitionTransform(*this, OutTransform, bRequireFloor, OutIssue, BodyLocalOffset);
}

bool UIMGCharacterMovementComponent::ValidateRagdollTransitionRequest(
	int32 ExpectedSession,
	const FIMGRagdollTransitionRequest& Request,
	FTransform& OutPlacement,
	EIMGRagdollIssue& OutIssue) const
{
	OutPlacement = FTransform::Identity;
	OutIssue = EIMGRagdollIssue::Unavailable;
	if (!HasValidData()
		|| !IsRegistered()
		|| !IsActive()
		|| IsBeingDestroyed()
		|| CharacterOwner->IsActorBeingDestroyed()
		|| CharacterOwner->bClientUpdating
		|| CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy
		|| !IsRagdollMovementMode()
		|| UpdatedComponent != CharacterOwner->GetCapsuleComponent()
		|| ActiveRagdollState.Phase != EIMGRagdollPhase::Simulating
		|| Request.TargetMode <= MOVE_None
		|| Request.TargetMode >= MOVE_MAX
		|| (Request.TargetMode != MOVE_Custom && Request.TargetCustomMode != 0)
		|| (Request.TargetMode == MOVE_Custom && Request.TargetCustomMode == static_cast<uint8>(EIMGCustomMovementMode::Ragdoll))
		|| !SupportsRagdollTransitionMode(Request.TargetMode, Request.TargetCustomMode))
	{
		return false;
	}

	if (ExpectedSession == 0 || ExpectedSession != ActiveRagdollState.Session)
	{
		OutIssue = EIMGRagdollIssue::SessionMismatch;
		return false;
	}

	const FIMGRagdollMovementInput Input = GetRagdollMovementInput();
	if (!Input.IsValid() || Input.Session != ExpectedSession)
	{
		OutIssue = Input.Status == EIMGRagdollTargetStatus::Expired
			? EIMGRagdollIssue::ExpiredTarget
			: EIMGRagdollIssue::MissingTarget;
		return false;
	}

	return BuildRagdollPlacement(
		Request.Transform,
		Request.TargetMode == MOVE_Walking,
		Request.BodyLocalOffset,
		OutPlacement,
		OutIssue);
}

bool UIMGCharacterMovementComponent::QueryRagdollTransition(
	int32 ExpectedSession,
	const FIMGRagdollTransitionRequest& Request,
	EIMGRagdollIssue& OutIssue) const
{
	FTransform Placement;
	return ValidateRagdollTransitionRequest(ExpectedSession, Request, Placement, OutIssue);
}

bool UIMGCharacterMovementComponent::TransitionRagdollMovementAt(
	int32 ExpectedSession,
	const FIMGRagdollTransitionRequest& Request,
	int32& OutTransitionRevision)
{
	OutTransitionRevision = 0;
	if (RagdollRuntime.bTransitioning || !HasValidData() || !CharacterOwner->HasAuthority())
	{
		return false;
	}

	FTransform Placement;
	EIMGRagdollIssue Issue;
	if (!ValidateRagdollTransitionRequest(ExpectedSession, Request, Placement, Issue))
	{
		SetRagdollMovementIssue(Issue);
		return false;
	}

	FVector QuantizedVelocity;
	if (!IMGRagdollMovement::QuantizeAndValidateWireVelocity(Request.CarryVelocity, QuantizedVelocity))
	{
		SetRagdollMovementIssue(EIMGRagdollIssue::InvalidTarget);
		return false;
	}

	const uint16 Revision = ActiveRagdollState.Revision;
	const FVector PreviousVelocity = Velocity;
	const FTransform PreviousTransform = UpdatedComponent->GetComponentTransform();
	const bool bFaceUp = GetRagdollMovementInput().bFaceUp;
	TGuardValue<bool> Transition(RagdollRuntime.bTransitioning, true);

	{
		FScopedMovementUpdate ScopedPlacement(UpdatedComponent, EScopedUpdate::DeferredUpdates);
		FHitResult Hit;
		MoveUpdatedComponent(
			Placement.GetLocation() - UpdatedComponent->GetComponentLocation(),
			Placement.GetRotation(),
			false,
			&Hit,
			ETeleportType::TeleportPhysics);
		if (!HasValidData() || ActiveRagdollState.Revision != Revision)
		{
			return false;
		}
		if (!IsRagdollMovementMode())
		{
			FinishRagdollMovement(EIMGRagdollEndReason::MovementChanged, false);
			return false;
		}
		if (!UpdatedComponent->GetComponentLocation().Equals(Placement.GetLocation(), IMGRagdollMovement::PlacementMatchTolerance))
		{
			ScopedPlacement.RevertMove();
			Velocity = PreviousVelocity;
			SetRagdollMovementIssue(EIMGRagdollIssue::PlacementBlocked);
			return false;
		}

		Velocity = QuantizedVelocity;
		RagdollMode.ReleaseOrientation(*this);
		SetMovementMode(Request.TargetMode, Request.TargetCustomMode);
		if (!HasValidData() || ActiveRagdollState.Revision != Revision)
		{
			return false;
		}
		if (MovementMode != Request.TargetMode
			|| (Request.TargetMode == MOVE_Custom && CustomMovementMode != Request.TargetCustomMode))
		{
			if (IsRagdollMovementMode())
			{
				ScopedPlacement.RevertMove();
				Velocity = PreviousVelocity;
				RagdollMode.AcquireOrientation(*this);
				SetRagdollMovementIssue(EIMGRagdollIssue::Unavailable);
				return false;
			}
			// A different execution took over during the mode callback; never roll it back to ragdoll.
			FinishRagdollMovement(EIMGRagdollEndReason::MovementChanged, false);
			return false;
		}
	} // Resolve deferred overlap/volume callbacks before publishing a successful transfer.

	if (!HasValidData() || ActiveRagdollState.Revision != Revision)
	{
		return false;
	}
	if (MovementMode != Request.TargetMode
		|| (Request.TargetMode == MOVE_Custom && CustomMovementMode != Request.TargetCustomMode))
	{
		if (IsRagdollMovementMode())
		{
			FHitResult RollbackHit;
			MoveUpdatedComponent(
				PreviousTransform.GetLocation() - UpdatedComponent->GetComponentLocation(),
				PreviousTransform.GetRotation(),
				false,
				&RollbackHit,
				ETeleportType::TeleportPhysics);
			Velocity = PreviousVelocity;
			RagdollMode.AcquireOrientation(*this);
			SetRagdollMovementIssue(EIMGRagdollIssue::Unavailable);
			return false;
		}
		FinishRagdollMovement(EIMGRagdollEndReason::MovementChanged, false);
		return false;
	}

	bJustTeleported = true;
	ActiveRagdollState.Phase = EIMGRagdollPhase::Inactive;
	ActiveRagdollState.EndReason = EIMGRagdollEndReason::Transferred;
	ActiveRagdollState.bHasRecovery = true;
	ActiveRagdollState.bExternalTransition = true;
	ActiveRagdollState.TransitionMode = Request.TargetMode;
	ActiveRagdollState.TransitionCustomMode = Request.TargetCustomMode;
	ActiveRagdollState.TransitionVelocity = QuantizedVelocity;
	ActiveRagdollState.AnchorLocation = FRepMovement::RebaseOntoZeroOrigin(UpdatedComponent->GetComponentLocation(), this);
	ActiveRagdollState.FacingYaw = FRotator::CompressAxisToShort(UpdatedComponent->GetComponentRotation().Yaw);
	ActiveRagdollState.bFaceUp = bFaceUp;

	RagdollRuntime.ClearInput();
	RagdollRuntime.RecoveryAppliedSession = ActiveRagdollState.Session;
	SetRagdollMovementIssue(EIMGRagdollIssue::None);
	PublishRagdollMovementState();
	OutTransitionRevision = ActiveRagdollState.Revision;
	OnRagdollMovementUpdated.Broadcast();
	return true;
}

bool UIMGCharacterMovementComponent::IsRagdollTransitionReady(int32 ExpectedSession, int32 ExpectedRevision) const
{
	return HasValidData()
		&& IsRegistered()
		&& IsActive()
		&& !CharacterOwner->bClientUpdating
		&& ExpectedSession != 0
		&& ExpectedSession == ActiveRagdollState.Session
		&& ExpectedRevision != 0
		&& ExpectedRevision == ActiveRagdollState.Revision
		&& ActiveRagdollState.bExternalTransition
		&& ActiveRagdollState.Phase == EIMGRagdollPhase::Inactive
		&& ActiveRagdollState.EndReason == EIMGRagdollEndReason::Transferred
		&& MovementMode == ActiveRagdollState.TransitionMode
		&& (MovementMode != MOVE_Custom || CustomMovementMode == ActiveRagdollState.TransitionCustomMode)
		&& !IsRagdollMovementMode()
		&& (CharacterOwner->HasAuthority()
			|| CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy
			|| RagdollRuntime.RecoveryAppliedSession == ExpectedSession);
}

bool UIMGCharacterMovementComponent::StopRagdollMovement(EIMGRagdollEndReason Reason, int32 ExpectedSession)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	if (ExpectedSession != 0 && ExpectedSession != ActiveRagdollState.Session)
	{
		SetRagdollMovementIssue(EIMGRagdollIssue::SessionMismatch);
		return false;
	}

	if (ActiveRagdollState.Phase == EIMGRagdollPhase::Inactive)
	{
		if (ActiveRagdollState.bExternalTransition)
		{
			return true;
		}
		if (!RagdollRuntime.bTransitioning && HasValidData() && IsRagdollMovementMode())
		{
			SetMovementMode(MOVE_Falling);
		}
		return !IsRagdollMovementMode();
	}

	const bool bForce = Reason == EIMGRagdollEndReason::Unavailable || Reason == EIMGRagdollEndReason::MovementChanged;
	TGuardValue<bool> Transition(RagdollRuntime.bTransitioning, true);

	if (bForce && (IsBeingDestroyed() || GetOwner()->IsActorBeingDestroyed() || !IsRegistered()))
	{
		if (HasValidData() && IsRagdollMovementMode())
		{
			SetMovementMode(MOVE_None);
		}
		FinishRagdollMovement(Reason, true);
		return ActiveRagdollState.Phase == EIMGRagdollPhase::Inactive;
	}

	if (HasValidData() && IsRagdollMovementMode())
	{
		FTransform ExitTransform;
		FTransform QuantizedTransform;
		EIMGRagdollIssue Issue;
		const bool bFoundExit = RagdollMode.FindExitTransform(*this, ExitTransform, Issue)
			&& BuildRagdollPlacement(ExitTransform, /*bRequireFloor=*/false, FVector::ZeroVector, QuantizedTransform, Issue);
		if (!bFoundExit)
		{
			if (RagdollRuntime.Issue != EIMGRagdollIssue::ExitBlocked)
			{
				UE_LOG(LogIMG, Verbose, TEXT("[Ragdoll][ExitRejected] %s Session=%u Cause=%s"),
					*GetNameSafe(GetOwner()),
					ActiveRagdollState.Session,
					*StaticEnum<EIMGRagdollIssue>()->GetNameStringByValue(static_cast<int64>(Issue)));
			}
			SetRagdollMovementIssue(EIMGRagdollIssue::ExitBlocked);
			if (!bForce)
			{
				return false;
			}
			// Forced teardown cannot retain a live session or restore blocking collision inside geometry.
			SetMovementMode(MOVE_None);
		}
		else
		{
			ActiveRagdollState.AnchorLocation = FRepMovement::RebaseOntoZeroOrigin(QuantizedTransform.GetLocation(), this);
			ActiveRagdollState.FacingYaw = FRotator::CompressAxisToShort(QuantizedTransform.Rotator().Yaw);
			ActiveRagdollState.bHasRecovery = true;
			const FIMGRagdollMovementInput ExitInput = GetRagdollMovementInput();
			if (ExitInput.IsValid() && ExitInput.Session == ActiveRagdollState.Session)
			{
				// A safe abort carries physical motion, not the velocity of capsule corrections.
				Velocity = FVector(ExitInput.PelvisVelocity).GetClampedToMaxSize(ActiveRagdollState.Settings.MaxFollowSpeed);
			}
			SetRagdollMovementIssue(EIMGRagdollIssue::None);
			FinishRagdollMovement(Reason, true, true);
			return ActiveRagdollState.Phase == EIMGRagdollPhase::Inactive;
		}
	}

	FinishRagdollMovement(Reason, true);
	return ActiveRagdollState.Phase == EIMGRagdollPhase::Inactive;
}

void UIMGCharacterMovementComponent::CompleteRagdollRecovery(int32 ExpectedSession, bool bInterrupted)
{
	if (GetOwner()
		&& GetOwner()->HasAuthority()
		&& ExpectedSession != 0
		&& ExpectedSession == ActiveRagdollState.Session
		&& IsRagdollRecovering())
	{
		FinishRagdollMovement(
			bInterrupted ? EIMGRagdollEndReason::Interrupted : EIMGRagdollEndReason::Recovered,
			false);
	}
}

void UIMGCharacterMovementComponent::FinishRagdollMovement(
	EIMGRagdollEndReason Reason,
	bool bLeaveMode,
	bool bKeepPlacement)
{
	if (ActiveRagdollState.Phase == EIMGRagdollPhase::Inactive)
	{
		if (bLeaveMode && IsRagdollMovementMode())
		{
			SetMovementMode(MOVE_Falling);
		}
		return;
	}

	TGuardValue<bool> Transition(RagdollRuntime.bTransitioning, true);
	bKeepPlacement |= IsRagdollRecovering() && IsWalking() && ActiveRagdollState.bHasRecovery;
	ActiveRagdollState.Phase = EIMGRagdollPhase::Inactive;
	ActiveRagdollState.EndReason = Reason;
	if (!bKeepPlacement && Reason != EIMGRagdollEndReason::Recovered)
	{
		ActiveRagdollState.bHasRecovery = false;
	}

	RagdollRuntime.ClearInput();
	PublishRagdollMovementState();

	if (bLeaveMode && IsRagdollMovementMode())
	{
		if (bKeepPlacement && HasValidData())
		{
			ApplyRagdollRecoveryTransform(GetRagdollPlacementTransform());
		}
		else
		{
			SetMovementMode(MOVE_Falling);
		}
	}

	RagdollMode.ReleaseOrientation(*this);
	if (!CharacterOwner || !CharacterOwner->bClientUpdating)
	{
		OnRagdollMovementUpdated.Broadcast();
	}
}

FTransform UIMGCharacterMovementComponent::GetRagdollPlacementTransform() const
{
	const FVector Up = -GetGravityDirection();
	const FVector Facing = FVector::VectorPlaneProject(
		FRotator(0.0f, FRotator::DecompressAxisFromShort(ActiveRagdollState.FacingYaw), 0.0f).Vector(), Up).GetSafeNormal();
	const FQuat Rotation = Facing.IsNearlyZero()
		? UpdatedComponent->GetComponentQuat()
		: FRotationMatrix::MakeFromXZ(Facing, Up).ToQuat();
	return FTransform(Rotation, FRepMovement::RebaseOntoLocalOrigin(ActiveRagdollState.AnchorLocation, this));
}

void UIMGCharacterMovementComponent::PublishRagdollMovementState()
{
	if (++ActiveRagdollState.Revision == 0)
	{
		++ActiveRagdollState.Revision;
	}
	ReplicatedRagdollState = ActiveRagdollState;
	PublishRagdollBodyReference(
		FVector::ZeroVector,
		ActiveRagdollState.Phase == EIMGRagdollPhase::Simulating ? EIMGRagdollIssue::MissingTarget : EIMGRagdollIssue::Unavailable);

	UE_LOG(LogIMG, Verbose, TEXT("[Ragdoll][State] %s Role=%d Session=%u Revision=%u Phase=%d End=%d Placement=%d"),
		*GetNameSafe(GetOwner()),
		CharacterOwner ? static_cast<int32>(CharacterOwner->GetLocalRole()) : -1,
		ActiveRagdollState.Session,
		ActiveRagdollState.Revision,
		static_cast<int32>(ActiveRagdollState.Phase),
		static_cast<int32>(ActiveRagdollState.EndReason),
		ActiveRagdollState.bHasRecovery);

	if (GetOwner())
	{
		GetOwner()->ForceNetUpdate();
	}
	ForceReplicationUpdate();
}

void UIMGCharacterMovementComponent::OnRep_RagdollMovementState()
{
	ReceiveRagdollMovementState(ReplicatedRagdollState);
}

void UIMGCharacterMovementComponent::ReceiveRagdollMovementState(const FIMGRagdollRepState& State)
{
	const bool bValidSettings = State.Phase == EIMGRagdollPhase::Inactive || State.Settings.IsValid();
	const bool bValidExternalTransition = !State.bExternalTransition
		|| (State.bHasRecovery
			&& State.Phase == EIMGRagdollPhase::Inactive
			&& State.EndReason == EIMGRagdollEndReason::Transferred
			&& !State.TransitionVelocity.ContainsNaN()
			&& (State.TransitionMode == MOVE_Custom || State.TransitionCustomMode == 0)
			&& !(State.TransitionMode == MOVE_Custom && State.TransitionCustomMode == static_cast<uint8>(EIMGCustomMovementMode::Ragdoll))
			&& SupportsRagdollTransitionMode(State.TransitionMode, State.TransitionCustomMode));

	if (!bValidSettings
		|| !bValidExternalTransition
		|| State.AnchorLocation.ContainsNaN()
		|| State.InitialVelocity.ContainsNaN()
		|| State.LaunchVelocityDelta.ContainsNaN()
		|| static_cast<uint8>(State.Phase) > static_cast<uint8>(EIMGRagdollPhase::Recovering)
		|| (State.Session == 0 && State.Phase != EIMGRagdollPhase::Inactive))
	{
		return;
	}

	if (State.Revision == 0
		|| (ActiveRagdollState.Revision != 0
			&& State.Revision != ActiveRagdollState.Revision
			&& static_cast<int16>(State.Revision - ActiveRagdollState.Revision) <= 0))
	{
		return;
	}
	if (State.Revision == ActiveRagdollState.Revision)
	{
		return;
	}

	if (State.Session != ActiveRagdollState.Session || State.Phase != EIMGRagdollPhase::Simulating)
	{
		RagdollRuntime.ClearCorrection();
		RagdollRuntime.ClearInput();
	}
	if (State.Session != ActiveRagdollState.Session || State.bExternalTransition)
	{
		RagdollRuntime.RecoveryAppliedSession = 0;
		RagdollRuntime.LocalPlacementSession = 0;
	}

	ActiveRagdollState = State;
	if (State.Phase != EIMGRagdollPhase::Inactive
		|| (State.bHasRecovery && RagdollRuntime.RecoveryAppliedSession != State.Session))
	{
		ResetAutonomousMeshSmoothing();
	}
	if (HasBegunPlay() && IsActive() && IsRegistered() && IsRagdollRecovering())
	{
		RagdollMode.AcquireOrientation(*this);
	}
	if (!IsActive() || !IsRegistered() || (State.Phase == EIMGRagdollPhase::Inactive && !IsRagdollMovementMode()))
	{
		RagdollMode.ReleaseOrientation(*this);
	}
	if (!CharacterOwner || !CharacterOwner->bClientUpdating)
	{
		OnRagdollMovementUpdated.Broadcast();
	}
}

void UIMGCharacterMovementComponent::UpdateRagdollMovementState()
{
	if (!HasValidData() || CharacterOwner->bClientUpdating || RagdollRuntime.bTransitioning || !IsRegistered() || !IsActive())
	{
		return;
	}

	if (IsRagdollRecovering())
	{
		RagdollMode.AcquireOrientation(*this);
	}

	if (CharacterOwner->HasAuthority() && ActiveRagdollState.Phase != EIMGRagdollPhase::Inactive)
	{
		if (MovementMode == MOVE_None
			|| UpdatedComponent != CharacterOwner->GetCapsuleComponent()
			|| UpdatedComponent->IsSimulatingPhysics())
		{
			StopRagdollMovement(EIMGRagdollEndReason::Unavailable);
		}
		else if (ActiveRagdollState.Phase == EIMGRagdollPhase::Simulating && !IsRagdollMovementMode()
			&& !(RagdollRuntime.bAwaitingInitialInput && (IsMovingOnGround() || IsFalling())))
		{
			FinishRagdollMovement(EIMGRagdollEndReason::MovementChanged, false);
		}
		else if (RagdollRuntime.OwningController.IsStale()
			|| CharacterOwner->GetController() != RagdollRuntime.OwningController.Get())
		{
			// A new movement owner must start its own session instead of inheriting the previous owner's body samples.
			StopRagdollMovement(EIMGRagdollEndReason::Unavailable);
		}
		else if (IsRagdollRecovering() && MovementMode != MOVE_Walking)
		{
			FinishRagdollMovement(EIMGRagdollEndReason::MovementChanged, false);
		}
	}

	if (!HasValidData())
	{
		return;
	}

	if (CharacterOwner->GetLocalRole() == ROLE_AutonomousProxy && MovementMode != MOVE_None)
	{
		if (ActiveRagdollState.Phase == EIMGRagdollPhase::Simulating)
		{
			SetMovementMode(MOVE_Custom, static_cast<uint8>(EIMGCustomMovementMode::Ragdoll));
		}
		else if (ActiveRagdollState.Revision != 0 && !ActiveRagdollState.bExternalTransition)
		{
			if (ActiveRagdollState.bHasRecovery
				&& RagdollRuntime.LocalPlacementSession != ActiveRagdollState.Session
				&& (IsRagdollRecovering() || IsRagdollMovementMode())
				&& (IsRagdollMovementMode() || IsWalking() || IsFalling()))
			{
				ApplyRagdollRecoveryTransform(GetRagdollPlacementTransform());
			}
			else if (IsRagdollMovementMode())
			{
				SetMovementMode(ActiveRagdollState.EndReason == EIMGRagdollEndReason::Unavailable ? MOVE_None : MOVE_Falling);
			}
		}
	}
}

void UIMGCharacterMovementComponent::TickRagdollRemoteInputTimeout()
{
	// Packet silence is an authority live-tick concern; saved move replay never consults this clock.
	if (!HasValidData()
		|| !CharacterOwner->HasAuthority()
		|| ActiveRagdollState.Phase != EIMGRagdollPhase::Simulating)
	{
		return;
	}

	if (RagdollRuntime.bAwaitingInitialInput)
	{
		if (GetWorld()->GetTimeSeconds() - ActiveRagdollState.StartServerTime > ActiveRagdollState.Settings.EntryInputTimeout)
		{
			FinishRagdollMovement(EIMGRagdollEndReason::Interrupted, false);
		}
		return;
	}
	if (!IsRagdollMovementMode())
	{
		return;
	}
	if (!CharacterOwner->IsLocallyControlled() && CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy)
	{
		const double Now = GetWorld()->GetTimeSeconds();
		if (RagdollRuntime.RemoteInputTime < 0.0)
		{
			RagdollRuntime.RemoteInputTime = Now;
		}
		if (RagdollMode.Input.Status != EIMGRagdollTargetStatus::Expired
			&& Now - RagdollRuntime.RemoteInputTime > ActiveRagdollState.Settings.MaxTargetAge)
		{
			RagdollMode.Input.Session = ActiveRagdollState.Session;
			RagdollMode.Input.Status = EIMGRagdollTargetStatus::Expired;
			Velocity = FVector::ZeroVector;
			SetRagdollMovementIssue(EIMGRagdollIssue::ExpiredTarget);
			PublishRagdollBodyReference(FVector::ZeroVector, EIMGRagdollIssue::ExpiredTarget);
			CharacterOwner->ForceNetUpdate();
			ForceReplicationUpdate();
		}
	}
	else
	{
		RagdollRuntime.RemoteInputTime = -1.0;
	}
}

void UIMGCharacterMovementComponent::ClearRagdollMovementResources()
{
	RagdollMode.Exit(*this);
	RagdollMode.ReleaseOrientation(*this);
	RagdollRuntime.ClearAll();
}
