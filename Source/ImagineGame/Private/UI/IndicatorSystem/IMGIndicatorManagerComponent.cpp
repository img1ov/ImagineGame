#include "UI/IndicatorSystem/IMGIndicatorManagerComponent.h"

#include "UI/IndicatorSystem/IndicatorDescriptor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGIndicatorManagerComponent)

UIMGIndicatorManagerComponent::UIMGIndicatorManagerComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bAutoRegister = true;
	bAutoActivate = true;
}

/*static*/ UIMGIndicatorManagerComponent* UIMGIndicatorManagerComponent::GetComponent(AController* Controller)
{
	if (Controller)
	{
		return Controller->FindComponentByClass<UIMGIndicatorManagerComponent>();
	}

	return nullptr;
}

void UIMGIndicatorManagerComponent::AddIndicator(UIndicatorDescriptor* IndicatorDescriptor)
{
	IndicatorDescriptor->SetIndicatorManagerComponent(this);
	OnIndicatorAdded.Broadcast(IndicatorDescriptor);
	Indicators.Add(IndicatorDescriptor);
}

void UIMGIndicatorManagerComponent::RemoveIndicator(UIndicatorDescriptor* IndicatorDescriptor)
{
	if (IndicatorDescriptor)
	{
		ensure(IndicatorDescriptor->GetIndicatorManagerComponent() == this);
	
		OnIndicatorRemoved.Broadcast(IndicatorDescriptor);
		Indicators.Remove(IndicatorDescriptor);
	}
}
