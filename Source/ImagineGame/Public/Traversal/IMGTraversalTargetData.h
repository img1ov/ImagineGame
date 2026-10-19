#pragma once

#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Traversal/IMGTraversalTypes.h"

#include "IMGTraversalTargetData.generated.h"

/** Client selection only. Authority repeats geometry detection and checks this request. */
USTRUCT()
struct FIMGTraversalTargetData : public FGameplayAbilityTargetData
{
	GENERATED_BODY()

	UPROPERTY()
	FVector_NetQuantizeNormal Direction = FVector::ForwardVector;

	UPROPERTY()
	EIMGTraversalAction Action = EIMGTraversalAction::None;

	UPROPERTY()
	TObjectPtr<UAnimMontage> Montage = nullptr;

	UPROPERTY()
	float StartTime = 0.f;

	UPROPERTY()
	float PlayRate = 1.f;

	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);

	virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }
};

template<>
struct TStructOpsTypeTraits<FIMGTraversalTargetData> : TStructOpsTypeTraitsBase2<FIMGTraversalTargetData>
{
	enum { WithNetSerializer = true };
};
