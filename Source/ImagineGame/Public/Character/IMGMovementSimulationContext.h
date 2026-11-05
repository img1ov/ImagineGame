#pragma once

#include "CoreMinimal.h"

class UCharacterMovementComponent;

/** Provider-owned nonnegative scalar inputs. CMC stores them without interpreting gameplay rules. */
struct IMAGINEGAME_API FMovementSimulationContext
{
	static constexpr int32 MaxParameters = 16;
	uint32 Revision = 0;
	TArray<float, TInlineAllocator<8>> Parameters;
	/** Local change identities; authority responses recover these from the acknowledged SavedMove. */
	TArray<uint32, TInlineAllocator<8>> ParameterRevisions;

	bool IsValid() const;
	bool HasSameParameters(const FMovementSimulationContext& Other) const;
	uint32 GetParameterHash() const;
	bool Serialize(FArchive& Ar);
};

/** The CMC configuration owned by the movement rule provider, not its physical velocity/input. */
struct IMAGINEGAME_API FCharacterMovementParameters
{
	float MaxWalkSpeed = 0.f;
	float MaxWalkSpeedCrouched = 0.f;
	float MaxAcceleration = 0.f;
	float GroundFriction = 0.f;
	float BrakingDecelerationWalking = 0.f;
	FRotator RotationRate = FRotator::ZeroRotator;
	bool bOrientRotationToMovement = false;
	bool bUseControllerDesiredRotation = false;

	static FCharacterMovementParameters Capture(const UCharacterMovementComponent& Movement);
	bool Equals(const FCharacterMovementParameters& Other, bool bCompareRotation = true) const;
	uint32 GetParameterHash() const;
	bool IsValid() const;
	void Apply(UCharacterMovementComponent& Movement, bool bApplyRotation = true) const;
};
