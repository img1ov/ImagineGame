#include "AbilitySystem/IMGGameplayEffectContext.h"

#include "AbilitySystem/IMGAbilitySourceInterface.h"
#include "Engine/HitResult.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

#include "Iris/ReplicationState/PropertyNetSerializerInfoRegistry.h"
#include "Serialization/GameplayEffectContextNetSerializer.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGGameplayEffectContext)

class FArchive;

FIMGGameplayEffectContext* FIMGGameplayEffectContext::ExtractEffectContext(struct FGameplayEffectContextHandle Handle)
{
	FGameplayEffectContext* BaseEffectContext = Handle.Get();
	if ((BaseEffectContext != nullptr) && BaseEffectContext->GetScriptStruct()->IsChildOf(FIMGGameplayEffectContext::StaticStruct()))
	{
		return static_cast<FIMGGameplayEffectContext*>(BaseEffectContext);
	}
	
	return nullptr;
}

void FIMGGameplayEffectContext::SetAbilitySource(const IIMGAbilitySourceInterface* InObject, float InSourceLevel)
{
	AbilitySourceObject = MakeWeakObjectPtr(Cast<const UObject>(InObject));
	//SourceLevel = InSourceLevel;
}

const IIMGAbilitySourceInterface* FIMGGameplayEffectContext::GetAbilitySource() const
{
	return Cast<IIMGAbilitySourceInterface>(AbilitySourceObject.Get());
}

bool FIMGGameplayEffectContext::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	FGameplayEffectContext::NetSerialize(Ar, Map, bOutSuccess);

	// Not serialized for post-activation use:
	// CartridgeID

	return true;
}

namespace UE::Net
{
	// Forward to FGameplayEffectContextNetSerializer
	// Note: If FIMGGameplayEffectContext::NetSerialize() is modified, a custom NetSerializer must be implemented as the current fallback will no longer be sufficient.
	UE_NET_IMPLEMENT_FORWARDING_NETSERIALIZER_AND_REGISTRY_DELEGATES(IMGGameplayEffectContext, FGameplayEffectContextNetSerializer);
}

const UPhysicalMaterial* FIMGGameplayEffectContext::GetPhysicalMaterial() const
{
	if (const FHitResult* HitResultPtr = GetHitResult())
	{
		return HitResultPtr->PhysMaterial.Get();
	}
	
	return nullptr;
}
