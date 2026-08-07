#include "UI/PerformanceStats/IMGPerfStatContainerBase.h"

#include "Blueprint/WidgetTree.h"
#include "UI/PerformanceStats/IMGPerfStatWidgetBase.h"
#include "Settings/IMGSettingsLocal.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGPerfStatContainerBase)

//////////////////////////////////////////////////////////////////////
// UIMGPerfStatsContainerBase

UIMGPerfStatContainerBase::UIMGPerfStatContainerBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UIMGPerfStatContainerBase::NativeConstruct()
{
	Super::NativeConstruct();
	UpdateVisibilityOfChildren();

	UIMGSettingsLocal::Get()->OnPerfStatDisplayStateChanged().AddUObject(this, &ThisClass::UpdateVisibilityOfChildren);
}

void UIMGPerfStatContainerBase::NativeDestruct()
{
	UIMGSettingsLocal::Get()->OnPerfStatDisplayStateChanged().RemoveAll(this);

	Super::NativeDestruct();
}

void UIMGPerfStatContainerBase::UpdateVisibilityOfChildren()
{
	UIMGSettingsLocal* UserSettings = UIMGSettingsLocal::Get();

	const bool bShowTextWidgets = (StatDisplayModeFilter == EIMGStatDisplayMode::TextOnly) || (StatDisplayModeFilter == EIMGStatDisplayMode::TextAndGraph);
	const bool bShowGraphWidgets = (StatDisplayModeFilter == EIMGStatDisplayMode::GraphOnly) || (StatDisplayModeFilter == EIMGStatDisplayMode::TextAndGraph);
	
	check(WidgetTree);
	WidgetTree->ForEachWidget([&](UWidget* Widget)
	{
		if (UIMGPerfStatWidgetBase* TypedWidget = Cast<UIMGPerfStatWidgetBase>(Widget))
		{
			const EIMGStatDisplayMode SettingMode = UserSettings->GetPerfStatDisplayState(TypedWidget->GetStatToDisplay());

			bool bShowWidget = false;
			switch (SettingMode)
			{
			case EIMGStatDisplayMode::Hidden:
				bShowWidget = false;
				break;
			case EIMGStatDisplayMode::TextOnly:
				bShowWidget = bShowTextWidgets;
				break;
			case EIMGStatDisplayMode::GraphOnly:
				bShowWidget = bShowGraphWidgets;
				break;
			case EIMGStatDisplayMode::TextAndGraph:
				bShowWidget = bShowTextWidgets || bShowGraphWidgets;
				break;
			}

			TypedWidget->SetVisibility(bShowWidget ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		}
	});
}

