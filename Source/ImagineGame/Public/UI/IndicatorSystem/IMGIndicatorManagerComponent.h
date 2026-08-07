#pragma once

#include "Components/ControllerComponent.h"

#include "IMGIndicatorManagerComponent.generated.h"

#define UE_API IMAGINEGAME_API

class AController;
class UIndicatorDescriptor;
class UObject;
struct FFrame;

/**
 * @class UIMGIndicatorManagerComponent
 */
UCLASS(MinimalAPI, BlueprintType, Blueprintable)
class UIMGIndicatorManagerComponent : public UControllerComponent
{
	GENERATED_BODY()

public:
	UE_API UIMGIndicatorManagerComponent(const FObjectInitializer& ObjectInitializer);

	static UE_API UIMGIndicatorManagerComponent* GetComponent(AController* Controller);

	UFUNCTION(BlueprintCallable, Category = Indicator)
	UE_API void AddIndicator(UIndicatorDescriptor* IndicatorDescriptor);
	
	UFUNCTION(BlueprintCallable, Category = Indicator)
	UE_API void RemoveIndicator(UIndicatorDescriptor* IndicatorDescriptor);

	DECLARE_EVENT_OneParam(UIMGIndicatorManagerComponent, FIndicatorEvent, UIndicatorDescriptor* Descriptor)
	FIndicatorEvent OnIndicatorAdded;
	FIndicatorEvent OnIndicatorRemoved;

	const TArray<UIndicatorDescriptor*>& GetIndicators() const { return Indicators; }

private:
	UPROPERTY()
	TArray<TObjectPtr<UIndicatorDescriptor>> Indicators;
};

#undef UE_API
