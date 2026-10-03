#pragma once

#include "Equipment/IMGGameplayAbility_FromEquipment.h"
#include "TimerManager.h"
#include "IMGGameplayAbility_WeaponReload.generated.h"

class UAnimMontage;

UCLASS()
class SHOOTERCORERUNTIME_API UIMGGameplayAbility_WeaponReload : public UIMGGameplayAbility_FromEquipment
{
	GENERATED_BODY()

public:
	UIMGGameplayAbility_WeaponReload(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Ammo")
	FGameplayTag MagazineAmmoTag;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Ammo")
	FGameplayTag SpareAmmoTag;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Ammo")
	FGameplayTag MagazineSizeTag;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Reload")
	FGameplayTag ReloadCompleteEventTag;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Reload")
	TObjectPtr<UAnimMontage> ReloadMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Reload", meta = (ClampMin = "0.01"))
	float ReloadDurationSeconds = 1.0f;

	UFUNCTION(BlueprintImplementableEvent, Category = "Weapon|Presentation")
	void K2_OnReloadStarted();

	UFUNCTION(BlueprintImplementableEvent, Category = "Weapon|Presentation")
	void K2_OnReloadEnded(bool bCompleted);

private:
	UFUNCTION()
	void OnReloadEvent(FGameplayEventData Payload);

	UFUNCTION()
	void OnMontageCompleted();

	UFUNCTION()
	void OnMontageInterrupted();

	void CompleteReload();

	FTimerHandle ReloadTimerHandle;
	bool bReloadStarted = false;
	bool bReloadCompleted = false;
};
