#pragma once

#include "CoreMinimal.h"
#include "Animation/PoseSnapshot.h"
#include "Engine/EngineTypes.h"
#include "Engine/NetSerialization.h"
#include "IMGRagdollTypes.generated.h"

class UAnimMontage;

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
	Unavailable,
	Transferred
};

UENUM(BlueprintType)
enum class EIMGRagdollIssue : uint8
{
	None,
	Unavailable,
	SessionMismatch,
	InvalidTarget,
	MissingTarget,
	ExpiredTarget,
	TargetTooFar,
	TargetPathBlocked,
	InputSourceBusy,
	RecoveryTooFast,
	NoFloor,
	UnwalkableFloor,
	PlacementBlocked,
	PlacementPathBlocked,
	ExitBlocked
};

UENUM(BlueprintType)
enum class EIMGRagdollBodyIssue : uint8
{
	None,
	WaitingForBodyReference,
	BodyNotReady,
	ExecutionUnavailable,
	ExecutionNotReady,
	BodyAttachmentTakenOver,
	BodyResourcesChanged,
	TransitionSuperseded,
	MissingPelvisBody,
	BodyStartFailed,
	BodyHandedOff,
	InvalidPhysicsControlComponent,
	MissingPhysicsControlComponent,
	PhysicsControlComponentBusy,
	PhysicsControlSetupFailed,
	PhysicsControlResourceLost
};

inline bool IsWaitingRagdollBodyIssue(EIMGRagdollBodyIssue Issue)
{
	return Issue == EIMGRagdollBodyIssue::WaitingForBodyReference
		|| Issue == EIMGRagdollBodyIssue::BodyNotReady;
}

UENUM()
enum class EIMGRagdollTargetStatus : uint8
{
	Missing,
	Ready,
	Expired
};

USTRUCT(BlueprintType)
struct IMAGINEGAME_API FIMGRagdollSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0", ForceUnits = "cm"))
	float FloorClearance = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "1", ForceUnits = "cm/s"))
	float MaxFollowSpeed = 4000.0f;

	/** Reject a remote body's target outside this distance from the authoritative capsule. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Network", meta = (ClampMin = "1", ForceUnits = "cm"))
	float MaxTargetDistance = 500.0f;

	/** Live capture/query age limit; saved input validity is preserved during replay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Network", meta = (ClampMin = "0.01", ForceUnits = "s"))
	float MaxTargetAge = 0.25f;

	/** Admission-to-first-owner-move deadline; established input uses MaxTargetAge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Network", meta = (ClampMin = "0.01", ForceUnits = "s"))
	float EntryInputTimeout = 2.0f;

	/** Validate the body-anchor path; the dormant capsule may extend through the floor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Network")
	bool bValidateTargetPath = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recovery", meta = (ClampMin = "1", ForceUnits = "cm"))
	float RecoveryTraceDistance = 200.0f;

	/** Zero permits recovery at any capsule speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recovery", meta = (ClampMin = "0", ForceUnits = "cm/s"))
	float MaxRecoverySpeed = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recovery", meta = (ClampMin = "0", ForceUnits = "cm"))
	float RecoverySearchRadius = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recovery", meta = (ClampMin = "1", ClampMax = "4"))
	int32 RecoverySearchRings = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recovery", meta = (ClampMin = "4", ClampMax = "16"))
	int32 RecoverySearchSamples = 8;

	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
	bool IsValid() const;
	float GetEffectiveFloorClearance(float MaxFloorDist) const;
};

/** Input parameters for transitioning out of free ragdoll into an approved external movement mode. */
USTRUCT(BlueprintType)
struct IMAGINEGAME_API FIMGRagdollTransitionRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	FTransform Transform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	TEnumAsByte<EMovementMode> TargetMode = MOVE_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	uint8 TargetCustomMode = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	FVector CarryVelocity = FVector::ZeroVector;

	/** Matched follow point in the proposed capsule's unit-scale space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	FVector BodyLocalOffset = FVector::ZeroVector;

	bool IsValid() const;
};

/** Frozen local pose and spatial facts handed off to recovery or external-transition animation. */
USTRUCT(BlueprintType)
struct IMAGINEGAME_API FIMGRagdollPoseSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Pose")
	FPoseSnapshot Pose;

	UPROPERTY(BlueprintReadOnly, Category = "Pose")
	FTransform MeshWorldTransform = FTransform::Identity;

	UPROPERTY(BlueprintReadOnly, Category = "Pose")
	FVector PelvisWorldLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Pose")
	float FacingYaw = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Pose")
	bool bFaceUp = false;

	bool IsValid() const
	{
		return Pose.bIsValid
			&& !MeshWorldTransform.ContainsNaN()
			&& !PelvisWorldLocation.ContainsNaN()
			&& FMath::IsFinite(FacingYaw);
	}
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
	FVector_NetQuantize10 PelvisVelocity = FVector::ZeroVector;

	/** Signed capsule-center height above the pelvis, captured once at body entry. */
	UPROPERTY()
	float CapsuleHeightOffset = 0.f;

	UPROPERTY()
	uint16 FacingYaw = 0;

	UPROPERTY()
	bool bFaceUp = false;

	UPROPERTY()
	EIMGRagdollTargetStatus Status = EIMGRagdollTargetStatus::Missing;

	bool IsValid() const
	{
		return Session != 0
			&& Status == EIMGRagdollTargetStatus::Ready
			&& !FVector(PelvisLocation).ContainsNaN()
			&& !FVector(PelvisVelocity).ContainsNaN()
			&& FMath::IsFinite(CapsuleHeightOffset);
	}

	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template <>
struct TStructOpsTypeTraits<FIMGRagdollMovementInput> : TStructOpsTypeTraitsBase2<FIMGRagdollMovementInput>
{
	enum { WithNetSerializer = true };
};

/** Accepted movement anchor for body presentation. Wire positions use zero origin; the CMC getter returns local-world positions. */
USTRUCT(BlueprintType)
struct IMAGINEGAME_API FIMGRagdollBodyReference
{
	GENERATED_BODY()

	UPROPERTY()
	uint16 Session = 0;

	UPROPERTY()
	uint16 MovementRevision = 0;

	UPROPERTY()
	uint32 Sequence = 0;

	UPROPERTY(BlueprintReadOnly)
	double ServerTime = 0.0;

	/** Native move correlation only; negative for server-controlled movement. */
	UPROPERTY(BlueprintReadOnly)
	float ClientTimeStamp = -1.f;

	UPROPERTY(BlueprintReadOnly)
	FVector_NetQuantize10 PelvisLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	FVector_NetQuantize10 CapsuleLocation = FVector::ZeroVector;

	/** Accepted pelvis velocity; capsule placement/lift is never a physical body velocity. */
	UPROPERTY(BlueprintReadOnly)
	FVector_NetQuantize10 Velocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	float FacingYaw = 0.f;

	UPROPERTY(BlueprintReadOnly)
	bool bFaceUp = false;

	UPROPERTY(BlueprintReadOnly)
	EIMGRagdollIssue Issue = EIMGRagdollIssue::MissingTarget;

	bool IsValid() const;
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template <>
struct TStructOpsTypeTraits<FIMGRagdollBodyReference> : TStructOpsTypeTraitsBase2<FIMGRagdollBodyReference>
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

	/** Autonomous entry adds the approved impulse to its current predicted movement velocity. */
	UPROPERTY()
	FVector_NetQuantize10 LaunchVelocityDelta = FVector::ZeroVector;

	/** Server clock of this session's launch; late resource binding must not replay an old launch velocity. */
	UPROPERTY()
	double StartServerTime = 0.0;

	UPROPERTY()
	uint16 FacingYaw = 0;

	UPROPERTY()
	bool bFaceUp = false;

	/** Retain approved recovery/exit placement when lifecycle updates coalesce in replication. */
	UPROPERTY()
	bool bHasRecovery = false;

	/** Retained completed transfer; late cleanup of the ragdoll session cannot own the receiving action. */
	UPROPERTY()
	bool bExternalTransition = false;

	UPROPERTY()
	TEnumAsByte<EMovementMode> TransitionMode = MOVE_None;

	UPROPERTY()
	uint8 TransitionCustomMode = 0;

	UPROPERTY()
	FVector_NetQuantize10 TransitionVelocity = FVector::ZeroVector;

	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template <>
struct TStructOpsTypeTraits<FIMGRagdollRepState> : TStructOpsTypeTraitsBase2<FIMGRagdollRepState>
{
	enum { WithNetSerializer = true };
};

/** Asset choice only; placement is rebuilt from accepted body facts by the authority. */
USTRUCT(BlueprintType)
struct IMAGINEGAME_API FIMGRagdollRecoverySelection
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recovery")
	TObjectPtr<UAnimMontage> Montage = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recovery")
	float StartTime = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recovery")
	float PlayRate = 1.f;

	bool GetPlaybackPosition(double ElapsedSeconds, float& OutPosition, float& OutRemainingSeconds) const;
};

/** Shared asset/matching contract used for local choice, server admission and simulated montage classification. */
USTRUCT(BlueprintType)
struct IMAGINEGAME_API FIMGRagdollRecoveryAnimation
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recovery")
	TObjectPtr<UAnimMontage> Montage = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recovery")
	bool bFaceUp = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recovery", meta = (ClampMin = "0"))
	float MinStartTime = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recovery", meta = (ClampMin = "0"))
	float MaxStartTime = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recovery", meta = (ClampMin = "0.01"))
	float MinPlayRate = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recovery", meta = (ClampMin = "0.01"))
	float MaxPlayRate = 2.f;
	/** Matched follow point relative to the entry capsule. Clients cannot override this offset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recovery")
	FVector BodyLocalOffset = FVector::ZeroVector;

	bool Allows(const FIMGRagdollRecoverySelection& Selection, bool bInFaceUp) const;
};

USTRUCT(BlueprintType)
struct IMAGINEGAME_API FIMGRagdollRecoveryContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Recovery")
	int32 Session = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Recovery")
	bool bFaceUp = false;
	UPROPERTY(BlueprintReadOnly, Category = "Recovery")
	FTransform Placement = FTransform::Identity;
	UPROPERTY(BlueprintReadOnly, Category = "Recovery")
	FVector LinearVelocity = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Recovery")
	FVector AngularVelocityRadians = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Recovery")
	float StableSeconds = 0.f;
};

/** Presentation approval only. CMC remains the sole replicated movement lifecycle. Keep native FRepLayout. */
USTRUCT()
struct IMAGINEGAME_API FIMGRagdollRecoveryRepState
{
	GENERATED_BODY()

	UPROPERTY()
	uint16 Session = 0;
	UPROPERTY()
	int16 ActivationKey = 0;
	UPROPERTY()
	uint32 ImpactRevision = 0;
	UPROPERTY()
	uint32 Attempt = 0;
	UPROPERTY()
	FIMGRagdollRecoverySelection Selection;
	UPROPERTY()
	double EarliestRecoveryServerTime = 0.0;
	UPROPERTY()
	double PlaybackServerTime = 0.0;
	UPROPERTY()
	uint8 MontagePlayInstanceId = 0;
	UPROPERTY()
	bool bApproved = false;
	UPROPERTY()
	bool bSkipMontage = false;
	UPROPERTY()
	bool bHasMontageInstance = false;
	UPROPERTY()
	bool bEnded = false;
};
