// Fill out your copyright notice in the Description page of Project Settings.

#include "Character/IMGCharacterMovementComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Character/IMGCharacter.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Player/IMGPlayerState.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGCharacterMovementComponent)

UE_DEFINE_GAMEPLAY_TAG(TAG_Gameplay_MovementStopped, "Gameplay.MovementStopped");

namespace IMGCharacter
{
	static float GroundTraceDistance = 100000.0f;
	FAutoConsoleVariableRef CVar_GroundTraceDistance(TEXT("IMGCharacter.GroundTraceDistance"), GroundTraceDistance, TEXT("Distance to trace down when generating ground information."), ECVF_Cheat);
}

UIMGCharacterMovementComponent::UIMGCharacterMovementComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	
}

void UIMGCharacterMovementComponent::InitializeComponent()
{
	Super::InitializeComponent();
}

void UIMGCharacterMovementComponent::SimulateMovement(float DeltaTime)
{
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

const FCharacterGroundInfo& UIMGCharacterMovementComponent::GetGroundInfo()
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

	return Super::GetMaxSpeed();
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

void UIMGCharacterMovementComponent::SetStrafeEnabled(const bool bEnable)
{
	bOrientRotationToMovement = !bEnable;
	bUseControllerDesiredRotation = bEnable;
}

bool UIMGCharacterMovementComponent::CanAttemptJump() const
{
	// Same as UCharacterMovementComponent's implementation but without the crouch check
	return IsJumpAllowed() &&
		(IsMovingOnGround() || IsFalling()); // Falling included for double-jump and non-zero jump hold time, but validated by character.
}
