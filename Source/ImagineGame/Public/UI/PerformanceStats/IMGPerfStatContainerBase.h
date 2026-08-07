#pragma once

#include "CommonUserWidget.h"
#include "Performance/IMGPerformanceStatTypes.h"

#include "IMGPerfStatContainerBase.generated.h"

class UObject;
struct FFrame;

/**
 * UIMGPerfStatsContainerBase
 *
 * Panel that contains a set of UIMGPerfStatWidgetBase widgets and manages
 * their visibility based on user settings.
 */
 UCLASS(Abstract)
class UIMGPerfStatContainerBase : public UCommonUserWidget
{
public:
	UIMGPerfStatContainerBase(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	GENERATED_BODY()

	//~UUserWidget interface
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	//~End of UUserWidget interface

	UFUNCTION(BlueprintCallable)
	void UpdateVisibilityOfChildren();

protected:
	// Are we showing text or graph stats?
	UPROPERTY(EditAnywhere, Category=Display)
	EIMGStatDisplayMode StatDisplayModeFilter = EIMGStatDisplayMode::TextAndGraph;
};
