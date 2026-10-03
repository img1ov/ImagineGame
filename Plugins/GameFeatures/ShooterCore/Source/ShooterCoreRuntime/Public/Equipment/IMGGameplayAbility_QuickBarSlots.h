#pragma once

#include "AbilitySystem/Abilities/IMGGameplayAbility.h"
#include "IMGGameplayAbility_QuickBarSlots.generated.h"

UCLASS()
class SHOOTERCORERUNTIME_API UIMGGameplayAbility_QuickBarSlots : public UIMGGameplayAbility
{
	GENERATED_BODY()

public:
	UIMGGameplayAbility_QuickBarSlots(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	UPROPERTY(EditDefaultsOnly, Category = "QuickBar|Input")
	FGameplayTag SelectSlotEventTag;

	UPROPERTY(EditDefaultsOnly, Category = "QuickBar|Input")
	FGameplayTag CycleForwardEventTag;

	UPROPERTY(EditDefaultsOnly, Category = "QuickBar|Input")
	FGameplayTag CycleBackwardEventTag;

private:
	UFUNCTION()
	void SelectSlot(FGameplayEventData Payload);

	UFUNCTION()
	void CycleForward(FGameplayEventData Payload);

	UFUNCTION()
	void CycleBackward(FGameplayEventData Payload);
};
