#pragma once
#include "CoreMinimal.h"
#include "Framework/SlateDelegates.h"
class SWidget;
class FDetailWidgetRow;

namespace IMGTraversalRewind
{
TSharedRef<SWidget> ReadOnlyText(TAttribute<FText> Value, bool bWrap = false);
TSharedRef<SWidget> ReadOnlyValue(const FText& Name, const FString& Display, const FString& Raw);
TSharedRef<SWidget> DetailIconButton(FName Icon, const FText& Tooltip, FOnClicked Action);
void SetDetailCopyActions(
    FDetailWidgetRow& Row, const FString& Name, const FString& Raw, const FString& Display);
} // namespace IMGTraversalRewind
