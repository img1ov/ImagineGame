// Fill out your copyright notice in the Description page of Project Settings.


#include "Equipment/IMGGameplayAbility_FromEquipment.h"

#include "Equipment/IMGEquipmentInstance.h"
#include "Inventory/IMGInventoryItemInstance.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGGameplayAbility_FromEquipment)


UIMGGameplayAbility_FromEquipment::UIMGGameplayAbility_FromEquipment(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UIMGEquipmentInstance* UIMGGameplayAbility_FromEquipment::GetAssociatedEquipment() const
{
	if (FGameplayAbilitySpec* Spec = UGameplayAbility::GetCurrentAbilitySpec())
	{
		return Cast<UIMGEquipmentInstance>(Spec->SourceObject.Get());
	}

	return nullptr;
}

UIMGInventoryItemInstance* UIMGGameplayAbility_FromEquipment::GetAssociatedItem() const
{
	if (const UIMGEquipmentInstance* Equipment = GetAssociatedEquipment())
	{
		return Cast<UIMGInventoryItemInstance>(Equipment->GetInstigator());
	}
	return nullptr;
}


#if WITH_EDITOR
EDataValidationResult UIMGGameplayAbility_FromEquipment::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
		if (InstancingPolicy == EGameplayAbilityInstancingPolicy::NonInstanced)
			PRAGMA_ENABLE_DEPRECATION_WARNINGS
	{
		Context.AddError(NSLOCTEXT("IMG", "EquipmentAbilityMustBeInstanced", "Equipment ability must be instanced"));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}

#endif
