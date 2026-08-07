#pragma once

#include "UObject/Object.h"

#include "IMGWidgetFactory.generated.h"

#define UE_API IMAGINEGAME_API

template <class TClass> class TSubclassOf;

class UUserWidget;
struct FFrame;

UCLASS(MinimalAPI, Abstract, Blueprintable, BlueprintType, EditInlineNew)
class UIMGWidgetFactory : public UObject
{
	GENERATED_BODY()

public:
	UIMGWidgetFactory() { }

	UFUNCTION(BlueprintNativeEvent)
	UE_API TSubclassOf<UUserWidget> FindWidgetClassForData(const UObject* Data) const;
};

#undef UE_API
