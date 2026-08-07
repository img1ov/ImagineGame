#pragma once

#include "IMGWidgetFactory.h"
#include "Templates/SubclassOf.h"
#include "UObject/SoftObjectPtr.h"

#include "IMGWidgetFactory_Class.generated.h"

#define UE_API IMAGINEGAME_API

class UObject;
class UUserWidget;

UCLASS(MinimalAPI)
class UIMGWidgetFactory_Class : public UIMGWidgetFactory
{
	GENERATED_BODY()

public:
	UIMGWidgetFactory_Class() { }

	UE_API virtual TSubclassOf<UUserWidget> FindWidgetClassForData_Implementation(const UObject* Data) const override;
	
protected:
	UPROPERTY(EditAnywhere, Category = ListEntries, meta = (AllowAbstract))
	TMap<TSoftClassPtr<UObject>, TSubclassOf<UUserWidget>> EntryWidgetForClass;
};

#undef UE_API
