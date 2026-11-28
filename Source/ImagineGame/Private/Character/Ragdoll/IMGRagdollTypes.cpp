#include "Character/Ragdoll/IMGRagdollTypes.h"
#include "Animation/AnimMontage.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGRagdollTypes)

namespace IMGRagdollTypes
{
	constexpr float MinFloorClearance = 2.0f;
	constexpr float FloorClearanceMargin = 0.1f;
	constexpr int32 MinSearchRings = 1;
	constexpr int32 MaxSearchRings = 4;
	constexpr int32 MinSearchSamples = 4;
	constexpr int32 MaxSearchSamples = 16;
}

bool FIMGRagdollSettings::IsValid() const
{
	const float PositiveValues[] =
	{
		MaxFollowSpeed,
		MaxTargetDistance,
		MaxTargetAge,
		EntryInputTimeout,
		RecoveryTraceDistance
	};
	for (const float Value : PositiveValues)
	{
		if (!FMath::IsFinite(Value) || Value <= 0.0f)
		{
			return false;
		}
	}

	return FMath::IsFinite(FloorClearance)
		&& FloorClearance >= 0.0f
		&& FMath::IsFinite(MaxRecoverySpeed)
		&& MaxRecoverySpeed >= 0.0f
		&& FMath::IsFinite(RecoverySearchRadius)
		&& RecoverySearchRadius >= 0.0f
		&& RecoverySearchRings >= IMGRagdollTypes::MinSearchRings
		&& RecoverySearchRings <= IMGRagdollTypes::MaxSearchRings
		&& RecoverySearchSamples >= IMGRagdollTypes::MinSearchSamples
		&& RecoverySearchSamples <= IMGRagdollTypes::MaxSearchSamples;
}

float FIMGRagdollSettings::GetEffectiveFloorClearance(float MaxFloorDist) const
{
	const float UpperBound = FMath::Max(IMGRagdollTypes::MinFloorClearance, MaxFloorDist - IMGRagdollTypes::FloorClearanceMargin);
	return FMath::Clamp(FloorClearance, IMGRagdollTypes::MinFloorClearance, UpperBound);
}

bool FIMGRagdollSettings::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	Ar << FloorClearance << MaxFollowSpeed << MaxTargetDistance;
	Ar << RecoveryTraceDistance << MaxRecoverySpeed;
	Ar << MaxTargetAge << EntryInputTimeout << RecoverySearchRadius;

	uint8 RingsByte = static_cast<uint8>(FMath::Clamp(RecoverySearchRings, 0, 7));
	uint8 SamplesByte = static_cast<uint8>(FMath::Clamp(RecoverySearchSamples, 0, 31));
	static_assert(IMGRagdollTypes::MaxSearchRings < (1u << 3));
	static_assert(IMGRagdollTypes::MaxSearchSamples < (1u << 5));
	Ar.SerializeBits(&RingsByte, 3);
	Ar.SerializeBits(&SamplesByte, 5);
	if (Ar.IsLoading())
	{
		RecoverySearchRings = RingsByte;
		RecoverySearchSamples = SamplesByte;
	}

	Ar.SerializeBits(&bValidateTargetPath, 1);
	bOutSuccess = !Ar.IsError() && IsValid();
	return bOutSuccess;
}

bool FIMGRagdollTransitionRequest::IsValid() const
{
	return !Transform.ContainsNaN()
		&& Transform.GetRotation().IsNormalized()
		&& Transform.GetScale3D().Equals(FVector::OneVector)
		&& TargetMode > MOVE_None
		&& TargetMode < MOVE_MAX
		&& (TargetMode == MOVE_Custom || TargetCustomMode == 0)
		&& !CarryVelocity.ContainsNaN()
		&& !BodyLocalOffset.ContainsNaN();
}

bool FIMGRagdollMovementInput::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	Ar << Session;
	bOutSuccess = true;
	if (Session != 0)
	{
		uint8 StatusByte = static_cast<uint8>(Status);
		static_assert(static_cast<uint8>(EIMGRagdollTargetStatus::Expired) < (1u << 2));
		Ar.SerializeBits(&StatusByte, 2);
		if (Ar.IsLoading())
		{
			Status = static_cast<EIMGRagdollTargetStatus>(StatusByte);
		}

		if (Status == EIMGRagdollTargetStatus::Ready)
		{
			bool bVectorSuccess = true;
			PelvisLocation.NetSerialize(Ar, Map, bVectorSuccess);
			bOutSuccess &= bVectorSuccess;
			PelvisVelocity.NetSerialize(Ar, Map, bVectorSuccess);
			bOutSuccess &= bVectorSuccess;
			Ar << CapsuleHeightOffset;
			Ar << FacingYaw;
			Ar.SerializeBits(&bFaceUp, 1);
		}

		bOutSuccess &= StatusByte <= static_cast<uint8>(EIMGRagdollTargetStatus::Expired)
			&& (Status != EIMGRagdollTargetStatus::Ready || IsValid());
	}
	else if (Ar.IsLoading())
	{
		*this = FIMGRagdollMovementInput();
	}

	bOutSuccess &= !Ar.IsError();
	return bOutSuccess;
}

bool FIMGRagdollBodyReference::IsValid() const
{
	return Session != 0
		&& MovementRevision != 0
		&& Sequence != 0
		&& Issue == EIMGRagdollIssue::None
		&& FMath::IsFinite(ServerTime)
		&& ServerTime >= 0.0
		&& FMath::IsFinite(ClientTimeStamp)
		&& FMath::IsFinite(FacingYaw)
		&& !PelvisLocation.ContainsNaN()
		&& !CapsuleLocation.ContainsNaN()
		&& !Velocity.ContainsNaN();
}

bool FIMGRagdollBodyReference::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	Ar.SerializeIntPacked(Sequence);
	if (Sequence == 0)
	{
		if (Ar.IsLoading())
		{
			*this = FIMGRagdollBodyReference();
		}
		bOutSuccess = !Ar.IsError();
		return bOutSuccess;
	}

	Ar << Session << MovementRevision;
	Ar << ServerTime << ClientTimeStamp;

	uint8 IssueByte = static_cast<uint8>(Issue);
	static_assert(static_cast<uint8>(EIMGRagdollIssue::ExitBlocked) < (1u << 5));
	Ar.SerializeBits(&IssueByte, 5);
	if (Ar.IsLoading())
	{
		Issue = static_cast<EIMGRagdollIssue>(IssueByte);
		PelvisLocation = FVector::ZeroVector;
		CapsuleLocation = FVector::ZeroVector;
		Velocity = FVector::ZeroVector;
		FacingYaw = 0.0f;
		bFaceUp = false;
	}

	bOutSuccess = true;
	if (Issue == EIMGRagdollIssue::None)
	{
		// Transmit CapsuleLocation + compact PelvisOffset to save bandwidth and prevent independent rounding drift.
		bool bVectorSuccess = true;
		CapsuleLocation.NetSerialize(Ar, Map, bVectorSuccess);
		bOutSuccess &= bVectorSuccess;

		FVector_NetQuantize10 PelvisOffset = Ar.IsSaving()
			? FVector_NetQuantize10(FVector(PelvisLocation) - FVector(CapsuleLocation))
			: FVector_NetQuantize10(FVector::ZeroVector);
		PelvisOffset.NetSerialize(Ar, Map, bVectorSuccess);
		bOutSuccess &= bVectorSuccess;
		if (Ar.IsLoading())
		{
			PelvisLocation = FVector(CapsuleLocation) + FVector(PelvisOffset);
		}

		Velocity.NetSerialize(Ar, Map, bVectorSuccess);
		bOutSuccess &= bVectorSuccess;

		uint16 PackedYaw = FRotator::CompressAxisToShort(FacingYaw);
		Ar << PackedYaw;
		if (Ar.IsLoading())
		{
			FacingYaw = FRotator::DecompressAxisFromShort(PackedYaw);
		}

		Ar.SerializeBits(&bFaceUp, 1);
		bOutSuccess &= IsValid();
	}

	bOutSuccess &= Session != 0
		&& MovementRevision != 0
		&& Sequence != 0
		&& FMath::IsFinite(ServerTime)
		&& ServerTime >= 0.0
		&& FMath::IsFinite(ClientTimeStamp)
		&& IssueByte <= static_cast<uint8>(EIMGRagdollIssue::ExitBlocked)
		&& !Ar.IsError();
	return bOutSuccess;
}

bool FIMGRagdollRepState::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	Ar << Revision;
	Ar << Session;

	uint8 PhaseByte = static_cast<uint8>(Phase);
	uint8 ReasonByte = static_cast<uint8>(EndReason);
	static_assert(static_cast<uint8>(EIMGRagdollPhase::Recovering) < (1u << 2));
	static_assert(static_cast<uint8>(EIMGRagdollEndReason::Transferred) < (1u << 3));
	Ar.SerializeBits(&PhaseByte, 2);
	Ar.SerializeBits(&ReasonByte, 3);
	Ar.SerializeBits(&bHasRecovery, 1);

	if (Ar.IsLoading())
	{
		Phase = static_cast<EIMGRagdollPhase>(PhaseByte);
		EndReason = static_cast<EIMGRagdollEndReason>(ReasonByte);
	}

	bOutSuccess = PhaseByte <= static_cast<uint8>(EIMGRagdollPhase::Recovering)
		&& ReasonByte <= static_cast<uint8>(EIMGRagdollEndReason::Transferred);

	// Avoid sending ~48 bytes of movement settings on every post-ragdoll normal movement response.
	if (Phase != EIMGRagdollPhase::Inactive)
	{
		bool bSettingsSuccess = true;
		Settings.NetSerialize(Ar, Map, bSettingsSuccess);
		bOutSuccess &= bSettingsSuccess;
	}
	else if (Ar.IsLoading())
	{
		Settings = FIMGRagdollSettings();
	}

	if (Phase != EIMGRagdollPhase::Inactive || bHasRecovery)
	{
		bool bVectorSuccess = true;
		AnchorLocation.NetSerialize(Ar, Map, bVectorSuccess);
		bOutSuccess &= bVectorSuccess;
		InitialVelocity.NetSerialize(Ar, Map, bVectorSuccess);
		bOutSuccess &= bVectorSuccess;
		LaunchVelocityDelta.NetSerialize(Ar, Map, bVectorSuccess);
		bOutSuccess &= bVectorSuccess;
		Ar << StartServerTime;
		bOutSuccess &= FMath::IsFinite(StartServerTime) && StartServerTime >= 0.0;
		Ar << FacingYaw;
		Ar.SerializeBits(&bFaceUp, 1);
	}
	else if (Ar.IsLoading())
	{
		AnchorLocation = FVector::ZeroVector;
		InitialVelocity = FVector::ZeroVector;
		LaunchVelocityDelta = FVector::ZeroVector;
		StartServerTime = 0.0;
		FacingYaw = 0;
		bFaceUp = false;
	}

	if (bHasRecovery)
	{
		Ar.SerializeBits(&bExternalTransition, 1);
		if (bExternalTransition)
		{
			uint8 ModeByte = static_cast<uint8>(TransitionMode);
			static_assert(static_cast<uint8>(MOVE_MAX) <= (1u << 3));
			Ar.SerializeBits(&ModeByte, 3);
			if (Ar.IsLoading())
			{
				TransitionMode = static_cast<EMovementMode>(ModeByte);
				TransitionCustomMode = 0;
			}
			if (ModeByte == MOVE_Custom)
			{
				Ar << TransitionCustomMode;
			}

			bool bVelocitySuccess = true;
			TransitionVelocity.NetSerialize(Ar, Map, bVelocitySuccess);
			bOutSuccess &= bVelocitySuccess
				&& !TransitionVelocity.ContainsNaN()
				&& ModeByte > MOVE_None
				&& ModeByte < MOVE_MAX
				&& Phase == EIMGRagdollPhase::Inactive
				&& EndReason == EIMGRagdollEndReason::Transferred;
		}
	}
	else if (Ar.IsLoading())
	{
		bExternalTransition = false;
	}

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

bool FIMGRagdollRecoverySelection::GetPlaybackPosition(
	double ElapsedSeconds, float& OutPosition, float& OutRemainingSeconds) const
{
	OutPosition = -1.f;
	OutRemainingSeconds = 0.f;
	if (!IsValid(Montage) || !Montage->bEnableAutoBlendOut || !FMath::IsFinite(StartTime) ||
		!FMath::IsFinite(PlayRate) || PlayRate <= 0.f || !FMath::IsFinite(Montage->RateScale) ||
		Montage->RateScale <= 0.f || !FMath::IsFinite(ElapsedSeconds) || ElapsedSeconds < 0.0 ||
		StartTime < 0.f || StartTime >= Montage->GetPlayLength() || Montage->SlotAnimTracks.IsEmpty())
	{
		return false;
	}
	const double EffectiveRate = static_cast<double>(PlayRate) * Montage->RateScale;
	double RemainingAdvance = ElapsedSeconds * EffectiveRate;
	double RemainingPath = 0.0;
	float Cursor = StartTime;
	int32 Section = Montage->GetSectionIndexFromPosition(Cursor);
	TArray<int32, TInlineAllocator<8>> Visited;
	if (Section == INDEX_NONE)
	{
		return false;
	}
	while (Section != INDEX_NONE)
	{
		if (Visited.Contains(Section) || !Montage->CompositeSections.IsValidIndex(Section))
		{
			return false;
		}
		Visited.Add(Section);
		float Begin = 0.f;
		float End = 0.f;
		Montage->GetSectionStartAndEndTime(Section, Begin, End);
		if (!FMath::IsFinite(Begin) || !FMath::IsFinite(End) || End <= Cursor || Cursor < Begin)
		{
			return false;
		}
		const double Span = End - Cursor;
		if (OutPosition < 0.f)
		{
			if (RemainingAdvance < Span)
			{
				OutPosition = Cursor + static_cast<float>(RemainingAdvance);
				RemainingPath += Span - RemainingAdvance;
			}
			else
			{
				RemainingAdvance -= Span;
			}
		}
		else
		{
			RemainingPath += Span;
		}
		const FName NextName = Montage->CompositeSections[Section].NextSectionName;
		Section = NextName.IsNone() ? INDEX_NONE : Montage->GetSectionIndex(NextName);
		if (!NextName.IsNone() && Section == INDEX_NONE)
		{
			return false;
		}
		if (Section != INDEX_NONE)
		{
			Montage->GetSectionStartAndEndTime(Section, Cursor, End);
		}
	}
	OutRemainingSeconds = static_cast<float>(RemainingPath / EffectiveRate);
	return OutPosition >= 0.f && FMath::IsFinite(OutRemainingSeconds) && OutRemainingSeconds > 0.f;
}

bool FIMGRagdollRecoveryAnimation::Allows(
	const FIMGRagdollRecoverySelection& Selection, bool bInFaceUp) const
{
	float Position = -1.f;
	float Duration = 0.f;
	return Montage && Montage == Selection.Montage && bFaceUp == bInFaceUp &&
		FMath::IsFinite(MinStartTime) && FMath::IsFinite(MaxStartTime) &&
		MinStartTime >= 0.f && MaxStartTime >= MinStartTime &&
		FMath::IsFinite(MinPlayRate) && FMath::IsFinite(MaxPlayRate) &&
		MinPlayRate > 0.f && MaxPlayRate >= MinPlayRate &&
		Selection.StartTime >= MinStartTime && Selection.StartTime <= MaxStartTime &&
		Selection.PlayRate >= MinPlayRate && Selection.PlayRate <= MaxPlayRate &&
		!BodyLocalOffset.ContainsNaN() && Selection.GetPlaybackPosition(0.0, Position, Duration);
}
