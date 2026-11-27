#include "Character/Ragdoll/IMGRagdollComponent.h"

#include "Character/IMGCharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "Cosmetics/IMGPawnComponent_CharacterParts.h"
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
	BodyPrePhysicsTick.bCanEverTick = true;
	BodyPrePhysicsTick.bStartWithTickEnabled = false;
	BodyPrePhysicsTick.TickGroup = TG_PrePhysics;
}

void UIMGRagdollComponent::RegisterComponentTickFunctions(bool bRegister)
{
	Super::RegisterComponentTickFunctions(bRegister);
	if (bRegister)
	{
		if (SetupActorComponentTickFunction(&BodyPrePhysicsTick)) { BodyPrePhysicsTick.Target = this; }
		PrimaryComponentTick.AddPrerequisite(this, BodyPrePhysicsTick);
	}
	else
	{
		PrimaryComponentTick.RemovePrerequisite(this, BodyPrePhysicsTick);
		BodyPrePhysicsTick.UnRegisterTickFunction();
	}
}

void UIMGRagdollComponent::SetExecutionTickEnabled(bool bEnabled)
{
	SetComponentTickEnabled(bEnabled);
	BodyPrePhysicsTick.SetTickFunctionEnable(bEnabled);
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
	if (!CapsuleCorrectionHandle.IsValid())
	{
		CapsuleCorrectionHandle = OwnerMovement->RagdollCapsuleCorrection.AddUObject(this, &ThisClass::HandleCapsuleCorrection);
	}
	AddTickPrerequisiteComponent(OwnerMovement);
	BodyPrePhysicsTick.AddPrerequisite(OwnerMovement, OwnerMovement->PrimaryComponentTick);
	RefreshMeshObservation();
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
	const bool bPendingTransition = ActiveState.bExternalTransition && ReleasedTransitionRevision != ActiveState.Revision
		&& (BodySession != 0 || ReadyTransitionRevision == ActiveState.Revision);
	SetExecutionTickEnabled(false);
	if (Movement.IsValid())
	{
		const int32 InputSession = Movement->GetRagdollMovementSession();
		Movement->OnRagdollMovementUpdated.RemoveDynamic(this, &ThisClass::HandleMovementUpdated);
		Movement->RagdollCapsuleCorrection.Remove(CapsuleCorrectionHandle);
		RemoveTickPrerequisiteComponent(Movement.Get());
		BodyPrePhysicsTick.RemovePrerequisite(Movement.Get(), Movement->PrimaryComponentTick);
		// Removing a coordinator must not terminate a session started by another caller.
		// Retain our cached target until the owned session has checked its safe exit.
		if (GetOwner() && GetOwner()->HasAuthority() && OwnedSession != 0)
		{
			Movement->StopRagdollMovement(EIMGRagdollEndReason::Unavailable, OwnedSession);
		}
		if (Movement.IsValid()) { Movement->ClearRagdollFollowTarget(this, InputSession); }
	}
	CapsuleCorrectionHandle.Reset();
	const bool bKeepBody = bKeepBodyWhenMovementDisabled && Movement.IsValid() && Movement->MovementMode == MOVE_None;
	ReleaseBody(bKeepBody, bKeepBody);
	ReleaseBodyResources();
	RestartBodyTransforms.Reset();
	RestartMesh.Reset();
	RestartPhysicsAsset.Reset();
	RestartMeshAsset.Reset();
	FailedBodySession = 0;
	FailedBodyMesh.Reset();
	FailedBodyMeshAsset.Reset();
	FailedBodyPhysicsAsset.Reset();
	if (LocalSession != 0 || bPendingTransition) { SetBodyIssue(TEXT("ExecutionUnavailable")); }
	OwnedSession = 0;
	if (SampledMesh.IsValid())
	{
		SampledMesh->UnregisterOnPhysicsCreatedDelegate(PhysicsCreatedHandle);
		RemoveTickPrerequisiteComponent(SampledMesh.Get());
		BodyPrePhysicsTick.RemovePrerequisite(SampledMesh.Get(), SampledMesh->PrimaryComponentTick);
	}
	PhysicsCreatedHandle.Reset();
	if (ObservedParts.IsValid()) { ObservedParts->OnCharacterPartsChanged.RemoveDynamic(this, &ThisClass::HandleCharacterPartsChanged); }
	ObservedParts.Reset();
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
	if (!IsRagdollReady()) { SetBodyIssue(TEXT("ExecutionNotReady")); return false; }
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

bool UIMGRagdollComponent::BeginTransitionAt(int32 ExpectedSession, const FTransform& Transform, EMovementMode TargetMode,
	uint8 TargetCustomMode, FVector CarryVelocity, int32& OutTransitionRevision, FVector BodyLocalOffset)
{
	OutTransitionRevision = 0;
	if (bTransitioning || !IsRagdollAvailable() || !GetOwner()->HasAuthority()) { return false; }
	TGuardValue<bool> Transition(bTransitioning, true);
	return Movement->TransitionRagdollMovementAt(ExpectedSession, Transform, TargetMode, TargetCustomMode,
		CarryVelocity, OutTransitionRevision, BodyLocalOffset);
}

bool UIMGRagdollComponent::QueryTransition(int32 ExpectedSession, const FTransform& Transform, EMovementMode TargetMode,
	uint8 TargetCustomMode, EIMGRagdollIssue& OutIssue, FVector BodyLocalOffset) const
{
	OutIssue = EIMGRagdollIssue::Unavailable;
	return IsRagdollAvailable() && Movement->QueryRagdollTransition(ExpectedSession, Transform, TargetMode, TargetCustomMode, OutIssue, BodyLocalOffset);
}

bool UIMGRagdollComponent::IsTransitionReady(int32 ExpectedSession, int32 ExpectedRevision) const
{
	return IsRagdollAvailable() && Movement->IsRagdollTransitionReady(ExpectedSession, ExpectedRevision);
}

bool UIMGRagdollComponent::GetTransitionMovement(int32 ExpectedSession, int32 ExpectedRevision, EMovementMode& OutMode,
	uint8& OutCustomMode, FVector& OutVelocity) const
{
	OutMode = MOVE_None;
	OutCustomMode = 0;
	OutVelocity = FVector::ZeroVector;
	if (!IsRagdollAvailable() || ExpectedSession == 0 || ExpectedSession != ActiveState.Session
		|| ExpectedRevision == 0 || ExpectedRevision != ActiveState.Revision || !ActiveState.bExternalTransition) { return false; }
	OutMode = ActiveState.TransitionMode;
	OutCustomMode = ActiveState.TransitionCustomMode;
	OutVelocity = ActiveState.TransitionVelocity;
	return true;
}

void UIMGRagdollComponent::ReconcileExternalTransition()
{
	const uint16 Session = ActiveState.Session;
	const uint16 Revision = ActiveState.Revision;
	const auto IsCurrentTransition = [this, Session, Revision]()
	{
		return !bTearingDown && IsRegistered() && IsActive() && !IsBeingDestroyed() && Character.IsValid() && Movement.IsValid()
			&& ActiveState.Session == Session && ActiveState.Revision == Revision && ActiveState.bExternalTransition;
	};
	if (BodySession != 0 && BodySession != Session) { EndLocalSession(EIMGRagdollEndReason::Interrupted); }
	if (!IsCurrentTransition()) { return; }
	if (!IsRagdollAvailable())
	{
		EndLocalSession(EIMGRagdollEndReason::Unavailable);
		if (!IsCurrentTransition()) { return; }
		ReleasedTransitionRevision = ReadyTransitionRevision = Revision;
		SetBodyIssue(TEXT("ExecutionUnavailable"));
		if (IsCurrentTransition()) { SetExecutionTickEnabled(false); }
		return;
	}
	if (BodySession != 0 && BodyMesh.IsValid() && BodyMesh->GetAttachParent())
	{
		HandedOffSession = BodySession;
		ReleaseBody(true);
		SetBodyIssue(TEXT("BodyAttachmentTakenOver"));
		if (!IsCurrentTransition()) { return; }
	}
	if (BodySession != 0 && (!HasCurrentBody() || (!bBodyRecovering && !IsBodySimulationValid())))
	{
		ReleaseBody();
		SetBodyIssue(TEXT("BodyResourcesChanged"));
		if (!IsCurrentTransition()) { return; }
	}
	if (!IsTransitionReady(Session, Revision))
	{
		const bool bTargetMode = Movement->MovementMode == ActiveState.TransitionMode
			&& (Movement->MovementMode != MOVE_Custom || Movement->CustomMovementMode == ActiveState.TransitionCustomMode);
		// A retained body does not prove native movement has received this transition yet.
		const bool bCanDetectSupersession = ReadyTransitionRevision == Revision || Character->HasAuthority();
		if (bCanDetectSupersession && !Movement->IsRagdollMovementMode() && !bTargetMode)
		{
			// Native movement has already handed control to a different action. Do not retain a frozen old body.
			EndLocalSession(EIMGRagdollEndReason::Transferred);
			if (!IsCurrentTransition()) { return; }
			ReleasedTransitionRevision = Revision;
			ReadyTransitionRevision = Revision;
			SetBodyIssue(TEXT("TransitionSuperseded"));
			if (!IsCurrentTransition()) { return; }
		}
		if (IsRegistered() && IsActive()) { SetExecutionTickEnabled(BodySession != 0); }
		return;
	}
	if (ReadyTransitionRevision != Revision)
	{
		if (ReleasedTransitionRevision != Revision) { PrepareBodyRecovery(); }
		if (!IsCurrentTransition()) { return; }
		ReadyTransitionRevision = Revision;
		const uint16 EndedSession = LocalSession;
		LocalSession = OwnedSession = 0;
		bStartedNotified = bRecoveryNotified = false;
		// Stop the old ability's work before the receiver starts animation. Neither callback owns capsule movement.
		if (EndedSession != 0) { OnRagdollEnded.Broadcast(EndedSession, EIMGRagdollEndReason::Transferred); }
		if (!IsTransitionReady(Session, Revision) || ReleasedTransitionRevision == Revision) { return; }
		OnTransitionReady.Broadcast(Session, Revision);
	}
	if (!IsRegistered() || !IsActive() || !Movement.IsValid() || ActiveState.Session != Session || ActiveState.Revision != Revision) { return; }
	SetExecutionTickEnabled(BodySession != 0);
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
	if (ObservedTransitionSession != ActiveState.Session)
	{
		ObservedTransitionSession = ActiveState.Session;
		ReadyTransitionRevision = ReleasedTransitionRevision = 0;
	}
	if (ActiveState.bExternalTransition) { OwnedSession = 0; }
	if (OwnedSession != ActiveState.Session) { OwnedSession = 0; }
	if (!Movement->IsActive())
	{
		EndLocalSession(EIMGRagdollEndReason::Unavailable);
		SetExecutionTickEnabled(false);
		return;
	}
	SetExecutionTickEnabled((ActiveState.Phase != EIMGRagdollPhase::Inactive || LocalSession != 0 || BodySession != 0
		|| (ActiveState.bExternalTransition && ReadyTransitionRevision != ActiveState.Revision
			&& Movement->IsRagdollTransitionReady(ActiveState.Session, ActiveState.Revision)))
		&& Movement.IsValid() && IsRegistered() && IsActive() && !bTearingDown);
}

void UIMGRagdollComponent::EndLocalSession(EIMGRagdollEndReason Reason)
{
	const bool bKeepBody = bKeepBodyWhenMovementDisabled && Movement.IsValid() && Movement->MovementMode == MOVE_None;
	ReleaseBody(bKeepBody, bKeepBody);
	RecoveryPose = FPoseSnapshot();
	const uint16 EndedSession = LocalSession;
	LocalSession = 0;
	bStartedNotified = false;
	bRecoveryNotified = false;
	if (EndedSession != 0) { OnRagdollEnded.Broadcast(EndedSession, Reason); }
}

void UIMGRagdollComponent::ReconcileState()
{
	if (bReconciling || bTearingDown || !Character.IsValid() || !Movement.IsValid() || !Movement->IsActive() || Character->bClientUpdating) { return; }
	TGuardValue<bool> ReconcileGuard(bReconciling, true);
	TGuardValue<bool> Transition(bTransitioning, true);
	if (ActiveState.bExternalTransition && ActiveState.Phase == EIMGRagdollPhase::Inactive)
	{
		ReconcileExternalTransition();
		return;
	}
	if (ActiveState.Phase == EIMGRagdollPhase::Inactive && Movement->IsRagdollMovementMode()) { return; }
	if (LocalSession != ActiveState.Session)
	{
		FailedBodySession = 0;
		if (HandedOffSession != ActiveState.Session) { HandedOffSession = 0; }
		RestartBodyTransforms.Reset();
		if (ActiveState.Phase == EIMGRagdollPhase::Simulating) { CaptureRestartPose(); }
		EndLocalSession(EIMGRagdollEndReason::Interrupted);
		if (bTearingDown || !Movement.IsValid() || !IsRegistered() || !IsActive()) { return; }
		if (ActiveState.Phase != EIMGRagdollPhase::Inactive) { LocalSession = ActiveState.Session; }
	}
	if (BodySession != 0 && BodyMesh.IsValid() && BodyMesh->GetAttachParent())
	{
		HandedOffSession = BodySession;
		ReleaseBody(true);
		SetBodyIssue(TEXT("BodyAttachmentTakenOver"));
	}
	if (BodySession != 0 && (!HasCurrentBody() || (!bBodyRecovering && !IsBodySimulationValid())))
	{
		ReleaseBody();
		SetBodyIssue(TEXT("BodyResourcesChanged"));
	}
	if (ActiveState.Phase == EIMGRagdollPhase::Simulating && IsRagdollAvailable() && Movement->IsRagdollMovementMode()
		&& (ShouldExecuteBody() ? EnsureBody() : true) && !bStartedNotified)
	{
		bStartedNotified = true;
		OnRagdollStarted.Broadcast(LocalSession);
	}
	if (bTearingDown || !Movement.IsValid() || !Movement->IsActive() || !IsRegistered() || !IsActive()) { return; }
	if (ActiveState.Phase == EIMGRagdollPhase::Recovering && IsRagdollAvailable() && Movement->IsRagdollRecoveryReady() && !bRecoveryNotified)
	{
		PrepareBodyRecovery();
		bRecoveryNotified = true;
		OnRecoveryStarted.Broadcast(LocalSession);
	}
	if (bTearingDown || !Movement.IsValid()) { return; }
	if (ActiveState.Phase == EIMGRagdollPhase::Inactive)
	{
		OwnedSession = 0;
		EndLocalSession(ActiveState.EndReason);
		SetExecutionTickEnabled(false);
	}
}

bool UIMGRagdollComponent::GetRagdollFollowTarget_Implementation(FTransform& OutTarget, bool& bFaceUp) const
{
	OutTarget = FTransform::Identity;
	bFaceUp = false;
	if (!IsRagdollBodyActive(ActiveState.Session)) { return false; }
	OutTarget = Character->GetActorTransform();
	bFaceUp = false;
	bool bHasPelvis = false;
	const USkeletalMeshComponent* Mesh = Character->GetMesh();
	if (IsValid(Mesh) && Mesh->GetNumComponentSpaceTransforms() > 0)
	{
		const int32 PelvisIndex = Mesh->GetBoneIndex(PelvisBone);
		if (PelvisIndex != INDEX_NONE && Mesh->IsSimulatingPhysics(PelvisBone))
		{
			const FVector Pelvis = Mesh->GetBoneTransform(PelvisIndex).GetLocation();
			if (Pelvis.ContainsNaN()) { return false; }
			bHasPelvis = true;
			// The movement frame is corrected immediately; the local physical body consumes this offset smoothly.
			OutTarget.SetLocation(Pelvis + OwnerCorrectionRemaining);
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
	return bHasPelvis && !OutTarget.ContainsNaN();
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
	if (!IsRagdollBodyActive(ActiveState.Session)) { return false; }
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
		FIMGRagdollBodyReference Reference;
		if (Movement->GetRagdollBodyReference(ActiveState.Session, Reference)) { return Reference.bFaceUp; }
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
	if (TickType != LEVELTICK_All) { return; }
	if (ThisTickFunction == &PrimaryComponentTick) { Super::TickComponent(DeltaTime, TickType, ThisTickFunction); }
	if (!Character.IsValid() || !Movement.IsValid() || !Movement->IsRegistered()
		|| Character->GetCharacterMovement() != Movement.Get()) { TearDown(); return; }
	HandleMovementUpdated();
	RefreshMeshObservation();
	if (bTearingDown || !Movement.IsValid() || !Movement->IsActive() || !IsRegistered() || !IsActive()) { return; }
	ReconcileState();
	if (bBodyRecovering && HasCurrentBody())
	{
		// Walking corrections may touch the mesh before animation consumes the held pose.
		BodyMesh->SetWorldTransform(RecoveryMeshTransform, false, nullptr, ETeleportType::TeleportPhysics);
	}
	if (ThisTickFunction == &BodyPrePhysicsTick)
	{
		if (IsRagdollBodyActive(ActiveState.Session))
		{
			UpdateBodyCorrection(DeltaTime);
			if (IsRagdollBodyActive(ActiveState.Session)) { UpdateBodySimulation(DeltaTime); }
		}
	}
	else if (IsRagdollAvailable()) { SampleTarget(); }
}
