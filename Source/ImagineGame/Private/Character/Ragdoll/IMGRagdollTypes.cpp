#include "Character/Ragdoll/IMGRagdollTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGRagdollTypes)

bool FIMGRagdollSettings::IsValid() const
{
	const float Positive[] = {FollowTraceRadius, FollowTraceDistance, MaxFollowSpeed, MaxTargetDistance, RecoveryTraceDistance};
	for (float Value : Positive) { if (!FMath::IsFinite(Value) || Value <= 0.0f) { return false; } }
	return FMath::IsFinite(FloorClearance) && FloorClearance >= 0.0f && FMath::IsFinite(MaxRecoverySpeed)
		&& MaxRecoverySpeed >= 0.0f && FloorTraceChannel < ECC_MAX;
}

bool FIMGRagdollSettings::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	uint8 Channel = FloorTraceChannel;
	Ar << Channel;
	if (Ar.IsLoading()) { FloorTraceChannel = static_cast<ECollisionChannel>(Channel); }
	Ar << FollowTraceRadius << FollowTraceDistance << FloorClearance << MaxFollowSpeed << MaxTargetDistance;
	Ar << RecoveryTraceDistance << MaxRecoverySpeed;
	bOutSuccess = !Ar.IsError() && IsValid();
	return bOutSuccess;
}

bool FIMGRagdollMovementInput::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	Ar << Session;
	bOutSuccess = true;
	if (Session != 0)
	{
		PelvisLocation.NetSerialize(Ar, Map, bOutSuccess);
		Ar << FacingYaw;
		Ar.SerializeBits(&bFaceUp, 1);
	}
	bOutSuccess &= !Ar.IsError() && (Session == 0 || IsValid());
	return bOutSuccess;
}

bool FIMGRagdollRepState::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	Ar << Revision;
	Ar << Session;
	uint8 PhaseByte = static_cast<uint8>(Phase);
	uint8 ReasonByte = static_cast<uint8>(EndReason);
	Ar.SerializeBits(&PhaseByte, 2);
	Ar.SerializeBits(&ReasonByte, 2);
	Ar.SerializeBits(&bHasRecovery, 1);
	if (Ar.IsLoading())
	{
		Phase = static_cast<EIMGRagdollPhase>(PhaseByte);
		EndReason = static_cast<EIMGRagdollEndReason>(ReasonByte);
	}
	bool bSettingsSuccess = true;
	Settings.NetSerialize(Ar, Map, bSettingsSuccess);
	bOutSuccess = bSettingsSuccess && PhaseByte <= static_cast<uint8>(EIMGRagdollPhase::Recovering);
	if (Phase != EIMGRagdollPhase::Inactive || bHasRecovery)
	{
		bool bVectorSuccess = true;
		AnchorLocation.NetSerialize(Ar, Map, bVectorSuccess);
		bOutSuccess &= bVectorSuccess;
		InitialVelocity.NetSerialize(Ar, Map, bVectorSuccess);
		bOutSuccess &= bVectorSuccess;
		Ar << FacingYaw;
		Ar.SerializeBits(&bFaceUp, 1);
	}
	bOutSuccess &= !Ar.IsError();
	return bOutSuccess;
}
