#include "Character/IMGCharacterMovementComponent.h"

#include "Character/IMGCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"
#include "Templates/UnrealTemplate.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGCharacterMovementComponent)

DEFINE_LOG_CATEGORY(LogIMGMovementNet);

namespace IMGCharacterCore
{
	static TAutoConsoleVariable<int32> CVarMovementNetDiagnostics(
		TEXT("IMG.Movement.NetDiagnostics"),
		0,
		TEXT("0: Off. 1: Packed corrections, movement context differences and owner mesh offsets. 2: Also log velocity simulation inputs and outcomes."),
		ECVF_Default);

	static float GroundTraceDistance = 100000.0f;
	static FAutoConsoleVariableRef CVar_GroundTraceDistance(
		TEXT("IMGCharacter.GroundTraceDistance"),
		GroundTraceDistance,
		TEXT("Distance to trace down when generating ground information."),
		ECVF_Cheat);
}

int32 UIMGCharacterMovementComponent::GetMovementNetDiagnosticsLevel()
{
	return IMGCharacterCore::CVarMovementNetDiagnostics.GetValueOnGameThread();
}

UIMGCharacterMovementComponent::UIMGCharacterMovementComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NavAgentProps.bCanCrouch = true;
	SetIsReplicatedByDefault(true);
	SetCrouchedHalfHeight(60.0f);
	MaxWalkSpeedCrouched = 250.0f;
	SetNetworkMoveDataContainer(IMGNetworkMoveData);
	SetMoveResponseDataContainer(IMGMoveResponseData);
}

void UIMGCharacterMovementComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UIMGCharacterMovementComponent, ReplicatedRagdollState);
	DOREPLIFETIME(UIMGCharacterMovementComponent, ReplicatedRagdollBodyReference);
}

const FIMGCharacterGroundInfo& UIMGCharacterMovementComponent::GetGroundInfo()
{
	ACharacter* Character = CharacterOwner.Get();
	if (!Character || (GFrameCounter == CachedGroundInfo.LastUpdateFrame))
	{
		return CachedGroundInfo;
	}

	if (MovementMode == MOVE_Walking)
	{
		CachedGroundInfo.GroundHitResult = CurrentFloor.HitResult;
		CachedGroundInfo.GroundDistance = 0.0f;
	}
	else
	{
		const UCapsuleComponent* CapsuleComp = Character->GetCapsuleComponent();
		check(CapsuleComp);

		const float CapsuleHalfHeight = CapsuleComp->GetUnscaledCapsuleHalfHeight();
		const ECollisionChannel CollisionChannel = UpdatedComponent ? UpdatedComponent->GetCollisionObjectType() : ECC_Pawn;
		const FVector TraceStart(GetActorLocation());
		const FVector TraceEnd(TraceStart.X, TraceStart.Y, TraceStart.Z - IMGCharacterCore::GroundTraceDistance - CapsuleHalfHeight);

		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(IMGCharacterMovementComponent_GetGroundInfo), false, Character);
		FCollisionResponseParams ResponseParams;
		InitCollisionParams(QueryParams, ResponseParams);

		FHitResult HitResult;
		GetWorld()->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, CollisionChannel, QueryParams, ResponseParams);

		CachedGroundInfo.GroundHitResult = HitResult;
		CachedGroundInfo.GroundDistance = IMGCharacterCore::GroundTraceDistance;

		if (MovementMode == MOVE_NavWalking)
		{
			CachedGroundInfo.GroundDistance = 0.0f;
		}
		else if (HitResult.bBlockingHit)
		{
			CachedGroundInfo.GroundDistance = FMath::Max(HitResult.Distance - CapsuleHalfHeight, 0.0f);
		}
	}

	CachedGroundInfo.LastUpdateFrame = GFrameCounter;
	return CachedGroundInfo;
}

void UIMGCharacterMovementComponent::SetReplicatedAcceleration(const FVector& InAcceleration)
{
	bHasReplicatedAcceleration = true;
	Acceleration = InAcceleration;
}

void UIMGCharacterMovementComponent::SetStrafeEnabled(const bool bEnable)
{
	bOrientRotationToMovement = !bEnable;
	bUseControllerDesiredRotation = bEnable;
}

float UIMGCharacterMovementComponent::GetMaxSpeed() const
{
	if (IsRagdollRecovering())
	{
		return 0.0f;
	}
	const float MaxSpeed = Super::GetMaxSpeed();
	return IsCrawling() && IsMovingOnGround() ? FMath::Min(MaxSpeed, MaxCrawlSpeed) : MaxSpeed;
}

void UIMGCharacterMovementComponent::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
	const bool bRecord = GetMovementNetDiagnosticsLevel() >= 2
		&& HasValidData()
		&& (CharacterOwner->GetLocalRole() == ROLE_AutonomousProxy
			|| (CharacterOwner->HasAuthority() && CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy));
	if (!bRecord)
	{
		Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
		return;
	}

	const FCharacterNetworkMoveData* MoveData = GetCurrentNetworkMoveData();
	const float MoveTime = MoveData
		? MoveData->TimeStamp
		: (HasPredictionData_Client() ? GetPredictionData_Client_Character()->CurrentTimeStamp : -1.f);
	const FVector Before = Velocity;
	const float MaxAccel = GetMaxAcceleration();
	const float SpeedLimit = GetMaxSpeed();
	const float AnalogBefore = GetAnalogInputModifier();
	const FVector InputAcceleration = GetCurrentAcceleration();

	Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
	if (!HasValidData())
	{
		return;
	}

	UE_LOG(LogIMGMovementNet, Log,
		TEXT("[VelocityStep] Actor=%s Role=%d Stage=%s Move=%.6f Dt=%.6f Mode=%u Request=%u HasRequest=%d Context=%u ContextHash=%u MaxSpeed=%.3f MaxAccel=%.3f Analog=%.6f->%.6f Friction=%.3f Brake=%.3f BrakeFriction=%.3f BrakeFactor=%.3f SeparateBrake=%d Accel=%s Velocity=%s->%s AnimRootMotion=%d"),
		*GetPathNameSafe(CharacterOwner.Get()),
		static_cast<int32>(CharacterOwner->GetLocalRole()),
		CharacterOwner->bClientUpdating ? TEXT("Replay") : (CharacterOwner->HasAuthority() ? TEXT("Authority") : TEXT("Predict")),
		MoveTime,
		DeltaTime,
		static_cast<uint8>(MovementMode),
		MovementRequestRuntime.Request,
		MovementRequestRuntime.bHasRequest,
		MovementRequestRuntime.Context.Revision,
		MovementRequestRuntime.Context.GetParameterHash(),
		SpeedLimit,
		MaxAccel,
		AnalogBefore,
		GetAnalogInputModifier(),
		Friction,
		BrakingDeceleration,
		BrakingFriction,
		BrakingFrictionFactor,
		bUseSeparateBrakingFriction,
		*InputAcceleration.ToCompactString(),
		*Before.ToCompactString(),
		*Velocity.ToCompactString(),
		HasAnimRootMotion());
}

void UIMGCharacterMovementComponent::TickCharacterPose(float DeltaTime)
{
	// Skip autonomous pose tick on listen server for remote clients.
	// Defers animation evaluation to the regular mesh tick, which runs
	// AFTER SmoothClientPosition has settled the capsule rotation.
	if (IsNetMode(NM_ListenServer)
		&& CharacterOwner
		&& !CharacterOwner->bClientUpdating
		&& !CharacterOwner->IsPlayingRootMotion()
		&& CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy
		&& !CharacterOwner->IsLocallyControlled())
	{
		return;
	}
	Super::TickCharacterPose(DeltaTime);
}

bool UIMGCharacterMovementComponent::DoJump(bool bReplayingMoves, float DeltaTime)
{
	if (IsRagdollMovementMode() || IsRagdollRecovering())
	{
		return false;
	}
	// Server moves and prediction replay also need to stand before changing movement mode.
	return RequestStance(EIMGStance::Stand)
		&& GetStance() == EIMGStance::Stand
		&& Super::DoJump(bReplayingMoves, DeltaTime);
}

bool UIMGCharacterMovementComponent::CanAttemptJump() const
{
	if (IsRagdollMovementMode() || IsRagdollRecovering())
	{
		return false;
	}
	// Same as UCharacterMovementComponent's implementation but without the crouch check.
	return IsJumpAllowed() && (IsMovingOnGround() || IsFalling());
}

void UIMGCharacterMovementComponent::SimulateMovement(float DeltaTime)
{
	if (CharacterOwner
		&& CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy
		&& GetDesiredStance() != GetStance())
	{
		// OnRep can arrive before movement data is ready. Retry through the same transition path.
		StanceMachine.ApplyReplicatedStance(*this, GetDesiredStance());
	}

	if (bHasReplicatedAcceleration)
	{
		const FVector OriginalAcceleration = Acceleration;
		Super::SimulateMovement(DeltaTime);
		Acceleration = OriginalAcceleration;
	}
	else
	{
		Super::SimulateMovement(DeltaTime);
	}
}

void UIMGCharacterMovementComponent::PhysCustom(float DeltaTime, int32 Iterations)
{
	switch (static_cast<EIMGCustomMovementMode>(CustomMovementMode))
	{
	case EIMGCustomMovementMode::Ragdoll:
		RagdollMode.Simulate(*this, DeltaTime);
		return;
	default:
		Super::PhysCustom(DeltaTime, Iterations);
		return;
	}
}

void UIMGCharacterMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	if (!CharacterOwner)
	{
		return;
	}
	const bool bWasTransitioning = RagdollRuntime.bTransitioning;
	TGuardValue<bool> Transition(RagdollRuntime.bTransitioning, true);
	USkeletalMeshComponent* EntryMesh = CharacterOwner ? CharacterOwner->GetMesh() : nullptr;
	const bool bPreserveEntryPose = CharacterOwner && !CharacterOwner->bClientUpdating
		&& IsRagdollMovementMode()
		&& !(PreviousMovementMode == MOVE_Custom && PreviousCustomMode == static_cast<uint8>(EIMGCustomMovementMode::Ragdoll))
		&& IsValid(EntryMesh) && EntryMesh->GetAttachParent() == UpdatedComponent;
	const FTransform EntryMeshWorld = bPreserveEntryPose ? EntryMesh->GetComponentTransform() : FTransform::Identity;

	if (!CharacterOwner->bClientUpdating && ((!IsMovingOnGround() && !IsFalling()) || IsRagdollRecovering()))
	{
		ResetAutonomousMeshSmoothing();
	}

	const bool bWasRagdoll = PreviousMovementMode == MOVE_Custom
		&& PreviousCustomMode == static_cast<uint8>(EIMGCustomMovementMode::Ragdoll);
	const bool bIsRagdoll = IsRagdollMovementMode();
	if (bWasRagdoll && !bIsRagdoll)
	{
		RagdollMode.Exit(*this);
	}
	else if (!bWasRagdoll && bIsRagdoll)
	{
		// Capture once before Enter detaches the movement base; replay never re-launches a body.
		if (HasValidData() && !CharacterOwner->bClientUpdating
			&& RagdollRuntime.LocalLaunchSession != ActiveRagdollState.Session)
		{
			RagdollRuntime.LocalLaunchVelocity = Velocity + GetImpartedMovementBaseVelocity()
				+ FVector(ActiveRagdollState.LaunchVelocityDelta);
			RagdollRuntime.LocalLaunchSession = ActiveRagdollState.Session;
		}
		RagdollMode.Enter(*this);
	}

	if (!HasValidData())
	{
		return;
	}

	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
	if (!HasValidData())
	{
		return;
	}
	if (bPreserveEntryPose && IsValid(EntryMesh)
		&& EntryMesh == CharacterOwner->GetMesh() && EntryMesh->GetAttachParent() == UpdatedComponent)
	{
		EntryMesh->SetWorldTransform(EntryMeshWorld, false, nullptr, ETeleportType::TeleportPhysics);
	}

	if (!bWasTransitioning
		&& CharacterOwner->HasAuthority()
		&& ActiveRagdollState.Phase != EIMGRagdollPhase::Inactive
		&& ((ActiveRagdollState.Phase == EIMGRagdollPhase::Simulating && !IsRagdollMovementMode()
			&& !RagdollRuntime.bAwaitingInitialInput)
			|| (IsRagdollRecovering() && MovementMode != MOVE_Walking)))
	{
		FinishRagdollMovement(EIMGRagdollEndReason::MovementChanged, false);
	}

	if (!HasValidData())
	{
		return;
	}

	if (!CharacterOwner->bClientUpdating
		&& (bWasRagdoll != bIsRagdoll || IsRagdollRecovering() || ActiveRagdollState.bExternalTransition))
	{
		OnRagdollMovementUpdated.Broadcast();
	}
}

void UIMGCharacterMovementComponent::PhysicsRotation(float DeltaTime)
{
	if (!IsRagdollMovementMode() && !IsRagdollRecovering())
	{
		Super::PhysicsRotation(DeltaTime);
	}
}

void UIMGCharacterMovementComponent::OnTeleported()
{
	ResetAutonomousMeshSmoothing();
	Super::OnTeleported();
}

void UIMGCharacterMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	RemoveAutonomousMeshOffset();
	DecayAutonomousMeshOffset(DeltaTime);

	UpdateRagdollMovementState();
	if (IsBeingDestroyed() || !IsRegistered())
	{
		return;
	}

	TickRagdollRemoteInputTimeout();
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ApplyAutonomousMeshOffset();
}

void UIMGCharacterMovementComponent::TeardownMovementSubsystems(bool bEndingPlay, bool bBroadcastRagdollUpdate)
{
	ResetAutonomousMeshSmoothing();
	if (bEndingPlay)
	{
		UnregisterMovementRequestProvider(MovementRequestRuntime.Source.Get());
	}
	else
	{
		ResetMovementRequestHistory();
	}

	TGuardValue<bool> Transition(RagdollRuntime.bTransitioning, true);
	if (bEndingPlay)
	{
		if (HasValidData() && IsRagdollMovementMode())
		{
			SetMovementMode(MOVE_None);
		}
		if (GetOwner() && GetOwner()->HasAuthority())
		{
			FinishRagdollMovement(EIMGRagdollEndReason::Unavailable, false);
		}
	}
	else
	{
		StopRagdollMovement(EIMGRagdollEndReason::Unavailable);
		if (IsRagdollMovementMode() && HasValidData())
		{
			SetMovementMode(MOVE_Falling);
		}
	}

	ClearRagdollMovementResources();
	if (bBroadcastRagdollUpdate && !IsBeingDestroyed())
	{
		OnRagdollMovementUpdated.Broadcast();
	}
}

void UIMGCharacterMovementComponent::Deactivate()
{
	TeardownMovementSubsystems(/*bEndingPlay=*/false, /*bBroadcastRagdollUpdate=*/false);
	Super::Deactivate();
	if (!IsBeingDestroyed())
	{
		OnRagdollMovementUpdated.Broadcast();
	}
}

void UIMGCharacterMovementComponent::OnUnregister()
{
	TeardownMovementSubsystems(/*bEndingPlay=*/false, /*bBroadcastRagdollUpdate=*/false);
	Super::OnUnregister();
	OnRagdollMovementUpdated.Broadcast();
}

void UIMGCharacterMovementComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	TeardownMovementSubsystems(/*bEndingPlay=*/true, /*bBroadcastRagdollUpdate=*/false);
	Super::EndPlay(EndPlayReason);
}

void UIMGCharacterMovementComponent::OnRep_IsActive()
{
	Super::OnRep_IsActive();
	if (!IsActive())
	{
		TeardownMovementSubsystems(/*bEndingPlay=*/false, /*bBroadcastRagdollUpdate=*/true);
	}
}
