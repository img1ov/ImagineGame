#include "IMGWorldCollectable.h"

#include "Async/TaskGraphInterfaces.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGWorldCollectable)

struct FInteractionQuery;

AIMGWorldCollectable::AIMGWorldCollectable()
{
}

void AIMGWorldCollectable::GatherInteractionOptions(const FInteractionQuery& InteractQuery, FInteractionOptionBuilder& InteractionBuilder)
{
	InteractionBuilder.AddInteractionOption(Option);
}

FInventoryPickup AIMGWorldCollectable::GetPickupInventory() const
{
	return StaticInventory;
}
