#include "Character/Ragdoll/IMGCustomMovementMode_Ragdoll.h"

#include "Character/IMGCharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Engine/ReplicatedState.h"
#include "Engine/ScopedMovementUpdate.h"
#include "GameFramework/Character.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"

namespace IMGRagdoll
{
#if ENABLE_DRAW_DEBUG
	TAutoConsoleVariable<int32> DebugDraw(TEXT("IMG.Ragdoll.DebugDraw"), 0,
		TEXT("0: off, 1: follow target, 2: also placement candidates/support/path."), ECVF_Cheat);
	TAutoConsoleVariable<float> DebugDuration(TEXT("IMG.Ragdoll.DebugDuration"), 2.0f,
		TEXT("Lifetime of explicit recovery/exit query drawings."), ECVF_Cheat);

	void DrawPlacement(const UIMGCharacterMovementComponent& Movement, const FTransform& Transform, EIMGRagdollIssue Issue, const FVector& BodyLocalOffset = FVector::ZeroVector)
	{
		const ACharacter* Character = Movement.GetCharacterOwner();
		if (DebugDraw.GetValueOnGameThread() < 2 || !IsValid(Character) || Character->bClientUpdating) { return; }
		const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		const float Duration = FMath::Clamp(DebugDuration.GetValueOnGameThread(), 0.0f, 10.0f);
		const FColor Color = Issue == EIMGRagdollIssue::None ? FColor::Green : FColor::Red;
		DrawDebugCapsule(Movement.GetWorld(), Transform.GetLocation(), Capsule->GetScaledCapsuleHalfHeight(),
			Capsule->GetScaledCapsuleRadius(), Transform.GetRotation(), Color, false, Duration);
		DrawDebugLine(Movement.GetWorld(), Capsule->GetComponentLocation(), Transform.GetLocation(), Color, false, Duration);
		const FIMGRagdollMovementInput Target = Movement.GetRagdollMovementInput();
		if (Target.IsValid() && Target.Session == Movement.GetRagdollMovementSession() && !BodyLocalOffset.ContainsNaN()
			&& BodyLocalOffset.SizeSquared() <= FMath::Square(Movement.GetRagdollMovementState().Settings.MaxTargetDistance))
		{
			const FVector BodyLocation = FRepMovement::RebaseOntoLocalOrigin(Target.PelvisLocation, &Movement);
			const FVector MatchedBodyLocation = Transform.TransformPosition(BodyLocalOffset);
			DrawDebugPoint(Movement.GetWorld(), BodyLocation, 8.0f, FColor::Cyan, false, Duration);
			DrawDebugPoint(Movement.GetWorld(), MatchedBodyLocation, 8.0f, FColor::Yellow, false, Duration);
			DrawDebugLine(Movement.GetWorld(), BodyLocation, MatchedBodyLocation, Color, false, Duration);
			DrawDebugLine(Movement.GetWorld(), MatchedBodyLocation, Transform.GetLocation(), Color, false, Duration);
		}
		DrawDebugString(Movement.GetWorld(), Transform.GetLocation(),
			FString::Printf(TEXT("%s Role=%d Session=%d %s"), *GetNameSafe(Movement.GetOwner()),
				static_cast<int32>(Character->GetLocalRole()), Movement.GetRagdollMovementSession(),
				*StaticEnum<EIMGRagdollIssue>()->GetNameStringByValue(static_cast<int64>(Issue))), nullptr, Color, Duration);
	}

	void DrawFollowIssue(const UIMGCharacterMovementComponent& Movement, const FVector& Target, EIMGRagdollIssue Issue)
	{
		const ACharacter* Character = Movement.GetCharacterOwner();
		if (DebugDraw.GetValueOnGameThread() < 1 || !IsValid(Character) || Character->bClientUpdating) { return; }
		const FVector Origin = Movement.UpdatedComponent->GetComponentLocation();
		DrawDebugLine(Movement.GetWorld(), Origin, Target, FColor::Red);
		DrawDebugPoint(Movement.GetWorld(), Target, 10.0f, FColor::Red);
		DrawDebugString(Movement.GetWorld(), Origin, FString::Printf(TEXT("%s Role=%d Session=%d %s"),
			*GetNameSafe(Movement.GetOwner()), static_cast<int32>(Character->GetLocalRole()), Movement.GetRagdollMovementSession(),
			*StaticEnum<EIMGRagdollIssue>()->GetNameStringByValue(static_cast<int64>(Issue))), nullptr, FColor::Red, 0.0f);
	}
#endif
}

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
	// The body provider owns contact; the capsule follows without sweeping.
	Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Movement.SetBase(static_cast<FMovementBaseInterfaceData*>(nullptr));
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
		Movement.SetRagdollMovementIssue(Input.Session != 0 && Input.Session != State.Session ? EIMGRagdollIssue::SessionMismatch
			: Input.Status == EIMGRagdollTargetStatus::Expired ? EIMGRagdollIssue::ExpiredTarget : EIMGRagdollIssue::MissingTarget);
#if ENABLE_DRAW_DEBUG
		IMGRagdoll::DrawFollowIssue(Movement, Movement.UpdatedComponent->GetComponentLocation(), Movement.GetRagdollMovementIssue());
#endif
		return;
	}

	const FIMGRagdollSettings& Settings = State.Settings;
	const uint16 Revision = State.Revision;
	const FVector OldLocation = Movement.UpdatedComponent->GetComponentLocation();
	const FVector PelvisLocation = FRepMovement::RebaseOntoLocalOrigin(Input.PelvisLocation, &Movement);
	if (FVector::DistSquared(PelvisLocation, OldLocation) > FMath::Square(FMath::Max(1.0f, Settings.MaxTargetDistance)))
	{
		Movement.Velocity = FVector::ZeroVector;
		Movement.SetRagdollMovementIssue(EIMGRagdollIssue::TargetTooFar);
#if ENABLE_DRAW_DEBUG
		IMGRagdoll::DrawFollowIssue(Movement, PelvisLocation, EIMGRagdollIssue::TargetTooFar);
#endif
		return;
	}
	const FVector Up = -Movement.GetGravityDirection();
	FVector TargetLocation = PelvisLocation;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(IMGRagdollFollow), false, Movement.CharacterOwner.Get());
	FCollisionResponseParams Responses;
	Movement.InitCollisionParams(Query, Responses);
	Responses = FCollisionResponseParams(SavedResponses);
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
	if (Settings.bValidateTargetPath)
	{
		FHitResult Obstruction;
		if (Movement.GetWorld()->LineTraceSingleByChannel(Obstruction, OldLocation, TargetLocation,
			Movement.CharacterOwner->GetCapsuleComponent()->GetCollisionObjectType(), Query, Responses))
		{
			Movement.Velocity = FVector::ZeroVector;
			Movement.SetRagdollMovementIssue(EIMGRagdollIssue::TargetPathBlocked);
#if ENABLE_DRAW_DEBUG
			IMGRagdoll::DrawFollowIssue(Movement, TargetLocation, EIMGRagdollIssue::TargetPathBlocked);
#endif
			return;
		}
	}
#if ENABLE_DRAW_DEBUG
	if (IMGRagdoll::DebugDraw.GetValueOnGameThread() > 0 && !Movement.CharacterOwner->bClientUpdating)
	{
		DrawDebugSphere(Movement.GetWorld(), PelvisLocation, TraceRadius, 12, FColor::Cyan);
		DrawDebugLine(Movement.GetWorld(), OldLocation, TargetLocation, FColor::Yellow);
		DrawDebugLine(Movement.GetWorld(), TraceStart, TraceEnd, Floor.bBlockingHit ? FColor::Green : FColor::Orange);
		if (Floor.bBlockingHit) { DrawDebugPoint(Movement.GetWorld(), Floor.ImpactPoint, 8.0f, FColor::Green); }
	}
#endif
	Movement.SetRagdollMovementIssue(EIMGRagdollIssue::None);
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

bool FIMGCustomMovementMode_Ragdoll::FindRecoveryTransform(const UIMGCharacterMovementComponent& Movement,
	FTransform& OutTransform, EIMGRagdollIssue& OutIssue, const FVector& BodyLocalOffset) const
{
	return FindPlacement(Movement, true, OutTransform, OutIssue, BodyLocalOffset);
}

bool FIMGCustomMovementMode_Ragdoll::FindExitTransform(const UIMGCharacterMovementComponent& Movement,
	FTransform& OutTransform, EIMGRagdollIssue& OutIssue) const
{
	return FindPlacement(Movement, false, OutTransform, OutIssue, FVector::ZeroVector);
}

bool FIMGCustomMovementMode_Ragdoll::ValidateRecoveryTransform(const UIMGCharacterMovementComponent& Movement,
	const FTransform& Transform, EIMGRagdollIssue& OutIssue, const FVector& BodyLocalOffset) const
{
	const bool bValid = ValidatePlacement(Movement, Transform, true, OutIssue, BodyLocalOffset);
#if ENABLE_DRAW_DEBUG
	if (Movement.HasValidData() && !Transform.ContainsNaN()) { IMGRagdoll::DrawPlacement(Movement, Transform, OutIssue, BodyLocalOffset); }
#endif
	return bValid;
}

bool FIMGCustomMovementMode_Ragdoll::ValidateExitTransform(const UIMGCharacterMovementComponent& Movement,
	const FTransform& Transform, EIMGRagdollIssue& OutIssue) const
{
	const bool bValid = ValidatePlacement(Movement, Transform, false, OutIssue, FVector::ZeroVector);
#if ENABLE_DRAW_DEBUG
	if (Movement.HasValidData() && !Transform.ContainsNaN()) { IMGRagdoll::DrawPlacement(Movement, Transform, OutIssue); }
#endif
	return bValid;
}

bool FIMGCustomMovementMode_Ragdoll::GetBodyLocation(const UIMGCharacterMovementComponent& Movement,
	FVector& OutLocation, EIMGRagdollIssue& OutIssue) const
{
	const FIMGRagdollMovementInput PlacementInput = Movement.GetRagdollMovementInput();
	if (!PlacementInput.IsValid() || PlacementInput.Session != Movement.GetRagdollMovementState().Session)
	{
		OutIssue = PlacementInput.Session != 0 && PlacementInput.Session != Movement.GetRagdollMovementState().Session ? EIMGRagdollIssue::SessionMismatch
			: PlacementInput.Status == EIMGRagdollTargetStatus::Expired ? EIMGRagdollIssue::ExpiredTarget
			: PlacementInput.Status == EIMGRagdollTargetStatus::Ready ? EIMGRagdollIssue::InvalidTarget : EIMGRagdollIssue::MissingTarget;
		return false;
	}
	OutLocation = FRepMovement::RebaseOntoLocalOrigin(PlacementInput.PelvisLocation, &Movement);
	OutIssue = EIMGRagdollIssue::TargetTooFar;
	if (FVector::DistSquared(OutLocation, Movement.UpdatedComponent->GetComponentLocation())
		> FMath::Square(Movement.GetRagdollMovementState().Settings.MaxTargetDistance)) { return false; }
	OutIssue = EIMGRagdollIssue::None;
	return true;
}

bool FIMGCustomMovementMode_Ragdoll::ValidatePlacement(const UIMGCharacterMovementComponent& Movement,
	const FTransform& Transform, bool bRequireFloor, EIMGRagdollIssue& OutIssue, const FVector& BodyLocalOffset) const
{
	OutIssue = EIMGRagdollIssue::Unavailable;
	if (!Movement.HasValidData() || !bOwnsCapsuleCollision || Movement.UpdatedComponent != Movement.CharacterOwner->GetCapsuleComponent()) { return false; }
	FVector BodyLocation;
	const bool bHasBodyTarget = GetBodyLocation(Movement, BodyLocation, OutIssue);
	if (bRequireFloor && !bHasBodyTarget) { return false; }
	OutIssue = EIMGRagdollIssue::InvalidTarget;
	const FVector Up = -Movement.GetGravityDirection();
	if (Transform.ContainsNaN() || !Transform.GetRotation().IsNormalized() || !Transform.GetScale3D().Equals(FVector::OneVector)
		|| FVector::DotProduct(Transform.GetUnitAxis(EAxis::Z), Up) < 0.999f) { return false; }
	const FIMGRagdollSettings& Settings = Movement.GetRagdollMovementState().Settings;
	if (BodyLocalOffset.ContainsNaN() || BodyLocalOffset.SizeSquared() > FMath::Square(Settings.MaxTargetDistance)) { return false; }
	const FVector Origin = Movement.UpdatedComponent->GetComponentLocation();
	const FVector Delta = Transform.GetLocation() - Origin;
	if (!Movement.ConstrainDirectionToPlane(Delta).Equals(Delta, 0.01f)
		|| FVector::VectorPlaneProject(Delta, Up).SizeSquared() > FMath::Square(Settings.RecoverySearchRadius + 0.1f)
		|| FMath::Abs(FVector::DotProduct(Delta, Up)) > Settings.RecoveryTraceDistance + 0.1f) { return false; }
	const UCapsuleComponent* Capsule = Movement.CharacterOwner->GetCapsuleComponent();
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight());
	FCollisionQueryParams Query(SCENE_QUERY_STAT(IMGRagdollPlacement), false, Movement.CharacterOwner.Get());
	FCollisionResponseParams Responses;
	Movement.InitCollisionParams(Query, Responses);
	Responses = FCollisionResponseParams(SavedResponses);
	const ECollisionChannel Channel = Capsule->GetCollisionObjectType();
	OutIssue = EIMGRagdollIssue::PlacementBlocked;
	if (Movement.GetWorld()->OverlapBlockingTestByChannel(Transform.GetLocation(), Transform.GetRotation(), Channel, Shape, Query, Responses)) { return false; }
	if (bRequireFloor)
	{
		FHitResult Floor;
		OutIssue = EIMGRagdollIssue::NoFloor;
		if (!Movement.GetWorld()->SweepSingleByChannel(Floor, Transform.GetLocation(),
			Transform.GetLocation() - Up * (UCharacterMovementComponent::MAX_FLOOR_DIST + 0.1f),
			Transform.GetRotation(), Channel, Shape, Query, Responses) || Floor.bStartPenetrating) { return false; }
		OutIssue = EIMGRagdollIssue::UnwalkableFloor;
		if (!Movement.IsWalkable(Floor) || !Movement.IsWithinEdgeTolerance(Floor.Location, Floor.ImpactPoint, Capsule->GetScaledCapsuleRadius())) { return false; }
	}
	OutIssue = EIMGRagdollIssue::PlacementPathBlocked;
	FHitResult Obstruction;
	if (bHasBodyTarget)
	{
		const FVector MatchedBodyLocation = Transform.TransformPosition(BodyLocalOffset);
		if (Movement.GetWorld()->LineTraceSingleByChannel(Obstruction, BodyLocation, MatchedBodyLocation, Channel, Query, Responses)
			|| Movement.GetWorld()->LineTraceSingleByChannel(Obstruction, MatchedBodyLocation, Transform.GetLocation(), Channel, Query, Responses)) { return false; }
	}
	if (!Movement.GetWorld()->OverlapBlockingTestByChannel(Origin, Capsule->GetComponentQuat(), Channel, Shape, Query, Responses))
	{
		if (Movement.GetWorld()->SweepSingleByChannel(Obstruction, Origin, Transform.GetLocation(), Transform.GetRotation(), Channel, Shape, Query, Responses)) { return false; }
	}
	else
	{
		// A penetrating capsule needs a current, reachable body target to escape locally.
		if (!bHasBodyTarget) { return false; }
	}
	OutIssue = EIMGRagdollIssue::None;
	return true;
}

bool FIMGCustomMovementMode_Ragdoll::FindPlacement(const UIMGCharacterMovementComponent& Movement, bool bRequireFloor,
	FTransform& OutTransform, EIMGRagdollIssue& OutIssue, const FVector& BodyLocalOffset) const
{
	OutTransform = FTransform::Identity;
	OutIssue = EIMGRagdollIssue::Unavailable;
	if (!Movement.HasValidData() || !bOwnsCapsuleCollision || Movement.UpdatedComponent != Movement.CharacterOwner->GetCapsuleComponent()) { return false; }
	const FIMGRagdollSettings& Settings = Movement.GetRagdollMovementState().Settings;
	if (!Settings.IsValid()) { return false; }
	OutIssue = EIMGRagdollIssue::InvalidTarget;
	if (BodyLocalOffset.ContainsNaN() || BodyLocalOffset.SizeSquared() > FMath::Square(Settings.MaxTargetDistance)) { return false; }
	FVector BodyLocation;
	if (bRequireFloor && !GetBodyLocation(Movement, BodyLocation, OutIssue))
	{
#if ENABLE_DRAW_DEBUG
		IMGRagdoll::DrawPlacement(Movement, Movement.UpdatedComponent->GetComponentTransform(), OutIssue, BodyLocalOffset);
#endif
		return false;
	}
	const UCapsuleComponent* Capsule = Movement.CharacterOwner->GetCapsuleComponent();
	const FVector Origin = Movement.UpdatedComponent->GetComponentLocation();
	const FVector Up = -Movement.GetGravityDirection();
	const FQuat Rotation = Movement.UpdatedComponent->GetComponentQuat();
	const FVector Forward = FVector::VectorPlaneProject(Rotation.GetForwardVector(), Up).GetSafeNormal();
	const FVector Right = FVector::CrossProduct(Up, Forward);
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius, HalfHeight);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(IMGRagdollRecovery), false, Movement.CharacterOwner.Get());
	FCollisionResponseParams Responses;
	Movement.InitCollisionParams(Query, Responses);
	Responses = FCollisionResponseParams(SavedResponses);
	const ECollisionChannel Channel = Capsule->GetCollisionObjectType();
	const int32 CandidateCount = Settings.RecoverySearchRadius > 0.0f ? 1 + Settings.RecoverySearchRings * Settings.RecoverySearchSamples : 1;
	EIMGRagdollIssue FirstPlacementIssue = EIMGRagdollIssue::NoFloor;
	for (int32 Index = 0; Index < CandidateCount; ++Index)
	{
		FVector Position = Origin;
		if (Index > 0)
		{
			const int32 Ring = (Index - 1) / Settings.RecoverySearchSamples + 1;
			const float Angle = 2.0f * PI * ((Index - 1) % Settings.RecoverySearchSamples) / Settings.RecoverySearchSamples;
			Position += (Forward * FMath::Cos(Angle) + Right * FMath::Sin(Angle)) * Settings.RecoverySearchRadius * Ring / Settings.RecoverySearchRings;
		}
		bool bHasFloor = !bRequireFloor;
		if (bRequireFloor)
		{
			FHitResult Ground;
			const FVector End = Position - Up * (HalfHeight + Settings.RecoveryTraceDistance);
			bHasFloor = Movement.GetWorld()->LineTraceSingleByChannel(Ground, Position, End, Channel, Query, Responses) && !Ground.bStartPenetrating;
			OutIssue = bHasFloor ? EIMGRagdollIssue::UnwalkableFloor : EIMGRagdollIssue::NoFloor;
#if ENABLE_DRAW_DEBUG
			if (IMGRagdoll::DebugDraw.GetValueOnGameThread() >= 2)
			{
				DrawDebugLine(Movement.GetWorld(), Position, End, FColor::Cyan, false,
					FMath::Clamp(IMGRagdoll::DebugDuration.GetValueOnGameThread(), 0.0f, 10.0f));
			}
#endif
			if (bHasFloor && Movement.IsWalkable(Ground))
			{
				const FVector SurfaceCenter = Ground.ImpactPoint + Up * HalfHeight;
				const float Clearance = FMath::Clamp(Settings.FloorClearance, 2.0f, UCharacterMovementComponent::MAX_FLOOR_DIST - 0.1f);
				bHasFloor = false;
				OutIssue = EIMGRagdollIssue::PlacementBlocked;
				for (int32 Lift = 0; Lift <= 4; ++Lift)
				{
					const FVector SweepStart = SurfaceCenter + Up * (Clearance + Radius * Lift / 4.0f);
					if (Movement.GetWorld()->OverlapBlockingTestByChannel(SweepStart, Rotation, Channel, Shape, Query, Responses)) { continue; }
					bHasFloor = Movement.GetWorld()->SweepSingleByChannel(Ground, SweepStart, SurfaceCenter - Up * Radius,
						Rotation, Channel, Shape, Query, Responses) && !Ground.bStartPenetrating && Movement.IsWalkable(Ground)
						&& Movement.IsWithinEdgeTolerance(Ground.Location, Ground.ImpactPoint, Radius);
					if (bHasFloor) { Position = Ground.Location + Up * Clearance; }
					break;
				}
			}
			else { bHasFloor = false; }
		}
		const FTransform Candidate(Rotation, Position);
		const bool bValid = bHasFloor && ValidatePlacement(Movement, Candidate, bRequireFloor, OutIssue, BodyLocalOffset);
#if ENABLE_DRAW_DEBUG
		IMGRagdoll::DrawPlacement(Movement, Candidate, OutIssue, BodyLocalOffset);
#endif
		if (bValid) { OutTransform = Candidate; return true; }
		if (FirstPlacementIssue == EIMGRagdollIssue::NoFloor) { FirstPlacementIssue = OutIssue; }
	}
	OutIssue = FirstPlacementIssue;
	return false;
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
	if (Movement.IsRagdollRecovering() || Movement.GetRagdollMovementState().EndReason == EIMGRagdollEndReason::Recovered)
	{
		Movement.Velocity = FVector::ZeroVector;
	}
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
