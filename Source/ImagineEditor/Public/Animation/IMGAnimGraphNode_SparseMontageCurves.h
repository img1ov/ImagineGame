#pragma once

#include "AnimGraphNode_Base.h"
#include "Animation/IMGAnimNode_SparseMontageCurves.h"

#include "IMGAnimGraphNode_SparseMontageCurves.generated.h"

class FBlueprintActionFilter;

UCLASS()
class IMAGINEEDITOR_API UIMGAnimGraphNode_SparseMontageCurves : public UAnimGraphNode_Base
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Settings)
	FIMGAnimNode_SparseMontageCurves Node;

	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FString GetNodeCategory() const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual bool IsActionFilteredOut(const FBlueprintActionFilter& Filter) override;
	virtual bool IsCompatibleWithGraph(const UEdGraph* Graph) const override;
	virtual void ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog) override;
};
