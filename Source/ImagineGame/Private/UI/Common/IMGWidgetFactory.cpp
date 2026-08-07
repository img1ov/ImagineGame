#include "UI/Common/IMGWidgetFactory.h"
#include "Templates/SubclassOf.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGWidgetFactory)

class UUserWidget;

TSubclassOf<UUserWidget> UIMGWidgetFactory::FindWidgetClassForData_Implementation(const UObject* Data) const
{
	return TSubclassOf<UUserWidget>();
}
