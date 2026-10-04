#pragma once

#include "Weapons/IMGGameplayAbility_RangedWeapon.h"
#include "TimerManager.h"
#include "IMGGameplayAbility_WeaponFire.generated.h"

class UGameplayEffect;
class UAnimMontage;

UCLASS()
class IMAGINEGAME_API UIMGGameplayAbility_WeaponFire : public UIMGGameplayAbility_RangedWeapon
{
	GENERATED_BODY()

public:
	UIMGGameplayAbility_WeaponFire(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	virtual void ApplyCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) const override;
	bool ValidateTargetData(FGameplayAbilityTargetDataHandle& TargetData) const;
	void HandleTargetData(const FGameplayAbilityTargetDataHandle& TargetData);
	void OnTargetDataReady(const FGameplayAbilityTargetDataHandle& TargetData, FGameplayTag ApplicationTag);
	void StartTargeting();

	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Ammo")
	FGameplayTag MagazineAmmoTag;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Damage")
	TSubclassOf<UGameplayEffect> DamageEffect;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Timing", meta = (ClampMin = "0.01"))
	float FireIntervalSeconds = 0.1f;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Validation", meta = (ClampMin = "0.0"))
	float MaxTraceOriginDistance = 350.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Validation", meta = (ClampMin = "0.0"))
	float TraceRangeTolerance = 350.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Validation", meta = (ClampMin = "1"))
	int32 MaxTargetDataPerBullet = 8;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Animation")
	TObjectPtr<UAnimMontage> FireMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Animation", meta = (ClampMin = "0.01"))
	float FireMontagePlayRate = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Effects")
	FGameplayTag FireCueTag;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Effects")
	FGameplayTag ImpactCueTag;

private:
	void FinishShot();

	FTimerHandle FireTimerHandle;
	FDelegateHandle TargetDataDelegateHandle;
	double LastShotTime = -1.0e10;
};
