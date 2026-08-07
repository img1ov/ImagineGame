#include "UI/IndicatorSystem/IndicatorLibrary.h"

#include "UI/IndicatorSystem/IMGIndicatorManagerComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IndicatorLibrary)

class AController;

UIndicatorLibrary::UIndicatorLibrary()
{
}

UIMGIndicatorManagerComponent* UIndicatorLibrary::GetIndicatorManagerComponent(AController* Controller)
{
	return UIMGIndicatorManagerComponent::GetComponent(Controller);
}

