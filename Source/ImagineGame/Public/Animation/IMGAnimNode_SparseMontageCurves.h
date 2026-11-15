#pragma once

#include "Animation/AnimNodeBase.h"

#include "IMGAnimNode_SparseMontageCurves.generated.h"

/** Keeps the input pose and replaces its curves with sparse, weighted Montage overrides. */
USTRUCT(BlueprintInternalUseOnly)
struct IMAGINEGAME_API FIMGAnimNode_SparseMontageCurves : public FAnimNode_Base
{
	GENERATED_BODY()

public:
	FIMGAnimNode_SparseMontageCurves();

	/** The native Slot output. Its bones and attributes pass through unchanged. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Links)
	FPoseLink Pose;

	/** Use the same cached pose that feeds the Slot, before its Montage curves are blended. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Links)
	FPoseLink BaseCurves;

	/** Must match the native Slot. Only curves authored on the Montage itself are overrides. */
	UPROPERTY(EditAnywhere, Category = Settings, meta = (NeverAsPin))
	FName SlotName;

	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
};
