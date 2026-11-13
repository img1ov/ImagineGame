#include "Character/Traversal/Widgets/IMGTraversalDetailWidgets.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Text/SMultiLineEditableText.h"

namespace IMGTraversalRewind
{
namespace
{
FUIAction CopyAction(FString Value)
{
	return FUIAction(FExecuteAction::CreateLambda(
	    [Value = MoveTemp(Value)]() { FPlatformApplicationMisc::ClipboardCopy(*Value); }));
}
TSharedRef<SWidget> BuildReadOnlyText(TAttribute<FText> Value, bool bWrap, FMenuExtensionDelegate Menu)
{
	if (bWrap)
	{
		return SNew(SMultiLineEditableText)
		    .Text(Value)
		    .ToolTipText(Value)
		    .Font(IDetailLayoutBuilder::GetDetailFont())
		    .IsReadOnly(true)
		    .SelectAllTextWhenFocused(true)
		    .AllowContextMenu(true)
		    .AutoWrapText(true)
		    .ContextMenuExtender(Menu);
	}
	return SNew(SEditableText)
	    .Text(Value)
	    .ToolTipText(Value)
	    .Font(IDetailLayoutBuilder::GetDetailFont())
	    .ColorAndOpacity(FSlateColor::UseForeground())
	    .IsReadOnly(true)
	    .SelectAllTextWhenFocused(true)
	    .AllowContextMenu(true)
	    .ContextMenuExtender(Menu);
}
} // namespace
TSharedRef<SWidget> ReadOnlyText(TAttribute<FText> Value, bool bWrap)
{
	return BuildReadOnlyText(Value, bWrap, FMenuExtensionDelegate());
}
TSharedRef<SWidget> ReadOnlyValue(const FText& Name, const FString& Display, const FString& Raw)
{
	const auto Menu = FMenuExtensionDelegate::CreateLambda([Name, Raw](FMenuBuilder& Builder) {
		Builder.AddMenuSeparator();
		Builder.AddMenuEntry(FText::FromString(TEXT("Copy full recorded value")),
		    FText::FromString(TEXT("Preserve the complete recorded value, including numeric precision.")),
		    FSlateIcon(), CopyAction(Raw));
		Builder.AddMenuEntry(FText::FromString(TEXT("Copy name and full value")), FText::GetEmpty(),
		    FSlateIcon(), CopyAction(Name.ToString() + TEXT(" = ") + Raw));
	});
	return BuildReadOnlyText(
	    FText::FromString(Display), Display.Len() > 120 || Display.Contains(TEXT("\n")), Menu);
}
TSharedRef<SWidget> DetailIconButton(FName Icon, const FText& Tooltip, FOnClicked Action)
{
	return SNew(SButton)
	    .ButtonStyle(FAppStyle::Get(), "SimpleButton")
	    .ContentPadding(2)
	    .ToolTipText(Tooltip)
	    .OnClicked(Action)
	        [SNew(SImage).Image(FAppStyle::GetBrush(Icon)).ColorAndOpacity(FSlateColor::UseForeground())];
}
void SetDetailCopyActions(
    FDetailWidgetRow& Row, const FString& Name, const FString& Raw, const FString& Display)
{
	Row.CopyAction(CopyAction(Raw));
	// PropertyEditor requires both actions to recognize custom Copy; recorded values remain read-only.
	Row.PasteAction(FUIAction(
	    FExecuteAction::CreateLambda([]() {}), FCanExecuteAction::CreateLambda([]() { return false; })));
	Row.AddCustomContextMenuAction(CopyAction(Name), FText::FromString(TEXT("Copy field name")));
	Row.AddCustomContextMenuAction(
	    CopyAction(Name + TEXT(" = ") + Raw), FText::FromString(TEXT("Copy name and full value")));
	if (Raw != Display)
	{
		Row.AddCustomContextMenuAction(CopyAction(Display), FText::FromString(TEXT("Copy displayed text")),
		    FText::FromString(TEXT(
		        "Copy the formatted display. The standard Copy action preserves the full recorded value.")));
	}
}
} // namespace IMGTraversalRewind
