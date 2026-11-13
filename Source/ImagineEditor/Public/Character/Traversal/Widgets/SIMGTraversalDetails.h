#pragma once
#include "CoreMinimal.h"
#include "Character/Traversal/IMGTraversalDebugTypes.h"
#include "Widgets/SCompoundWidget.h"
class IStructureDetailsView;
class FStructOnScope;
namespace IMGTraversalRewind
{
class SDetails final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDetails) : _Lane(-1), _ActorId(0) {}
	SLATE_ARGUMENT(int32, Lane)
	SLATE_ARGUMENT(uint64, ActorId)
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	void SetFrame(TSharedPtr<const FIMGTraversalDebugFrame> Frame);

private:
	void RefreshView();
	FReply CopyFrame();
	FReply ExportRange();
	int32 Lane = -1;
	uint64 ActorId = 0;
	FString ExportStatus, ExportPath;
	TSharedPtr<const FIMGTraversalDebugFrame> CurrentFrame;
	TSharedPtr<IStructureDetailsView> Details;
	TSharedPtr<FStructOnScope> LayoutData;
	TSharedPtr<TMap<FName, int32>> Pages;
};
} // namespace IMGTraversalRewind
