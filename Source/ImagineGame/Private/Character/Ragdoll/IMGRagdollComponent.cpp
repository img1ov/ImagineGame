#include "Character/Ragdoll/IMGRagdollComponent.h"

#include "Character/IMGCharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGRagdollComponent)

UIMGRagdollComponent::UIMGRagdollComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsReplicatedByDefault(true);
	bAutoActivate = true;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

UIMGRagdollComponent* UIMGRagdollComponent::FindRagdollComponent(const AActor* Actor)
{
	if (!IsValid(Actor)) { return nullptr; }
	TArray<UIMGRagdollComponent*> Components;
	Actor->GetComponents(Components);
	for (UIMGRagdollComponent* Component : Components)
	{
		if (IsValid(Component) && Component->IsRegistered() && Component->IsActive() && !Component->IsBeingDestroyed()
			&& Component->Movement.IsValid()) { return Component; }
	}
	return nullptr;
}

void UIMGRagdollComponent::BeginPlay()
{
	Initialize();
	Super::BeginPlay();
	if (IsRegistered() && !IsBeingDestroyed()) { Initialize(); }
}

void UIMGRagdollComponent::OnRegister()
{
	Super::OnRegister();
	if (HasBegunPlay()) { Initialize(); }
}

bool UIMGRagdollComponent::Initialize()
{
	if (bTearingDown || !IsRegistered() || !IsActive() || IsBeingDestroyed() || !GetOwner() || GetOwner()->IsActorBeingDestroyed()) { return false; }
	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	UIMGCharacterMovementComponent* OwnerMovement = OwnerCharacter ? Cast<UIMGCharacterMovementComponent>(OwnerCharacter->GetCharacterMovement()) : nullptr;
	if (!IsValid(OwnerMovement) || OwnerMovement->IsBeingDestroyed()) { return false; }
	TArray<UIMGRagdollComponent*> Components;
	GetOwner()->GetComponents(Components);
	for (const UIMGRagdollComponent* Other : Components)
	{
		if (Other != this && Other->Movement.IsValid() && !Other->bTearingDown) { return false; }
	}
	if (Movement.IsValid() && Movement.Get() != OwnerMovement)
	{
		TearDown();
		if (!IsRegistered() || !IsActive() || IsBeingDestroyed() || !IsValid(OwnerMovement)
			|| !IsValid(OwnerCharacter) || OwnerCharacter->IsActorBeingDestroyed()) { return false; }
	}
	Character = OwnerCharacter;
	Movement = OwnerMovement;
	OwnerMovement->OnRagdollMovementUpdated.AddUniqueDynamic(this, &ThisClass::HandleMovementUpdated);
	AddTickPrerequisiteComponent(OwnerMovement);
	if (SampledMesh.IsValid()) { RemoveTickPrerequisiteComponent(SampledMesh.Get()); }
	SampledMesh = OwnerCharacter->GetMesh();
	if (SampledMesh.IsValid()) { AddTickPrerequisiteComponent(SampledMesh.Get()); }
	HandleMovementUpdated();
	return true;
}

void UIMGRagdollComponent::Activate(bool bReset)
{
	Super::Activate(bReset);
	if (HasBegunPlay()) { Initialize(); }
}

void UIMGRagdollComponent::Deactivate()
{
	TearDown();
	Super::Deactivate();
}

void UIMGRagdollComponent::OnRep_IsActive()
{
	Super::OnRep_IsActive();
	if (IsActive() && HasBegunPlay()) { Initialize(); }
	else if (!IsActive()) { TearDown(); }
}

void UIMGRagdollComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	TearDown();
	Super::EndPlay(EndPlayReason);
}

void UIMGRagdollComponent::OnUnregister()
{
	TearDown();
	Super::OnUnregister();
}

void UIMGRagdollComponent::TearDown()
{
	if (bTearingDown) { return; }
	TGuardValue<bool> TearDownGuard(bTearingDown, true);
	SetComponentTickEnabled(false);
	if (Movement.IsValid())
	{
		const int32 InputSession = Movement->GetRagdollMovementSession();
		Movement->OnRagdollMovementUpdated.RemoveDynamic(this, &ThisClass::HandleMovementUpdated);
		RemoveTickPrerequisiteComponent(Movement.Get());
		// Removing a coordinator must not terminate a session started by another caller.
		// Retain our cached target until the owned session has checked its safe exit.
		if (GetOwner() && GetOwner()->HasAuthority() && OwnedSession != 0)
		{
			Movement->StopRagdollMovement(EIMGRagdollEndReason::Unavailable, OwnedSession);
		}
		if (Movement.IsValid()) { Movement->ClearRagdollFollowTarget(this, InputSession); }
	}
	OwnedSession = 0;
	if (SampledMesh.IsValid()) { RemoveTickPrerequisiteComponent(SampledMesh.Get()); }
	SampledMesh.Reset();
	Movement.Reset();
	Character.Reset();
	ActiveState.Phase = EIMGRagdollPhase::Inactive;
	EndLocalSession(EIMGRagdollEndReason::Unavailable);
}

bool UIMGRagdollComponent::IsRagdollAvailable() const
{
	return !bTearingDown && IsRegistered() && IsActive() && !IsBeingDestroyed() && Character.IsValid() && Movement.IsValid()
		&& Character->GetCharacterMovement() == Movement.Get() && !Character->IsActorBeingDestroyed()
		&& !Movement->IsBeingDestroyed() && Movement->IsRegistered() && Movement->IsActive() && Movement->UpdatedComponent
		&& Movement->MovementMode != MOVE_None && !Movement->UpdatedComponent->IsSimulatingPhysics();
}

bool UIMGRagdollComponent::StartRagdoll(FVector AdditionalVelocity)
{
	if (bTransitioning || !IsRagdollAvailable() || !GetOwner()->HasAuthority()) { return false; }
	TGuardValue<bool> Transition(bTransitioning, true);
	const TWeakObjectPtr<UIMGCharacterMovementComponent> StartedMovement = Movement;
	const uint16 PreviousSession = Movement->GetRagdollMovementState().Session;
	const bool bStarted = Movement->StartRagdollMovement(Settings, AdditionalVelocity);
	if (!StartedMovement.IsValid()) { return false; }
	const uint16 Session = StartedMovement->GetRagdollMovementState().Session;
	if (bStarted && Session != PreviousSession)
	{
		if (!IsRagdollAvailable() || Movement != StartedMovement)
		{
			StartedMovement->StopRagdollMovement(EIMGRagdollEndReason::Unavailable, Session);
			return false;
		}
		OwnedSession = Session;
	}
	if (bStarted && IsRagdollAvailable()) { SampleTarget(); }
	return bStarted && IsRagdollAvailable() && ActiveState.Session == Session && ActiveState.Phase == EIMGRagdollPhase::Simulating;
}

bool UIMGRagdollComponent::BeginRecovery(int32 ExpectedSession)
{
	if (bTransitioning || !IsRagdollAvailable() || !GetOwner()->HasAuthority() || ExpectedSession == 0
		|| ExpectedSession != ActiveState.Session || ActiveState.Phase != EIMGRagdollPhase::Simulating) { return false; }
	TGuardValue<bool> Transition(bTransitioning, true);
	return Movement->RecoverRagdollMovement(ExpectedSession);
}

bool UIMGRagdollComponent::QueryRecovery(int32 ExpectedSession, FTransform& OutTransform, EIMGRagdollIssue& OutIssue, FVector BodyLocalOffset) const
{
	OutTransform = FTransform::Identity;
	OutIssue = EIMGRagdollIssue::Unavailable;
	return IsRagdollAvailable() && Movement->QueryRagdollRecovery(ExpectedSession, OutTransform, OutIssue, BodyLocalOffset);
}

bool UIMGRagdollComponent::BeginRecoveryAt(int32 ExpectedSession, const FTransform& Transform, FVector BodyLocalOffset)
{
	if (bTransitioning || !IsRagdollAvailable() || !GetOwner()->HasAuthority()) { return false; }
	TGuardValue<bool> Transition(bTransitioning, true);
	return Movement->RecoverRagdollMovementAt(ExpectedSession, Transform, BodyLocalOffset);
}

bool UIMGRagdollComponent::AbortRagdoll(int32 ExpectedSession, bool bForce)
{
	return ExpectedSession != 0 && Movement.IsValid() && Movement->StopRagdollMovement(
		bForce ? EIMGRagdollEndReason::Unavailable : EIMGRagdollEndReason::Interrupted, ExpectedSession);
}

void UIMGRagdollComponent::CompleteRecovery(int32 ExpectedSession, bool bInterrupted)
{
	if (Movement.IsValid()) { Movement->CompleteRagdollRecovery(ExpectedSession, bInterrupted); }
}

void UIMGRagdollComponent::HandleMovementUpdated()
{
	if (bTearingDown || !IsRegistered() || !IsActive() || !Movement.IsValid()) { return; }
	if (!Movement->IsRegistered() || Movement->IsBeingDestroyed()) { TearDown(); return; }
	ActiveState = Movement->GetRagdollMovementState();
	if (OwnedSession != ActiveState.Session) { OwnedSession = 0; }
	if (!Movement->IsActive())
	{
		EndLocalSession(EIMGRagdollEndReason::Unavailable);
		SetComponentTickEnabled(false);
		return;
	}
	if (HasBegunPlay()) { ReconcileState(); }
	SetComponentTickEnabled((ActiveState.Phase != EIMGRagdollPhase::Inactive || LocalSession != 0)
		&& Movement.IsValid() && IsRegistered() && IsActive() && !bTearingDown);
}

void UIMGRagdollComponent::EndLocalSession(EIMGRagdollEndReason Reason)
{
	const uint16 EndedSession = LocalSession;
	LocalSession = 0;
	bStartedNotified = false;
	bRecoveryNotified = false;
	if (EndedSession != 0) { OnRagdollEnded.Broadcast(EndedSession, Reason); }
}

void UIMGRagdollComponent::ReconcileState()
{
	if (bReconciling || bTearingDown || !Character.IsValid() || !Movement.IsValid() || Character->bClientUpdating) { return; }
	TGuardValue<bool> ReconcileGuard(bReconciling, true);
	TGuardValue<bool> Transition(bTransitioning, true);
	if (ActiveState.Phase == EIMGRagdollPhase::Inactive && Movement->IsRagdollMovementMode()) { return; }
	if (LocalSession != ActiveState.Session)
	{
		EndLocalSession(EIMGRagdollEndReason::Interrupted);
		if (bTearingDown || !Movement.IsValid() || !IsRegistered() || !IsActive()) { return; }
		if (ActiveState.Phase != EIMGRagdollPhase::Inactive) { LocalSession = ActiveState.Session; }
	}
	if (ActiveState.Phase == EIMGRagdollPhase::Simulating && IsRagdollAvailable() && Movement->IsRagdollMovementMode() && !bStartedNotified)
	{
		bStartedNotified = true;
		OnRagdollStarted.Broadcast(LocalSession);
	}
	if (bTearingDown || !Movement.IsValid() || !IsRegistered() || !IsActive()) { return; }
	if (ActiveState.Phase == EIMGRagdollPhase::Recovering && IsRagdollAvailable() && Movement->IsRagdollRecoveryReady() && !bRecoveryNotified)
	{
		bRecoveryNotified = true;
		OnRecoveryStarted.Broadcast(LocalSession);
	}
	if (bTearingDown || !Movement.IsValid()) { return; }
	if (ActiveState.Phase == EIMGRagdollPhase::Inactive)
	{
		OwnedSession = 0;
		EndLocalSession(ActiveState.EndReason);
		SetComponentTickEnabled(false);
	}
}

bool UIMGRagdollComponent::GetRagdollFollowTarget_Implementation(FTransform& OutTarget, bool& bFaceUp) const
{
	if (!IsRagdollAvailable()) { return false; }
	OutTarget = Character->GetActorTransform();
	bFaceUp = false;
	const USkeletalMeshComponent* Mesh = Character->GetMesh();
	if (IsValid(Mesh) && Mesh->GetNumComponentSpaceTransforms() > 0)
	{
		const int32 PelvisIndex = Mesh->GetBoneIndex(PelvisBone);
		if (PelvisIndex != INDEX_NONE && Mesh->IsSimulatingPhysics(PelvisBone))
		{
			const FVector Pelvis = Mesh->GetBoneTransform(PelvisIndex).GetLocation();
			OutTarget.SetLocation(Pelvis);
			// An optional orientation bone must not disable a valid position sample.
			const int32 OrientationIndex = Mesh->GetBoneIndex(OrientationBone);
			if (OrientationIndex != INDEX_NONE)
			{
				const FTransform Orientation = Mesh->GetBoneTransform(OrientationIndex);
				const FVector Up = -Movement->GetGravityDirection();
				const FVector Facing = FVector::VectorPlaneProject(Orientation.GetLocation() - Pelvis, Up).GetSafeNormal();
				if (!Facing.IsNearlyZero()) { OutTarget.SetRotation(Facing.Rotation().Quaternion()); }
				if (!FaceUpAxis.ContainsNaN())
				{
					bFaceUp = FVector::DotProduct(Orientation.GetRotation().RotateVector(FaceUpAxis.GetSafeNormal()), Up) > 0.0f;
				}
			}
		}
	}
	return true;
}

void UIMGRagdollComponent::SampleTarget()
{
	if (!IsRagdollAvailable() || ActiveState.Phase != EIMGRagdollPhase::Simulating || Character->bClientUpdating
		|| (Character->GetLocalRole() != ROLE_AutonomousProxy && !(Character->HasAuthority()
			&& (Character->IsLocallyControlled() || Character->GetRemoteRole() != ROLE_AutonomousProxy)))) { return; }
	const uint16 Session = ActiveState.Session;
	FTransform Target;
	bool bFaceUp = false;
	const bool bProvided = GetRagdollFollowTarget(Target, bFaceUp);
	if (!IsRagdollAvailable() || ActiveState.Session != Session || ActiveState.Phase != EIMGRagdollPhase::Simulating) { return; }
	if (!bProvided || !Movement->SubmitRagdollFollowTarget(Target, bFaceUp, this, Session)) { Movement->ClearRagdollFollowTarget(this, Session); }
}

bool UIMGRagdollComponent::GetRagdollBodyMotion_Implementation(FVector& OutLinearVelocity, FVector& OutAngularVelocityRadians) const
{
	OutLinearVelocity = FVector::ZeroVector;
	OutAngularVelocityRadians = FVector::ZeroVector;
	if (!IsRagdollAvailable()) { return false; }
	const USkeletalMeshComponent* Mesh = Character->GetMesh();
	if (!IsValid(Mesh) || Mesh->GetBoneIndex(PelvisBone) == INDEX_NONE || !Mesh->IsSimulatingPhysics(PelvisBone)) { return false; }
	OutLinearVelocity = Mesh->GetPhysicsLinearVelocity(PelvisBone);
	OutAngularVelocityRadians = Mesh->GetPhysicsAngularVelocityInRadians(PelvisBone);
	if (OutLinearVelocity.ContainsNaN() || OutAngularVelocityRadians.ContainsNaN())
	{
		OutLinearVelocity = FVector::ZeroVector;
		OutAngularVelocityRadians = FVector::ZeroVector;
		return false;
	}
	return true;
}

EIMGRagdollIssue UIMGRagdollComponent::GetRagdollMovementIssue() const
{
	return Movement.IsValid() ? Movement->GetRagdollMovementIssue() : EIMGRagdollIssue::Unavailable;
}

bool UIMGRagdollComponent::IsFaceUp() const
{
	if (Movement.IsValid())
	{
		const FIMGRagdollMovementInput Input = Movement->GetRagdollMovementInput();
		if (Input.IsValid() && Input.Session == ActiveState.Session) { return Input.bFaceUp; }
	}
	return ActiveState.bFaceUp;
}

void UIMGRagdollComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UIMGRagdollComponent, PelvisBone);
	DOREPLIFETIME(UIMGRagdollComponent, OrientationBone);
	DOREPLIFETIME(UIMGRagdollComponent, FaceUpAxis);
}

void UIMGRagdollComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!Character.IsValid() || !Movement.IsValid() || !Movement->IsRegistered()
		|| Character->GetCharacterMovement() != Movement.Get()) { TearDown(); return; }
	HandleMovementUpdated();
	if (IsRagdollAvailable()) { SampleTarget(); }
}
