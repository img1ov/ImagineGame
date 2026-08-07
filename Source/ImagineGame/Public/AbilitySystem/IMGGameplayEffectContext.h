#pragma once

#include "GameplayEffectTypes.h"

#include "IMGGameplayEffectContext.generated.h"

class AActor;
class FArchive;
class IIMGAbilitySourceInterface;
class UObject;
class UPhysicalMaterial;

USTRUCT()
struct FIMGGameplayEffectContext : public FGameplayEffectContext
{
	GENERATED_BODY()
	
public:
	
	FIMGGameplayEffectContext()
		: FGameplayEffectContext()
	{}
	
	FIMGGameplayEffectContext(AActor* InInstigator, AActor* EffectCauser)
		: FGameplayEffectContext(InInstigator, EffectCauser)
	{}
	
	/** Returns the wrapped FIMGGameplayEffectContext from the handle, or nullptr if it doesn't exist or is the wrong type */
	static IMAGINEGAME_API FIMGGameplayEffectContext* ExtractEffectContext(struct FGameplayEffectContextHandle Handle);
	
	/** Sets the object used as the ability source */
	void SetAbilitySource(const IIMGAbilitySourceInterface* InObject, float InSourceLevel);
	
	/** Returns the ability source interface associated with the source object. Only valid on the authority. */
	const IIMGAbilitySourceInterface* GetAbilitySource() const;
	
	virtual FGameplayEffectContext* Duplicate() const override
	{
		FIMGGameplayEffectContext* NewContext = new FIMGGameplayEffectContext();
		*NewContext = *this;
		if (GetHitResult())
		{
			// Does a deep copy of the hit result
			NewContext->AddHitResult(*GetHitResult(), true);
		}
		return NewContext;
	}
	
	virtual UScriptStruct* GetScriptStruct() const override
	{
		return FIMGGameplayEffectContext::StaticStruct();
	}
	
	/** Overridden to serialize new fields */
	virtual bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess) override;
	
	/** Returns the physical material from the hit result if there is one */
	const UPhysicalMaterial* GetPhysicalMaterial() const;
	
public:
	
	/** ID to allow the identification of multiple bullets that were part of the same cartridge */
	UPROPERTY()
	int32 CartridgeID = -1;
	
protected:
	
	/** Ability Source object (should implement IIMGAbilitySourceInterface). NOT replicated currently */
	UPROPERTY()
	TWeakObjectPtr<const UObject> AbilitySourceObject;
};

template<>
struct TStructOpsTypeTraits<FIMGGameplayEffectContext> : public TStructOpsTypeTraitsBase2<FIMGGameplayEffectContext>
{
	enum
	{
		WithNetSerializer = true,
		WithCopy = true
	};
};
