#pragma once

#include "Input/CommonBoundActionButton.h"

#include "IMGBoundActionButton.generated.h"

#define UE_API IMAGINEGAME_API

class UCommonButtonStyle;
class UObject;

/**
 * 
 */
UCLASS(MinimalAPI, Abstract, meta = (DisableNativeTick))
class UIMGBoundActionButton : public UCommonBoundActionButton
{
	GENERATED_BODY()
	
protected:
	UE_API virtual void NativeConstruct() override;

private:
	void HandleInputMethodChanged(ECommonInputType NewInputMethod);

	UPROPERTY(EditAnywhere, Category = "Styles")
	TSubclassOf<UCommonButtonStyle> KeyboardStyle;

	UPROPERTY(EditAnywhere, Category = "Styles")
	TSubclassOf<UCommonButtonStyle> GamepadStyle;

	UPROPERTY(EditAnywhere, Category = "Styles")
	TSubclassOf<UCommonButtonStyle> TouchStyle;
};

#undef UE_API
