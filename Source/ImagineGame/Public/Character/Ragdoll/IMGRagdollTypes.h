#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Engine/NetSerialization.h"
#include "IMGRagdollTypes.generated.h"

UENUM(BlueprintType)
enum class EIMGRagdollPhase : uint8
{
	Inactive,
	Simulating,
	/** Movement input lock after safe placement; the caller explicitly completes recovery. */
	Recovering
};

UENUM(BlueprintType)
enum class EIMGRagdollEndReason : uint8
{
	Recovered,
	Interrupted,
	MovementChanged,
	Unavailable
};

USTRUCT(BlueprintType)
struct IMAGINEGAME_API FIMGRagdollSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	TEnumAsByte<ECollisionChannel> FloorTraceChannel = ECC_Visibility;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "1", ForceUnits = "cm"))
	float FollowTraceRadius = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "1", ForceUnits = "cm"))
	float FollowTraceDistance = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0", ForceUnits = "cm"))
	float FloorClearance = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "1", ForceUnits = "cm/s"))
	float MaxFollowSpeed = 4000.0f;

	/** Reject a remote body's target outside this distance from the authoritative capsule. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Network", meta = (ClampMin = "1", ForceUnits = "cm"))
	float MaxTargetDistance = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recovery", meta = (ClampMin = "1", ForceUnits = "cm"))
	float RecoveryTraceDistance = 200.0f;

	/** Zero permits recovery at any capsule speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recovery", meta = (ClampMin = "0", ForceUnits = "cm/s"))
	float MaxRecoverySpeed = 200.0f;

	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
	bool IsValid() const;
};

/** A zero-origin body target, saved and replayed with the move that consumes it. */
USTRUCT()
struct IMAGINEGAME_API FIMGRagdollMovementInput
{
	GENERATED_BODY()

	UPROPERTY()
	uint16 Session = 0;

	UPROPERTY()
	FVector_NetQuantize10 PelvisLocation = FVector::ZeroVector;

	UPROPERTY()
	uint16 FacingYaw = 0;

	UPROPERTY()
	bool bFaceUp = false;

	bool IsValid() const { return Session != 0 && !FVector(PelvisLocation).ContainsNaN(); }
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template <>
struct TStructOpsTypeTraits<FIMGRagdollMovementInput> : TStructOpsTypeTraitsBase2<FIMGRagdollMovementInput>
{
	enum { WithNetSerializer = true };
};

/** Ordered movement contract; capsule transforms continue to use CMC replication. */
USTRUCT()
struct IMAGINEGAME_API FIMGRagdollRepState
{
	GENERATED_BODY()

	UPROPERTY()
	FIMGRagdollSettings Settings;

	UPROPERTY()
	uint16 Revision = 0;

	UPROPERTY()
	uint16 Session = 0;

	UPROPERTY()
	EIMGRagdollPhase Phase = EIMGRagdollPhase::Inactive;

	UPROPERTY()
	EIMGRagdollEndReason EndReason = EIMGRagdollEndReason::Recovered;

	UPROPERTY()
	FVector_NetQuantize100 AnchorLocation = FVector::ZeroVector;

	UPROPERTY()
	FVector_NetQuantize10 InitialVelocity = FVector::ZeroVector;

	UPROPERTY()
	uint16 FacingYaw = 0;

	UPROPERTY()
	bool bFaceUp = false;

	/** Retain the approved placement when recovery and completion coalesce in replication. */
	UPROPERTY()
	bool bHasRecovery = false;

	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template <>
struct TStructOpsTypeTraits<FIMGRagdollRepState> : TStructOpsTypeTraitsBase2<FIMGRagdollRepState>
{
	enum { WithNetSerializer = true };
};
