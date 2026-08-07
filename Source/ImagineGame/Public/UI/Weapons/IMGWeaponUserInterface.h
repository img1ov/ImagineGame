#pragma once

#include "CommonUserWidget.h"

#include "IMGWeaponUserInterface.generated.h"

class UIMGWeaponInstance;
class UObject;
struct FGeometry;

UCLASS()
class UIMGWeaponUserInterface : public UCommonUserWidget
{
	GENERATED_BODY()

public:
	UIMGWeaponUserInterface(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION(BlueprintImplementableEvent)
	void OnWeaponChanged(UIMGWeaponInstance* OldWeapon, UIMGWeaponInstance* NewWeapon);

private:
	void RebuildWidgetFromWeapon();

private:
	UPROPERTY(Transient)
	TObjectPtr<UIMGWeaponInstance> CurrentInstance;
};
