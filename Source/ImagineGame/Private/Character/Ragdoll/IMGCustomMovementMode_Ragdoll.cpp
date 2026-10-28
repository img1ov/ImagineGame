#include "Character/Ragdoll/IMGCustomMovementMode_Ragdoll.h"

#include "Character/IMGCharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Engine/ReplicatedState.h"
#include "Engine/ScopedMovementUpdate.h"
#include "GameFramework/Character.h"

void FIMGCustomMovementMode_Ragdoll::Enter(UIMGCharacterMovementComponent& Movement)
{
	if (!Movement.HasValidData()) { return; }
	UCapsuleComponent* Capsule = Movement.CharacterOwner->GetCapsuleComponent();
	if (!IsValid(Capsule)) { return; }
	FScopedMovementUpdate ScopedCapsuleUpdate(Capsule, EScopedUpdate::DeferredUpdates);
	AcquireOrientation(Movement);
	if (!bOwnsCapsuleCollision)
	{
		SavedProfile = Capsule->GetCollisionProfileName();
		SavedResponses = Capsule->GetCollisionResponseToChannels();
		SavedCollisionEnabled = Capsule->GetCollisionEnabled();
		bOwnsCapsuleCollision = true;
	}
	// GASP's ragdoll capsule follows without sweeping; the skeletal bodies own contact.
	Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Movement.SetBase(static_cast<UPrimitiveComponent*>(nullptr));
	Movement.CurrentFloor.Clear();
	Movement.ClearAccumulatedForces();
	Movement.CurrentRootMotion.Clear();
	Movement.RootMotionParams.Clear();
}

void FIMGCustomMovementMode_Ragdoll::AcquireOrientation(UIMGCharacterMovementComponent& Movement)
{
	if (!Movement.CharacterOwner) { return; }
	if (!bOwnsOrientation)
	{
		bSavedControllerYaw = Movement.CharacterOwner->bUseControllerRotationYaw;
		bSavedControllerPitch = Movement.CharacterOwner->bUseControllerRotationPitch;
		bSavedControllerRoll = Movement.CharacterOwner->bUseControllerRotationRoll;
		bOwnsOrientation = true;
	}
	Movement.CharacterOwner->bUseControllerRotationYaw = false;
	Movement.CharacterOwner->bUseControllerRotationPitch = false;
	Movement.CharacterOwner->bUseControllerRotationRoll = false;
}

void FIMGCustomMovementMode_Ragdoll::Exit(UIMGCharacterMovementComponent& Movement)
{
	if (bOwnsCapsuleCollision && Movement.CharacterOwner)
	{
		UCapsuleComponent* Capsule = Movement.CharacterOwner->GetCapsuleComponent();
		FScopedMovementUpdate ScopedCapsuleUpdate(Capsule, EScopedUpdate::DeferredUpdates);
		bOwnsCapsuleCollision = false;
		if (Movement.MovementMode == MOVE_None)
		{
			Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		else
		{
			Capsule->SetCollisionProfileName(SavedProfile);
			Capsule->SetCollisionResponseToChannels(SavedResponses);
			Capsule->SetCollisionEnabled(SavedCollisionEnabled);
		}
	}
	bOwnsCapsuleCollision = false;
	Input = FIMGRagdollMovementInput();
	if (!Movement.IsRagdollRecovering()) { ReleaseOrientation(Movement); }
	Movement.bForceNextFloorCheck = true;
}

void FIMGCustomMovementMode_Ragdoll::ReleaseOrientation(UIMGCharacterMovementComponent& Movement)
{
	if (bOwnsOrientation && Movement.CharacterOwner)
	{
		Movement.CharacterOwner->bUseControllerRotationYaw = bSavedControllerYaw;
		Movement.CharacterOwner->bUseControllerRotationPitch = bSavedControllerPitch;
		Movement.CharacterOwner->bUseControllerRotationRoll = bSavedControllerRoll;
	}
	bOwnsOrientation = false;
}

void FIMGCustomMovementMode_Ragdoll::Simulate(UIMGCharacterMovementComponent& Movement, float DeltaTime)
{
	if (DeltaTime < UCharacterMovementComponent::MIN_TICK_TIME || !Movement.HasValidData())
	{
		return;
	}
	Movement.RootMotionParams.Clear();
	Movement.CurrentRootMotion.Clear();
	if (Movement.CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		// Proxies extrapolate CMC's authoritative velocity; their own pelvis is visual only.
		FHitResult Hit;
		Movement.MoveUpdatedComponent(Movement.Velocity * DeltaTime, Movement.UpdatedComponent->GetComponentQuat(), false, &Hit);
		return;
	}
	const FIMGRagdollRepState& State = Movement.GetRagdollMovementState();
	if (State.Phase != EIMGRagdollPhase::Simulating || Input.Session != State.Session || !Input.IsValid())
	{
		Movement.Velocity = FVector::ZeroVector;
		return;
	}

	const FIMGRagdollSettings& Settings = State.Settings;
	const uint16 Revision = State.Revision;
	const FVector OldLocation = Movement.UpdatedComponent->GetComponentLocation();
	const FVector PelvisLocation = FRepMovement::RebaseOntoLocalOrigin(Input.PelvisLocation, &Movement);
	if (FVector::DistSquared(PelvisLocation, OldLocation) > FMath::Square(FMath::Max(1.0f, Settings.MaxTargetDistance)))
	{
		Movement.Velocity = FVector::ZeroVector;
		return;
	}
	const FVector Up = -Movement.GetGravityDirection();
	FVector TargetLocation = PelvisLocation;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(IMGRagdollFollow), false, Movement.CharacterOwner.Get());
	FHitResult Floor;
	const float TraceRadius = FMath::Max(1.0f, Settings.FollowTraceRadius);
	const FVector TraceStart = PelvisLocation + Up * TraceRadius;
	const FVector TraceEnd = PelvisLocation - Up * FMath::Max(1.0f, Settings.FollowTraceDistance);
	Movement.GetWorld()->SweepSingleByChannel(Floor, TraceStart, TraceEnd,
		FQuat::Identity, Settings.FloorTraceChannel, FCollisionShape::MakeSphere(TraceRadius), Query);
	if (Floor.bStartPenetrating)
	{
		Movement.GetWorld()->LineTraceSingleByChannel(Floor, PelvisLocation, TraceEnd, Settings.FloorTraceChannel, Query);
	}
	if (Floor.bBlockingHit && !Floor.bStartPenetrating && FVector::DotProduct(Floor.ImpactNormal, Up) > UE_KINDA_SMALL_NUMBER)
	{
		const float HalfHeight = Movement.CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const float PelvisHeight = FVector::DotProduct(PelvisLocation - Floor.ImpactPoint, Up);
		TargetLocation += Up * FMath::Max(0.0f, HalfHeight + Settings.FloorClearance - PelvisHeight);
	}
	const FVector Delta = (TargetLocation - OldLocation).GetClampedToMaxSize(FMath::Max(1.0f, Settings.MaxFollowSpeed) * DeltaTime);
	FVector Facing = FVector::VectorPlaneProject(FRotator(0.0f, FRotator::DecompressAxisFromShort(Input.FacingYaw), 0.0f).Vector(), Up).GetSafeNormal();
	if (Facing.IsNearlyZero())
	{
		Facing = Movement.UpdatedComponent->GetForwardVector();
	}
	FHitResult Hit;
	Movement.MoveUpdatedComponent(Delta, FRotationMatrix::MakeFromXZ(Facing, Up).ToQuat(), false, &Hit);
	if (!Movement.HasValidData() || !Movement.IsRagdollMovementMode() || Movement.GetRagdollMovementState().Revision != Revision) { return; }
	Movement.Velocity = (Movement.UpdatedComponent->GetComponentLocation() - OldLocation) / DeltaTime;
	Movement.bJustTeleported = false;
}

bool FIMGCustomMovementMode_Ragdoll::FindRecoveryTransform(const UIMGCharacterMovementComponent& Movement, FTransform& OutTransform) const
{
	if (!Movement.HasValidData() || !bOwnsCapsuleCollision)
	{
		return false;
	}
	const FIMGRagdollSettings& Settings = Movement.GetRagdollMovementState().Settings;
	const UCapsuleComponent* Capsule = Movement.CharacterOwner->GetCapsuleComponent();
	const FVector Up = -Movement.GetGravityDirection();
	const FVector Location = Movement.UpdatedComponent->GetComponentLocation();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	FCollisionQueryParams Query(SCENE_QUERY_STAT(IMGRagdollRecovery), false, Movement.CharacterOwner.Get());
	const FCollisionResponseParams Responses(SavedResponses);
	FHitResult Ground;
	Movement.GetWorld()->LineTraceSingleByChannel(Ground, Location,
		Location - Up * (HalfHeight + Settings.RecoveryTraceDistance), Capsule->GetCollisionObjectType(), Query, Responses);
	if (!Ground.bBlockingHit || Ground.bStartPenetrating || !Movement.IsWalkable(Ground))
	{
		return false;
	}
	const FVector RecoveryLocation = Ground.ImpactPoint + Up * (HalfHeight + FMath::Max(2.0f, Settings.FloorClearance));
	const FQuat Rotation = Movement.UpdatedComponent->GetComponentQuat();
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), HalfHeight);
	if (Movement.GetWorld()->OverlapBlockingTestByChannel(RecoveryLocation, Rotation, Capsule->GetCollisionObjectType(), Shape, Query, Responses))
	{
		return false;
	}
	FHitResult Obstruction;
	if (Movement.GetWorld()->SweepSingleByChannel(Obstruction, Location, RecoveryLocation, Rotation,
		Capsule->GetCollisionObjectType(), Shape, Query, Responses))
	{
		return false;
	}
	OutTransform = FTransform(Rotation, RecoveryLocation);
	return true;
}

void FIMGCustomMovementMode_Ragdoll::ApplyRecoveryTransform(UIMGCharacterMovementComponent& Movement, const FTransform& Transform)
{
	if (!Movement.HasValidData() || (!Movement.IsRagdollMovementMode() && !Movement.IsWalking() && !Movement.IsFalling()) || Transform.ContainsNaN()) { return; }
	const uint16 Revision = Movement.GetRagdollMovementState().Revision;
	const EMovementMode PreviousMode = Movement.MovementMode;
	FHitResult Hit;
	Movement.MoveUpdatedComponent(Transform.GetLocation() - Movement.UpdatedComponent->GetComponentLocation(), Transform.GetRotation(), false, &Hit, ETeleportType::TeleportPhysics);
	if (!Movement.HasValidData() || Movement.MovementMode != PreviousMode || Movement.GetRagdollMovementState().Revision != Revision) { return; }
	Movement.bJustTeleported = true;
	Movement.Velocity = FVector::ZeroVector;
	Movement.SetMovementMode(MOVE_Falling);
	if (!Movement.HasValidData() || Movement.MovementMode != MOVE_Falling) { return; }
	Movement.FindFloor(Movement.UpdatedComponent->GetComponentLocation(), Movement.CurrentFloor, false);
	if (Movement.CurrentFloor.IsWalkableFloor())
	{
		Movement.SetMovementMode(MOVE_Walking);
		if (Movement.HasValidData() && Movement.MovementMode == MOVE_Walking) { Movement.SetBaseFromFloor(Movement.CurrentFloor); }
	}
	Movement.bForceNextFloorCheck = true;
	Movement.ForceReplicationUpdate();
}
