// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "System/GameplayTagStack.h"
#include "Templates/SubclassOf.h"

#include "IMGInventoryItemInstance.generated.h"

class FLifetimeProperty;

class UIMGInventoryItemDefinition;
class UIMGInventoryItemFragment;
struct FFrame;
struct FGameplayTag;

/**
 * UIMGInventoryItemInstance
 */
UCLASS(BlueprintType)
class UIMGInventoryItemInstance : public UObject
{
	GENERATED_BODY()
	
public:
	UIMGInventoryItemInstance(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	
	//~UObject interface
	virtual bool IsSupportedForNetworking() const override { return true; }
	//~End of UObject interface
	
	// Adds a specified number of stacks to the tag (does nothing if StackCount is below 1)
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category=Inventory)
	void AddStatTagStack(FGameplayTag Tag, int32 StackCount);
	
	// Removes a specified number of stacks from the tag (does nothing if StackCount is below 1)
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category= Inventory)
	void RemoveStatTagStack(FGameplayTag Tag, int32 StackCount);
	
	// Returns the stack count of the specified tag (or 0 if the tag is not present)
	UFUNCTION(BlueprintCallable, Category=Inventory)
	int32 GetStatTagStackCount(FGameplayTag Tag) const;
	
	// Returns true if there is at least one stack of the specified tag
	UFUNCTION(BlueprintCallable, Category=Inventory)
	bool HasStatTag(FGameplayTag Tag) const;
	
	TSubclassOf<UIMGInventoryItemDefinition> GetItemDef() const
	{
		return ItemDef;
	}
	
	UFUNCTION(BlueprintCallable, BlueprintPure=false, meta=(DeterminesOutputType=FragmentClass))
	const UIMGInventoryItemFragment* FindFragmentByClass(TSubclassOf<UIMGInventoryItemFragment> FragmentClass) const;

	template <typename ResultClass>
	const ResultClass* FindFragmentByClass() const
	{
		return (ResultClass*)FindFragmentByClass(ResultClass::StaticClass());
	}
	
private:
	/** Register all replication fragments */
	virtual void RegisterReplicationFragments(UE::Net::FFragmentRegistrationContext& Context, UE::Net::EFragmentRegistrationFlags RegistrationFlags) override;

	void SetItemDef(TSubclassOf<UIMGInventoryItemDefinition> InDef);

	friend struct FIMGInventoryList;

private:
	UPROPERTY(Replicated)
	FGameplayTagStackContainer StatTags;

	// The item definition
	UPROPERTY(Replicated)
	TSubclassOf<UIMGInventoryItemDefinition> ItemDef;
};
