#pragma once

#include "CoreMinimal.h"

class UCharacterMovementComponent;
struct FHitResult;

/** Capsule geometry shared by prepared placement and the actual movement handoff. No action or animation policy. */
namespace IMGGroundPlacement
{
double FloorGap();
bool PlaneAdjustment(const FVector& Center, double HalfHeight, double Radius, const FHitResult& Plane,
    double& OutAdjustment);
bool IsUsableSupport(const UCharacterMovementComponent& Movement, const FHitResult& Hit, double Radius);
/** Uses the current capsule and collision filters. Resolves only bounded local penetration; never teleports to a predicted endpoint. */
bool FindGround(UCharacterMovementComponent& Movement, double MaxAdjustment,
    FHitResult* OutSupport = nullptr, FString* OutFailure = nullptr);
}
