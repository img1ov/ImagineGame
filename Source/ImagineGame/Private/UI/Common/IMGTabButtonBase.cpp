#include "UI/Common/IMGTabButtonBase.h"

#include "CommonLazyImage.h"
#include "UI/Common/IMGTabListWidgetBase.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGTabButtonBase)

class UObject;
struct FSlateBrush;

void UIMGTabButtonBase::SetIconFromLazyObject(TSoftObjectPtr<UObject> LazyObject)
{
	if (LazyImage_Icon)
	{
		LazyImage_Icon->SetBrushFromLazyDisplayAsset(LazyObject);
	}
}

void UIMGTabButtonBase::SetIconBrush(const FSlateBrush& Brush)
{
	if (LazyImage_Icon)
	{
		LazyImage_Icon->SetBrush(Brush);
	}
}

void UIMGTabButtonBase::SetTabLabelInfo_Implementation(const FIMGTabDescriptor& TabLabelInfo)
{
	SetButtonText(TabLabelInfo.TabText);
	SetIconBrush(TabLabelInfo.IconBrush);
}

