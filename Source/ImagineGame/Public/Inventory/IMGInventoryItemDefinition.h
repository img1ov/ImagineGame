#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"

#include "IMGInventoryItemDefinition.generated.h"

template <typename T> class TSubclassOf;

class UIMGInventoryItemInstance;
struct FFrame;

//////////////////////////////////////////////////////////////////////


// Represents a fragment of an item definition
UCLASS(MinimalAPI, DefaultToInstanced, EditInlineNew, Abstract)
class UIMGInventoryItemFragment : public UObject
{
	GENERATED_BODY()

public:
	virtual void OnInstanceCreated(UIMGInventoryItemInstance* Instance) const {}
};

//////////////////////////////////////////////////////////////////////

/**
 * UIMGInventoryItemDefinition
 */
UCLASS(Blueprintable, Const, Abstract)
class UIMGInventoryItemDefinition : public UObject
{
	GENERATED_BODY()
	
public:
	UIMGInventoryItemDefinition(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=Display)
	FText DisplayName;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=Display, Instanced)
	TArray<TObjectPtr<UIMGInventoryItemFragment>> Fragments;
	
public:
	const UIMGInventoryItemFragment* FindFragmentByClass(TSubclassOf<UIMGInventoryItemFragment> FragmentClass) const;
};

//@TODO: Make into a subsystem instead?
UCLASS()
class UIMGInventoryFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

	UFUNCTION(BlueprintCallable, meta=(DeterminesOutputType=FragmentClass))
	static const UIMGInventoryItemFragment* FindItemDefinitionFragment(TSubclassOf<UIMGInventoryItemDefinition> ItemDef, TSubclassOf<UIMGInventoryItemFragment> FragmentClass);
};