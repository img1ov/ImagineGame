#include "Character/Traversal/IMGTraversalTargetData.h"

#include "Animation/AnimMontage.h"
#include "Engine/PackageMapClient.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGTraversalTargetData)

// Source and Action are packed into two bits each; extending either enum must widen the packing below.
static_assert(static_cast<uint8>(EIMGTraversalRequestSource::AI) < 4, "Widen FIMGTraversalTargetData Source packing");
static_assert(static_cast<uint8>(EIMGTraversalAction::Mantle) < 4, "Widen FIMGTraversalTargetData Action packing");

bool FIMGTraversalTargetData::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	bOutSuccess = Map != nullptr;
	if (!bOutSuccess)
	{
		return false;
	}
	Direction.NetSerialize(Ar, Map, bOutSuccess);
	bool bIntentSuccess = false;
	MovementIntent.NetSerialize(Ar, Map, bIntentSuccess);
	bOutSuccess &= bIntentSuccess;
	uint8 PackedSource = static_cast<uint8>(Source);
	Ar.SerializeBits(&PackedSource, 2);
	if (Ar.IsLoading())
	{
		Source = static_cast<EIMGTraversalRequestSource>(PackedSource);
	}
	uint8 PackedAction = static_cast<uint8>(Action);
	Ar.SerializeBits(&PackedAction, 2);
	if (Ar.IsLoading())
	{
		Action = static_cast<EIMGTraversalAction>(PackedAction);
	}
	UObject* MontageObject = Montage.Get();
	bOutSuccess &= Map->SerializeObject(Ar, UAnimMontage::StaticClass(), MontageObject);
	if (Ar.IsLoading())
	{
		Montage = Cast<UAnimMontage>(MontageObject);
	}
	Ar << StartTime;
	Ar << PlayRate;
	bOutSuccess &= !Ar.IsError();
	return true;
}
