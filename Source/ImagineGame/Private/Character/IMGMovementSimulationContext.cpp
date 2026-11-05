#include "Character/IMGMovementSimulationContext.h"

#include "GameFramework/CharacterMovementComponent.h"

namespace
{
template <typename T>
uint32 HashMovementScalar(uint32 Hash, T Value)
{
	static_assert(sizeof(T) == 4 || sizeof(T) == 8);
	const T CanonicalValue = Value == T(0) ? T(0) : Value;
	uint64 Bits = 0;
	FMemory::Memcpy(&Bits, &CanonicalValue, sizeof(T));
	Hash = (Hash ^ static_cast<uint32>(Bits)) * 16777619u;
	if constexpr (sizeof(T) == 8) { Hash = (Hash ^ static_cast<uint32>(Bits >> 32)) * 16777619u; }
	return Hash;
}
}

bool FMovementSimulationContext::IsValid() const
{
	if (Revision == 0 || Parameters.IsEmpty() || Parameters.Num() > MaxParameters) { return false; }
	if (!ParameterRevisions.IsEmpty())
	{
		if (ParameterRevisions.Num() != Parameters.Num()) { return false; }
		for (const uint32 ParameterRevision : ParameterRevisions) { if (ParameterRevision == 0) { return false; } }
	}
	for (const float Value : Parameters)
	{
		if (!FMath::IsFinite(Value) || Value < 0.f) { return false; }
	}
	return true;
}

bool FMovementSimulationContext::HasSameParameters(const FMovementSimulationContext& Other) const
{
	return Parameters == Other.Parameters;
}

uint32 FMovementSimulationContext::GetParameterHash() const
{
	uint32 Hash = 2166136261u;
	for (const float Value : Parameters)
	{
		Hash = HashMovementScalar(Hash, Value);
	}
	return (Hash ^ static_cast<uint32>(Parameters.Num())) * 16777619u;
}

bool FMovementSimulationContext::Serialize(FArchive& Ar)
{
	Ar.SerializeIntPacked(Revision);
	uint8 Count = static_cast<uint8>(Parameters.Num());
	Ar.SerializeBits(&Count, 5);
	if (Count == 0 || Count > MaxParameters) { Ar.SetError(); return false; }
	if (Ar.IsLoading()) { Parameters.SetNum(Count); ParameterRevisions.Reset(); }
	for (float& Value : Parameters) { Ar << Value; }
	if (!IsValid()) { Ar.SetError(); }
	return !Ar.IsError();
}

FCharacterMovementParameters FCharacterMovementParameters::Capture(const UCharacterMovementComponent& Movement)
{
	FCharacterMovementParameters Result;
	Result.MaxWalkSpeed = Movement.MaxWalkSpeed;
	Result.MaxWalkSpeedCrouched = Movement.MaxWalkSpeedCrouched;
	Result.MaxAcceleration = Movement.MaxAcceleration;
	Result.GroundFriction = Movement.GroundFriction;
	Result.BrakingDecelerationWalking = Movement.BrakingDecelerationWalking;
	Result.RotationRate = Movement.RotationRate;
	Result.bOrientRotationToMovement = Movement.bOrientRotationToMovement;
	Result.bUseControllerDesiredRotation = Movement.bUseControllerDesiredRotation;
	return Result;
}

bool FCharacterMovementParameters::IsValid() const
{
	return FMath::IsFinite(MaxWalkSpeed) && MaxWalkSpeed >= 0.f
		&& FMath::IsFinite(MaxWalkSpeedCrouched) && MaxWalkSpeedCrouched >= 0.f
		&& FMath::IsFinite(MaxAcceleration) && MaxAcceleration >= 0.f
		&& FMath::IsFinite(GroundFriction) && GroundFriction >= 0.f
		&& FMath::IsFinite(BrakingDecelerationWalking) && BrakingDecelerationWalking >= 0.f
		&& !RotationRate.ContainsNaN();
}

bool FCharacterMovementParameters::Equals(const FCharacterMovementParameters& Other, bool bCompareRotation) const
{
	return MaxWalkSpeed == Other.MaxWalkSpeed && MaxWalkSpeedCrouched == Other.MaxWalkSpeedCrouched
		&& MaxAcceleration == Other.MaxAcceleration && GroundFriction == Other.GroundFriction
		&& BrakingDecelerationWalking == Other.BrakingDecelerationWalking
		&& (!bCompareRotation || (RotationRate == Other.RotationRate
			&& bOrientRotationToMovement == Other.bOrientRotationToMovement
			&& bUseControllerDesiredRotation == Other.bUseControllerDesiredRotation));
}

uint32 FCharacterMovementParameters::GetParameterHash() const
{
	uint32 Hash = 2166136261u;
	for (const float Value : { MaxWalkSpeed, MaxWalkSpeedCrouched, MaxAcceleration, GroundFriction, BrakingDecelerationWalking })
	{
		Hash = HashMovementScalar(Hash, Value);
	}
	Hash = HashMovementScalar(Hash, RotationRate.Pitch);
	Hash = HashMovementScalar(Hash, RotationRate.Yaw);
	Hash = HashMovementScalar(Hash, RotationRate.Roll);
	const uint32 RotationFlags = static_cast<uint32>(bOrientRotationToMovement)
		| (static_cast<uint32>(bUseControllerDesiredRotation) << 1);
	return (Hash ^ RotationFlags) * 16777619u;
}

void FCharacterMovementParameters::Apply(UCharacterMovementComponent& Movement, bool bApplyRotation) const
{
	Movement.MaxWalkSpeed = MaxWalkSpeed;
	Movement.MaxWalkSpeedCrouched = MaxWalkSpeedCrouched;
	Movement.MaxAcceleration = MaxAcceleration;
	Movement.GroundFriction = GroundFriction;
	Movement.BrakingDecelerationWalking = BrakingDecelerationWalking;
	if (bApplyRotation)
	{
		Movement.RotationRate = RotationRate;
		Movement.bOrientRotationToMovement = bOrientRotationToMovement;
		Movement.bUseControllerDesiredRotation = bUseControllerDesiredRotation;
	}
}
