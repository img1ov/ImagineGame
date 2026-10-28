#pragma once

#include "Character/Ragdoll/IMGRagdollTypes.h"

class UIMGCharacterMovementComponent;

/** Capsule movement only. Does not depend on the coordinator, body driver or animation. */
class IMAGINEGAME_API FIMGCustomMovementMode_Ragdoll
{
public:
	void Enter(UIMGCharacterMovementComponent& Movement);
	void Exit(UIMGCharacterMovementComponent& Movement);
	void AcquireOrientation(UIMGCharacterMovementComponent& Movement);
	void ReleaseOrientation(UIMGCharacterMovementComponent& Movement);
	void Simulate(UIMGCharacterMovementComponent& Movement, float DeltaTime);
	bool FindRecoveryTransform(const UIMGCharacterMovementComponent& Movement, FTransform& OutTransform) const;
	void ApplyRecoveryTransform(UIMGCharacterMovementComponent& Movement, const FTransform& Transform);

	FIMGRagdollMovementInput Input;

private:
	FCollisionResponseContainer SavedResponses;
	FName SavedProfile;
	ECollisionEnabled::Type SavedCollisionEnabled = ECollisionEnabled::QueryAndPhysics;
	bool bOwnsCapsuleCollision = false;
	bool bOwnsOrientation = false;
	bool bSavedControllerYaw = false;
	bool bSavedControllerPitch = false;
	bool bSavedControllerRoll = false;
};
