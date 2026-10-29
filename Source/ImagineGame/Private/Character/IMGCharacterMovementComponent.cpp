
#include "Character/IMGCharacterMovementComponent.h"

#include "Character/IMGCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/ScopedMovementUpdate.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"
#include "IMGLogChannels.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGCharacterMovementComponent)

class FSavedMove_IMG final : public FSavedMove_Character
{
public:
	EIMGStance SavedStance = EIMGStance::Stand;
	FIMGRagdollMovementInput SavedRagdollInput;

	virtual void Clear() override
	{
		FSavedMove_Character::Clear();
		SavedStance = EIMGStance::Stand;
		SavedRagdollInput = FIMGRagdollMovementInput();
	}

	virtual uint8 GetCompressedFlags() const override
	{
		uint8 Flags = FSavedMove_Character::GetCompressedFlags();
		Flags |= (SavedStance == EIMGStance::Crawl) ? FLAG_Custom_1 : 0;
		return Flags;
	}

	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override
	{
		const FSavedMove_IMG* OtherMove = static_cast<const FSavedMove_IMG*>(NewMove.Get());
		return SavedRagdollInput.Session == 0 && OtherMove->SavedRagdollInput.Session == 0
			&& SavedStance == OtherMove->SavedStance && FSavedMove_Character::CanCombineWith(NewMove, Character, MaxDelta);
	}

	virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override
	{
		FSavedMove_Character::SetMoveFor(Character, InDeltaTime, NewAccel, ClientData);
		UIMGCharacterMovementComponent* Movement = CastChecked<UIMGCharacterMovementComponent>(Character->GetCharacterMovement());
		SavedStance = Movement->GetDesiredStance();
		SavedRagdollInput = Movement->CaptureRagdollInput();
		Movement->RagdollMode.Input = SavedRagdollInput;
		bForceNoCombine |= SavedRagdollInput.Session != 0;
	}

	virtual void PrepMoveFor(ACharacter* Character) override
	{
		FSavedMove_Character::PrepMoveFor(Character);
		CastChecked<UIMGCharacterMovementComponent>(Character->GetCharacterMovement())->SetDesiredStanceFromMove(SavedStance);
		CastChecked<UIMGCharacterMovementComponent>(Character->GetCharacterMovement())->RagdollMode.Input = SavedRagdollInput;
	}
};

void FIMGCharacterNetworkMoveData::ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType)
{
	FCharacterNetworkMoveData::ClientFillNetworkMoveData(ClientMove, MoveType);
	RagdollInput = static_cast<const FSavedMove_IMG&>(ClientMove).SavedRagdollInput;
}

bool FIMGCharacterNetworkMoveData::Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType)
{
	const bool bBaseSuccess = FCharacterNetworkMoveData::Serialize(CharacterMovement, Ar, PackageMap, MoveType);
	bool bSuccess = true;
	RagdollInput.NetSerialize(Ar, PackageMap, bSuccess);
	return bBaseSuccess && bSuccess && !Ar.IsError();
}

FIMGCharacterNetworkMoveDataContainer::FIMGCharacterNetworkMoveDataContainer()
{
	NewMoveData = &Moves[0];
	PendingMoveData = &Moves[1];
	OldMoveData = &Moves[2];
}

void FIMGCharacterMoveResponseDataContainer::ServerFillResponseData(const UCharacterMovementComponent& CharacterMovement, const FClientAdjustment& PendingAdjustment)
{
	FCharacterMoveResponseDataContainer::ServerFillResponseData(CharacterMovement, PendingAdjustment);
	const UIMGCharacterMovementComponent& Movement = *CastChecked<const UIMGCharacterMovementComponent>(&CharacterMovement);
	RagdollState = Movement.GetRagdollMovementState();
	bHasRagdollState = RagdollState.Revision != 0;
	bRagdollPlacementReflected = bHasRagdollState && RagdollState.bHasRecovery
		&& (!PendingAdjustment.bAckGoodMove || !Movement.bIgnoreClientMovementErrorChecksAndCorrection)
		&& Movement.RagdollAdjustmentTimeStamp == PendingAdjustment.TimeStamp
		&& Movement.RagdollAdjustmentPlacementSession == RagdollState.Session;
	if (bHasRagdollState && (RagdollState.Phase != EIMGRagdollPhase::Inactive || RagdollState.bHasRecovery))
	{
		bHasRotation = true;
	}
	TEnumAsByte<EMovementMode> PendingMode;
	TEnumAsByte<EMovementMode> PendingGroundMode;
	uint8 PendingCustomMode = 0;
	Movement.UnpackNetworkMovementMode(PendingAdjustment.MovementMode, PendingMode, PendingCustomMode, PendingGroundMode);
	if (Movement.IsRagdollMovementMode() || (PendingMode == MOVE_Custom && PendingCustomMode == static_cast<uint8>(EIMGCustomMovementMode::Ragdoll)))
	{
		bRootMotionMontageCorrection = false;
		bRootMotionSourceCorrection = false;
	}
}

bool FIMGCharacterMoveResponseDataContainer::Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap)
{
	const bool bBaseSuccess = FCharacterMoveResponseDataContainer::Serialize(CharacterMovement, Ar, PackageMap);
	Ar.SerializeBits(&bHasRagdollState, 1);
	bool bSuccess = true;
	if (bHasRagdollState)
	{
		RagdollState.NetSerialize(Ar, PackageMap, bSuccess);
		if (RagdollState.bHasRecovery) { Ar.SerializeBits(&bRagdollPlacementReflected, 1); }
		else if (Ar.IsLoading()) { bRagdollPlacementReflected = false; }
	}
	else if (Ar.IsLoading()) { bRagdollPlacementReflected = false; }
	return bBaseSuccess && bSuccess && !Ar.IsError();
}

class FNetworkPredictionData_Client_IMG final : public FNetworkPredictionData_Client_Character
{
public:
	explicit FNetworkPredictionData_Client_IMG(const UCharacterMovementComponent& Movement)
		: FNetworkPredictionData_Client_Character(Movement) {}

	virtual FSavedMovePtr AllocateNewMove() override { return FSavedMovePtr(new FSavedMove_IMG()); }
};

namespace IMGCharacter
{
	static float GroundTraceDistance = 100000.0f;
	FAutoConsoleVariableRef CVar_GroundTraceDistance(TEXT("IMGCharacter.GroundTraceDistance"), GroundTraceDistance, TEXT("Distance to trace down when generating ground information."), ECVF_Cheat);
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

bool FIMGStanceStateMachine::TransitionTo(UIMGCharacterMovementComponent& Movement, EIMGStance NewStance, bool bClientSimulation)
{
	AIMGCharacter* Character = Cast<AIMGCharacter>(Movement.CharacterOwner.Get());
	if (!Character || !Movement.HasValidData()
		|| static_cast<uint8>(NewStance) > static_cast<uint8>(EIMGStance::Crawl))
	{
		return false;
	}

	if (CurrentStance == NewStance)
	{
		return true;
	}

	if (bTransitionInProgress || (!bClientSimulation && !Movement.CanEnterStance(NewStance)))
	{
		return false;
	}

	// Native crouch callbacks edit the mesh relative transform directly. Defer capsule
	// propagation until those callbacks finish, including requests outside PerformMovement.
	FScopedMovementUpdate ScopedMovement(Movement.UpdatedComponent, EScopedUpdate::DeferredUpdates);
	TGuardValue<bool> TransitionGuard(bTransitionInProgress, true);
	const EIMGStance PreviousStance = CurrentStance;
	CurrentStance = NewStance;
	if (!Movement.ApplyStanceTransition(PreviousStance, NewStance, bClientSimulation))
	{
		CurrentStance = PreviousStance;
		return false;
	}

	Character->SetReplicatedStance(CurrentStance);
	return true;
}

bool FIMGStanceStateMachine::RequestTransition(UIMGCharacterMovementComponent& Movement, EIMGStance NewStance)
{
	if (!Movement.CharacterOwner || Movement.CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy
		|| static_cast<uint8>(NewStance) > static_cast<uint8>(EIMGStance::Crawl))
	{
		return false;
	}

	Movement.SetDesiredStanceFromMove(NewStance);
	// Requests from stance callbacks are consumed by the next movement update.
	return bTransitionInProgress || ReconcileDesiredStance(Movement);
}

void FIMGStanceStateMachine::SetDesiredStance(EIMGStance NewStance)
{
	if (static_cast<uint8>(NewStance) <= static_cast<uint8>(EIMGStance::Crawl))
	{
		DesiredStance = NewStance;
	}
}

bool FIMGStanceStateMachine::ReconcileDesiredStance(UIMGCharacterMovementComponent& Movement)
{
	const EIMGStance RequestedStance = DesiredStance;
	if (TransitionTo(Movement, RequestedStance))
	{
		return true;
	}

	if (DesiredStance == RequestedStance)
	{
		Movement.SetDesiredStanceFromMove(CurrentStance);
	}
	return false;
}

bool FIMGStanceStateMachine::ApplyReplicatedStance(UIMGCharacterMovementComponent& Movement, EIMGStance ReplicatedStance)
{
	if (static_cast<uint8>(ReplicatedStance) > static_cast<uint8>(EIMGStance::Crawl))
	{
		return false;
	}

	Movement.SetDesiredStanceFromMove(ReplicatedStance);
	return TransitionTo(Movement, ReplicatedStance, true);
}

void FIMGStanceStateMachine::UpdateCurrentState(UIMGCharacterMovementComponent& Movement)
{
	if (CurrentStance != EIMGStance::Stand && !Movement.CanEnterStance(CurrentStance))
	{
		Movement.RequestStance(EIMGStance::Stand);
	}
}

EIMGStance UIMGCharacterMovementComponent::GetStance() const
{
	return StanceMachine.GetCurrentStance();
}

EIMGStance UIMGCharacterMovementComponent::GetDesiredStance() const
{
	return StanceMachine.GetDesiredStance();
}

bool UIMGCharacterMovementComponent::CanEnterStance(EIMGStance NewStance) const
{
	if (!HasValidData())
	{
		return false;
	}

	switch (NewStance)
	{
	case EIMGStance::Stand:
		return true;
	case EIMGStance::Crouch:
		return CanCrouchInCurrentState();
	case EIMGStance::Crawl:
		return bCanCrawl && IsMovingOnGround() && !UpdatedComponent->IsSimulatingPhysics();
	default:
		return false;
	}
}

float UIMGCharacterMovementComponent::GetStanceHalfHeight(EIMGStance Stance) const
{
	const ACharacter* DefaultCharacter = CharacterOwner->GetClass()->GetDefaultObject<ACharacter>();
	const UCapsuleComponent* Capsule = CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy
		? DefaultCharacter->GetCapsuleComponent() : CharacterOwner->GetCapsuleComponent();
	switch (Stance)
	{
	case EIMGStance::Crouch:
		return FMath::Max(Capsule->GetUnscaledCapsuleRadius(), GetCrouchedHalfHeight());
	case EIMGStance::Crawl:
		return FMath::Max(Capsule->GetUnscaledCapsuleRadius(), CrawlHalfHeight);
	default:
		return DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	}
}

bool UIMGCharacterMovementComponent::ApplyStanceTransition(EIMGStance PreviousStance, EIMGStance NewStance, bool bClientSimulation)
{
	AIMGCharacter* Character = CastChecked<AIMGCharacter>(CharacterOwner.Get());
	if (PreviousStance != EIMGStance::Crawl && NewStance != EIMGStance::Crawl)
	{
		// Native simulated crouch expects the replicated boolean to be set before its callback.
		if (bClientSimulation)
		{
			Character->SetIsCrouched(NewStance == EIMGStance::Crouch);
		}
		if (NewStance == EIMGStance::Crouch)
		{
			Super::Crouch(bClientSimulation);
		}
		else
		{
			Super::UnCrouch(bClientSimulation);
		}
		return Character->IsCrouched() == (NewStance == EIMGStance::Crouch);
	}

	// Crawl crossings resize directly: an intermediate standing capsule may not fit.
	if (!ResizeForCrawlTransition(PreviousStance, NewStance, bClientSimulation))
	{
		return false;
	}

	const float StandingHalfHeight = GetStanceHalfHeight(EIMGStance::Stand);
	const float ComponentScale = Character->GetCapsuleComponent()->GetShapeScale();
	const float PreviousAdjust = StandingHalfHeight - GetStanceHalfHeight(PreviousStance);
	const float NewAdjust = StandingHalfHeight - GetStanceHalfHeight(NewStance);
	Character->SetIsCrouched(NewStance == EIMGStance::Crouch);

	if (PreviousStance == EIMGStance::Crouch)
	{
		Character->OnEndCrouch(PreviousAdjust, PreviousAdjust * ComponentScale);
	}
	else if (PreviousStance == EIMGStance::Crawl)
	{
		Character->OnEndCrawl(PreviousAdjust, PreviousAdjust * ComponentScale);
	}

	if (NewStance == EIMGStance::Crouch)
	{
		Character->OnStartCrouch(NewAdjust, NewAdjust * ComponentScale);
	}
	else if (NewStance == EIMGStance::Crawl)
	{
		Character->OnStartCrawl(NewAdjust, NewAdjust * ComponentScale);
	}
	return true;
}

bool UIMGCharacterMovementComponent::ResizeForCrawlTransition(EIMGStance PreviousStance, EIMGStance NewStance, bool bClientSimulation)
{
	if (!HasValidData())
	{
		return false;
	}

	UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent();
	const bool bSimulatedProxy = bClientSimulation && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy;
	// Proxy shrink is applied once to the target size, never accumulated across transitions.
	const float CapsuleRadius = bSimulatedProxy
		? CharacterOwner->GetClass()->GetDefaultObject<ACharacter>()->GetCapsuleComponent()->GetUnscaledCapsuleRadius()
		: Capsule->GetUnscaledCapsuleRadius();
	const float OldHalfHeight = bSimulatedProxy ? GetStanceHalfHeight(PreviousStance) : Capsule->GetUnscaledCapsuleHalfHeight();
	const float NewHalfHeight = GetStanceHalfHeight(NewStance);
	if (!bSimulatedProxy && FMath::IsNearlyEqual(OldHalfHeight, NewHalfHeight))
	{
		return true;
	}

	const float ComponentScale = Capsule->GetShapeScale();
	const FVector BaseLocationOffset = (OldHalfHeight - NewHalfHeight) * ComponentScale * GetGravityDirection();
	const FVector ProposedLocation = UpdatedComponent->GetComponentLocation()
		+ (bCrouchMaintainsBaseLocation ? BaseLocationOffset : FVector::ZeroVector);

	if (!bClientSimulation && NewHalfHeight > OldHalfHeight)
	{
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(IMGStanceTrace), false, CharacterOwner.Get());
		FCollisionResponseParams ResponseParams;
		InitCollisionParams(QueryParams, ResponseParams);

		const float ScaledRadius = CapsuleRadius * ComponentScale;
		const float ScaledHalfHeight = FMath::Max(ScaledRadius, NewHalfHeight * ComponentScale - UE_KINDA_SMALL_NUMBER);
		const FCollisionShape TargetCapsuleShape = FCollisionShape::MakeCapsule(ScaledRadius, ScaledHalfHeight);
		if (GetWorld()->OverlapBlockingTestByChannel(
			ProposedLocation,
			GetWorldToGravityTransform(),
			UpdatedComponent->GetCollisionObjectType(),
			TargetCapsuleShape,
			QueryParams,
			ResponseParams))
		{
			return false;
		}
	}

	const bool bExpandingCapsule = NewHalfHeight > OldHalfHeight;
	if (!bClientSimulation && bCrouchMaintainsBaseLocation && bExpandingCapsule)
	{
		UpdatedComponent->MoveComponent(BaseLocationOffset, UpdatedComponent->GetComponentQuat(), false, nullptr, EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
	}

	Capsule->SetCapsuleSize(CapsuleRadius, NewHalfHeight, true);

	if (!bClientSimulation && bCrouchMaintainsBaseLocation && !bExpandingCapsule)
	{
		UpdatedComponent->MoveComponent(BaseLocationOffset, UpdatedComponent->GetComponentQuat(), false, nullptr, EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
	}

	bForceNextFloorCheck = true;
	if (bSimulatedProxy)
	{
		bShrinkProxyCapsule = true;
	}

	AdjustProxyCapsuleSize();
	if (bSimulatedProxy
		|| (IsNetMode(NM_ListenServer) && CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy))
	{
		if (FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character())
		{
			ClientData->MeshTranslationOffset -= (OldHalfHeight - NewHalfHeight) * ComponentScale * -GetGravityDirection();
			ClientData->OriginalMeshTranslationOffset = ClientData->MeshTranslationOffset;
		}
	}

	return true;
}

bool UIMGCharacterMovementComponent::RequestStance(EIMGStance NewStance)
{
	if (NewStance != GetStance() && (IsRagdollMovementMode() || IsRagdollRecovering()))
	{
		return false;
	}
	return StanceMachine.RequestTransition(*this, NewStance);
}

void UIMGCharacterMovementComponent::Crouch(bool bClientSimulation)
{
	if (bClientSimulation)
	{
		ApplyReplicatedStance(EIMGStance::Crouch);
	}
	else
	{
		RequestStance(EIMGStance::Crouch);
	}
}

void UIMGCharacterMovementComponent::UnCrouch(bool bClientSimulation)
{
	if (bClientSimulation)
	{
		ApplyReplicatedStance(EIMGStance::Stand);
	}
	else
	{
		RequestStance(EIMGStance::Stand);
	}
}

void UIMGCharacterMovementComponent::SetDesiredStanceFromMove(EIMGStance NewStance)
{
	StanceMachine.SetDesiredStance(NewStance);
	bWantsToCrouch = GetDesiredStance() == EIMGStance::Crouch;
}

void UIMGCharacterMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	UpdateRagdollMovementState();
	if (!HasValidData()) { return; }
	if (ActiveRagdollState.Phase == EIMGRagdollPhase::Simulating)
	{
		if (CharacterOwner && CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy && MovementMode != MOVE_None)
		{
			SetMovementMode(MOVE_Custom, static_cast<uint8>(EIMGCustomMovementMode::Ragdoll));
			if (CharacterOwner->HasAuthority() && (CharacterOwner->IsLocallyControlled() || CharacterOwner->GetRemoteRole() != ROLE_AutonomousProxy))
			{
				RagdollMode.Input = CaptureRagdollInput();
			}
		}
		return;
	}
	if (IsRagdollRecovering())
	{
		Acceleration = FVector::ZeroVector;
		bHasRequestedVelocity = false;
		RequestedVelocity = FVector::ZeroVector;
		return;
	}
	if (!CharacterOwner || CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		return;
	}

	// Accept native crouch intent (e.g. navigation) through the same state machine.
	if (bWantsToCrouch != (GetDesiredStance() == EIMGStance::Crouch))
	{
		SetDesiredStanceFromMove(bWantsToCrouch ? EIMGStance::Crouch : EIMGStance::Stand);
	}
	StanceMachine.UpdateCurrentState(*this);
	if (!StanceMachine.ReconcileDesiredStance(*this))
	{
		if (AIMGCharacter* Character = Cast<AIMGCharacter>(CharacterOwner.Get());
			Character && Character->HasAuthority() && Character->GetRemoteRole() == ROLE_AutonomousProxy)
		{
			Character->ClientCorrectStance(GetStance());
		}
	}
}

void UIMGCharacterMovementComponent::UpdateCharacterStateAfterMovement(float DeltaSeconds)
{
	if (IsRagdollMovementMode() || IsRagdollRecovering())
	{
		return;
	}
	if (CharacterOwner && CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy)
	{
		StanceMachine.UpdateCurrentState(*this);
	}
}

void UIMGCharacterMovementComponent::ApplyReplicatedStance(EIMGStance ReplicatedStance)
{
	StanceMachine.ApplyReplicatedStance(*this, ReplicatedStance);
}

void UIMGCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	SetDesiredStanceFromMove((Flags & FSavedMove_Character::FLAG_Custom_1) != 0
		? EIMGStance::Crawl
		: (bWantsToCrouch ? EIMGStance::Crouch : EIMGStance::Stand));
}

FNetworkPredictionData_Client* UIMGCharacterMovementComponent::GetPredictionData_Client() const
{
	if (!ClientPredictionData)
	{
		UIMGCharacterMovementComponent* MutableThis = const_cast<UIMGCharacterMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_IMG(*this);
	}
	return ClientPredictionData;
}

void UIMGCharacterMovementComponent::SimulateMovement(float DeltaTime)
{
	if (CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy
		&& GetDesiredStance() != GetStance())
	{
		// OnRep can arrive before movement data is ready. Retry through the same transition path.
		StanceMachine.ApplyReplicatedStance(*this, GetDesiredStance());
	}

	if (bHasReplicatedAcceleration)
	{
		// Preserve our replicated acceleration
		const FVector OriginalAcceleration = Acceleration;
		Super::SimulateMovement(DeltaTime);
		Acceleration = OriginalAcceleration;
	}
	else
	{
		Super::SimulateMovement(DeltaTime);
	}
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
		const FVector TraceEnd(TraceStart.X, TraceStart.Y, TraceStart.Z - IMGCharacter::GroundTraceDistance - CapsuleHalfHeight);

		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(IMGCharacterMovementComponent_GetGroundInfo), false, Character);
		FCollisionResponseParams ResponseParams;
		InitCollisionParams(QueryParams, ResponseParams);

		FHitResult HitResult;
		GetWorld()->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, CollisionChannel, QueryParams, ResponseParams);

		CachedGroundInfo.GroundHitResult = HitResult;
		CachedGroundInfo.GroundDistance = IMGCharacter::GroundTraceDistance;

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

float UIMGCharacterMovementComponent::GetMaxSpeed() const
{
	if (IsRagdollRecovering()) { return 0.0f; }
	const float MaxSpeed = Super::GetMaxSpeed();
	return IsCrawling() && IsMovingOnGround() ? FMath::Min(MaxSpeed, MaxCrawlSpeed) : MaxSpeed;
}

void UIMGCharacterMovementComponent::SetStrafeEnabled(const bool bEnable)
{
	bOrientRotationToMovement = !bEnable;
	bUseControllerDesiredRotation = bEnable;
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
	return RequestStance(EIMGStance::Stand) && GetStance() == EIMGStance::Stand
		&& Super::DoJump(bReplayingMoves, DeltaTime);
}

bool UIMGCharacterMovementComponent::CanAttemptJump() const
{
	if (IsRagdollMovementMode() || IsRagdollRecovering())
	{
		return false;
	}
	// Same as UCharacterMovementComponent's implementation but without the crouch check
	return IsJumpAllowed() &&
		(IsMovingOnGround() || IsFalling()); // Falling included for double-jump and non-zero jump hold time, but validated by character.
}

bool UIMGCharacterMovementComponent::IsRagdollMovementMode() const
{
	return MovementMode == MOVE_Custom && CustomMovementMode == static_cast<uint8>(EIMGCustomMovementMode::Ragdoll);
}

bool UIMGCharacterMovementComponent::HasRagdollNetworkMovementMode() const
{
	if (IsRagdollMovementMode()) { return true; }
	if (!CharacterOwner) { return false; }
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
		ApplyNetworkMovementMode(CharacterOwner->GetReplicatedMovementMode());
		bNetworkMovementModeChanged = false;
	}
	if (!HasValidData()) { return; }
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
		const FVector SavedMeshLocation = Mesh ? Mesh->GetRelativeLocation() : FVector::ZeroVector;
		const FQuat SavedRotation = UpdatedComponent->GetComponentQuat();
		const bool bPreventMeshMovement = !bNetworkSmoothingComplete;
		{
			const FScopedPreventAttachedComponentMove PreventMeshMovement(bPreventMeshMovement ? Mesh : nullptr);
			SimulateMovement(DeltaSeconds);
		}
		if (HasValidData() && bPreventMeshMovement && NetworkSmoothingMode == ENetworkSmoothingMode::Linear
			&& Mesh == CharacterOwner->GetMesh() && ClientPredictionData)
		{
			const FQuat Rotation = UpdatedComponent->GetComponentQuat();
			if (!Rotation.Equals(SavedRotation, 1e-6f))
			{
				const FQuat RotationDelta = Rotation - SavedRotation;
				ClientPredictionData->MeshRotationTarget += RotationDelta;
				ClientPredictionData->OriginalMeshRotationOffset += RotationDelta;
				ClientPredictionData->MeshRotationOffset = Rotation;
				if (Mesh) { Mesh->SetRelativeLocationAndRotation(SavedMeshLocation, CharacterOwner->GetBaseRotationOffset()); }
			}
		}
	}
	if (HasValidData() && !bNetworkSmoothingComplete) { SmoothClientPosition(DeltaSeconds); }
}

FIMGRagdollMovementInput UIMGCharacterMovementComponent::CaptureRagdollInput() const
{
	FIMGRagdollMovementInput Input;
	if (ActiveRagdollState.Phase != EIMGRagdollPhase::Simulating) { return Input; }
	Input.Session = ActiveRagdollState.Session;
	if (!RagdollInputSource.IsValid() || SubmittedRagdollInput.Session != Input.Session) { return Input; }
	Input = SubmittedRagdollInput;
	if (!GetWorld() || RagdollInputSampleTime < 0.0 || GetWorld()->GetTimeSeconds() - RagdollInputSampleTime > ActiveRagdollState.Settings.MaxTargetAge)
	{
		Input.Status = EIMGRagdollTargetStatus::Expired;
	}
	return Input;
}

FIMGRagdollMovementInput UIMGCharacterMovementComponent::GetRagdollMovementInput() const
{
	if (!HasValidData() || CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy) { return FIMGRagdollMovementInput(); }
	if (CharacterOwner->bClientUpdating) { return RagdollMode.Input; }
	if (CharacterOwner->HasAuthority() && !CharacterOwner->IsLocallyControlled() && CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy)
	{
		FIMGRagdollMovementInput Input = RagdollMode.Input;
		if (Input.Status == EIMGRagdollTargetStatus::Ready && (RagdollRemoteInputTime < 0.0
			|| GetWorld()->GetTimeSeconds() - RagdollRemoteInputTime > ActiveRagdollState.Settings.MaxTargetAge))
		{
			Input.Status = EIMGRagdollTargetStatus::Expired;
		}
		return Input;
	}
	return CaptureRagdollInput();
}

void UIMGCharacterMovementComponent::PhysCustom(float DeltaTime, int32 Iterations)
{
	if (IsRagdollMovementMode())
	{
		RagdollMode.Simulate(*this, DeltaTime);
		return;
	}
	Super::PhysCustom(DeltaTime, Iterations);
}

void UIMGCharacterMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	const bool bWasTransitioning = bRagdollTransitioning;
	TGuardValue<bool> Transition(bRagdollTransitioning, true);
	const bool bWasRagdoll = PreviousMovementMode == MOVE_Custom && PreviousCustomMode == static_cast<uint8>(EIMGCustomMovementMode::Ragdoll);
	const bool bIsRagdoll = IsRagdollMovementMode();
	if (bWasRagdoll && !bIsRagdoll)
	{
		RagdollMode.Exit(*this);
	}
	else if (!bWasRagdoll && bIsRagdoll)
	{
		RagdollMode.Enter(*this);
	}
	if (!HasValidData())
	{
		return;
	}
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
	if (!HasValidData()) { return; }
	if (!bWasTransitioning && CharacterOwner->HasAuthority() && ActiveRagdollState.Phase != EIMGRagdollPhase::Inactive
		&& ((ActiveRagdollState.Phase == EIMGRagdollPhase::Simulating && !IsRagdollMovementMode())
			|| (IsRagdollRecovering() && MovementMode != MOVE_Walking)))
	{
		FinishRagdollMovement(EIMGRagdollEndReason::MovementChanged, false);
	}
	if (!HasValidData()) { return; }
	if (!CharacterOwner->bClientUpdating && (bWasRagdoll != bIsRagdoll || IsRagdollRecovering())) { OnRagdollMovementUpdated.Broadcast(); }
	if (!HasValidData()) { return; }
}

void UIMGCharacterMovementComponent::PhysicsRotation(float DeltaTime)
{
	if (!IsRagdollMovementMode() && !IsRagdollRecovering())
	{
		Super::PhysicsRotation(DeltaTime);
	}
}

void UIMGCharacterMovementComponent::MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags, const FVector& NewAccel)
{
	if (const FCharacterNetworkMoveData* Data = GetCurrentNetworkMoveData(); Data && CharacterOwner && CharacterOwner->HasAuthority())
	{
		RagdollMode.Input = static_cast<const FIMGCharacterNetworkMoveData*>(Data)->RagdollInput;
		if (ActiveRagdollState.Phase == EIMGRagdollPhase::Simulating && RagdollMode.Input.Session == ActiveRagdollState.Session)
		{
			RagdollRemoteInputTime = GetWorld()->GetTimeSeconds();
		}
	}
	Super::MoveAutonomous(ClientTimeStamp, DeltaTime, CompressedFlags, NewAccel);
}

void UIMGCharacterMovementComponent::ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& MoveResponse)
{
	const FIMGCharacterMoveResponseDataContainer& Response = static_cast<const FIMGCharacterMoveResponseDataContainer&>(MoveResponse);
	const FNetworkPredictionData_Client_Character* PreviousClientData = HasPredictionData_Client() ? GetPredictionData_Client_Character() : nullptr;
	const FSavedMovePtr PreviousAckedMove = PreviousClientData ? PreviousClientData->LastAckedMove : FSavedMovePtr();
	if (Response.bHasRagdollState)
	{
		ReceiveRagdollMovementState(Response.RagdollState);
	}
	Super::ClientHandleMoveResponse(MoveResponse);
	const FNetworkPredictionData_Client_Character* ClientData = HasPredictionData_Client() ? GetPredictionData_Client_Character() : nullptr;
	if (HasValidData() && (!Response.IsCorrection() || !bClientIgnoreMovementCorrections) && Response.bHasRagdollState && Response.bRagdollPlacementReflected
		&& Response.RagdollState.bHasRecovery && Response.RagdollState.Session == ActiveRagdollState.Session
		&& ClientData && ClientData->LastAckedMove.IsValid() && ClientData->LastAckedMove != PreviousAckedMove
		&& ClientData->LastAckedMove->TimeStamp == Response.ClientAdjustment.TimeStamp && !IsRagdollMovementMode())
	{
		// This accepted ACK/correction was evaluated after the approved placement, not merely packed after it.
		RagdollRecoveryAppliedSession = ActiveRagdollState.Session;
	}
}

void UIMGCharacterMovementComponent::ServerMoveHandleClientError(float ClientTimeStamp, float DeltaTime, const FVector& Accel,
	const FVector& RelativeClientLocation, FMovementBaseInterfaceData* ClientMovementBaseInterfaceData, FName ClientBaseBoneName, uint8 ClientMovementMode)
{
	const uint16 Revision = ActiveRagdollState.Revision;
	const uint16 PlacementSession = ActiveRagdollState.bHasRecovery ? ActiveRagdollState.Session : 0;
	Super::ServerMoveHandleClientError(ClientTimeStamp, DeltaTime, Accel, RelativeClientLocation, ClientMovementBaseInterfaceData, ClientBaseBoneName, ClientMovementMode);
	if (HasPredictionData_Server() && GetPredictionData_Server_Character()->PendingAdjustment.TimeStamp == ClientTimeStamp)
	{
		RagdollAdjustmentTimeStamp = ClientTimeStamp;
		RagdollAdjustmentPlacementSession = ActiveRagdollState.Revision == Revision ? PlacementSession : 0;
	}
}

bool UIMGCharacterMovementComponent::FindRagdollRecoveryTransform(FTransform& OutTransform) const
{
	EIMGRagdollIssue Issue;
	return QueryRagdollRecovery(ActiveRagdollState.Session, OutTransform, Issue);
}

bool UIMGCharacterMovementComponent::QueryRagdollRecovery(int32 ExpectedSession, FTransform& OutTransform, EIMGRagdollIssue& OutIssue, FVector BodyLocalOffset) const
{
	OutTransform = FTransform::Identity;
	OutIssue = EIMGRagdollIssue::Unavailable;
	if (!HasValidData() || !IsRegistered() || !IsActive() || IsBeingDestroyed() || !IsRagdollMovementMode()
		|| CharacterOwner->IsActorBeingDestroyed() || CharacterOwner->bClientUpdating || CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy
		|| UpdatedComponent != CharacterOwner->GetCapsuleComponent()
		|| ActiveRagdollState.Phase != EIMGRagdollPhase::Simulating) { return false; }
	OutIssue = EIMGRagdollIssue::SessionMismatch;
	if (ExpectedSession == 0 || ExpectedSession != ActiveRagdollState.Session) { return false; }
	OutIssue = EIMGRagdollIssue::RecoveryTooFast;
	const float MaxSpeed = ActiveRagdollState.Settings.MaxRecoverySpeed;
	if (Velocity.ContainsNaN()) { OutIssue = EIMGRagdollIssue::InvalidTarget; return false; }
	if (MaxSpeed > 0.0f && Velocity.SizeSquared() > FMath::Square(MaxSpeed)) { return false; }
	return RagdollMode.FindRecoveryTransform(*this, OutTransform, OutIssue, BodyLocalOffset);
}

void UIMGCharacterMovementComponent::SetRagdollMovementIssue(EIMGRagdollIssue Issue)
{
	if (RagdollMovementIssue == Issue || (CharacterOwner && CharacterOwner->bClientUpdating)) { return; }
	RagdollMovementIssue = Issue;
	UE_LOG(LogIMG, Verbose, TEXT("[Ragdoll] %s Role=%d Session=%u Revision=%u Phase=%d Issue=%s"),
		*GetNameSafe(GetOwner()), CharacterOwner ? static_cast<int32>(CharacterOwner->GetLocalRole()) : -1,
		ActiveRagdollState.Session, ActiveRagdollState.Revision, static_cast<int32>(ActiveRagdollState.Phase),
		*StaticEnum<EIMGRagdollIssue>()->GetNameStringByValue(static_cast<int64>(Issue)));
}

void UIMGCharacterMovementComponent::ApplyRagdollRecoveryTransform(const FTransform& Transform)
{
	if (!HasValidData() || (!IsRagdollMovementMode() && !IsWalking() && !IsFalling()) || Transform.ContainsNaN()) { return; }
	RagdollRecoveryAppliedSession = ActiveRagdollState.Session;
	RagdollMode.ApplyRecoveryTransform(*this, Transform);
}

void UIMGCharacterMovementComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UIMGCharacterMovementComponent, ReplicatedRagdollState);
}

const FIMGRagdollRepState& UIMGCharacterMovementComponent::GetRagdollMovementState() const
{
	return ActiveRagdollState;
}

bool UIMGCharacterMovementComponent::IsRagdollRecovering() const
{
	return ActiveRagdollState.Phase == EIMGRagdollPhase::Recovering;
}

bool UIMGCharacterMovementComponent::IsRagdollRecoveryReady() const
{
	return IsRagdollRecovering() && HasValidData() && IsWalking()
		&& (CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy || RagdollRecoveryAppliedSession == ActiveRagdollState.Session);
}

bool UIMGCharacterMovementComponent::StartRagdollMovement(const FIMGRagdollSettings& Settings, FVector AdditionalVelocity)
{
	if (bRagdollTransitioning || !HasValidData() || !CharacterOwner->HasAuthority() || !IsRegistered() || !IsActive()
		|| IsBeingDestroyed() || CharacterOwner->IsActorBeingDestroyed() || UpdatedComponent != CharacterOwner->GetCapsuleComponent()
		|| MovementMode == MOVE_None || UpdatedComponent->IsSimulatingPhysics() || !CharacterOwner->GetCapsuleComponent()->IsQueryCollisionEnabled() || !Settings.IsValid()
		|| (!CharacterOwner->IsLocallyControlled() && CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy && !ShouldUsePackedMovementRPCs())
		|| AdditionalVelocity.ContainsNaN() || (Velocity + AdditionalVelocity).ContainsNaN())
	{
		SetRagdollMovementIssue(EIMGRagdollIssue::Unavailable);
		return false;
	}
	if (ActiveRagdollState.Phase == EIMGRagdollPhase::Simulating) { return IsRagdollMovementMode(); }
	TGuardValue<bool> Transition(bRagdollTransitioning, true);
	const uint16 Revision = ActiveRagdollState.Revision;
	uint16 Session = ActiveRagdollState.Session + 1;
	if (Session == 0) { ++Session; }
	ActiveRagdollState = FIMGRagdollRepState();
	ActiveRagdollState.Revision = Revision;
	ActiveRagdollState.Session = Session;
	ActiveRagdollState.Settings = Settings;
	ActiveRagdollState.Phase = EIMGRagdollPhase::Simulating;
	ActiveRagdollState.AnchorLocation = FRepMovement::RebaseOntoZeroOrigin(CharacterOwner->GetActorLocation(), this);
	ActiveRagdollState.InitialVelocity = Velocity + AdditionalVelocity;
	ActiveRagdollState.FacingYaw = FRotator::CompressAxisToShort(CharacterOwner->GetActorRotation().Yaw);
	SubmittedRagdollInput = FIMGRagdollMovementInput();
	RagdollInputSource.Reset();
	RagdollInputSampleTime = -1.0;
	RagdollRemoteInputTime = -1.0;
	RagdollRecoveryAppliedSession = 0;
	RagdollAdjustmentTimeStamp = -1.0f;
	RagdollAdjustmentPlacementSession = 0;
	SetRagdollMovementIssue(EIMGRagdollIssue::None);
	PublishRagdollMovementState();
	SetMovementMode(MOVE_Custom, static_cast<uint8>(EIMGCustomMovementMode::Ragdoll));
	if (HasValidData() && !IsRagdollMovementMode()) { FinishRagdollMovement(EIMGRagdollEndReason::MovementChanged, false); }
	if (HasValidData() && !CharacterOwner->bClientUpdating) { OnRagdollMovementUpdated.Broadcast(); }
	return HasValidData() && ActiveRagdollState.Phase == EIMGRagdollPhase::Simulating;
}

bool UIMGCharacterMovementComponent::SubmitRagdollFollowTarget(const FTransform& Target, bool bFaceUp, UObject* InputSource, int32 ExpectedSession)
{
	if (!HasValidData() || !IsRegistered() || !IsActive() || IsBeingDestroyed() || CharacterOwner->bClientUpdating
		|| ActiveRagdollState.Phase != EIMGRagdollPhase::Simulating
		|| (CharacterOwner->GetLocalRole() != ROLE_AutonomousProxy && !CharacterOwner->HasAuthority()))
	{
		SetRagdollMovementIssue(EIMGRagdollIssue::Unavailable);
		return false;
	}
	if (ExpectedSession == 0 || ExpectedSession != ActiveRagdollState.Session)
	{
		SetRagdollMovementIssue(EIMGRagdollIssue::SessionMismatch);
		return false;
	}
	if (Target.ContainsNaN() || !Target.GetRotation().IsNormalized() || !IsValid(InputSource))
	{
		SetRagdollMovementIssue(EIMGRagdollIssue::InvalidTarget);
		return false;
	}
	if (FVector::DistSquared(Target.GetLocation(), UpdatedComponent->GetComponentLocation()) > FMath::Square(ActiveRagdollState.Settings.MaxTargetDistance))
	{
		SetRagdollMovementIssue(EIMGRagdollIssue::TargetTooFar);
		return false;
	}
	if (RagdollInputSource.IsValid() && RagdollInputSource.Get() != InputSource)
	{
		SetRagdollMovementIssue(EIMGRagdollIssue::InputSourceBusy);
		return false;
	}
	const FVector Position = FRepMovement::RebaseOntoZeroOrigin(Target.GetLocation(), this);
	SubmittedRagdollInput.Session = ActiveRagdollState.Session;
	SubmittedRagdollInput.Status = EIMGRagdollTargetStatus::Ready;
	SubmittedRagdollInput.PelvisLocation = FVector(FMath::RoundToDouble(Position.X * 10.0) / 10.0,
		FMath::RoundToDouble(Position.Y * 10.0) / 10.0, FMath::RoundToDouble(Position.Z * 10.0) / 10.0);
	SubmittedRagdollInput.FacingYaw = FRotator::CompressAxisToShort(Target.Rotator().Yaw);
	SubmittedRagdollInput.bFaceUp = bFaceUp;
	RagdollInputSource = InputSource;
	RagdollInputSampleTime = GetWorld()->GetTimeSeconds();
	return true;
}

void UIMGCharacterMovementComponent::ClearRagdollFollowTarget(const UObject* InputSource, int32 ExpectedSession)
{
	if (ExpectedSession != 0 && ExpectedSession == ActiveRagdollState.Session && RagdollInputSource.Get() == InputSource)
	{
		SubmittedRagdollInput = FIMGRagdollMovementInput();
		RagdollInputSource.Reset();
		RagdollInputSampleTime = -1.0;
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

bool UIMGCharacterMovementComponent::RecoverRagdollMovementAt(int32 ExpectedSession, const FTransform& Transform, FVector BodyLocalOffset)
{
	if (bRagdollTransitioning || !HasValidData() || !CharacterOwner->HasAuthority() || !IsRegistered() || !IsActive()
		|| IsBeingDestroyed() || CharacterOwner->IsActorBeingDestroyed() || !IsRagdollMovementMode()
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
	if (Velocity.ContainsNaN()) { SetRagdollMovementIssue(EIMGRagdollIssue::InvalidTarget); return false; }
	if (ActiveRagdollState.Settings.MaxRecoverySpeed > 0.0f && Velocity.SizeSquared() > FMath::Square(ActiveRagdollState.Settings.MaxRecoverySpeed))
	{
		SetRagdollMovementIssue(EIMGRagdollIssue::RecoveryTooFast);
		return false;
	}
	EIMGRagdollIssue Issue;
	if (!RagdollMode.ValidateRecoveryTransform(*this, Transform, Issue, BodyLocalOffset)) { SetRagdollMovementIssue(Issue); return false; }
	// Validate the quantized placement that the owner will actually receive.
	const FVector Position = FRepMovement::RebaseOntoZeroOrigin(Transform.GetLocation(), this);
	const FVector QuantizedPosition(FMath::RoundToDouble(Position.X * 100.0) / 100.0,
		FMath::RoundToDouble(Position.Y * 100.0) / 100.0, FMath::RoundToDouble(Position.Z * 100.0) / 100.0);
	const uint16 FacingYaw = FRotator::CompressAxisToShort(Transform.Rotator().Yaw);
	const FVector Up = -GetGravityDirection();
	const FVector Facing = FVector::VectorPlaneProject(FRotator(0.0f, FRotator::DecompressAxisFromShort(FacingYaw), 0.0f).Vector(), Up).GetSafeNormal();
	if (Facing.IsNearlyZero()) { SetRagdollMovementIssue(EIMGRagdollIssue::InvalidTarget); return false; }
	const FTransform RecoveryTransform(FRotationMatrix::MakeFromXZ(Facing, Up).ToQuat(), FRepMovement::RebaseOntoLocalOrigin(QuantizedPosition, this));
	if (!RagdollMode.ValidateRecoveryTransform(*this, RecoveryTransform, Issue, BodyLocalOffset)) { SetRagdollMovementIssue(Issue); return false; }
	const bool bFaceUp = GetRagdollMovementInput().bFaceUp;
	TGuardValue<bool> Transition(bRagdollTransitioning, true);
	ActiveRagdollState.Phase = EIMGRagdollPhase::Recovering;
	ActiveRagdollState.bHasRecovery = true;
	ActiveRagdollState.bFaceUp = bFaceUp;
	ActiveRagdollState.AnchorLocation = QuantizedPosition;
	ActiveRagdollState.FacingYaw = FacingYaw;
	SubmittedRagdollInput = FIMGRagdollMovementInput();
	RagdollInputSource.Reset();
	RagdollInputSampleTime = -1.0;
	RagdollRemoteInputTime = -1.0;
	SetRagdollMovementIssue(EIMGRagdollIssue::None);
	PublishRagdollMovementState();
	ApplyRagdollRecoveryTransform(RecoveryTransform);
	if (HasValidData() && MovementMode != MOVE_Walking) { FinishRagdollMovement(EIMGRagdollEndReason::MovementChanged, false); }
	if (HasValidData() && !CharacterOwner->bClientUpdating) { OnRagdollMovementUpdated.Broadcast(); }
	return HasValidData() && ActiveRagdollState.Session == ExpectedSession
		&& (ActiveRagdollState.Phase == EIMGRagdollPhase::Recovering
			|| (ActiveRagdollState.Phase == EIMGRagdollPhase::Inactive && ActiveRagdollState.EndReason == EIMGRagdollEndReason::Recovered));
}

bool UIMGCharacterMovementComponent::StopRagdollMovement(EIMGRagdollEndReason Reason, int32 ExpectedSession)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) { return false; }
	if (ExpectedSession != 0 && ExpectedSession != ActiveRagdollState.Session)
	{
		SetRagdollMovementIssue(EIMGRagdollIssue::SessionMismatch);
		return false;
	}
	if (ActiveRagdollState.Phase == EIMGRagdollPhase::Inactive)
	{
		if (!bRagdollTransitioning && HasValidData() && IsRagdollMovementMode()) { SetMovementMode(MOVE_Falling); }
		return !IsRagdollMovementMode();
	}
	const bool bForce = Reason == EIMGRagdollEndReason::Unavailable || Reason == EIMGRagdollEndReason::MovementChanged;
	TGuardValue<bool> Transition(bRagdollTransitioning, true);
	if (bForce && (IsBeingDestroyed() || GetOwner()->IsActorBeingDestroyed() || !IsRegistered()))
	{
		if (HasValidData() && IsRagdollMovementMode()) { SetMovementMode(MOVE_None); }
		FinishRagdollMovement(Reason, true);
		return ActiveRagdollState.Phase == EIMGRagdollPhase::Inactive;
	}
	if (HasValidData() && IsRagdollMovementMode())
	{
		FTransform ExitTransform;
		EIMGRagdollIssue Issue;
		bool bFoundExit = RagdollMode.FindExitTransform(*this, ExitTransform, Issue);
		FVector QuantizedPosition = FVector::ZeroVector;
		uint16 FacingYaw = 0;
		if (bFoundExit)
		{
			const FVector Position = FRepMovement::RebaseOntoZeroOrigin(ExitTransform.GetLocation(), this);
			QuantizedPosition = FVector(FMath::RoundToDouble(Position.X * 100.0) / 100.0,
				FMath::RoundToDouble(Position.Y * 100.0) / 100.0, FMath::RoundToDouble(Position.Z * 100.0) / 100.0);
			FacingYaw = FRotator::CompressAxisToShort(ExitTransform.Rotator().Yaw);
			const FVector Up = -GetGravityDirection();
			const FVector Facing = FVector::VectorPlaneProject(FRotator(0.0f, FRotator::DecompressAxisFromShort(FacingYaw), 0.0f).Vector(), Up).GetSafeNormal();
			if (Facing.IsNearlyZero()) { bFoundExit = false; Issue = EIMGRagdollIssue::InvalidTarget; }
			else
			{
				bFoundExit = RagdollMode.ValidateExitTransform(*this,
					FTransform(FRotationMatrix::MakeFromXZ(Facing, Up).ToQuat(), FRepMovement::RebaseOntoLocalOrigin(QuantizedPosition, this)), Issue);
			}
		}
		if (!bFoundExit)
		{
			if (RagdollMovementIssue != EIMGRagdollIssue::ExitBlocked)
			{
				UE_LOG(LogIMG, Verbose, TEXT("[Ragdoll][ExitRejected] %s Session=%u Cause=%s"), *GetNameSafe(GetOwner()),
					ActiveRagdollState.Session, *StaticEnum<EIMGRagdollIssue>()->GetNameStringByValue(static_cast<int64>(Issue)));
			}
			SetRagdollMovementIssue(EIMGRagdollIssue::ExitBlocked);
			if (!bForce) { return false; }
			// Forced teardown cannot retain a live session or restore blocking collision inside geometry.
			SetMovementMode(MOVE_None);
		}
		else
		{
			ActiveRagdollState.AnchorLocation = QuantizedPosition;
			ActiveRagdollState.FacingYaw = FacingYaw;
			ActiveRagdollState.bHasRecovery = true;
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
	if (GetOwner() && GetOwner()->HasAuthority() && ExpectedSession != 0 && ExpectedSession == ActiveRagdollState.Session && IsRagdollRecovering())
	{
		FinishRagdollMovement(bInterrupted ? EIMGRagdollEndReason::Interrupted : EIMGRagdollEndReason::Recovered, false);
	}
}

void UIMGCharacterMovementComponent::FinishRagdollMovement(EIMGRagdollEndReason Reason, bool bLeaveMode, bool bKeepPlacement)
{
	if (ActiveRagdollState.Phase == EIMGRagdollPhase::Inactive)
	{
		if (bLeaveMode && IsRagdollMovementMode()) { SetMovementMode(MOVE_Falling); }
		return;
	}
	TGuardValue<bool> Transition(bRagdollTransitioning, true);
	bKeepPlacement |= IsRagdollRecovering() && IsWalking() && ActiveRagdollState.bHasRecovery;
	ActiveRagdollState.Phase = EIMGRagdollPhase::Inactive;
	ActiveRagdollState.EndReason = Reason;
	if (!bKeepPlacement && Reason != EIMGRagdollEndReason::Recovered)
	{
		ActiveRagdollState.bHasRecovery = false;
	}
	SubmittedRagdollInput = FIMGRagdollMovementInput();
	RagdollInputSource.Reset();
	RagdollInputSampleTime = -1.0;
	RagdollRemoteInputTime = -1.0;
	PublishRagdollMovementState();
	if (bLeaveMode && IsRagdollMovementMode())
	{
		if (bKeepPlacement && HasValidData())
		{
			ApplyRagdollRecoveryTransform(GetRagdollPlacementTransform());
		}
		else { SetMovementMode(MOVE_Falling); }
	}
	RagdollMode.ReleaseOrientation(*this);
	if (!CharacterOwner || !CharacterOwner->bClientUpdating) { OnRagdollMovementUpdated.Broadcast(); }
}

FTransform UIMGCharacterMovementComponent::GetRagdollPlacementTransform() const
{
	const FVector Up = -GetGravityDirection();
	const FVector Facing = FVector::VectorPlaneProject(FRotator(0.0f, FRotator::DecompressAxisFromShort(ActiveRagdollState.FacingYaw), 0.0f).Vector(), Up).GetSafeNormal();
	const FQuat Rotation = Facing.IsNearlyZero() ? UpdatedComponent->GetComponentQuat() : FRotationMatrix::MakeFromXZ(Facing, Up).ToQuat();
	return FTransform(Rotation, FRepMovement::RebaseOntoLocalOrigin(ActiveRagdollState.AnchorLocation, this));
}

void UIMGCharacterMovementComponent::PublishRagdollMovementState()
{
	++ActiveRagdollState.Revision;
	if (ActiveRagdollState.Revision == 0) { ++ActiveRagdollState.Revision; }
	ReplicatedRagdollState = ActiveRagdollState;
	UE_LOG(LogIMG, Verbose, TEXT("[Ragdoll][State] %s Role=%d Session=%u Revision=%u Phase=%d End=%d Placement=%d"),
		*GetNameSafe(GetOwner()), CharacterOwner ? static_cast<int32>(CharacterOwner->GetLocalRole()) : -1,
		ActiveRagdollState.Session, ActiveRagdollState.Revision, static_cast<int32>(ActiveRagdollState.Phase),
		static_cast<int32>(ActiveRagdollState.EndReason), ActiveRagdollState.bHasRecovery);
	if (GetOwner()) { GetOwner()->ForceNetUpdate(); }
	ForceReplicationUpdate();
}

void UIMGCharacterMovementComponent::OnRep_RagdollMovementState()
{
	ReceiveRagdollMovementState(ReplicatedRagdollState);
}

void UIMGCharacterMovementComponent::ReceiveRagdollMovementState(const FIMGRagdollRepState& State)
{
	if (!State.Settings.IsValid() || State.AnchorLocation.ContainsNaN() || State.InitialVelocity.ContainsNaN()
		|| static_cast<uint8>(State.Phase) > static_cast<uint8>(EIMGRagdollPhase::Recovering)
		|| (State.Session == 0 && State.Phase != EIMGRagdollPhase::Inactive)) { return; }
	if (State.Revision == 0 || (ActiveRagdollState.Revision != 0 && State.Revision != ActiveRagdollState.Revision
		&& static_cast<int16>(State.Revision - ActiveRagdollState.Revision) <= 0)) { return; }
	if (State.Revision == ActiveRagdollState.Revision) { return; }
	if (State.Session != ActiveRagdollState.Session || State.Phase != EIMGRagdollPhase::Simulating)
	{
		SubmittedRagdollInput = FIMGRagdollMovementInput();
		RagdollInputSource.Reset();
		RagdollInputSampleTime = -1.0;
		RagdollRemoteInputTime = -1.0;
	}
	if (State.Session != ActiveRagdollState.Session) { RagdollRecoveryAppliedSession = 0; }
	ActiveRagdollState = State;
	if (HasBegunPlay() && IsActive() && IsRegistered() && IsRagdollRecovering()) { RagdollMode.AcquireOrientation(*this); }
	if ((!IsActive() || !IsRegistered()) || (State.Phase == EIMGRagdollPhase::Inactive && !IsRagdollMovementMode())) { RagdollMode.ReleaseOrientation(*this); }
	if (!CharacterOwner || !CharacterOwner->bClientUpdating) { OnRagdollMovementUpdated.Broadcast(); }
}

void UIMGCharacterMovementComponent::UpdateRagdollMovementState()
{
	if (!HasValidData() || CharacterOwner->bClientUpdating || bRagdollTransitioning) { return; }
	if (!IsRegistered() || !IsActive()) { return; }
	if (IsRagdollRecovering()) { RagdollMode.AcquireOrientation(*this); }
	if (CharacterOwner->HasAuthority() && ActiveRagdollState.Phase != EIMGRagdollPhase::Inactive)
	{
		if (MovementMode == MOVE_None || UpdatedComponent != CharacterOwner->GetCapsuleComponent() || UpdatedComponent->IsSimulatingPhysics())
		{
			StopRagdollMovement(EIMGRagdollEndReason::Unavailable);
		}
		else if (IsRagdollRecovering() && MovementMode != MOVE_Walking) { FinishRagdollMovement(EIMGRagdollEndReason::MovementChanged, false); }
	}
	if (!HasValidData()) { return; }
	if (CharacterOwner->GetLocalRole() == ROLE_AutonomousProxy && MovementMode != MOVE_None)
	{
		if (ActiveRagdollState.Phase == EIMGRagdollPhase::Simulating) { SetMovementMode(MOVE_Custom, static_cast<uint8>(EIMGCustomMovementMode::Ragdoll)); }
		else if (ActiveRagdollState.Revision != 0)
		{
			if (ActiveRagdollState.bHasRecovery && RagdollRecoveryAppliedSession != ActiveRagdollState.Session
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

void UIMGCharacterMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	UpdateRagdollMovementState();
	if (IsBeingDestroyed() || !IsRegistered()) { return; }
	// Packet silence is an authority live-tick concern; saved move replay never consults this clock.
	if (HasValidData() && CharacterOwner->HasAuthority() && IsRagdollMovementMode()
		&& ActiveRagdollState.Phase == EIMGRagdollPhase::Simulating)
	{
		if (!CharacterOwner->IsLocallyControlled() && CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy)
		{
			const double Now = GetWorld()->GetTimeSeconds();
			if (RagdollRemoteInputTime < 0.0) { RagdollRemoteInputTime = Now; }
			if (RagdollMode.Input.Status != EIMGRagdollTargetStatus::Expired && Now - RagdollRemoteInputTime > ActiveRagdollState.Settings.MaxTargetAge)
			{
				RagdollMode.Input.Session = ActiveRagdollState.Session;
				RagdollMode.Input.Status = EIMGRagdollTargetStatus::Expired;
				Velocity = FVector::ZeroVector;
				SetRagdollMovementIssue(EIMGRagdollIssue::ExpiredTarget);
				CharacterOwner->ForceNetUpdate();
				ForceReplicationUpdate();
			}
		}
		else { RagdollRemoteInputTime = -1.0; }
	}
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void UIMGCharacterMovementComponent::Deactivate()
{
	TGuardValue<bool> Transition(bRagdollTransitioning, true);
	StopRagdollMovement(EIMGRagdollEndReason::Unavailable);
	if (IsRagdollMovementMode() && HasValidData()) { SetMovementMode(MOVE_Falling); }
	ClearRagdollMovementResources();
	Super::Deactivate();
	if (!IsBeingDestroyed()) { OnRagdollMovementUpdated.Broadcast(); }
}

void UIMGCharacterMovementComponent::OnUnregister()
{
	TGuardValue<bool> Transition(bRagdollTransitioning, true);
	StopRagdollMovement(EIMGRagdollEndReason::Unavailable);
	if (IsRagdollMovementMode() && HasValidData()) { SetMovementMode(MOVE_Falling); }
	ClearRagdollMovementResources();
	Super::OnUnregister();
	OnRagdollMovementUpdated.Broadcast();
}

void UIMGCharacterMovementComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	TGuardValue<bool> Transition(bRagdollTransitioning, true);
	if (HasValidData() && IsRagdollMovementMode()) { SetMovementMode(MOVE_None); }
	if (GetOwner() && GetOwner()->HasAuthority()) { FinishRagdollMovement(EIMGRagdollEndReason::Unavailable, false); }
	ClearRagdollMovementResources();
	Super::EndPlay(EndPlayReason);
}

void UIMGCharacterMovementComponent::ClearRagdollMovementResources()
{
	RagdollMode.Exit(*this);
	RagdollMode.ReleaseOrientation(*this);
	SubmittedRagdollInput = FIMGRagdollMovementInput();
	RagdollInputSource.Reset();
	RagdollInputSampleTime = -1.0;
	RagdollRemoteInputTime = -1.0;
	RagdollAdjustmentTimeStamp = -1.0f;
	RagdollAdjustmentPlacementSession = 0;
}

void UIMGCharacterMovementComponent::OnRep_IsActive()
{
	Super::OnRep_IsActive();
	if (!IsActive())
	{
		if (IsRagdollMovementMode() && HasValidData()) { SetMovementMode(MOVE_Falling); }
		ClearRagdollMovementResources();
		OnRagdollMovementUpdated.Broadcast();
	}
}
