#include "UI/Common/IMGListView.h"
#include "UI/Common/IMGWidgetFactory.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGListView)

#if WITH_EDITOR
#include "Editor/WidgetCompilerLog.h"
#endif

#define LOCTEXT_NAMESPACE "IMGListView"

UIMGListView::UIMGListView(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

#if WITH_EDITOR

void UIMGListView::ValidateCompiledDefaults(IWidgetCompilerLog& InCompileLog) const
{
	Super::ValidateCompiledDefaults(InCompileLog);

	if (FactoryRules.Num() == 0)
	{
		InCompileLog.Error(FText::Format(FText::FromString("{0} has no Factory Rules defined, can't create widgets without them."), FText::FromString(GetName())));
	}
}

#endif

UUserWidget& UIMGListView::OnGenerateEntryWidgetInternal(UObject* Item, TSubclassOf<UUserWidget> DesiredEntryClass, const TSharedRef<STableViewBase>& OwnerTable)
{
	TSubclassOf<UUserWidget> WidgetClass = DesiredEntryClass;
	
	for (const UIMGWidgetFactory* Rule : FactoryRules)
	{
		if (Rule)
		{
			if (const TSubclassOf<UUserWidget> EntryClass = Rule->FindWidgetClassForData(Item))
			{
				WidgetClass = EntryClass;
				break;
			}
		}
	}
	
	UUserWidget& EntryWidget = GenerateTypedEntry<UUserWidget>(WidgetClass, OwnerTable);

	return EntryWidget;
}

#undef LOCTEXT_NAMESPACE
