#pragma once

#include "GameplayTagContainer.h"
#include "Weapons/IMGWeaponSpawner.h"

#include "IMGShooterWeaponSpawner.generated.h"

UCLASS(Blueprintable)
class SHOOTERCORERUNTIME_API AIMGShooterWeaponSpawner : public AIMGWeaponSpawner
{
	GENERATED_BODY()

protected:
	virtual void AttemptPickUpWeapon_Implementation(APawn* Pawn) override;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Pickup")
	FGameplayTag BlockAutoEquipTag;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Pickup")
	bool bRefillDuplicateWeaponAmmo = false;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Pickup", meta = (EditCondition = "bRefillDuplicateWeaponAmmo"))
	FGameplayTag SpareAmmoTag;

private:
	bool GrantWeapon(TSubclassOf<UIMGInventoryItemDefinition> WeaponItemClass, APawn* ReceivingPawn);
};
