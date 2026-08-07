#include "UI/Foundation/IMGLoadingScreenSubsystem.h"

#include "Blueprint/UserWidget.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGLoadingScreenSubsystem)

class UUserWidget;

//////////////////////////////////////////////////////////////////////
// UIMGLoadingScreenSubsystem

UIMGLoadingScreenSubsystem::UIMGLoadingScreenSubsystem()
{
}

void UIMGLoadingScreenSubsystem::SetLoadingScreenContentWidget(TSubclassOf<UUserWidget> NewWidgetClass)
{
	if (LoadingScreenWidgetClass != NewWidgetClass)
	{
		LoadingScreenWidgetClass = NewWidgetClass;

		OnLoadingScreenWidgetChanged.Broadcast(LoadingScreenWidgetClass);
	}
}

TSubclassOf<UUserWidget> UIMGLoadingScreenSubsystem::GetLoadingScreenContentWidget() const
{
	return LoadingScreenWidgetClass;
}

