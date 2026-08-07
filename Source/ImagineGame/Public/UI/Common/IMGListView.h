#pragma once

#include "CommonListView.h"

#include "IMGListView.generated.h"

#define UE_API IMAGINEGAME_API

class UUserWidget;
class ULocalPlayer;
class UIMGWidgetFactory;

UCLASS(MinimalAPI, meta = (DisableNativeTick))
class UIMGListView : public UCommonListView
{
	GENERATED_BODY()

public:
	UE_API UIMGListView(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

#if WITH_EDITOR
	UE_API virtual void ValidateCompiledDefaults(IWidgetCompilerLog& InCompileLog) const override;
#endif

protected:
	UE_API virtual UUserWidget& OnGenerateEntryWidgetInternal(UObject* Item, TSubclassOf<UUserWidget> DesiredEntryClass, const TSharedRef<STableViewBase>& OwnerTable) override;
	//virtual bool OnIsSelectableOrNavigableInternal(UObject* SelectedItem) override;

protected:
	UPROPERTY(EditAnywhere, Instanced, Category="Entry Creation")
	TArray<TObjectPtr<UIMGWidgetFactory>> FactoryRules;
};

#undef UE_API
