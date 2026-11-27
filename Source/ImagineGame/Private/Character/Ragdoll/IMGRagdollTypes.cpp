#include "Character/Ragdoll/IMGRagdollTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGRagdollTypes)

bool FIMGRagdollSettings::IsValid() const
{
	const float Positive[] = {FollowTraceRadius, FollowTraceDistance, MaxFollowSpeed, MaxTargetDistance, MaxTargetAge, RecoveryTraceDistance};
	for (float Value : Positive) { if (!FMath::IsFinite(Value) || Value <= 0.0f) { return false; } }
	return FMath::IsFinite(FloorClearance) && FloorClearance >= 0.0f && FMath::IsFinite(MaxRecoverySpeed)
		&& MaxRecoverySpeed >= 0.0f && FloorTraceChannel < ECC_MAX && FMath::IsFinite(RecoverySearchRadius)
		&& RecoverySearchRadius >= 0.0f && RecoverySearchRings >= 1 && RecoverySearchRings <= 4
		&& RecoverySearchSamples >= 4 && RecoverySearchSamples <= 16;
}

bool FIMGRagdollSettings::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	uint8 Channel = FloorTraceChannel;
	Ar << Channel;
	if (Ar.IsLoading()) { FloorTraceChannel = static_cast<ECollisionChannel>(Channel); }
	Ar << FollowTraceRadius << FollowTraceDistance << FloorClearance << MaxFollowSpeed << MaxTargetDistance;
	Ar << RecoveryTraceDistance << MaxRecoverySpeed;
	Ar << MaxTargetAge << RecoverySearchRadius << RecoverySearchRings << RecoverySearchSamples;
	Ar.SerializeBits(&bValidateTargetPath, 1);
	bOutSuccess = !Ar.IsError() && IsValid();
	return bOutSuccess;
}

bool FIMGRagdollMovementInput::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	Ar << Session;
	bOutSuccess = true;
	if (Session != 0)
	{
		uint8 StatusByte = static_cast<uint8>(Status);
		Ar.SerializeBits(&StatusByte, 2);
		if (Ar.IsLoading()) { Status = static_cast<EIMGRagdollTargetStatus>(StatusByte); }
		if (Status == EIMGRagdollTargetStatus::Ready)
		{
			PelvisLocation.NetSerialize(Ar, Map, bOutSuccess);
			Ar << FacingYaw;
			Ar.SerializeBits(&bFaceUp, 1);
		}
		bOutSuccess &= StatusByte <= static_cast<uint8>(EIMGRagdollTargetStatus::Expired)
			&& (Status != EIMGRagdollTargetStatus::Ready || IsValid());
	}
	else if (Ar.IsLoading()) { *this = FIMGRagdollMovementInput(); }
	bOutSuccess &= !Ar.IsError();
	return bOutSuccess;
}

bool FIMGRagdollBodyReference::IsValid() const
{
	return Session != 0 && MovementRevision != 0 && Sequence != 0 && Issue == EIMGRagdollIssue::None
		&& FMath::IsFinite(ServerTime) && ServerTime >= 0.f && FMath::IsFinite(ClientTimeStamp)
		&& FMath::IsFinite(FacingYaw) && !PelvisLocation.ContainsNaN() && !CapsuleLocation.ContainsNaN() && !Velocity.ContainsNaN();
}

bool FIMGRagdollBodyReference::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	Ar.SerializeIntPacked(Sequence);
	if (Sequence == 0)
	{
		if (Ar.IsLoading()) { *this = FIMGRagdollBodyReference(); }
		bOutSuccess = !Ar.IsError();
		return bOutSuccess;
	}
	Ar << Session << MovementRevision;
	Ar << ServerTime << ClientTimeStamp;
	uint8 IssueByte = static_cast<uint8>(Issue);
	static_assert(static_cast<uint8>(EIMGRagdollIssue::ExitBlocked) < 32);
	Ar.SerializeBits(&IssueByte, 5);
	if (Ar.IsLoading())
	{
		Issue = static_cast<EIMGRagdollIssue>(IssueByte);
		PelvisLocation = CapsuleLocation = Velocity = FVector::ZeroVector;
		FacingYaw = 0;
		bFaceUp = false;
	}
	bOutSuccess = true;
	if (Issue == EIMGRagdollIssue::None)
	{
		bool bVectorSuccess = true;
		PelvisLocation.NetSerialize(Ar, Map, bVectorSuccess);
		bOutSuccess &= bVectorSuccess;
		CapsuleLocation.NetSerialize(Ar, Map, bVectorSuccess);
		bOutSuccess &= bVectorSuccess;
		Velocity.NetSerialize(Ar, Map, bVectorSuccess);
		bOutSuccess &= bVectorSuccess;
		uint16 PackedYaw = FRotator::CompressAxisToShort(FacingYaw);
		Ar << PackedYaw;
		if (Ar.IsLoading()) { FacingYaw = FRotator::DecompressAxisFromShort(PackedYaw); }
		Ar.SerializeBits(&bFaceUp, 1);
		bOutSuccess &= IsValid();
	}
	bOutSuccess &= Session != 0 && MovementRevision != 0 && Sequence != 0
		&& FMath::IsFinite(ServerTime) && ServerTime >= 0.f && FMath::IsFinite(ClientTimeStamp)
		&& IssueByte <= static_cast<uint8>(EIMGRagdollIssue::ExitBlocked) && !Ar.IsError();
	return bOutSuccess;
}

bool FIMGRagdollRepState::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	Ar << Revision;
	Ar << Session;
	uint8 PhaseByte = static_cast<uint8>(Phase);
	uint8 ReasonByte = static_cast<uint8>(EndReason);
	Ar.SerializeBits(&PhaseByte, 2);
	Ar.SerializeBits(&ReasonByte, 3);
	Ar.SerializeBits(&bHasRecovery, 1);
	if (Ar.IsLoading())
	{
		Phase = static_cast<EIMGRagdollPhase>(PhaseByte);
		EndReason = static_cast<EIMGRagdollEndReason>(ReasonByte);
	}
	bool bSettingsSuccess = true;
	Settings.NetSerialize(Ar, Map, bSettingsSuccess);
	bOutSuccess = bSettingsSuccess && PhaseByte <= static_cast<uint8>(EIMGRagdollPhase::Recovering)
		&& ReasonByte <= static_cast<uint8>(EIMGRagdollEndReason::Transferred);
	if (Phase != EIMGRagdollPhase::Inactive || bHasRecovery)
	{
		bool bVectorSuccess = true;
		AnchorLocation.NetSerialize(Ar, Map, bVectorSuccess);
		bOutSuccess &= bVectorSuccess;
		InitialVelocity.NetSerialize(Ar, Map, bVectorSuccess);
		bOutSuccess &= bVectorSuccess;
		Ar << StartServerTime;
		bOutSuccess &= FMath::IsFinite(StartServerTime) && StartServerTime >= 0.f;
		Ar << FacingYaw;
		Ar.SerializeBits(&bFaceUp, 1);
	}
	else if (Ar.IsLoading()) { StartServerTime = 0.f; }
	if (bHasRecovery)
	{
		Ar.SerializeBits(&bExternalTransition, 1);
		if (bExternalTransition)
		{
			uint8 ModeByte = static_cast<uint8>(TransitionMode);
			Ar.SerializeBits(&ModeByte, 3);
			if (Ar.IsLoading()) { TransitionMode = static_cast<EMovementMode>(ModeByte); TransitionCustomMode = 0; }
			if (ModeByte == MOVE_Custom) { Ar << TransitionCustomMode; }
			bool bVelocitySuccess = true;
			TransitionVelocity.NetSerialize(Ar, Map, bVelocitySuccess);
			bOutSuccess &= bVelocitySuccess && !TransitionVelocity.ContainsNaN() && ModeByte > MOVE_None && ModeByte < MOVE_MAX
				&& Phase == EIMGRagdollPhase::Inactive && EndReason == EIMGRagdollEndReason::Transferred;
		}
	}
	else if (Ar.IsLoading()) { bExternalTransition = false; }
	if (Ar.IsLoading() && !bExternalTransition)
	{
		TransitionMode = MOVE_None;
		TransitionCustomMode = 0;
		TransitionVelocity = FVector::ZeroVector;
	}
	bOutSuccess &= EndReason != EIMGRagdollEndReason::Transferred || (bHasRecovery && bExternalTransition);
	bOutSuccess &= !Ar.IsError();
	return bOutSuccess;
}
