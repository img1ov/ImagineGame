#include "Animation/IMGAnimGraphNode_SparseMontageCurves.h"

#include "AnimGraphNode_Slot.h"
#include "Animation/IMGAnimInstance.h"
#include "Animation/Skeleton.h"
#include "BlueprintActionFilter.h"
#include "EdGraph/EdGraphPin.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGAnimGraphNode_SparseMontageCurves)

#define LOCTEXT_NAMESPACE "IMGAnimGraphNode_SparseMontageCurves"

namespace
{
	bool SupportsSparseMontageCurves(const UBlueprint* Blueprint)
	{
		const UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Blueprint);
		return AnimBlueprint && AnimBlueprint->ParentClass && AnimBlueprint->ParentClass->IsChildOf(UIMGAnimInstance::StaticClass());
	}
}

FText UIMGAnimGraphNode_SparseMontageCurves::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return FText::Format(LOCTEXT("Title", "Sparse Montage Curves ({0})"), FText::FromName(Node.SlotName));
}

FText UIMGAnimGraphNode_SparseMontageCurves::GetTooltipText() const
{
	return LOCTEXT("Tooltip", "Pass through the native Slot pose and attributes. Rebuild curves from Base Curves, blending only Montage-owned float curves using their actual weights. Connect Base Curves to the cached pose feeding the Slot. Additive tracks also use absolute curve overrides. Requires IMGAnimInstance.");
}

FString UIMGAnimGraphNode_SparseMontageCurves::GetNodeCategory() const
{
	return TEXT("Animation|Montage");
}

FLinearColor UIMGAnimGraphNode_SparseMontageCurves::GetNodeTitleColor() const
{
	return FLinearColor(0.2f, 0.6f, 0.7f);
}

bool UIMGAnimGraphNode_SparseMontageCurves::IsActionFilteredOut(const FBlueprintActionFilter& Filter)
{
	if (Super::IsActionFilteredOut(Filter))
	{
		return true;
	}
	for (const UBlueprint* Blueprint : Filter.Context.Blueprints)
	{
		if (!SupportsSparseMontageCurves(Blueprint))
		{
			return true;
		}
	}
	for (const UEdGraph* Graph : Filter.Context.Graphs)
	{
		if (!IsCompatibleWithGraph(Graph))
		{
			return true;
		}
	}
	return false;
}

bool UIMGAnimGraphNode_SparseMontageCurves::IsCompatibleWithGraph(const UEdGraph* Graph) const
{
	return Graph && Super::IsCompatibleWithGraph(Graph)
		&& SupportsSparseMontageCurves(FBlueprintEditorUtils::FindBlueprintForGraph(Graph));
}

void UIMGAnimGraphNode_SparseMontageCurves::ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog)
{
	Super::ValidateAnimNodeDuringCompilation(ForSkeleton, MessageLog);

	if (!SupportsSparseMontageCurves(GetAnimBlueprint()))
	{
		MessageLog.Error(TEXT("@@ requires an IMGAnimInstance-derived Animation Blueprint, including in linked layers."), this);
	}
	if (Node.SlotName.IsNone())
	{
		MessageLog.Error(TEXT("@@ requires a Slot Name matching the native Slot supplying Pose."), this);
	}
	else if (ForSkeleton && !ForSkeleton->HasAnyFlags(RF_NeedPostLoad) && !ForSkeleton->ContainsSlotName(Node.SlotName))
	{
		MessageLog.Warning(TEXT("@@ references a Slot Name absent from the target Skeleton. Match the native Slot supplying Pose."), this);
	}

	for (FName PinName : {GET_MEMBER_NAME_CHECKED(FIMGAnimNode_SparseMontageCurves, Pose), GET_MEMBER_NAME_CHECKED(FIMGAnimNode_SparseMontageCurves, BaseCurves)})
	{
		const UEdGraphPin* Pin = FindPin(PinName);
		if (!Pin || Pin->LinkedTo.IsEmpty())
		{
			MessageLog.Error(TEXT("@@ requires both Pose and Base Curves connections. Use the same cached pre-Slot pose for Base Curves and the native Slot source."), this);
			break;
		}
	}

	const UEdGraphPin* PosePin = FindPin(GET_MEMBER_NAME_CHECKED(FIMGAnimNode_SparseMontageCurves, Pose));
	if (PosePin && PosePin->LinkedTo.Num() == 1)
	{
		if (const UAnimGraphNode_Slot* Slot = Cast<UAnimGraphNode_Slot>(PosePin->LinkedTo[0]->GetOwningNode()))
		{
			if (Slot->Node.SlotName != Node.SlotName)
			{
				MessageLog.Error(TEXT("@@ Slot Name must match the native Slot supplying Pose."), this);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
