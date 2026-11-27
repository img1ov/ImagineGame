#include "Character/IMGCharacterMovementComponent.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"

namespace IMGMovementSmoothing
{
	static TAutoConsoleVariable<bool> CVarAutonomousMeshSmoothing(
		TEXT("IMG.Movement.AutonomousMeshSmoothing"),
		true,
		TEXT("Smooth small post-replay corrections on the owning client's character mesh."),
		ECVF_Default);

	constexpr double AutonomousMeshSmoothingMaxDistance = 40.0;
	constexpr double AutonomousMeshSmoothingTimeConstant = 0.05;
	constexpr double AutonomousMeshSmoothingTolerance = 0.1;
}

void FIMGAutonomousMeshSmoothingState::ResetTracking()
{
	Mesh.Reset();
	PreCorrectionLocation.Reset();
	WorldOffset = FVector::ZeroVector;
}

void UIMGCharacterMovementComponent::SmoothClientPosition(float DeltaSeconds)
{
	if (!HasValidData() || NetworkSmoothingMode == ENetworkSmoothingMode::Disabled)
	{
		return;
	}

	const USkeletalMeshComponent* Mesh = CharacterOwner->GetMesh();
	if (Mesh && Mesh->GetAttachParent() != UpdatedComponent)
	{
		// Detached mesh (ragdoll or recovery pose hold) must never receive capsule-relative smoothing as world coords.
		SmoothClientPosition_Interpolate(DeltaSeconds);
		if (NetworkSmoothingMode == ENetworkSmoothingMode::Linear)
		{
			if (const FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character())
			{
				if (!UpdatedComponent->GetComponentQuat().Equals(ClientData->MeshRotationOffset, 1e-6f))
				{
					UpdatedComponent->SetWorldRotation(ClientData->MeshRotationOffset);
				}
			}
		}
		return;
	}

	Super::SmoothClientPosition(DeltaSeconds);
}

void UIMGCharacterMovementComponent::OnClientCorrectionReceived(
	FNetworkPredictionData_Client_Character& ClientData,
	float TimeStamp,
	FVector NewLocation,
	FVector NewVelocity,
	FMovementBaseInterfaceData* NewMovementBaseInterfaceData,
	FName NewBaseBoneName,
	bool bHasBase,
	bool bBaseRelativePosition,
	uint8 ServerMovementMode,
	FVector ServerGravityDirection)
{
	const FCharacterMoveResponseDataContainer& Response = GetMoveResponseDataContainer();
	USkeletalMeshComponent* Mesh = GetAutonomousSmoothingMesh();
	if (Mesh && !Response.bRootMotionMontageCorrection && !Response.bRootMotionSourceCorrection)
	{
		if (AutonomousMeshSmoothing.Mesh.Get() != Mesh)
		{
			ResetAutonomousMeshSmoothing();
			AutonomousMeshSmoothing.Mesh = Mesh;
		}
		// Several corrections can arrive before a single replay. Keep the original displayed position.
		if (!AutonomousMeshSmoothing.PreCorrectionLocation.IsSet())
		{
			AutonomousMeshSmoothing.PreCorrectionLocation = UpdatedComponent->GetComponentLocation();
		}
	}
	else
	{
		ResetAutonomousMeshSmoothing();
	}

	Super::OnClientCorrectionReceived(
		ClientData, TimeStamp, NewLocation, NewVelocity,
		NewMovementBaseInterfaceData, NewBaseBoneName, bHasBase,
		bBaseRelativePosition, ServerMovementMode, ServerGravityDirection);
}

USkeletalMeshComponent* UIMGCharacterMovementComponent::GetAutonomousSmoothingMesh() const
{
	if (!IMGMovementSmoothing::CVarAutonomousMeshSmoothing.GetValueOnGameThread()
		|| !HasValidData()
		|| !IsRegistered()
		|| !IsActive()
		|| IsBeingDestroyed()
		|| CharacterOwner->IsActorBeingDestroyed()
		|| CharacterOwner->GetLocalRole() != ROLE_AutonomousProxy
		|| !CharacterOwner->IsLocallyControlled()
		|| (!IsMovingOnGround() && !IsFalling())
		|| ActiveRagdollState.Phase != EIMGRagdollPhase::Inactive
		|| (ActiveRagdollState.bHasRecovery && RagdollRuntime.RecoveryAppliedSession != ActiveRagdollState.Session)
		|| UpdatedComponent != CharacterOwner->GetCapsuleComponent()
		|| UpdatedComponent->IsSimulatingPhysics()
		|| HasRootMotionSources())
	{
		return nullptr;
	}

	USkeletalMeshComponent* Mesh = CharacterOwner->GetMesh();
	if (!IsValid(Mesh)
		|| !Mesh->IsRegistered()
		|| Mesh->IsBeingDestroyed()
		|| Mesh->GetAttachParent() != UpdatedComponent
		|| Mesh->IsUsingAbsoluteLocation()
		|| Mesh->IsAnySimulatingPhysics())
	{
		return nullptr;
	}
	return Mesh;
}

void UIMGCharacterMovementComponent::FinishAutonomousMeshCorrection()
{
	if (!AutonomousMeshSmoothing.PreCorrectionLocation.IsSet())
	{
		return;
	}

	const USkeletalMeshComponent* Mesh = GetAutonomousSmoothingMesh();
	if (!Mesh || Mesh != AutonomousMeshSmoothing.Mesh.Get())
	{
		ResetAutonomousMeshSmoothing();
		return;
	}

	const FVector PostReplayDelta = UpdatedComponent->GetComponentLocation() - AutonomousMeshSmoothing.PreCorrectionLocation.GetValue();
	AutonomousMeshSmoothing.PreCorrectionLocation.Reset();

	const bool bSmallCorrection = !PostReplayDelta.ContainsNaN()
		&& PostReplayDelta.SizeSquared() <= FMath::Square(IMGMovementSmoothing::AutonomousMeshSmoothingMaxDistance);
	if (bSmallCorrection)
	{
		const FVector PlanarCorrection = FVector::VectorPlaneProject(PostReplayDelta, GetGravityDirection());
		AutonomousMeshSmoothing.WorldOffset = (AutonomousMeshSmoothing.WorldOffset - PlanarCorrection)
			.GetClampedToMaxSize(IMGMovementSmoothing::AutonomousMeshSmoothingMaxDistance);
	}
	else
	{
		AutonomousMeshSmoothing.WorldOffset = FVector::ZeroVector;
	}

	if (GetMovementNetDiagnosticsLevel() >= 1)
	{
		UE_LOG(LogIMGMovementNet, Log,
			TEXT("[AutonomousMeshCorrection] Actor=%s PostReplayDelta=%s MeshOffset=%s WithinLimit=%d"),
			*GetPathNameSafe(CharacterOwner.Get()),
			*PostReplayDelta.ToCompactString(),
			*AutonomousMeshSmoothing.WorldOffset.ToCompactString(),
			bSmallCorrection);
	}
}

void UIMGCharacterMovementComponent::DecayAutonomousMeshOffset(float DeltaTime)
{
	if (!AutonomousMeshSmoothing.PreCorrectionLocation.IsSet()
		&& !AutonomousMeshSmoothing.WorldOffset.IsZero()
		&& DeltaTime > 0.f)
	{
		AutonomousMeshSmoothing.WorldOffset *= FMath::Exp(-DeltaTime / IMGMovementSmoothing::AutonomousMeshSmoothingTimeConstant);
	}
}

void UIMGCharacterMovementComponent::ApplyAutonomousMeshOffset()
{
	if (!AutonomousMeshSmoothing.PreCorrectionLocation.IsSet()
		&& AutonomousMeshSmoothing.WorldOffset.IsNearlyZero(IMGMovementSmoothing::AutonomousMeshSmoothingTolerance))
	{
		AutonomousMeshSmoothing.Mesh.Reset();
		AutonomousMeshSmoothing.WorldOffset = FVector::ZeroVector;
		return;
	}

	USkeletalMeshComponent* Mesh = GetAutonomousSmoothingMesh();
	if (!Mesh || Mesh != AutonomousMeshSmoothing.Mesh.Get())
	{
		ResetAutonomousMeshSmoothing();
		return;
	}

	const FTransform ParentTransform = UpdatedComponent->GetSocketTransform(Mesh->GetAttachSocketName());
	AutonomousMeshSmoothing.AppliedRelativeOffset = ParentTransform.InverseTransformVector(AutonomousMeshSmoothing.WorldOffset);
	if (!AutonomousMeshSmoothing.AppliedRelativeOffset.IsZero())
	{
		AutonomousMeshSmoothing.AppliedRelativeLocation = Mesh->GetRelativeLocation() + AutonomousMeshSmoothing.AppliedRelativeOffset;
		// Update the mesh presentation without a movement sweep or a physics transform update.
		Mesh->SetRelativeLocation_Direct(AutonomousMeshSmoothing.AppliedRelativeLocation);
		Mesh->UpdateComponentToWorld(EUpdateTransformFlags::SkipPhysicsUpdate);
	}
}

void UIMGCharacterMovementComponent::RemoveAutonomousMeshOffset()
{
	const FVector RelativeOffset = AutonomousMeshSmoothing.AppliedRelativeOffset;
	if (RelativeOffset.IsZero())
	{
		return;
	}

	AutonomousMeshSmoothing.AppliedRelativeOffset = FVector::ZeroVector;
	if (USkeletalMeshComponent* Mesh = AutonomousMeshSmoothing.Mesh.Get(); Mesh && !Mesh->IsBeingDestroyed())
	{
		const FVector CurrentLocation = Mesh->GetRelativeLocation();
		FVector RestoredLocation = CurrentLocation;
		// A stance or another owner may have replaced a relative axis since we applied the offset.
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (CurrentLocation[Axis] == AutonomousMeshSmoothing.AppliedRelativeLocation[Axis])
			{
				RestoredLocation[Axis] -= RelativeOffset[Axis];
			}
		}
		if (RestoredLocation != CurrentLocation)
		{
			Mesh->SetRelativeLocation_Direct(RestoredLocation);
			Mesh->UpdateComponentToWorld(EUpdateTransformFlags::SkipPhysicsUpdate);
		}
	}
}

void UIMGCharacterMovementComponent::ResetAutonomousMeshSmoothing()
{
	RemoveAutonomousMeshOffset();
	AutonomousMeshSmoothing.ResetTracking();
}
