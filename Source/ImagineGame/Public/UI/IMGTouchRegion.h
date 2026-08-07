#pragma once

#include "UI/IMGSimulatedInputWidget.h"

#include "IMGTouchRegion.generated.h"

#define UE_API IMAGINEGAME_API

class UObject;
struct FFrame;
struct FGeometry;
struct FPointerEvent;

/**
 * A "Touch Region" is used to define an area on the screen that should trigger some
 * input when the user presses a finger on it
 */
UCLASS(MinimalAPI, meta=( DisplayName="IMG Touch Region" ))
class UIMGTouchRegion : public UIMGSimulatedInputWidget
{
	GENERATED_BODY()
	
public:
	
	//~ Begin UUserWidget
	UE_API virtual FReply NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
	UE_API virtual FReply NativeOnTouchMoved(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
	UE_API virtual FReply NativeOnTouchEnded(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
	UE_API virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	//~ End UUserWidget interface

	UFUNCTION(BlueprintCallable)
	bool ShouldSimulateInput() const { return bShouldSimulateInput; }

protected:

	/** True while this widget is being touched */
	bool bShouldSimulateInput = false;
};

#undef UE_API
