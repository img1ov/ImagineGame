
#include "Character/IMGCharacterMovementComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Character/IMGCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/ScopedMovementUpdate.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGCharacterMovementComponent)

UE_DEFINE_GAMEPLAY_TAG(TAG_Gameplay_MovementStopped, "Gameplay.MovementStopped");

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
		return !SavedRagdollInput.IsValid() && !OtherMove->SavedRagdollInput.IsValid()
			&& SavedStance == OtherMove->SavedStance && FSavedMove_Character::CanCombineWith(NewMove, Character, MaxDelta);
	}

	virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override
	{
		FSavedMove_Character::SetMoveFor(Character, InDeltaTime, NewAccel, ClientData);
		UIMGCharacterMovementComponent* Movement = CastChecked<UIMGCharacterMovementComponent>(Character->GetCharacterMovement());
		SavedStance = Movement->GetDesiredStance();
		SavedRagdollInput = Movement->CaptureRagdollInput();
		Movement->RagdollMode.Input = SavedRagdollInput;
		bForceNoCombine |= SavedRagdollInput.IsValid();
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
	RagdollState = CastChecked<const UIMGCharacterMovementComponent>(&CharacterMovement)->GetRagdollMovementState();
	bHasRagdollState = RagdollState.Revision != 0;
	if (bHasRagdollState && RagdollState.Phase != EIMGRagdollPhase::Inactive)
	{
		bHasRotation = true;
	}
	if (CastChecked<const UIMGCharacterMovementComponent>(&CharacterMovement)->IsRagdollMovementMode())
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
	}
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

FRotator UIMGCharacterMovementComponent::ComputeOrientToMovementRotation(
	const FRotator& CurrentRotation,
	float DeltaTime,
	FRotator& DeltaRotation) const
{
	const bool bHasInputAcceleration = Acceleration.SizeSquared() >= UE_KINDA_SMALL_NUMBER;
	const bool bHasPathFollowingDirection = bHasRequestedVelocity
		&& RequestedVelocity.SizeSquared() > UE_KINDA_SMALL_NUMBER;
	if (!bOrientToVelocityDuringBraking
		|| bHasInputAcceleration
		|| bHasPathFollowingDirection
		|| !IsMovingOnGround()
		|| HasAnimRootMotion()
		|| HasRootMotionSources())
	{
		return Super::ComputeOrientToMovementRotation(CurrentRotation, DeltaTime, DeltaRotation);
	}

	const FVector PlanarVelocity = FVector::VectorPlaneProject(Velocity, GetGravityDirection());
	const float MinSpeed = FMath::Max(BrakingOrientationMinSpeed, UE_KINDA_SMALL_NUMBER);
	if (PlanarVelocity.SizeSquared() <= FMath::Square(MinSpeed))
	{
		return CurrentRotation;
	}

	return PlanarVelocity.GetSafeNormal().Rotation();
}

float UIMGCharacterMovementComponent::GetMaxSpeed() const
{
	if (IsRagdollRecovering()) { return 0.0f; }
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
	return ActiveRagdollState.Phase == EIMGRagdollPhase::Simulating && RagdollInputSource.IsValid()
		&& SubmittedRagdollInput.Session == ActiveRagdollState.Session ? SubmittedRagdollInput : FIMGRagdollMovementInput();
}

const FIMGRagdollMovementInput& UIMGCharacterMovementComponent::GetRagdollMovementInput() const
{
	return RagdollMode.Input;
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
	if (const FCharacterNetworkMoveData* Data = GetCurrentNetworkMoveData())
	{
		RagdollMode.Input = static_cast<const FIMGCharacterNetworkMoveData*>(Data)->RagdollInput;
	}
	Super::MoveAutonomous(ClientTimeStamp, DeltaTime, CompressedFlags, NewAccel);
}

void UIMGCharacterMovementComponent::ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& MoveResponse)
{
	const FIMGCharacterMoveResponseDataContainer& Response = static_cast<const FIMGCharacterMoveResponseDataContainer&>(MoveResponse);
	if (Response.bHasRagdollState)
	{
		ReceiveRagdollMovementState(Response.RagdollState);
	}
	Super::ClientHandleMoveResponse(MoveResponse);
}

bool UIMGCharacterMovementComponent::FindRagdollRecoveryTransform(FTransform& OutTransform) const
{
	return IsRagdollMovementMode() && RagdollMode.FindRecoveryTransform(*this, OutTransform);
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
		|| MovementMode == MOVE_None || UpdatedComponent->IsSimulatingPhysics() || !Settings.IsValid()
		|| AdditionalVelocity.ContainsNaN() || (Velocity + AdditionalVelocity).ContainsNaN()) { return false; }
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
	RagdollRecoveryAppliedSession = 0;
	PublishRagdollMovementState();
	SetMovementMode(MOVE_Custom, static_cast<uint8>(EIMGCustomMovementMode::Ragdoll));
	if (HasValidData() && !IsRagdollMovementMode()) { FinishRagdollMovement(EIMGRagdollEndReason::MovementChanged, false); }
	if (HasValidData() && !CharacterOwner->bClientUpdating) { OnRagdollMovementUpdated.Broadcast(); }
	return HasValidData() && ActiveRagdollState.Phase == EIMGRagdollPhase::Simulating;
}

bool UIMGCharacterMovementComponent::SubmitRagdollFollowTarget(const FTransform& Target, bool bFaceUp, UObject* InputSource)
{
	if (!HasValidData() || ActiveRagdollState.Phase != EIMGRagdollPhase::Simulating || Target.ContainsNaN()
		|| !IsValid(InputSource) || (CharacterOwner->GetLocalRole() != ROLE_AutonomousProxy && !CharacterOwner->HasAuthority())) { return false; }
	const FVector Position = FRepMovement::RebaseOntoZeroOrigin(Target.GetLocation(), this);
	SubmittedRagdollInput.Session = ActiveRagdollState.Session;
	SubmittedRagdollInput.PelvisLocation = FVector(FMath::RoundToDouble(Position.X * 10.0) / 10.0,
		FMath::RoundToDouble(Position.Y * 10.0) / 10.0, FMath::RoundToDouble(Position.Z * 10.0) / 10.0);
	SubmittedRagdollInput.FacingYaw = FRotator::CompressAxisToShort(Target.Rotator().Yaw);
	SubmittedRagdollInput.bFaceUp = bFaceUp;
	RagdollInputSource = InputSource;
	return true;
}

void UIMGCharacterMovementComponent::ClearRagdollFollowTarget(const UObject* InputSource)
{
	if (RagdollInputSource.Get() == InputSource)
	{
		SubmittedRagdollInput = FIMGRagdollMovementInput();
		RagdollInputSource.Reset();
	}
}

bool UIMGCharacterMovementComponent::RecoverRagdollMovement(int32 ExpectedSession)
{
	if (bRagdollTransitioning || !HasValidData() || !CharacterOwner->HasAuthority() || !IsActive() || !IsRagdollMovementMode()
		|| ExpectedSession == 0 || ExpectedSession != ActiveRagdollState.Session
		|| ActiveRagdollState.Phase != EIMGRagdollPhase::Simulating) { return false; }
	const FIMGRagdollSettings& Settings = ActiveRagdollState.Settings;
	if (Settings.MaxRecoverySpeed > 0.0f && Velocity.SizeSquared() > FMath::Square(Settings.MaxRecoverySpeed)) { return false; }
	FTransform RecoveryTransform;
	if (!FindRagdollRecoveryTransform(RecoveryTransform)) { return false; }
	TGuardValue<bool> Transition(bRagdollTransitioning, true);
	ActiveRagdollState.Phase = EIMGRagdollPhase::Recovering;
	ActiveRagdollState.bHasRecovery = true;
	ActiveRagdollState.bFaceUp = RagdollMode.Input.Session == ActiveRagdollState.Session ? RagdollMode.Input.bFaceUp : SubmittedRagdollInput.bFaceUp;
	ActiveRagdollState.AnchorLocation = FRepMovement::RebaseOntoZeroOrigin(RecoveryTransform.GetLocation(), this);
	ActiveRagdollState.FacingYaw = FRotator::CompressAxisToShort(RecoveryTransform.Rotator().Yaw);
	SubmittedRagdollInput = FIMGRagdollMovementInput();
	RagdollInputSource.Reset();
	PublishRagdollMovementState();
	ApplyRagdollRecoveryTransform(RecoveryTransform);
	if (HasValidData() && MovementMode != MOVE_Walking) { FinishRagdollMovement(EIMGRagdollEndReason::MovementChanged, false); }
	if (HasValidData() && !CharacterOwner->bClientUpdating) { OnRagdollMovementUpdated.Broadcast(); }
	return HasValidData() && ActiveRagdollState.Session == ExpectedSession
		&& (ActiveRagdollState.Phase == EIMGRagdollPhase::Recovering
			|| (ActiveRagdollState.Phase == EIMGRagdollPhase::Inactive && ActiveRagdollState.EndReason == EIMGRagdollEndReason::Recovered));
}

void UIMGCharacterMovementComponent::StopRagdollMovement(EIMGRagdollEndReason Reason, int32 ExpectedSession)
{
	if (GetOwner() && GetOwner()->HasAuthority() && (ExpectedSession == 0 || ExpectedSession == ActiveRagdollState.Session))
	{
		FinishRagdollMovement(Reason, true);
	}
}

void UIMGCharacterMovementComponent::CompleteRagdollRecovery(int32 ExpectedSession, bool bInterrupted)
{
	if (GetOwner() && GetOwner()->HasAuthority() && ExpectedSession != 0 && ExpectedSession == ActiveRagdollState.Session && IsRagdollRecovering())
	{
		FinishRagdollMovement(bInterrupted ? EIMGRagdollEndReason::Interrupted : EIMGRagdollEndReason::Recovered, false);
	}
}

void UIMGCharacterMovementComponent::FinishRagdollMovement(EIMGRagdollEndReason Reason, bool bLeaveMode)
{
	if (ActiveRagdollState.Phase == EIMGRagdollPhase::Inactive)
	{
		if (bLeaveMode && IsRagdollMovementMode()) { SetMovementMode(MOVE_Falling); }
		return;
	}
	TGuardValue<bool> Transition(bRagdollTransitioning, true);
	ActiveRagdollState.Phase = EIMGRagdollPhase::Inactive;
	ActiveRagdollState.EndReason = Reason;
	if (Reason != EIMGRagdollEndReason::Recovered)
	{
		ActiveRagdollState.bHasRecovery = false;
	}
	SubmittedRagdollInput = FIMGRagdollMovementInput();
	RagdollInputSource.Reset();
	PublishRagdollMovementState();
	if (bLeaveMode && IsRagdollMovementMode()) { SetMovementMode(MOVE_Falling); }
	RagdollMode.ReleaseOrientation(*this);
	if (!CharacterOwner || !CharacterOwner->bClientUpdating) { OnRagdollMovementUpdated.Broadcast(); }
}

void UIMGCharacterMovementComponent::PublishRagdollMovementState()
{
	++ActiveRagdollState.Revision;
	if (ActiveRagdollState.Revision == 0) { ++ActiveRagdollState.Revision; }
	ReplicatedRagdollState = ActiveRagdollState;
	if (GetOwner()) { GetOwner()->ForceNetUpdate(); }
	ForceReplicationUpdate();
}

void UIMGCharacterMovementComponent::OnRep_RagdollMovementState()
{
	ReceiveRagdollMovementState(ReplicatedRagdollState);
}

void UIMGCharacterMovementComponent::ReceiveRagdollMovementState(const FIMGRagdollRepState& State)
{
	if (State.Revision == 0 || (ActiveRagdollState.Revision != 0 && State.Revision != ActiveRagdollState.Revision
		&& static_cast<int16>(State.Revision - ActiveRagdollState.Revision) <= 0)) { return; }
	if (State.Revision == ActiveRagdollState.Revision) { return; }
	if (State.Session != ActiveRagdollState.Session || State.Phase != EIMGRagdollPhase::Simulating)
	{
		SubmittedRagdollInput = FIMGRagdollMovementInput();
		RagdollInputSource.Reset();
	}
	if (State.Session != ActiveRagdollState.Session) { RagdollRecoveryAppliedSession = 0; }
	ActiveRagdollState = State;
	if (IsActive() && IsRegistered() && IsRagdollRecovering()) { RagdollMode.AcquireOrientation(*this); }
	if ((!IsActive() || !IsRegistered()) || (State.Phase == EIMGRagdollPhase::Inactive && !IsRagdollMovementMode())) { RagdollMode.ReleaseOrientation(*this); }
	if (!CharacterOwner || !CharacterOwner->bClientUpdating) { OnRagdollMovementUpdated.Broadcast(); }
}

void UIMGCharacterMovementComponent::UpdateRagdollMovementState()
{
	if (!HasValidData() || CharacterOwner->bClientUpdating || bRagdollTransitioning) { return; }
	if (!IsRegistered() || !IsActive()) { return; }
	if (CharacterOwner->HasAuthority() && ActiveRagdollState.Phase != EIMGRagdollPhase::Inactive)
	{
		if (MovementMode == MOVE_None || UpdatedComponent != CharacterOwner->GetCapsuleComponent() || UpdatedComponent->IsSimulatingPhysics())
		{
			FinishRagdollMovement(EIMGRagdollEndReason::Unavailable, true);
		}
		else if (IsRagdollRecovering() && MovementMode != MOVE_Walking) { FinishRagdollMovement(EIMGRagdollEndReason::MovementChanged, false); }
	}
	if (!HasValidData()) { return; }
	if (CharacterOwner->GetLocalRole() == ROLE_AutonomousProxy && MovementMode != MOVE_None)
	{
		if (ActiveRagdollState.Phase == EIMGRagdollPhase::Simulating) { SetMovementMode(MOVE_Custom, static_cast<uint8>(EIMGCustomMovementMode::Ragdoll)); }
		else if (ActiveRagdollState.Revision != 0)
		{
			if (ActiveRagdollState.bHasRecovery && (IsRagdollMovementMode()
				|| (IsRagdollRecovering() && RagdollRecoveryAppliedSession != ActiveRagdollState.Session && (IsWalking() || IsFalling()))))
			{
				const FVector Up = -GetGravityDirection();
				const FVector Facing = FVector::VectorPlaneProject(FRotator(0.0f, FRotator::DecompressAxisFromShort(ActiveRagdollState.FacingYaw), 0.0f).Vector(), Up).GetSafeNormal();
				const FQuat Rotation = Facing.IsNearlyZero() ? UpdatedComponent->GetComponentQuat() : FRotationMatrix::MakeFromXZ(Facing, Up).ToQuat();
				ApplyRagdollRecoveryTransform(FTransform(Rotation, FRepMovement::RebaseOntoLocalOrigin(ActiveRagdollState.AnchorLocation, this)));
			}
			else if (IsRagdollMovementMode()) { SetMovementMode(MOVE_Falling); }
		}
	}
}

void UIMGCharacterMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	UpdateRagdollMovementState();
	if (IsBeingDestroyed() || !IsRegistered()) { return; }
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void UIMGCharacterMovementComponent::Deactivate()
{
	StopRagdollMovement(EIMGRagdollEndReason::Unavailable);
	Super::Deactivate();
}

void UIMGCharacterMovementComponent::OnUnregister()
{
	StopRagdollMovement(EIMGRagdollEndReason::Unavailable);
	if (IsRagdollMovementMode() && HasValidData()) { SetMovementMode(MOVE_Falling); }
	RagdollMode.Exit(*this);
	RagdollMode.ReleaseOrientation(*this);
	SubmittedRagdollInput = FIMGRagdollMovementInput();
	RagdollInputSource.Reset();
	Super::OnUnregister();
}

void UIMGCharacterMovementComponent::OnRep_IsActive()
{
	Super::OnRep_IsActive();
	if (!IsActive())
	{
		if (IsRagdollMovementMode() && HasValidData()) { SetMovementMode(MOVE_Falling); }
		RagdollMode.Exit(*this);
		RagdollMode.ReleaseOrientation(*this);
		SubmittedRagdollInput = FIMGRagdollMovementInput();
		RagdollInputSource.Reset();
		OnRagdollMovementUpdated.Broadcast();
	}
}
