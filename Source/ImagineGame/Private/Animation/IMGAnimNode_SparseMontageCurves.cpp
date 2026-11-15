#include "Animation/IMGAnimNode_SparseMontageCurves.h"

#include "Animation/IMGAnimInstance.h"
#include "Animation/Skeleton.h"
#include "Logging/TokenizedMessage.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGAnimNode_SparseMontageCurves)

#define LOCTEXT_NAMESPACE "IMGAnimNode_SparseMontageCurves"

FIMGAnimNode_SparseMontageCurves::FIMGAnimNode_SparseMontageCurves()
	: SlotName(FAnimSlotGroup::DefaultSlotName)
{
}

void FIMGAnimNode_SparseMontageCurves::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_Base::Initialize_AnyThread(Context);
	Pose.Initialize(Context);
	BaseCurves.Initialize(Context);
}

void FIMGAnimNode_SparseMontageCurves::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	Pose.CacheBones(Context);
	BaseCurves.CacheBones(Context);
}

void FIMGAnimNode_SparseMontageCurves::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	Pose.Update(Context);
	// Keep the cache advancing at full Montage weight without adding notify/root motion weight.
	// SaveCachedPose selects the native pose path's context whenever that path also updates it.
	BaseCurves.Update(Context.FractionalWeightAndRootMotion(0.0f, 0.0f).AsInactive());
}

void FIMGAnimNode_SparseMontageCurves::Evaluate_AnyThread(FPoseContext& Output)
{
	Pose.Evaluate(Output);
	FPoseContext BaseContext(Output);
	BaseCurves.Evaluate(BaseContext);

	if (const UIMGAnimInstance* AnimInstance = Cast<UIMGAnimInstance>(Output.GetAnimInstanceObject()))
	{
		AnimInstance->EvaluateSparseMontageCurves(SlotName, BaseContext.Curve, Output.Curve);
	}
	else
	{
		Output.Curve = BaseContext.Curve;
		Output.LogMessage(EMessageSeverity::Warning,
			LOCTEXT("IncompatibleInstance", "Sparse Montage Curves requires an IMGAnimInstance-derived Animation Blueprint, including in linked layers."));
	}
}

void FIMGAnimNode_SparseMontageCurves::GatherDebugData(FNodeDebugData& DebugData)
{
	DebugData.AddDebugItem(DebugData.GetNodeName(this) + FString::Printf(TEXT(" (Slot: %s)"), *SlotName.ToString()));
	Pose.GatherDebugData(DebugData.BranchFlow(1.0f));
	BaseCurves.GatherDebugData(DebugData.BranchFlow(0.0f));
}

#undef LOCTEXT_NAMESPACE
