#pragma once

#include "AbilitySystem/Abilities/IMGGameplayAbility.h"
#include "IMGGameplayAbility_DropWeapon.generated.h"

UCLASS()
class SHOOTERCORERUNTIME_API UIMGGameplayAbility_DropWeapon : public UIMGGameplayAbility
{
	GENERATED_BODY()

public:
	UIMGGameplayAbility_DropWeapon(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};
