#pragma once

#include "Inventory/IMGInventoryItemDefinition.h"

#include "InventoryFragment_ReticleConfig.generated.h"

class UIMGReticleWidgetBase;
class UObject;

UCLASS()
class UInventoryFragment_ReticleConfig : public UIMGInventoryItemFragment
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Reticle)
	TArray<TSubclassOf<UIMGReticleWidgetBase>> ReticleWidgets;
};
