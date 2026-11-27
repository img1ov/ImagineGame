#include "Character/IMGCharacterMovementComponent.h"

#include "Character/IMGCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/ScopedMovementUpdate.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Templates/UnrealTemplate.h"

bool FIMGStanceStateMachine::TransitionTo(UIMGCharacterMovementComponent& Movement, EIMGStance NewStance, bool bClientSimulation)
{
	AIMGCharacter* Character = Cast<AIMGCharacter>(Movement.CharacterOwner.Get());
	if (!Character
		|| !Movement.HasValidData()
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
	if (!Movement.CharacterOwner
		|| Movement.CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy
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
		? DefaultCharacter->GetCapsuleComponent()
		: CharacterOwner->GetCapsuleComponent();
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
		UpdatedComponent->MoveComponent(
			BaseLocationOffset, UpdatedComponent->GetComponentQuat(), false, nullptr,
			EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
	}

	Capsule->SetCapsuleSize(CapsuleRadius, NewHalfHeight, true);

	if (!bClientSimulation && bCrouchMaintainsBaseLocation && !bExpandingCapsule)
	{
		UpdatedComponent->MoveComponent(
			BaseLocationOffset, UpdatedComponent->GetComponentQuat(), false, nullptr,
			EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
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
	if (!HasValidData())
	{
		return;
	}

	if (ActiveRagdollState.Phase == EIMGRagdollPhase::Simulating)
	{
		if (CharacterOwner && CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy && MovementMode != MOVE_None)
		{
			SetMovementMode(MOVE_Custom, static_cast<uint8>(EIMGCustomMovementMode::Ragdoll));
			if (CharacterOwner->HasAuthority()
				&& (CharacterOwner->IsLocallyControlled() || CharacterOwner->GetRemoteRole() != ROLE_AutonomousProxy))
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
