#include "UI/Weapons/IMGWeaponUserInterface.h"

#include "Equipment/IMGEquipmentManagerComponent.h"
#include "GameFramework/Pawn.h"
#include "Weapons/IMGWeaponInstance.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGWeaponUserInterface)

struct FGeometry;

UIMGWeaponUserInterface::UIMGWeaponUserInterface(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UIMGWeaponUserInterface::NativeConstruct()
{
	Super::NativeConstruct();
}

void UIMGWeaponUserInterface::NativeDestruct()
{
	Super::NativeDestruct();
}

void UIMGWeaponUserInterface::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (APawn* Pawn = GetOwningPlayerPawn())
	{
		if (UIMGEquipmentManagerComponent* EquipmentManager = Pawn->FindComponentByClass<UIMGEquipmentManagerComponent>())
		{
			if (UIMGWeaponInstance* NewInstance = EquipmentManager->GetFirstInstanceOfType<UIMGWeaponInstance>())
			{
				if (NewInstance != CurrentInstance && NewInstance->GetInstigator() != nullptr)
				{
					UIMGWeaponInstance* OldWeapon = CurrentInstance;
					CurrentInstance = NewInstance;
					RebuildWidgetFromWeapon();
					OnWeaponChanged(OldWeapon, CurrentInstance);
				}
			}
		}
	}
}

void UIMGWeaponUserInterface::RebuildWidgetFromWeapon()
{
	
}

