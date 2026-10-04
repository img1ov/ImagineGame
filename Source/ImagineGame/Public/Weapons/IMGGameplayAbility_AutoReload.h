#pragma once

#include "Equipment/IMGGameplayAbility_FromEquipment.h"
#include "TimerManager.h"
#include "IMGGameplayAbility_AutoReload.generated.h"

UCLASS()
class IMAGINEGAME_API UIMGGameplayAbility_AutoReload : public UIMGGameplayAbility_FromEquipment
{
	GENERATED_BODY()

public:
	UIMGGameplayAbility_AutoReload(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Ammo")
	FGameplayTag MagazineAmmoTag;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Ammo")
	FGameplayTag SpareAmmoTag;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Reload")
	FGameplayTag ReloadEventTag;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Reload", meta = (ClampMin = "0.05"))
	float PollIntervalSeconds = 0.25f;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Reload", meta = (ClampMin = "0.0"))
	float IdleDelaySeconds = 0.5f;

private:
	void CheckAutoReload();

	FTimerHandle PollTimerHandle;
};
