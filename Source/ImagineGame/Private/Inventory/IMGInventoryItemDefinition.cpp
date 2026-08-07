// Fill out your copyright notice in the Description page of Project Settings.


#include "Inventory/IMGInventoryItemDefinition.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGInventoryItemDefinition)

//////////////////////////////////////////////////////////////////////
// UIMGInventoryItemDefinition

UIMGInventoryItemDefinition::UIMGInventoryItemDefinition(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

const UIMGInventoryItemFragment* UIMGInventoryItemDefinition::FindFragmentByClass(
	TSubclassOf<UIMGInventoryItemFragment> FragmentClass) const
{
	if (FragmentClass != nullptr)
	{
		for (UIMGInventoryItemFragment* Fragment : Fragments)
		{
			if (Fragment && Fragment->IsA(FragmentClass))
			{
				return Fragment;
			}
		}
	}
	
	return nullptr;
}

//////////////////////////////////////////////////////////////////////
// UIMGInventoryItemDefinition

const UIMGInventoryItemFragment* UIMGInventoryFunctionLibrary::FindItemDefinitionFragment(
	TSubclassOf<UIMGInventoryItemDefinition> ItemDef, TSubclassOf<UIMGInventoryItemFragment> FragmentClass)
{
	if ((ItemDef != nullptr) && (FragmentClass != nullptr))
	{
		return GetDefault<UIMGInventoryItemDefinition>(ItemDef)->FindFragmentByClass(FragmentClass);
	}
	return nullptr;
}
