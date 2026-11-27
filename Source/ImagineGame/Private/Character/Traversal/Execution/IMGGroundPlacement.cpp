#include "Character/Traversal/Execution/IMGGroundPlacement.h"

#include "Components/CapsuleComponent.h"
#include "Engine/ScopedMovementUpdate.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

double IMGGroundPlacement::FloorGap()
{
	return (UCharacterMovementComponent::MIN_FLOOR_DIST + UCharacterMovementComponent::MAX_FLOOR_DIST) * 0.5;
}

bool IMGGroundPlacement::PlaneAdjustment(const FVector& Center, double HalfHeight, double Radius,
    const FHitResult& Plane, double& OutAdjustment)
{
	const FVector Normal = Plane.ImpactNormal.GetSafeNormal();
	if (Normal.Z <= UE_KINDA_SMALL_NUMBER) { return false; }
	const FVector BottomSphere = Center - FVector::UpVector * (HalfHeight - Radius);
	OutAdjustment = (Radius - FVector::DotProduct(BottomSphere - Plane.ImpactPoint, Normal)) / Normal.Z + FloorGap();
	return FMath::IsFinite(OutAdjustment);
}

bool IMGGroundPlacement::IsUsableSupport(const UCharacterMovementComponent& Movement, const FHitResult& Hit, double Radius)
{
	return Hit.IsValidBlockingHit() && Movement.IsWalkable(Hit) &&
	    Movement.IsWithinEdgeTolerance(Hit.Location, Hit.ImpactPoint, Radius);
}

bool IMGGroundPlacement::FindGround(UCharacterMovementComponent& Movement, double MaxAdjustment,
    FHitResult* OutSupport, FString* OutFailure)
{
	if (OutSupport) { *OutSupport = FHitResult(); }
	if (OutFailure) { OutFailure->Reset(); }
	auto Fail = [OutFailure](const TCHAR* Reason)
	{
		if (OutFailure) { *OutFailure = Reason; }
		return false;
	};
	const ACharacter* Character = Movement.GetCharacterOwner();
	const UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	UWorld* World = Movement.GetWorld();
	if (!World || !Capsule || Movement.UpdatedComponent != Capsule || !FMath::IsFinite(MaxAdjustment) || MaxAdjustment < 0.0 ||
	    !Capsule->GetUpVector().Equals(FVector::UpVector, 1.e-3))
	{
		return Fail(TEXT("Invalid world, capsule, gravity orientation or recovery limit"));
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IMGGroundPlacement), false, Character);
	FCollisionResponseParams Responses;
	Movement.InitCollisionParams(Params, Responses);
	const FVector Initial = Capsule->GetComponentLocation();
	const FQuat Rotation = Capsule->GetComponentQuat();
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(
	    Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight());
	const bool bWasJustTeleported = Movement.bJustTeleported;
	FScopedMovementUpdate Scope(Movement.UpdatedComponent, EScopedUpdate::DeferredUpdates);
	FHitResult Hit;
	Movement.GetWorld()->SweepSingleByChannel(Hit, Initial, Initial, Rotation,
	    Capsule->GetCollisionObjectType(), Shape, Params, Responses);
	if (Hit.bStartPenetrating)
	{
		// The public base interface still dispatches to the character's protected native override.
		const FVector Adjustment = static_cast<UMovementComponent&>(Movement).GetPenetrationAdjustment(Hit);
		if (Adjustment.ContainsNaN() || Adjustment.Size() > MaxAdjustment ||
		    !Movement.ResolvePenetration(Adjustment, Hit, Rotation) ||
		    FVector::Dist(Initial, Capsule->GetComponentLocation()) > MaxAdjustment)
		{
			Scope.RevertMove();
			Movement.bJustTeleported = bWasJustTeleported;
			return Fail(TEXT("Initial penetration cannot be resolved within the recovery limit"));
		}
	}
	if (Movement.GetWorld()->OverlapBlockingTestByChannel(Capsule->GetComponentLocation(), Rotation,
	        Capsule->GetCollisionObjectType(), Shape, Params, Responses))
	{
		Scope.RevertMove();
		Movement.bJustTeleported = bWasJustTeleported;
		return Fail(TEXT("Capsule remains blocked after local recovery"));
	}
	FFindFloorResult Floor;
	Movement.FindFloor(Capsule->GetComponentLocation(), Floor, false);
	const float FloorDistance = Floor.GetDistanceToFloor();
	if (!Floor.IsWalkableFloor() || !FMath::IsFinite(FloorDistance) ||
	    FloorDistance > MaxAdjustment + FloorGap() || FloorDistance < -MaxAdjustment)
	{
		Scope.RevertMove();
		Movement.bJustTeleported = bWasJustTeleported;
		return Fail(TEXT("No walkable support within the recovery limit"));
	}
	if (OutSupport) { *OutSupport = Floor.HitResult; }
	return true;
}
