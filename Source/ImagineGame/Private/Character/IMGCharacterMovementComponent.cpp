
#include "Character/IMGCharacterMovementComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Character/IMGCharacter.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Player/IMGPlayerState.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGCharacterMovementComponent)

UE_DEFINE_GAMEPLAY_TAG(TAG_Gameplay_MovementStopped, "Gameplay.MovementStopped");

/** Immutable behavior for one stance. Instances are static and allocation-free. */
struct FIMGStanceStateBase
{
	virtual ~FIMGStanceStateBase() = default;

	virtual float GetHalfHeight(const UIMGCharacterMovementComponent& Movement) const = 0;
	virtual bool CanEnter(const UIMGCharacterMovementComponent& Movement) const = 0;
	virtual float GetMaxSpeed(const UIMGCharacterMovementComponent& Movement, float DefaultSpeed) const { return DefaultSpeed; }

	virtual void OnEnter(UIMGCharacterMovementComponent& Movement) const {}
	virtual void OnUpdate(UIMGCharacterMovementComponent& Movement, float DeltaSeconds) const {}
	virtual void OnExit(UIMGCharacterMovementComponent& Movement) const {}
};

struct FIMGStandStance final : FIMGStanceStateBase
{
	virtual float GetHalfHeight(const UIMGCharacterMovementComponent& Movement) const override
	{
		return Movement.CharacterOwner->GetClass()->GetDefaultObject<ACharacter>()->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	}

	virtual bool CanEnter(const UIMGCharacterMovementComponent& Movement) const override
	{
		return Movement.HasValidData();
	}
};

struct FIMGCrouchStance final : FIMGStanceStateBase
{
	virtual float GetHalfHeight(const UIMGCharacterMovementComponent& Movement) const override
	{
		return FMath::Max(Movement.CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleRadius(), Movement.GetCrouchedHalfHeight());
	}

	virtual bool CanEnter(const UIMGCharacterMovementComponent& Movement) const override
	{
		return Movement.IsMovingOnGround() && Movement.CanCrouchInCurrentState();
	}

	virtual void OnEnter(UIMGCharacterMovementComponent& Movement) const override
	{
		AIMGCharacter* Character = CastChecked<AIMGCharacter>(Movement.CharacterOwner.Get());
		Character->SetIsCrouched(true);

		const float HalfHeightAdjust = Movement.GetStanceHalfHeight(EIMGStance::Stand) - GetHalfHeight(Movement);
		Character->OnStartCrouch(HalfHeightAdjust, HalfHeightAdjust * Character->GetCapsuleComponent()->GetShapeScale());
	}

	virtual void OnUpdate(UIMGCharacterMovementComponent& Movement, float DeltaSeconds) const override
	{
		if (!CanEnter(Movement))
		{
			Movement.RequestStance(EIMGStance::Stand);
		}
	}

	virtual void OnExit(UIMGCharacterMovementComponent& Movement) const override
	{
		AIMGCharacter* Character = CastChecked<AIMGCharacter>(Movement.CharacterOwner.Get());
		Character->SetIsCrouched(false);

		const float HalfHeightAdjust = Movement.GetStanceHalfHeight(EIMGStance::Stand) - GetHalfHeight(Movement);
		Character->OnEndCrouch(HalfHeightAdjust, HalfHeightAdjust * Character->GetCapsuleComponent()->GetShapeScale());
	}
};

struct FIMGCrawlStance final : FIMGStanceStateBase
{
	virtual float GetHalfHeight(const UIMGCharacterMovementComponent& Movement) const override
	{
		return FMath::Max(Movement.CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleRadius(), Movement.CrawlHalfHeight);
	}

	virtual bool CanEnter(const UIMGCharacterMovementComponent& Movement) const override
	{
		return Movement.bCanCrawl && Movement.HasValidData() && Movement.IsMovingOnGround()
			&& Movement.UpdatedComponent && !Movement.UpdatedComponent->IsSimulatingPhysics();
	}

	virtual float GetMaxSpeed(const UIMGCharacterMovementComponent& Movement, float DefaultSpeed) const override
	{
		return Movement.IsMovingOnGround() ? FMath::Min(DefaultSpeed, Movement.MaxCrawlSpeed) : DefaultSpeed;
	}

	virtual void OnEnter(UIMGCharacterMovementComponent& Movement) const override
	{
		AIMGCharacter* Character = CastChecked<AIMGCharacter>(Movement.CharacterOwner.Get());
		const float HalfHeightAdjust = Movement.GetStanceHalfHeight(EIMGStance::Stand) - GetHalfHeight(Movement);
		Character->OnStartCrawl(HalfHeightAdjust, HalfHeightAdjust * Character->GetCapsuleComponent()->GetShapeScale());
	}

	virtual void OnUpdate(UIMGCharacterMovementComponent& Movement, float DeltaSeconds) const override
	{
		if (!CanEnter(Movement))
		{
			Movement.RequestStance(EIMGStance::Stand);
		}
	}

	virtual void OnExit(UIMGCharacterMovementComponent& Movement) const override
	{
		AIMGCharacter* Character = CastChecked<AIMGCharacter>(Movement.CharacterOwner.Get());
		const float HalfHeightAdjust = Movement.GetStanceHalfHeight(EIMGStance::Stand) - GetHalfHeight(Movement);
		Character->OnEndCrawl(HalfHeightAdjust, HalfHeightAdjust * Character->GetCapsuleComponent()->GetShapeScale());
	}
};

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
		Flags |= (SavedStance == EIMGStance::Crouch) ? FLAG_Custom_0 : 0;
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

const FIMGStanceStateBase& FIMGStanceStateMachine::ResolveState(EIMGStance Stance)
{
	static const FIMGStandStance StandState;
	static const FIMGCrouchStance CrouchState;
	static const FIMGCrawlStance CrawlState;

	switch (Stance)
	{
	case EIMGStance::Crouch:
		return CrouchState;
	case EIMGStance::Crawl:
		return CrawlState;
	case EIMGStance::Stand:
	default:
		return StandState;
	}
}

bool FIMGStanceStateMachine::TransitionTo(UIMGCharacterMovementComponent& Movement, EIMGStance NewStance, bool bClientSimulation)
{
	AIMGCharacter* Character = Cast<AIMGCharacter>(Movement.CharacterOwner.Get());
	if (!Character || static_cast<uint8>(NewStance) > static_cast<uint8>(EIMGStance::Crawl))
	{
		return false;
	}

	if (CurrentStance == NewStance)
	{
		return true;
	}

	const FIMGStanceStateBase& NextState = ResolveState(NewStance);
	if (!bClientSimulation && !NextState.CanEnter(Movement))
	{
		return false;
	}

	// Capsule fitting is validated before either state's lifecycle is changed.
	if (!Movement.ResizeForStance(NewStance, bClientSimulation))
	{
		return false;
	}

	ResolveState(CurrentStance).OnExit(Movement);
	CurrentStance = NewStance;
	Character->SetReplicatedStance(NewStance);
	NextState.OnEnter(Movement);
	return true;
}

bool FIMGStanceStateMachine::RequestTransition(UIMGCharacterMovementComponent& Movement, EIMGStance NewStance)
{
	if (static_cast<uint8>(NewStance) > static_cast<uint8>(EIMGStance::Crawl))
	{
		return false;
	}

	if (Movement.CharacterOwner && Movement.CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		return false;
	}

	if (NewStance != EIMGStance::Stand && (Movement.IsFalling() || Movement.IsFlying()))
	{
		return false;
	}

	DesiredStance = NewStance;
	if (Movement.HasValidData() && !TransitionTo(Movement, NewStance))
	{
		DesiredStance = CurrentStance;
		return false;
	}

	return true;
}

void FIMGStanceStateMachine::SetDesiredStance(EIMGStance NewStance)
{
	DesiredStance = static_cast<uint8>(NewStance) <= static_cast<uint8>(EIMGStance::Crawl)
		? NewStance
		: EIMGStance::Stand;
}

bool FIMGStanceStateMachine::ReconcileDesiredStance(UIMGCharacterMovementComponent& Movement)
{
	if (DesiredStance == CurrentStance)
	{
		return true;
	}

	if (TransitionTo(Movement, DesiredStance))
	{
		return true;
	}

	DesiredStance = CurrentStance;
	return false;
}

bool FIMGStanceStateMachine::ApplyReplicatedStance(UIMGCharacterMovementComponent& Movement, EIMGStance ReplicatedStance)
{
	SetDesiredStance(ReplicatedStance);
	return TransitionTo(Movement, DesiredStance, true);
}

void FIMGStanceStateMachine::UpdateCurrentState(UIMGCharacterMovementComponent& Movement, float DeltaSeconds)
{
	ResolveState(CurrentStance).OnUpdate(Movement, DeltaSeconds);
}

float FIMGStanceStateMachine::GetMaxWalkSpeed(const UIMGCharacterMovementComponent& Movement, float DefaultSpeed) const
{
	return ResolveState(CurrentStance).GetMaxSpeed(Movement, DefaultSpeed);
}

EIMGStance UIMGCharacterMovementComponent::GetStance() const
{
	return StanceMachine.GetCurrentStance();
}

float UIMGCharacterMovementComponent::GetStanceHalfHeight(EIMGStance Stance) const
{
	return FIMGStanceStateMachine::ResolveState(Stance).GetHalfHeight(*this);
}

bool UIMGCharacterMovementComponent::ResizeForStance(EIMGStance NewStance, bool bClientSimulation)
{
	if (!HasValidData())
	{
		return false;
	}

	UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent();
	const float OldHalfHeight = Capsule->GetUnscaledCapsuleHalfHeight();
	const float NewHalfHeight = GetStanceHalfHeight(NewStance);
	if (FMath::IsNearlyEqual(OldHalfHeight, NewHalfHeight))
	{
		return true;
	}

	const float ComponentScale = Capsule->GetShapeScale();
	const FVector BaseLocationOffset = (OldHalfHeight - NewHalfHeight) * ComponentScale * GetGravityDirection();
	const FVector ProposedLocation = UpdatedComponent->GetComponentLocation()
		+ (bCrouchMaintainsBaseLocation ? BaseLocationOffset : FVector::ZeroVector);

	if (!bClientSimulation && NewHalfHeight > OldHalfHeight && !(NewStance == EIMGStance::Stand && (IsFalling() || IsFlying())))
	{
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(IMGStanceTrace), false, CharacterOwner.Get());
		FCollisionResponseParams ResponseParams;
		InitCollisionParams(QueryParams, ResponseParams);

		const float ScaledRadius = Capsule->GetUnscaledCapsuleRadius() * ComponentScale;
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
	if (bCrouchMaintainsBaseLocation && bExpandingCapsule)
	{
		UpdatedComponent->MoveComponent(BaseLocationOffset, UpdatedComponent->GetComponentQuat(), false, nullptr, EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
	}

	Capsule->SetCapsuleSize(Capsule->GetUnscaledCapsuleRadius(), NewHalfHeight, true);

	if (bCrouchMaintainsBaseLocation && !bExpandingCapsule)
	{
		UpdatedComponent->MoveComponent(BaseLocationOffset, UpdatedComponent->GetComponentQuat(), false, nullptr, EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
	}

	bForceNextFloorCheck = true;
	if (bClientSimulation && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		bShrinkProxyCapsule = true;
	}

	AdjustProxyCapsuleSize();
	if ((bClientSimulation && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
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
	if (StanceMachine.RequestTransition(*this, NewStance))
	{
		bWantsToCrouch = false;
		return true;
	}
	return false;
}

void UIMGCharacterMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);

	if ((IsFalling() || IsFlying()) && CharacterOwner && CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy)
	{
		// Airborne stances expand around the current center after the base movement mode
		// has disabled ground-only base preservation.
		RequestStance(EIMGStance::Stand);
	}
}

void UIMGCharacterMovementComponent::SetDesiredStanceFromMove(EIMGStance NewStance)
{
	StanceMachine.SetDesiredStance(NewStance);
	bWantsToCrouch = false;
}

void UIMGCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	const uint8 StanceFlags = Flags & (FSavedMove_Character::FLAG_Custom_0 | FSavedMove_Character::FLAG_Custom_1);
	SetDesiredStanceFromMove(StanceFlags == FSavedMove_Character::FLAG_Custom_0 ? EIMGStance::Crouch
		: StanceFlags == FSavedMove_Character::FLAG_Custom_1 ? EIMGStance::Crawl : EIMGStance::Stand);
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

void UIMGCharacterMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	// Base implementation owns bWantsToCrouch. IMG stance requests replace that path.
	if (!CharacterOwner || CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		return;
	}

	if (!StanceMachine.ReconcileDesiredStance(*this))
	{
		if (AIMGCharacter* Character = Cast<AIMGCharacter>(CharacterOwner.Get()); Character && Character->HasAuthority() && Character->GetRemoteRole() == ROLE_AutonomousProxy)
		{
			Character->ClientCorrectStance(Character->GetStance());
		}
	}
}

void UIMGCharacterMovementComponent::UpdateCharacterStateAfterMovement(float DeltaSeconds)
{
	// State validation runs once after movement so transitions caused by physics are settled.
	if (CharacterOwner && CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy)
	{
		StanceMachine.UpdateCurrentState(*this, DeltaSeconds);
	}
}

void UIMGCharacterMovementComponent::ApplyReplicatedStance(EIMGStance ReplicatedStance)
{
	StanceMachine.ApplyReplicatedStance(*this, ReplicatedStance);
}

void UIMGCharacterMovementComponent::Crouch(bool bClientSimulation)
{
	if (bClientSimulation)
	{
		StanceMachine.ApplyReplicatedStance(*this, EIMGStance::Crouch);
	}
	else
	{
		RequestStance(EIMGStance::Crouch);
	}
}

void UIMGCharacterMovementComponent::UnCrouch(bool bClientSimulation)
{
	if (GetDesiredStance() != EIMGStance::Crouch && GetStance() != EIMGStance::Crouch)
	{
		return;
	}

	if (bClientSimulation)
	{
		StanceMachine.ApplyReplicatedStance(*this, EIMGStance::Stand);
	}
	else
	{
		RequestStance(EIMGStance::Stand);
	}
}

void UIMGCharacterMovementComponent::InitializeComponent()
{
	Super::InitializeComponent();
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

	return StanceMachine.GetMaxWalkSpeed(*this, Super::GetMaxSpeed());
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

bool UIMGCharacterMovementComponent::CanAttemptJump() const
{
	// Same as UCharacterMovementComponent's implementation but without the crouch check
	return IsJumpAllowed() &&
		(IsMovingOnGround() || IsFalling()); // Falling included for double-jump and non-zero jump hold time, but validated by character.
}
