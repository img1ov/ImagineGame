#include "Character/Ragdoll/IMGCustomMovementMode_Ragdoll.h"

#include "Character/IMGCharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/ReplicatedState.h"
#include "Engine/ScopedMovementUpdate.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"

namespace IMGRagdollMode
{
	constexpr float UpAlignmentCosThreshold = 0.999f;
	constexpr float PlaneConstraintTolerance = 0.01f;
	constexpr float PlacementRangeMargin = 0.1f;
	constexpr float FloorCheckExtraDistance = 0.1f;
	constexpr float MaxDebugDuration = 10.0f;
	constexpr int32 FloorSweepLiftSteps = 4;

#if ENABLE_DRAW_DEBUG
	TAutoConsoleVariable<int32> DebugDraw(
		TEXT("IMG.Ragdoll.DebugDraw"),
		0,
		TEXT("0: off, 1: follow target, 2: also placement candidates/support/path."),
		ECVF_Cheat);

	TAutoConsoleVariable<float> DebugDuration(
		TEXT("IMG.Ragdoll.DebugDuration"),
		2.0f,
		TEXT("Lifetime of explicit recovery/exit query drawings."),
		ECVF_Cheat);

	void DrawPlacement(
		const UIMGCharacterMovementComponent& Movement,
		const FTransform& Transform,
		EIMGRagdollIssue Issue,
		const FVector& BodyLocalOffset = FVector::ZeroVector)
	{
		const ACharacter* Character = Movement.GetCharacterOwner();
		if (DebugDraw.GetValueOnGameThread() < 2 || !IsValid(Character) || Character->bClientUpdating)
		{
			return;
		}

		const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		const float Duration = FMath::Clamp(DebugDuration.GetValueOnGameThread(), 0.0f, MaxDebugDuration);
		const FColor Color = Issue == EIMGRagdollIssue::None ? FColor::Green : FColor::Red;
		DrawDebugCapsule(
			Movement.GetWorld(),
			Transform.GetLocation(),
			Capsule->GetScaledCapsuleHalfHeight(),
			Capsule->GetScaledCapsuleRadius(),
			Transform.GetRotation(),
			Color,
			false,
			Duration);
		DrawDebugLine(Movement.GetWorld(), Capsule->GetComponentLocation(), Transform.GetLocation(), Color, false, Duration);

		const FIMGRagdollMovementInput Target = Movement.GetRagdollMovementInput();
		if (Target.IsValid()
			&& Target.Session == Movement.GetRagdollMovementSession()
			&& !BodyLocalOffset.ContainsNaN()
			&& BodyLocalOffset.SizeSquared() <= FMath::Square(Movement.GetRagdollMovementState().Settings.MaxTargetDistance))
		{
			const FVector BodyLocation = FRepMovement::RebaseOntoLocalOrigin(Target.PelvisLocation, &Movement);
			const FVector MatchedBodyLocation = Transform.TransformPosition(BodyLocalOffset);
			DrawDebugPoint(Movement.GetWorld(), BodyLocation, 8.0f, FColor::Cyan, false, Duration);
			DrawDebugPoint(Movement.GetWorld(), MatchedBodyLocation, 8.0f, FColor::Yellow, false, Duration);
			DrawDebugLine(Movement.GetWorld(), BodyLocation, MatchedBodyLocation, Color, false, Duration);
			DrawDebugLine(Movement.GetWorld(), MatchedBodyLocation, Transform.GetLocation(), Color, false, Duration);
		}

		DrawDebugString(
			Movement.GetWorld(),
			Transform.GetLocation(),
			FString::Printf(
				TEXT("%s Role=%d Session=%d %s"),
				*GetNameSafe(Movement.GetOwner()),
				static_cast<int32>(Character->GetLocalRole()),
				Movement.GetRagdollMovementSession(),
				*StaticEnum<EIMGRagdollIssue>()->GetNameStringByValue(static_cast<int64>(Issue))),
			nullptr,
			Color,
			Duration);
	}

	void DrawFollowIssue(const UIMGCharacterMovementComponent& Movement, const FVector& Target, EIMGRagdollIssue Issue)
	{
		const ACharacter* Character = Movement.GetCharacterOwner();
		if (DebugDraw.GetValueOnGameThread() < 1 || !IsValid(Character) || Character->bClientUpdating)
		{
			return;
		}

		const FVector Origin = Movement.UpdatedComponent->GetComponentLocation();
		DrawDebugLine(Movement.GetWorld(), Origin, Target, FColor::Red);
		DrawDebugPoint(Movement.GetWorld(), Target, 10.0f, FColor::Red);
		DrawDebugString(
			Movement.GetWorld(),
			Origin,
			FString::Printf(
				TEXT("%s Role=%d Session=%d %s"),
				*GetNameSafe(Movement.GetOwner()),
				static_cast<int32>(Character->GetLocalRole()),
				Movement.GetRagdollMovementSession(),
				*StaticEnum<EIMGRagdollIssue>()->GetNameStringByValue(static_cast<int64>(Issue))),
			nullptr,
			FColor::Red,
			0.0f);
	}
#endif

	EIMGRagdollIssue ResolveInputIssue(const FIMGRagdollMovementInput& Input, uint16 ExpectedSession)
	{
		if (Input.Session != 0 && Input.Session != ExpectedSession)
		{
			return EIMGRagdollIssue::SessionMismatch;
		}
		if (Input.Status == EIMGRagdollTargetStatus::Expired)
		{
			return EIMGRagdollIssue::ExpiredTarget;
		}
		return Input.Status == EIMGRagdollTargetStatus::Ready
			? EIMGRagdollIssue::InvalidTarget
			: EIMGRagdollIssue::MissingTarget;
	}

	int32 GetPlacementIssuePriority(EIMGRagdollIssue Issue)
	{
		switch (Issue)
		{
		case EIMGRagdollIssue::PlacementPathBlocked:
			return 4;
		case EIMGRagdollIssue::PlacementBlocked:
			return 3;
		case EIMGRagdollIssue::UnwalkableFloor:
			return 2;
		case EIMGRagdollIssue::NoFloor:
			return 1;
		default:
			return 0;
		}
	}

	FVector ComputeSearchCandidateOrigin(
		const FVector& Origin,
		const FVector& Forward,
		const FVector& Right,
		const FIMGRagdollSettings& Settings,
		int32 Index)
	{
		if (Index == 0)
		{
			return Origin;
		}

		const int32 Ring = (Index - 1) / Settings.RecoverySearchSamples + 1;
		const float Angle = 2.0f * PI * ((Index - 1) % Settings.RecoverySearchSamples) / Settings.RecoverySearchSamples;
		const float RingRadius = Settings.RecoverySearchRadius * Ring / Settings.RecoverySearchRings;
		return Origin + (Forward * FMath::Cos(Angle) + Right * FMath::Sin(Angle)) * RingRadius;
	}

	bool ResolveWalkableFloorPosition(
		const UIMGCharacterMovementComponent& Movement,
		const FIMGRagdollSettings& Settings,
		const FQuat& Rotation,
		const FVector& Up,
		float Radius,
		float HalfHeight,
		ECollisionChannel Channel,
		const FCollisionShape& Shape,
		const FCollisionQueryParams& Query,
		const FCollisionResponseParams& Responses,
		FVector& InOutPosition,
		EIMGRagdollIssue& OutIssue)
	{
		FHitResult Ground;
		const FVector TraceEnd = InOutPosition - Up * (HalfHeight + Settings.RecoveryTraceDistance);
		const bool bHitGround = Movement.GetWorld()->LineTraceSingleByChannel(
			Ground, InOutPosition, TraceEnd, Channel, Query, Responses) && !Ground.bStartPenetrating;

#if ENABLE_DRAW_DEBUG
		if (DebugDraw.GetValueOnGameThread() >= 2)
		{
			DrawDebugLine(
				Movement.GetWorld(),
				InOutPosition,
				TraceEnd,
				FColor::Cyan,
				false,
				FMath::Clamp(DebugDuration.GetValueOnGameThread(), 0.0f, MaxDebugDuration));
		}
#endif

		if (!bHitGround)
		{
			OutIssue = EIMGRagdollIssue::NoFloor;
			return false;
		}
		if (!Movement.IsWalkable(Ground))
		{
			OutIssue = EIMGRagdollIssue::UnwalkableFloor;
			return false;
		}

		const FVector SurfaceCenter = Ground.ImpactPoint + Up * HalfHeight;
		const float Clearance = Settings.GetEffectiveFloorClearance(UCharacterMovementComponent::MAX_FLOOR_DIST);
		OutIssue = EIMGRagdollIssue::PlacementBlocked;

		for (int32 Lift = 0; Lift <= FloorSweepLiftSteps; ++Lift)
		{
			const FVector SweepStart = SurfaceCenter + Up * (Clearance + Radius * Lift / static_cast<float>(FloorSweepLiftSteps));
			if (Movement.GetWorld()->OverlapBlockingTestByChannel(SweepStart, Rotation, Channel, Shape, Query, Responses))
			{
				continue;
			}

			const bool bSweptFloor = Movement.GetWorld()->SweepSingleByChannel(
				Ground, SweepStart, SurfaceCenter - Up * Radius, Rotation, Channel, Shape, Query, Responses)
				&& !Ground.bStartPenetrating
				&& Movement.IsWalkable(Ground)
				&& Movement.IsWithinEdgeTolerance(Ground.Location, Ground.ImpactPoint, Radius);
			if (bSweptFloor)
			{
				InOutPosition = Ground.Location + Up * Clearance;
				return true;
			}
			return false;
		}

		return false;
	}
}

void FIMGCustomMovementMode_Ragdoll::Enter(UIMGCharacterMovementComponent& Movement)
{
	if (!Movement.HasValidData())
	{
		return;
	}

	UCapsuleComponent* Capsule = Movement.CharacterOwner->GetCapsuleComponent();
	if (!IsValid(Capsule))
	{
		return;
	}

	FScopedMovementUpdate ScopedCapsuleUpdate(Capsule, EScopedUpdate::DeferredUpdates);
	AcquireOrientation(Movement);
	if (!bOwnsCapsuleCollision)
	{
		SavedProfile = Capsule->GetCollisionProfileName();
		SavedResponses = Capsule->GetCollisionResponseToChannels();
		SavedCollisionEnabled = Capsule->GetCollisionEnabled();
		bOwnsCapsuleCollision = true;
	}

	// The articulated body owns contact. The capsule remains only a logical CMC root until safe placement.
	Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NormalizeStance(Movement);
	if (!Movement.HasValidData() || !Movement.IsRagdollMovementMode())
	{
		return;
	}
	Movement.SetBase(static_cast<FMovementBaseInterfaceData*>(nullptr));
	Movement.CurrentFloor.Clear();
	Movement.ClearAccumulatedForces();
	Movement.CurrentRootMotion.Clear();
	Movement.RootMotionParams.Clear();
}

void FIMGCustomMovementMode_Ragdoll::NormalizeStance(UIMGCharacterMovementComponent& Movement)
{
	if (!Movement.HasValidData() || Movement.GetStance() == EIMGStance::Stand)
	{
		return;
	}
	UCapsuleComponent* Capsule = Movement.CharacterOwner->GetCapsuleComponent();
	USkeletalMeshComponent* Mesh = Movement.CharacterOwner->GetMesh();
	const bool bAttachedMesh = Mesh && Mesh->GetAttachParent() == Capsule;
	const FTransform EntryMeshWorld = bAttachedMesh ? Mesh->GetComponentTransform() : FTransform::Identity;
	{
		const FScopedPreventAttachedComponentMove PreservePose(
			Mesh && Mesh->GetAttachParent() == Capsule ? Mesh : nullptr);
		const FVector TranslationOffset = Movement.ClientPredictionData
			? Movement.ClientPredictionData->MeshTranslationOffset : FVector::ZeroVector;
		const FVector OriginalTranslationOffset = Movement.ClientPredictionData
			? Movement.ClientPredictionData->OriginalMeshTranslationOffset : FVector::ZeroVector;
		Movement.StanceMachine.ApplyReplicatedStance(Movement, EIMGStance::Stand);
		if (Movement.HasValidData() && IsValid(Mesh) && bAttachedMesh
			&& Mesh == Movement.CharacterOwner->GetMesh() && Mesh->GetAttachParent() == Capsule)
		{
			Mesh->SetWorldTransform(EntryMeshWorld, false, nullptr, ETeleportType::TeleportPhysics);
		}
		if (Movement.ClientPredictionData)
		{
			Movement.ClientPredictionData->MeshTranslationOffset = TranslationOffset;
			Movement.ClientPredictionData->OriginalMeshTranslationOffset = OriginalTranslationOffset;
		}
	}
}

void FIMGCustomMovementMode_Ragdoll::AcquireOrientation(UIMGCharacterMovementComponent& Movement)
{
	if (!Movement.CharacterOwner)
	{
		return;
	}

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
	if (!Movement.IsRagdollRecovering())
	{
		ReleaseOrientation(Movement);
	}
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

	if (!Movement.CharacterOwner->bClientUpdating)
	{
		NormalizeStance(Movement);
		if (!Movement.HasValidData() || !Movement.IsRagdollMovementMode())
		{
			return;
		}
	}

	Movement.RootMotionParams.Clear();
	Movement.CurrentRootMotion.Clear();

	if (Movement.CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		// Proxies extrapolate CMC's authoritative velocity; their own pelvis is visual only.
		FHitResult Hit;
		Movement.MoveUpdatedComponent(
			Movement.Velocity * DeltaTime,
			Movement.UpdatedComponent->GetComponentQuat(),
			false,
			&Hit);
		return;
	}

	if (Movement.CharacterOwner->HasAuthority()
		&& !Movement.CharacterOwner->IsLocallyControlled()
		&& Movement.CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy)
	{
		// Forced server updates can run between ticks; they must observe packet silence too.
		Input = Movement.GetRagdollMovementInput();
	}

	const FIMGRagdollRepState& State = Movement.GetRagdollMovementState();
	if (State.Phase != EIMGRagdollPhase::Simulating || Input.Session != State.Session || !Input.IsValid()
		|| FMath::Abs(Input.CapsuleHeightOffset) > Movement.CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight())
	{
		const EIMGRagdollIssue Issue = IMGRagdollMode::ResolveInputIssue(Input, State.Session);
		Movement.Velocity = FVector::ZeroVector;
		Movement.SetRagdollMovementIssue(Issue);
		Movement.PublishRagdollBodyReference(FVector::ZeroVector, Issue);
#if ENABLE_DRAW_DEBUG
		IMGRagdollMode::DrawFollowIssue(Movement, Movement.UpdatedComponent->GetComponentLocation(), Issue);
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
		Movement.PublishRagdollBodyReference(FVector::ZeroVector, EIMGRagdollIssue::TargetTooFar);
#if ENABLE_DRAW_DEBUG
		IMGRagdollMode::DrawFollowIssue(Movement, PelvisLocation, EIMGRagdollIssue::TargetTooFar);
#endif
		return;
	}

	const FVector Up = -Movement.GetGravityDirection();
	FCollisionQueryParams Query(SCENE_QUERY_STAT(IMGRagdollFollow), false, Movement.CharacterOwner.Get());
	FCollisionResponseParams Responses;
	Movement.InitCollisionParams(Query, Responses);
	Responses = FCollisionResponseParams(SavedResponses);

	const FVector TargetLocation = PelvisLocation + Up * Input.CapsuleHeightOffset;

	if (Settings.bValidateTargetPath)
	{
		FHitResult Obstruction;
		if (Movement.GetWorld()->LineTraceSingleByChannel(
			Obstruction,
			OldLocation - Up * Input.CapsuleHeightOffset,
			PelvisLocation,
			Movement.CharacterOwner->GetCapsuleComponent()->GetCollisionObjectType(),
			Query,
			Responses))
		{
			Movement.Velocity = FVector::ZeroVector;
			Movement.SetRagdollMovementIssue(EIMGRagdollIssue::TargetPathBlocked);
			Movement.PublishRagdollBodyReference(FVector::ZeroVector, EIMGRagdollIssue::TargetPathBlocked);
#if ENABLE_DRAW_DEBUG
			IMGRagdollMode::DrawFollowIssue(Movement, TargetLocation, EIMGRagdollIssue::TargetPathBlocked);
#endif
			return;
		}
	}

#if ENABLE_DRAW_DEBUG
	if (IMGRagdollMode::DebugDraw.GetValueOnGameThread() > 0 && !Movement.CharacterOwner->bClientUpdating)
	{
		DrawDebugPoint(Movement.GetWorld(), PelvisLocation, 8.0f, FColor::Cyan);
		DrawDebugLine(Movement.GetWorld(), OldLocation, TargetLocation, FColor::Yellow);
	}
#endif

	Movement.SetRagdollMovementIssue(EIMGRagdollIssue::None);
	const FVector Delta = (TargetLocation - OldLocation).GetClampedToMaxSize(FMath::Max(1.0f, Settings.MaxFollowSpeed) * DeltaTime);
	// Free-body heading is an input/recovery fact, not an instruction to rotate the capsule and its waiting mesh.
	FHitResult Hit;
	Movement.MoveUpdatedComponent(Delta, Movement.UpdatedComponent->GetComponentQuat(), false, &Hit);
	if (!Movement.HasValidData() || !Movement.IsRagdollMovementMode() || Movement.GetRagdollMovementState().Revision != Revision)
	{
		return;
	}

	Movement.Velocity = (Movement.UpdatedComponent->GetComponentLocation() - OldLocation) / DeltaTime;
	if (UIMGCharacterMovementComponent::GetMovementNetDiagnosticsLevel() >= 2)
	{
		UE_LOG(LogIMGMovementNet, Log,
			TEXT("[RagdollFollow] Actor=%s Role=%d Replay=%d Session=%u Pelvis=%s HeightOffset=%.2f Capsule=%s->%s Target=%s BodyVelocity=%s CapsuleVelocity=%s"),
			*GetNameSafe(Movement.CharacterOwner.Get()), static_cast<int32>(Movement.CharacterOwner->GetLocalRole()),
			Movement.CharacterOwner->bClientUpdating, State.Session, *PelvisLocation.ToCompactString(), Input.CapsuleHeightOffset,
			*OldLocation.ToCompactString(), *Movement.UpdatedComponent->GetComponentLocation().ToCompactString(),
			*TargetLocation.ToCompactString(), *FVector(Input.PelvisVelocity).ToCompactString(),
			*Movement.Velocity.ToCompactString());
	}
	Movement.bJustTeleported = false;
	// Preserve the body-to-capsule offset after speed/plane constraints; never publish an unreachable raw target.
	Movement.PublishRagdollBodyReference(
		PelvisLocation + Movement.UpdatedComponent->GetComponentLocation() - TargetLocation,
		EIMGRagdollIssue::None);
}

bool FIMGCustomMovementMode_Ragdoll::FindRecoveryTransform(
	const UIMGCharacterMovementComponent& Movement,
	FTransform& OutTransform,
	EIMGRagdollIssue& OutIssue,
	const FVector& BodyLocalOffset) const
{
	return FindPlacement(Movement, true, OutTransform, OutIssue, BodyLocalOffset);
}

bool FIMGCustomMovementMode_Ragdoll::FindExitTransform(
	const UIMGCharacterMovementComponent& Movement,
	FTransform& OutTransform,
	EIMGRagdollIssue& OutIssue) const
{
	return FindPlacement(Movement, false, OutTransform, OutIssue, FVector::ZeroVector);
}

bool FIMGCustomMovementMode_Ragdoll::ValidateRecoveryTransform(
	const UIMGCharacterMovementComponent& Movement,
	const FTransform& Transform,
	EIMGRagdollIssue& OutIssue,
	const FVector& BodyLocalOffset) const
{
	const bool bValid = ValidatePlacement(Movement, Transform, true, OutIssue, BodyLocalOffset);
#if ENABLE_DRAW_DEBUG
	if (Movement.HasValidData() && !Transform.ContainsNaN())
	{
		IMGRagdollMode::DrawPlacement(Movement, Transform, OutIssue, BodyLocalOffset);
	}
#endif
	return bValid;
}

bool FIMGCustomMovementMode_Ragdoll::ValidateExitTransform(
	const UIMGCharacterMovementComponent& Movement,
	const FTransform& Transform,
	EIMGRagdollIssue& OutIssue) const
{
	const bool bValid = ValidatePlacement(Movement, Transform, false, OutIssue, FVector::ZeroVector);
#if ENABLE_DRAW_DEBUG
	if (Movement.HasValidData() && !Transform.ContainsNaN())
	{
		IMGRagdollMode::DrawPlacement(Movement, Transform, OutIssue);
	}
#endif
	return bValid;
}

bool FIMGCustomMovementMode_Ragdoll::ValidateTransitionTransform(
	const UIMGCharacterMovementComponent& Movement,
	const FTransform& Transform,
	bool bRequireFloor,
	EIMGRagdollIssue& OutIssue,
	const FVector& BodyLocalOffset) const
{
	return ValidatePlacement(Movement, Transform, bRequireFloor, OutIssue, BodyLocalOffset);
}

bool FIMGCustomMovementMode_Ragdoll::GetBodyLocation(
	const UIMGCharacterMovementComponent& Movement,
	FVector& OutLocation,
	EIMGRagdollIssue& OutIssue) const
{
	const FIMGRagdollMovementInput PlacementInput = Movement.GetRagdollMovementInput();
	const uint16 ExpectedSession = Movement.GetRagdollMovementState().Session;
	if (!PlacementInput.IsValid() || PlacementInput.Session != ExpectedSession)
	{
		OutIssue = IMGRagdollMode::ResolveInputIssue(PlacementInput, ExpectedSession);
		return false;
	}

	OutLocation = FRepMovement::RebaseOntoLocalOrigin(PlacementInput.PelvisLocation, &Movement);
	if (FVector::DistSquared(OutLocation, Movement.UpdatedComponent->GetComponentLocation())
		> FMath::Square(Movement.GetRagdollMovementState().Settings.MaxTargetDistance))
	{
		OutIssue = EIMGRagdollIssue::TargetTooFar;
		return false;
	}

	OutIssue = EIMGRagdollIssue::None;
	return true;
}

bool FIMGCustomMovementMode_Ragdoll::ValidatePlacement(
	const UIMGCharacterMovementComponent& Movement,
	const FTransform& Transform,
	bool bRequireFloor,
	EIMGRagdollIssue& OutIssue,
	const FVector& BodyLocalOffset) const
{
	OutIssue = EIMGRagdollIssue::Unavailable;
	if (!Movement.HasValidData()
		|| !bOwnsCapsuleCollision
		|| Movement.UpdatedComponent != Movement.CharacterOwner->GetCapsuleComponent())
	{
		return false;
	}

	FVector BodyLocation;
	const bool bHasBodyTarget = GetBodyLocation(Movement, BodyLocation, OutIssue);
	if (bRequireFloor && !bHasBodyTarget)
	{
		return false;
	}

	OutIssue = EIMGRagdollIssue::InvalidTarget;
	const FVector Up = -Movement.GetGravityDirection();
	if (Transform.ContainsNaN()
		|| !Transform.GetRotation().IsNormalized()
		|| !Transform.GetScale3D().Equals(FVector::OneVector)
		|| FVector::DotProduct(Transform.GetUnitAxis(EAxis::Z), Up) < IMGRagdollMode::UpAlignmentCosThreshold)
	{
		return false;
	}

	const FIMGRagdollSettings& Settings = Movement.GetRagdollMovementState().Settings;
	if (BodyLocalOffset.ContainsNaN() || BodyLocalOffset.SizeSquared() > FMath::Square(Settings.MaxTargetDistance))
	{
		return false;
	}

	const FVector Origin = Movement.UpdatedComponent->GetComponentLocation();
	const FVector Delta = Transform.GetLocation() - Origin;
	if (!Movement.ConstrainDirectionToPlane(Delta).Equals(Delta, IMGRagdollMode::PlaneConstraintTolerance)
		|| FVector::VectorPlaneProject(Delta, Up).SizeSquared() > FMath::Square(Settings.RecoverySearchRadius + IMGRagdollMode::PlacementRangeMargin)
		|| FMath::Abs(FVector::DotProduct(Delta, Up)) > Settings.RecoveryTraceDistance + IMGRagdollMode::PlacementRangeMargin)
	{
		return false;
	}

	const UCapsuleComponent* Capsule = Movement.CharacterOwner->GetCapsuleComponent();
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(
		Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight());
	FCollisionQueryParams Query(SCENE_QUERY_STAT(IMGRagdollPlacement), false, Movement.CharacterOwner.Get());
	FCollisionResponseParams Responses;
	Movement.InitCollisionParams(Query, Responses);
	Responses = FCollisionResponseParams(SavedResponses);
	const ECollisionChannel Channel = Capsule->GetCollisionObjectType();

	OutIssue = EIMGRagdollIssue::PlacementBlocked;
	if (Movement.GetWorld()->OverlapBlockingTestByChannel(
		Transform.GetLocation(), Transform.GetRotation(), Channel, Shape, Query, Responses))
	{
		return false;
	}

	if (bRequireFloor)
	{
		FHitResult Floor;
		OutIssue = EIMGRagdollIssue::NoFloor;
		const FVector FloorTraceEnd = Transform.GetLocation()
			- Up * (UCharacterMovementComponent::MAX_FLOOR_DIST + IMGRagdollMode::FloorCheckExtraDistance);
		if (!Movement.GetWorld()->SweepSingleByChannel(
			Floor, Transform.GetLocation(), FloorTraceEnd, Transform.GetRotation(), Channel, Shape, Query, Responses)
			|| Floor.bStartPenetrating)
		{
			return false;
		}

		OutIssue = EIMGRagdollIssue::UnwalkableFloor;
		if (!Movement.IsWalkable(Floor)
			|| !Movement.IsWithinEdgeTolerance(Floor.Location, Floor.ImpactPoint, Capsule->GetScaledCapsuleRadius()))
		{
			return false;
		}
	}

	OutIssue = EIMGRagdollIssue::PlacementPathBlocked;
	FHitResult Obstruction;
	if (bHasBodyTarget)
	{
		const FVector MatchedBodyLocation = Transform.TransformPosition(BodyLocalOffset);
		if (Movement.GetWorld()->LineTraceSingleByChannel(Obstruction, BodyLocation, MatchedBodyLocation, Channel, Query, Responses)
			|| Movement.GetWorld()->LineTraceSingleByChannel(Obstruction, MatchedBodyLocation, Transform.GetLocation(), Channel, Query, Responses))
		{
			return false;
		}
	}

	if (!Movement.GetWorld()->OverlapBlockingTestByChannel(Origin, Capsule->GetComponentQuat(), Channel, Shape, Query, Responses))
	{
		if (Movement.GetWorld()->SweepSingleByChannel(
			Obstruction, Origin, Transform.GetLocation(), Transform.GetRotation(), Channel, Shape, Query, Responses))
		{
			return false;
		}
	}
	else if (!bHasBodyTarget)
	{
		// A penetrating capsule needs a current, reachable body target to escape locally.
		return false;
	}

	OutIssue = EIMGRagdollIssue::None;
	return true;
}

bool FIMGCustomMovementMode_Ragdoll::FindPlacement(
	const UIMGCharacterMovementComponent& Movement,
	bool bRequireFloor,
	FTransform& OutTransform,
	EIMGRagdollIssue& OutIssue,
	const FVector& BodyLocalOffset) const
{
	OutTransform = FTransform::Identity;
	OutIssue = EIMGRagdollIssue::Unavailable;
	if (!Movement.HasValidData()
		|| !bOwnsCapsuleCollision
		|| Movement.UpdatedComponent != Movement.CharacterOwner->GetCapsuleComponent())
	{
		return false;
	}

	const FIMGRagdollSettings& Settings = Movement.GetRagdollMovementState().Settings;
	if (!Settings.IsValid())
	{
		return false;
	}

	OutIssue = EIMGRagdollIssue::InvalidTarget;
	if (BodyLocalOffset.ContainsNaN() || BodyLocalOffset.SizeSquared() > FMath::Square(Settings.MaxTargetDistance))
	{
		return false;
	}

	FVector BodyLocation;
	const bool bHasBodyLocation = GetBodyLocation(Movement, BodyLocation, OutIssue);
	if (bRequireFloor && !bHasBodyLocation)
	{
#if ENABLE_DRAW_DEBUG
		IMGRagdollMode::DrawPlacement(Movement, Movement.UpdatedComponent->GetComponentTransform(), OutIssue, BodyLocalOffset);
#endif
		return false;
	}

	const UCapsuleComponent* Capsule = Movement.CharacterOwner->GetCapsuleComponent();
	const FVector Origin = Movement.UpdatedComponent->GetComponentLocation();
	const FVector Up = -Movement.GetGravityDirection();
	FQuat Rotation = Movement.UpdatedComponent->GetComponentQuat();
	const FIMGRagdollMovementInput PlacementInput = Movement.GetRagdollMovementInput();
	if (PlacementInput.IsValid() && PlacementInput.Session == Movement.GetRagdollMovementState().Session)
	{
		const FVector BodyForward = FVector::VectorPlaneProject(
			FRotator(0.f, FRotator::DecompressAxisFromShort(PlacementInput.FacingYaw), 0.f).Vector(), Up).GetSafeNormal();
		if (!BodyForward.IsNearlyZero())
		{
			Rotation = FRotationMatrix::MakeFromXZ(BodyForward, Up).ToQuat();
		}
	}
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

	const int32 CandidateCount = Settings.RecoverySearchRadius > 0.0f
		? 1 + Settings.RecoverySearchRings * Settings.RecoverySearchSamples
		: 1;

	EIMGRagdollIssue BestFailureIssue = EIMGRagdollIssue::None;
	int32 BestFailurePriority = -1;

	for (int32 Index = 0; Index < CandidateCount; ++Index)
	{
		FVector Position = IMGRagdollMode::ComputeSearchCandidateOrigin(Origin, Forward, Right, Settings, Index);
		bool bCandidateReady = true;
		EIMGRagdollIssue CandidateIssue = EIMGRagdollIssue::None;

		FVector GroundedPosition = Position;
		if (bHasBodyLocation)
		{
			GroundedPosition += Up * FMath::Max(0.0, FVector::DotProduct(BodyLocation - Position, Up));
		}
		const bool bFoundFloor = IMGRagdollMode::ResolveWalkableFloorPosition(
			Movement, Settings, Rotation, Up, Radius, HalfHeight,
			Channel, Shape, Query, Responses, GroundedPosition, CandidateIssue);
		if (bFoundFloor)
		{
			Position = GroundedPosition;
		}
		bCandidateReady = !bRequireFloor || bFoundFloor;

		const FTransform Candidate(Rotation, Position);
		const bool bValid = bCandidateReady
			&& ValidatePlacement(Movement, Candidate, bRequireFloor, CandidateIssue, BodyLocalOffset);
#if ENABLE_DRAW_DEBUG
		IMGRagdollMode::DrawPlacement(Movement, Candidate, CandidateIssue, BodyLocalOffset);
#endif
		if (bValid)
		{
			OutTransform = Candidate;
			OutIssue = EIMGRagdollIssue::None;
			return true;
		}

		const int32 Priority = IMGRagdollMode::GetPlacementIssuePriority(CandidateIssue);
		if (Priority > BestFailurePriority)
		{
			BestFailurePriority = Priority;
			BestFailureIssue = CandidateIssue;
		}
	}

	OutIssue = BestFailureIssue != EIMGRagdollIssue::None
		? BestFailureIssue
		: (bRequireFloor ? EIMGRagdollIssue::NoFloor : EIMGRagdollIssue::PlacementBlocked);
	return false;
}

void FIMGCustomMovementMode_Ragdoll::ApplyRecoveryTransform(
	UIMGCharacterMovementComponent& Movement,
	const FTransform& Transform)
{
	if (!Movement.HasValidData()
		|| (!Movement.IsRagdollMovementMode() && !Movement.IsWalking() && !Movement.IsFalling())
		|| Transform.ContainsNaN())
	{
		return;
	}

	const uint16 Revision = Movement.GetRagdollMovementState().Revision;
	const EMovementMode PreviousMode = Movement.MovementMode;
	FHitResult Hit;
	Movement.MoveUpdatedComponent(
		Transform.GetLocation() - Movement.UpdatedComponent->GetComponentLocation(),
		Transform.GetRotation(),
		false,
		&Hit,
		ETeleportType::TeleportPhysics);
	if (!Movement.HasValidData()
		|| Movement.MovementMode != PreviousMode
		|| Movement.GetRagdollMovementState().Revision != Revision)
	{
		return;
	}

	Movement.bJustTeleported = true;
	if (Movement.IsRagdollRecovering() || Movement.GetRagdollMovementState().EndReason == EIMGRagdollEndReason::Recovered)
	{
		Movement.Velocity = FVector::ZeroVector;
	}

	Movement.SetMovementMode(MOVE_Falling);
	if (!Movement.HasValidData() || Movement.MovementMode != MOVE_Falling)
	{
		return;
	}

	Movement.FindFloor(Movement.UpdatedComponent->GetComponentLocation(), Movement.CurrentFloor, false);
	if (Movement.CurrentFloor.IsWalkableFloor())
	{
		Movement.SetMovementMode(MOVE_Walking);
		if (Movement.HasValidData() && Movement.MovementMode == MOVE_Walking)
		{
			Movement.SetBaseFromFloor(Movement.CurrentFloor);
		}
	}

	Movement.bForceNextFloorCheck = true;
	Movement.ForceReplicationUpdate();
}
