
#include "Character/IMGCharacterMovementComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Character/IMGCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/ScopedMovementUpdate.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGCharacterMovementComponent)

UE_DEFINE_GAMEPLAY_TAG(TAG_Gameplay_MovementStopped, "Gameplay.MovementStopped");

class FSavedMove_IMG final : public FSavedMove_Character
{
public:
	EIMGStance SavedStance = EIMGStance::Stand;

	virtual void Clear() override
	{
		FSavedMove_Character::Clear();
		SavedStance = EIMGStance::Stand;
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
		return SavedStance == OtherMove->SavedStance && FSavedMove_Character::CanCombineWith(NewMove, Character, MaxDelta);
	}

	virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override
	{
		FSavedMove_Character::SetMoveFor(Character, InDeltaTime, NewAccel, ClientData);
		SavedStance = CastChecked<UIMGCharacterMovementComponent>(Character->GetCharacterMovement())->GetDesiredStance();
	}

	virtual void PrepMoveFor(ACharacter* Character) override
	{
		FSavedMove_Character::PrepMoveFor(Character);
		CastChecked<UIMGCharacterMovementComponent>(Character->GetCharacterMovement())->SetDesiredStanceFromMove(SavedStance);
	}
};

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
	SetCrouchedHalfHeight(60.0f);
	MaxWalkSpeedCrouched = 250.0f;
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

FRotator UIMGCharacterMovementComponent::GetDeltaRotation(float DeltaTime) const
{
	if (const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner()))
	{
		if (ASC->HasMatchingGameplayTag(TAG_Gameplay_MovementStopped))
		{
			return FRotator(0,0,0);
		}
	}

	return Super::GetDeltaRotation(DeltaTime);
}

float UIMGCharacterMovementComponent::GetMaxSpeed() const
{
	if (const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner()))
	{
		if (ASC->HasMatchingGameplayTag(TAG_Gameplay_MovementStopped))
		{
			return 0;
		}
	}

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
	// Server moves and prediction replay also need to stand before changing movement mode.
	return RequestStance(EIMGStance::Stand) && GetStance() == EIMGStance::Stand
		&& Super::DoJump(bReplayingMoves, DeltaTime);
}

bool UIMGCharacterMovementComponent::CanAttemptJump() const
{
	// Same as UCharacterMovementComponent's implementation but without the crouch check
	return IsJumpAllowed() &&
		(IsMovingOnGround() || IsFalling()); // Falling included for double-jump and non-zero jump hold time, but validated by character.
}
