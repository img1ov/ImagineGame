#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"

#include "IndicatorLibrary.generated.h"

#define UE_API IMAGINEGAME_API

class AController;
class UIMGIndicatorManagerComponent;
class UObject;
struct FFrame;

UCLASS(MinimalAPI)
class UIndicatorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UE_API UIndicatorLibrary();
	
	/**  */
	UFUNCTION(BlueprintCallable, Category = Indicator)
	static UE_API UIMGIndicatorManagerComponent* GetIndicatorManagerComponent(AController* Controller);
};

#undef UE_API
