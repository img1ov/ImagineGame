#include "Character/Traversal/Widgets/IMGTraversalDetailsCustomization.h"
#include "Character/Traversal/Widgets/IMGTraversalDetailWidgets.h"
#include "Character/Traversal/IMGTraversalRewindData.h"
#include "Character/Traversal/Trace/IMGTraversalTraceExport.h"
#include "Framework/Commands/UIAction.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Styling/AppStyle.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailCustomNodeBuilder.h"
#include "IDetailCustomization.h"
#include "IDetailGroup.h"
#include "Editor.h"
#include "Misc/MessageDialog.h"
#include "SourceCodeNavigation.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"
#include "UObject/NoExportTypes.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace IMGTraversalRewind
{
namespace
{
FText Label(const FString& Value)
{
	return FText::FromString(Value);
}
TSharedRef<SWidget> Text(const FText& Value)
{
	return ReadOnlyText(Value, Value.ToString().Len() > 120 || Value.ToString().Contains(TEXT("\n")));
}
void FillRow(FDetailWidgetRow& Row, const FText& Name, const TSharedRef<SWidget>& Value)
{
	Row.NameContent()
	    [SNew(STextBlock).Text(Name).Font(IDetailLayoutBuilder::GetDetailFont()).AutoWrapText(true)];
	Row.ValueContent().MinDesiredWidth(180).MaxDesiredWidth(900)[Value];
}
void ValueRow(FDetailWidgetRow& Row, const FText& Name, const FString& Display, const FString& Raw,
    FName Key = NAME_None)
{
	FillRow(Row, Name, ReadOnlyValue(Name, Display, Raw));
	SetDetailCopyActions(Row, Key.IsNone() ? Name.ToString() : Key.ToString(), Raw, Display);
}
void OpenAsset(const FString& Path)
{
	UObject* Object = FSoftObjectPath(Path).TryLoad();
	if (auto* Class = Cast<UClass>(Object))
	{
		if (Class->ClassGeneratedBy)
		{
			Object = Class->ClassGeneratedBy;
		}
		else
		{
			if (!FSourceCodeNavigation::NavigateToClass(Class))
			{
				FMessageDialog::Open(EAppMsgType::Ok,
				    Label(TEXT("Source navigation is unavailable for this native ability.")));
			}
			return;
		}
	}
	auto* Editors = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
	if (!Object || !Editors || !Editors->OpenEditorForAsset(Object))
	{
		FMessageDialog::Open(EAppMsgType::Ok, Label(TEXT("Could not open the recorded asset: ") + Path));
	}
}
TSharedRef<SWidget> AssetLink(const FString& Path, const FString& Name)
{
	if (Path.IsEmpty() || Path == TEXT("None"))
	{
		return Text(Label(Name.IsEmpty() ? TEXT("Not recorded") : Name));
	}
	FString Display = Name.IsEmpty() ? FSoftObjectPath(Path).GetAssetName() : Name;
	Display.RemoveFromEnd(TEXT("_C"));
	return SNew(SHorizontalBox) +
	    SHorizontalBox::Slot().AutoWidth()
	        [SNew(SHyperlink).Text(Label(Display)).ToolTipText(Label(Path)).OnNavigate_Lambda([Path]() {
		        OpenAsset(Path);
	        })];
}
void BrowseAsset(const FString& Path)
{
	UObject* Object = FSoftObjectPath(Path).TryLoad();
	if (auto* Class = Cast<UClass>(Object); Class && Class->ClassGeneratedBy)
	{
		Object = Class->ClassGeneratedBy;
	}
	if (Object && GEditor)
	{
		TArray<UObject*> Objects{Object};
		GEditor->SyncBrowserToObjects(Objects);
	}
	else
	{
		FMessageDialog::Open(EAppMsgType::Ok, Label(TEXT("Could not locate the recorded asset: ") + Path));
	}
}
void AssetRow(FDetailWidgetRow& Row, const FText& Name, const FString& Path, FString DisplayName)
{
	const bool bHasPath = !Path.IsEmpty() && Path != TEXT("None");
	if (DisplayName.IsEmpty() && bHasPath)
	{
		DisplayName = FSoftObjectPath(Path).GetAssetName();
	}
	DisplayName.RemoveFromEnd(TEXT("_C"));
	FillRow(Row, Name, AssetLink(Path, DisplayName));
	SetDetailCopyActions(Row, Name.ToString(), bHasPath ? Path : DisplayName, DisplayName);
	if (!bHasPath)
	{
		return;
	}
	Row.AddCustomContextMenuAction(FUIAction(FExecuteAction::CreateLambda([DisplayName]() {
		FPlatformApplicationMisc::ClipboardCopy(*DisplayName);
	})),
	    Label(TEXT("Copy asset name")));
	const FUIAction Open(FExecuteAction::CreateLambda([Path]() { OpenAsset(Path); }));
	Row.AddCustomContextMenuAction(Open, Label(TEXT("Open asset / source")), Label(Path));
	const auto Actions = SNew(SHorizontalBox);
	if (!Path.StartsWith(TEXT("/Script/")))
	{
		Row.AddCustomContextMenuAction(
		    FUIAction(FExecuteAction::CreateLambda([Path]() { BrowseAsset(Path); })),
		    Label(TEXT("Browse to asset")), Label(Path));
		Actions->AddSlot().AutoWidth()[DetailIconButton(TEXT("Icons.BrowseContent"),
		    Label(TEXT("Browse to asset in the Content Browser")), FOnClicked::CreateLambda([Path]() {
			    BrowseAsset(Path);
			    return FReply::Handled();
		    }))];
	}
	Row.ExtensionContent()[Actions];
}
FString RawValue(const FProperty* Property, const void* Value, bool bDelimited = false)
{
	if (const auto* String = CastField<FStrProperty>(Property); String && !bDelimited)
	{
		return String->GetPropertyValue(Value);
	}
	if (const auto* Number = CastField<FNumericProperty>(Property); Number && Number->IsFloatingPoint())
	{
		const double NumericValue = Number->GetFloatingPointPropertyValue(Value);
		if (CastField<FFloatProperty>(Property))
		{
			return FString::Printf(TEXT("%.9g"), NumericValue);
		}
		return FString::Printf(TEXT("%.17g"), NumericValue);
	}
	if (const auto* Struct = CastField<FStructProperty>(Property))
	{
		FString Result = TEXT("(");
		for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
		{
			if (Result.Len() > 1)
			{
				Result += TEXT(",");
			}
			Result +=
			    It->GetName() + TEXT("=") + RawValue(*It, It->ContainerPtrToValuePtr<void>(Value), true);
		}
		return Result + TEXT(")");
	}
	if (const auto* Array = CastField<FArrayProperty>(Property))
	{
		FString Result = TEXT("(");
		FScriptArrayHelper Items(Array, Value);
		for (int32 Index = 0; Index < Items.Num(); ++Index)
		{
			if (Index)
			{
				Result += TEXT(",");
			}
			Result += RawValue(Array->Inner, Items.GetRawPtr(Index), true);
		}
		return Result + TEXT(")");
	}
	FString Result;
	Property->ExportTextItem_Direct(
	    Result, Value, nullptr, nullptr, PPF_Copy | (bDelimited ? PPF_Delimited : 0));
	return Result;
}
FString Display(const FProperty* Property, const void* Value)
{
	if (const auto* String = CastField<FStrProperty>(Property))
	{
		FString Result = String->GetPropertyValue(Value);
		if (Result.StartsWith(TEXT("EIMGTraversal")))
		{
			int32 Colon;
			if (Result.FindLastChar(TEXT(':'), Colon))
			{
				Result.RightChopInline(Colon + 1);
			}
		}
		return Result;
	}
	if (const auto* Enum = CastField<FEnumProperty>(Property))
	{
		return Enum->GetEnum()
		    ->GetDisplayNameTextByValue(Enum->GetUnderlyingProperty()->GetSignedIntPropertyValue(Value))
		    .ToString();
	}
	if (const auto* Number = CastField<FNumericProperty>(Property); Number && Number->IsFloatingPoint())
	{
		FNumberFormattingOptions Options;
		Options.SetUseGrouping(false).SetMaximumFractionalDigits(3);
		return FText::AsNumber(Number->GetFloatingPointPropertyValue(Value), &Options).ToString();
	}
	if (const auto* Structure = CastField<FStructProperty>(Property))
	{
		if (Structure->Struct == TBaseStructure<FVector>::Get())
		{
			const auto& V = *static_cast<const FVector*>(Value);
			return FString::Printf(TEXT("X=%.2f Y=%.2f Z=%.2f"), V.X, V.Y, V.Z);
		}
		return TEXT("Expand to inspect");
	}
	if (const auto* Array = CastField<FArrayProperty>(Property))
	{
		return FString::Printf(TEXT("%d entries"), FScriptArrayHelper(Array, Value).Num());
	}
	FString Result;
	Property->ExportTextItem_Direct(Result, Value, nullptr, nullptr, PPF_None);
	return Result;
}
FDetailWidgetRow& AddRow(IDetailCategoryBuilder& Owner, const FText& Search)
{
	return Owner.AddCustomRow(Search);
}
FDetailWidgetRow& AddRow(IDetailGroup& Owner, const FText& Search)
{
	return Owner.AddWidgetRow().FilterString(Search);
}

template <typename OwnerType>
void Fields(OwnerType& Owner, const UStruct* Type, const void* Data, const TArray<FName>& Names = {});

template <typename OwnerType>
void PropertyRow(OwnerType& Owner, const FProperty* Property, const void* Value, FName Key, const FText& Name)
{
	const FString ValueText = Display(Property, Value);
	const FString Raw = RawValue(Property, Value);
	if (const auto* Struct = CastField<FStructProperty>(Property))
	{
		auto& Group = Owner.AddGroup(Key, Name);
		ValueRow(Group.HeaderRow(), Name, ValueText, Raw, Key);
		Fields(Group, Struct->Struct, Value);
	}
	else if (const auto* Array = CastField<FArrayProperty>(Property))
	{
		auto& Group = Owner.AddGroup(Key, Name);
		ValueRow(Group.HeaderRow(), Name, ValueText, Raw, Key);
		FScriptArrayHelper Items(Array, Value);
		for (int32 Index = 0; Index < Items.Num(); ++Index)
		{
			PropertyRow(Group, Array->Inner, Items.GetRawPtr(Index), FName(*LexToString(Index)),
			    Label(LexToString(Index)));
		}
	}
	else
	{
		ValueRow(AddRow(Owner, Label(Name.ToString() + TEXT(" ") + ValueText)), Name, ValueText, Raw, Key);
	}
}

template <typename OwnerType>
void Fields(OwnerType& Owner, const UStruct* Type, const void* Data, const TArray<FName>& Names)
{
	for (TFieldIterator<FProperty> It(Type); It; ++It)
	{
		const FProperty* Property = *It;
		if (!Names.IsEmpty() && !Names.Contains(Property->GetFName()))
		{
			continue;
		}
		if (Type == FIMGTraversalDebugFrame::StaticStruct() &&
		    (Property->GetFName() == TEXT("Role") || Property->GetFName() == TEXT("NetMode")))
		{
			const bool bRole = Property->GetFName() == TEXT("Role");
			const auto* Frame = static_cast<const FIMGTraversalDebugFrame*>(Data);
			const TCHAR* Roles[] = {
			    TEXT("None"), TEXT("Simulated Proxy"), TEXT("Autonomous Proxy"), TEXT("Authority")};
			const TCHAR* Modes[] = {
			    TEXT("Standalone"), TEXT("Dedicated Server"), TEXT("Listen Server"), TEXT("Client")};
			const int32 Index = bRole ? Frame->Role : Frame->NetMode;
			const FString Name =
			    Index >= 0 && Index < 4 ? (bRole ? Roles[Index] : Modes[Index]) : LexToString(Index);
			ValueRow(AddRow(Owner, Label(Name)), Property->GetDisplayNameText(), Name, LexToString(Index),
			    Property->GetFName());
			continue;
		}
		PropertyRow(Owner, Property, Property->ContainerPtrToValuePtr<void>(Data), Property->GetFName(),
		    Property->GetDisplayNameText());
	}
}

class FRecords final : public IDetailCustomNodeBuilder
{
public:
	FRecords(TSharedRef<const FIMGTraversalDebugFrame> InFrame, int32 Lane, FName InKind,
	    TSharedRef<TMap<FName, int32>> InPages)
	    : Frame(InFrame), Kind(InKind), Pages(InPages)
	{
		if (Kind == TEXT("Events"))
		{
			for (int32 Index = 0; Index < Frame->Events.Num(); ++Index)
			{
				const auto& Event = Frame->Events[Index];
				if (IsInspectorEvent(Event) &&
				    (Lane < 0 ? IsTimelineEvent(Event) : GetEventLane(Event) == Lane))
				{
					Indices.Add(Index);
				}
			}
		}
		else
		{
			for (int32 Index = 0; Index < Frame->Candidates.Num(); ++Index)
			{
				const auto Phase = Frame->Candidates[Index].Phase;
				const int32 Target =
				    Phase == EIMGTraversalDebugPhase::Execution || Phase == EIMGTraversalDebugPhase::Placement
				    ? 2
				    : 1;
				if (Lane < 0 || Lane == Target)
				{
					Indices.Add(Index);
				}
			}
		}
		Pages->FindOrAdd(Kind) = FMath::Clamp(Page(), 0, FMath::Max(0, (Indices.Num() - 1) / PageSize));
	}
	virtual FName GetName() const override
	{
		return Kind;
	}
	bool HasRecords() const
	{
		return !Indices.IsEmpty();
	}
	virtual void SetOnRebuildChildren(FSimpleDelegate Delegate) override
	{
		Rebuild = Delegate;
	}
	virtual void GenerateHeaderRowContent(FDetailWidgetRow& Row) override
	{
		Row.NameContent()[Text(Label(FString::Printf(TEXT("%s (%d)"), *Kind.ToString(), Indices.Num())))];
		Row.ValueContent()[SNew(SHorizontalBox) +
		    SHorizontalBox::Slot().AutoWidth()[SNew(SButton)
		            .Text(Label(TEXT("Previous")))
		            .Visibility(Indices.Num() > PageSize ? EVisibility::Visible : EVisibility::Collapsed)
		            .IsEnabled_Lambda([this]() { return Page() > 0; })
		            .OnClicked_Lambda([this]() {
			            --Pages->FindOrAdd(Kind);
			            Rebuild.ExecuteIfBound();
			            return FReply::Handled();
		            })] +
		    SHorizontalBox::Slot().AutoWidth().Padding(
		        6, 0)[SNew(STextBlock).Font(IDetailLayoutBuilder::GetDetailFont()).Text_Lambda([this]() {
			    return Label(Indices.Num() ? FString::Printf(TEXT("%d - %d"), Page() * PageSize + 1,
			                                     FMath::Min((Page() + 1) * PageSize, Indices.Num()))
			                               : TEXT("No records in this frame"));
		    })] +
		    SHorizontalBox::Slot().AutoWidth()[SNew(SButton)
		            .Text(Label(TEXT("Next")))
		            .Visibility(Indices.Num() > PageSize ? EVisibility::Visible : EVisibility::Collapsed)
		            .IsEnabled_Lambda([this]() { return (Page() + 1) * PageSize < Indices.Num(); })
		            .OnClicked_Lambda([this]() {
			            ++Pages->FindOrAdd(Kind);
			            Rebuild.ExecuteIfBound();
			            return FReply::Handled();
		            })]];
		Row.NameContent().Widget->SetToolTipText(Label(TEXT(
		    "Expand records to inspect their fields. Large collections use pages; Details search applies to the current page. Text export includes every record.")));
	}
	virtual void GenerateChildContent(IDetailChildrenBuilder& Builder) override
	{
		for (int32 Item = Page() * PageSize; Item < FMath::Min((Page() + 1) * PageSize, Indices.Num());
		    ++Item)
		{
			const int32 Index = Indices[Item];
			FString Title;
			FString Summary;
			const UStruct* Type;
			const void* Data;
			if (Kind == TEXT("Events"))
			{
				const auto& Event = Frame->Events[Index];
				Title = Event.Name;
				Summary = FString::Printf(TEXT("%.3f s | Attempt %lld | %s"), Event.Time, Event.Attempt,
				    *UEnum::GetDisplayValueAsText(Event.Phase).ToString());
				Type = FIMGTraversalDebugEvent::StaticStruct();
				Data = &Event;
			}
			else
			{
				const auto& Candidate = Frame->Candidates[Index];
				Title = FString::Printf(TEXT("Attempt %lld | %s"), Candidate.Attempt, *Candidate.Label);
				Summary = FString::Printf(
				    TEXT("Height %.1f / Depth %.1f cm"), Candidate.ObstacleHeight, Candidate.ObstacleDepth);
				Type = FIMGTraversalDebugCandidate::StaticStruct();
				Data = &Candidate;
			}
			auto& Group = Builder.AddGroup(FName(*LexToString(Index)), Label(Title));
			FString Json;
			const bool bHasJson = RecordToJson(Type, Data, Json);
			const FString FullValue = bHasJson ? MoveTemp(Json) : Summary;
			auto& Header = Group.HeaderRow();
			FillRow(Header, Label(Title), ReadOnlyValue(Label(Title), Summary, FullValue));
			SetDetailCopyActions(Header, Title, FullValue, Summary);
			if (bHasJson)
			{
				Header.NameContent().Widget->SetToolTipText(
				    Label(TEXT("Right-click Copy to copy this complete record as JSON.")));
			}
			TArray<FName> FieldNames;
			if (Kind == TEXT("Events"))
			{
				for (TFieldIterator<FProperty> It(Type); It; ++It)
				{
					if (It->GetFName() != TEXT("Name") && It->GetFName() != TEXT("Stage"))
					{
						FieldNames.Add(It->GetFName());
					}
				}
			}
			Fields(Group, Type, Data, FieldNames);
		}
	}

private:
	int32 Page() const
	{
		return Pages->FindRef(Kind);
	}
	static constexpr int32 PageSize = 32;
	TSharedRef<const FIMGTraversalDebugFrame> Frame;
	FName Kind;
	TSharedRef<TMap<FName, int32>> Pages;
	TArray<int32> Indices;
	FSimpleDelegate Rebuild;
};

class FCustomization final : public IDetailCustomization
{
public:
	FCustomization(TSharedPtr<const FIMGTraversalDebugFrame> InFrame, int32 InLane,
	    TSharedRef<TMap<FName, int32>> InPages)
	    : Frame(InFrame), Lane(InLane), Pages(InPages)
	{
	}
	virtual void CustomizeDetails(IDetailLayoutBuilder& Builder) override
	{
		for (TFieldIterator<FProperty> It(FIMGTraversalDebugFrame::StaticStruct()); It; ++It)
		{
			Builder.HideProperty(It->GetFName(), FIMGTraversalDebugFrame::StaticStruct());
		}
		auto& Overview = Builder.EditCategory(TEXT("Traversal"));
		Overview.SetSortOrder(0);
		if (!Frame)
		{
			FillRow(Overview.AddCustomRow(Label(TEXT("No data"))), Label(TEXT("Status")),
			    Text(Label(TEXT("No Traversal sample at this time."))));
			return;
		}
		ValueRow(
		    Overview.AddCustomRow(Label(Frame->Actor)), Label(TEXT("Actor")), Frame->Actor, Frame->Actor);
		ValueRow(Overview.AddCustomRow(Label(TEXT("State"))), Label(TEXT("State")),
		    Frame->bActive ? TEXT("Active") : TEXT("Inactive / last known state"),
		    Frame->bActive ? TEXT("True") : TEXT("False"));
		AssetRow(Overview.AddCustomRow(Label(TEXT("GA Ability ") + Frame->AbilityClassPath)),
		    Label(TEXT("Gameplay Ability")), Frame->AbilityClassPath, FString());
		AssetRow(Overview.AddCustomRow(
		             Label(TEXT("Montage ") + Frame->Montage + TEXT(" ") + Frame->MontageAssetPath)),
		    Label(TEXT("Montage")), Frame->MontageAssetPath, Frame->Montage);
		auto Group = [&Builder, this](FName Name, const TArray<FName>& Properties, bool bCollapsed = false) {
			auto& Category = Builder.EditCategory(Name);
			Category.InitiallyCollapsed(bCollapsed);
			Fields(Category, FIMGTraversalDebugFrame::StaticStruct(), Frame.Get(), Properties);
		};
		Group(TEXT("Identity"),
		    {TEXT("World"), TEXT("Role"), TEXT("NetMode"), TEXT("AbilitySpec"), TEXT("PredictionKey"),
		        TEXT("Sequence"), TEXT("MontageInstance")},
		    Lane != 3);
		if (Lane < 0 || Lane == 0)
		{
			Group(TEXT("Request"), {TEXT("Attempt"), TEXT("Source"), TEXT("Direction")});
		}
		if (Lane < 0 || Lane == 1)
		{
			TArray<FName> DetectionFields = {TEXT("Failure"), TEXT("ObstacleHeight"), TEXT("ObstacleDepth"),
			    TEXT("DropHeight"), TEXT("HorizontalClearance"), TEXT("VerticalClearance"),
			    TEXT("DetectionMilliseconds"), TEXT("Queries")};
			if (Lane == 1)
			{
				DetectionFields.Add(TEXT("Attempt"));
				DetectionFields.Add(TEXT("Source"));
				DetectionFields.Add(TEXT("Direction"));
			}
			Group(TEXT("Detection"), DetectionFields);
		}
		if (Lane < 0 || Lane == 2)
		{
			Group(TEXT("Execution"),
			    {TEXT("Action"), TEXT("MontagePosition"), TEXT("MontageLength"), TEXT("MontagePhase")});
			Group(TEXT("Movement"), {TEXT("Location"), TEXT("Velocity"), TEXT("MovementMode")}, Lane < 0);
			if (Frame->bHasHeightSample)
			{
				Group(TEXT("Height Comparison"),
				    {TEXT("HeightReferenceZ"), TEXT("MontageRootHeight"), TEXT("WarpedCapsuleHeight")});
			}
			else
			{
				auto& Height = Builder.EditCategory(TEXT("Height Comparison"));
				ValueRow(Height.AddCustomRow(Label(TEXT("Height availability"))), Label(TEXT("Status")),
				    TEXT("No recorded start / height sample"), TEXT("No recorded start / height sample"));
			}
		}
		const auto Events = MakeShared<FRecords>(Frame.ToSharedRef(), Lane, TEXT("Events"), Pages);
		if (Events->HasRecords())
		{
			Builder.EditCategory(TEXT("Events")).AddCustomBuilder(Events);
		}
		if (Lane < 0 || Lane == 1 || Lane == 2)
		{
			const auto Candidates =
			    MakeShared<FRecords>(Frame.ToSharedRef(), Lane, TEXT("Candidates"), Pages);
			if (Candidates->HasRecords())
			{
				Builder.EditCategory(TEXT("Candidates")).AddCustomBuilder(Candidates);
			}
		}
		Group(TEXT("Trace"),
		    {TEXT("Time"), TEXT("DeltaSeconds"), TEXT("Cycle"), TEXT("ActorId"), TEXT("Frame"),
		        TEXT("Dropped"), TEXT("HeightCapture")},
		    true);
	}

private:
	TSharedPtr<const FIMGTraversalDebugFrame> Frame;
	int32 Lane;
	TSharedRef<TMap<FName, int32>> Pages;
};
} // namespace
TSharedRef<IDetailCustomization> MakeTraversalDetailsCustomization(
    TSharedPtr<const FIMGTraversalDebugFrame> Frame, int32 Lane, TSharedRef<TMap<FName, int32>> Pages)
{
	return MakeShared<FCustomization>(MoveTemp(Frame), Lane, Pages);
}
} // namespace IMGTraversalRewind
