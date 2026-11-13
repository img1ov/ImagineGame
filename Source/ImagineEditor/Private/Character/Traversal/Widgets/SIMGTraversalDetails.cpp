#include "Character/Traversal/Widgets/SIMGTraversalDetails.h"
#include "Character/Traversal/Widgets/IMGTraversalDetailsCustomization.h"
#include "Character/Traversal/Widgets/IMGTraversalDetailWidgets.h"
#include "DetailLayoutBuilder.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Character/Traversal/Trace/IMGTraversalTraceExport.h"
#include "HAL/PlatformApplicationMisc.h"
#include "IRewindDebugger.h"
#include "IStructureDetailsView.h"
#include "IDetailsView.h"
#include "PropertyEditorModule.h"
#include "Modules/ModuleManager.h"
#include "UObject/StructOnScope.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace IMGTraversalRewind
{
void SDetails::Construct(const FArguments& Args)
{
	Lane = Args._Lane;
	ActorId = Args._ActorId;
	Pages = MakeShared<TMap<FName, int32>>();
	const auto Body = SNew(SVerticalBox);
	const auto Toolbar = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6, 4)) +
	    SWrapBox::Slot()[SNew(SButton)
	            .ToolTipText(FText::FromString(
	                TEXT("Copy the complete recorded frame, independent of track and search filters.")))
	            .IsEnabled_Lambda([this]() { return CurrentFrame.IsValid(); })
	            .OnClicked(this, &SDetails::CopyFrame)[SNew(SHorizontalBox) +
	                SHorizontalBox::Slot()
	                    .AutoWidth()
	                    .VAlign(VAlign_Center)
	                    .Padding(0, 0, 5, 0)[SNew(SImage).Image(FAppStyle::GetBrush("Icons.Clipboard"))] +
	                SHorizontalBox::Slot().AutoWidth().VAlign(
	                    VAlign_Center)[SNew(STextBlock).Text(FText::FromString(TEXT("Copy frame JSON")))]]] +
	    SWrapBox::Slot()[SNew(SButton)
	            .ToolTipText(FText::FromString(TEXT(
	                "Export all analyzed frames for this actor in the visible timeline range to UTF-8 JSON Lines.")))
	            .IsEnabled_Lambda([this]() {
		            const auto* Debugger = IRewindDebugger::Instance();
		            return ActorId != 0 && Debugger && !Debugger->IsRecording() &&
		                Debugger->GetAnalysisSession();
	            })
	            .OnClicked(this, &SDetails::ExportRange)[SNew(SHorizontalBox) +
	                SHorizontalBox::Slot()
	                    .AutoWidth()
	                    .VAlign(VAlign_Center)
	                    .Padding(0, 0, 5, 0)[SNew(SImage).Image(FAppStyle::GetBrush("Icons.Save"))] +
	                SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock)
	                        .Text(FText::FromString(TEXT("Export visible range")))]]];
	const auto Actions = SNew(SVerticalBox);
	Actions->AddSlot().AutoHeight()[Toolbar];
	Actions->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[SNew(SBox).Visibility_Lambda([this]() {
		return ExportStatus.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
	})[ReadOnlyText(TAttribute<FText>::CreateLambda([this]() { return FText::FromString(ExportStatus); }),
	    true)]];
	Actions->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[SNew(SHorizontalBox).Visibility_Lambda([this]() {
		return ExportPath.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
	}) +
	    SHorizontalBox::Slot()
	        .AutoWidth()
	        .VAlign(VAlign_Center)
	        .Padding(0, 0, 6, 0)[SNew(STextBlock)
	                .Text(FText::FromString(TEXT("Last export")))
	                .Font(IDetailLayoutBuilder::GetDetailFont())] +
	    SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[ReadOnlyText(
	        TAttribute<FText>::CreateLambda([this]() { return FText::FromString(ExportPath); }))] +
	    SHorizontalBox::Slot()
	        .AutoWidth()
	        .VAlign(VAlign_Center)
	        .Padding(4, 0)[DetailIconButton(TEXT("Icons.Clipboard"),
	            FText::FromString(TEXT("Copy export path")), FOnClicked::CreateLambda([this]() {
		            FPlatformApplicationMisc::ClipboardCopy(*ExportPath);
		            return FReply::Handled();
	            }))] +
	    SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[DetailIconButton(TEXT("Icons.FolderOpen"),
	        FText::FromString(TEXT("Show exported file in Explorer")), FOnClicked::CreateLambda([this]() {
		        if (FPaths::FileExists(ExportPath))
		        {
			        FPlatformProcess::ExploreFolder(*ExportPath);
		        }
		        else
		        {
			        ExportStatus = TEXT("The exported file no longer exists. Export the range again.");
		        }
		        return FReply::Handled();
	        }))]];
	Body->AddSlot().AutoHeight().Padding(0, 0, 0,
	    4)[SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(6)[Actions]];

	FDetailsViewArgs ViewArgs;
	ViewArgs.bAllowSearch = true;
	ViewArgs.bShowScrollBar = true;
	ViewArgs.bHideSelectionTip = true;
	ViewArgs.bUpdatesFromSelection = false;
	ViewArgs.bLockable = false;
	FStructureDetailsViewArgs StructureArgs;
	StructureArgs.bShowObjects = false;
	Details = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"))
	              .CreateStructureDetailView(ViewArgs, StructureArgs, nullptr);
	Body->AddSlot().FillHeight(1)[Details->GetWidget().ToSharedRef()];
	ChildSlot[Body];
	RefreshView();
}
void SDetails::SetFrame(TSharedPtr<const FIMGTraversalDebugFrame> Frame)
{
	if (CurrentFrame == Frame)
	{
		return;
	}
	CurrentFrame = MoveTemp(Frame);
	RefreshView();
}
void SDetails::RefreshView()
{
	Details->GetDetailsView()->SetGenericLayoutDetailsDelegate(
	    FOnGetDetailCustomizationInstance::CreateLambda(
	        [Frame = CurrentFrame, InLane = Lane, InPages = Pages]() {
		        return MakeTraversalDetailsCustomization(Frame, InLane, InPages.ToSharedRef());
	        }));
	// The struct supplies the layout type only; displayed values come from the immutable recorded frame.
	LayoutData = MakeShared<FStructOnScope>(FIMGTraversalDebugFrame::StaticStruct());
	Details->SetStructureData(LayoutData);
}
FReply SDetails::CopyFrame()
{
	FString Json;
	if (CurrentFrame && FrameToJson(*CurrentFrame, Json))
	{
		FPlatformApplicationMisc::ClipboardCopy(*Json);
		ExportStatus = TEXT("Complete frame JSON copied. Paste it into the chat for analysis.");
	}
	else
	{
		ExportStatus = TEXT("No frame available, or frame conversion failed.");
	}
	return FReply::Handled();
}
FReply SDetails::ExportRange()
{
	FString Error, NewPath;
	if (ExportVisibleRange(ActorId, NewPath, Error))
	{
		ExportPath = MoveTemp(NewPath);
		ExportStatus = TEXT("Export saved. Copy the path or show the file in Explorer.");
	}
	else
	{
		ExportStatus = Error;
	}
	return FReply::Handled();
}
} // namespace IMGTraversalRewind
