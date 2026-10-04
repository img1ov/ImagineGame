#pragma once

#include "AbilitySystem/Abilities/IMGGameplayAbility.h"
#include "TimerManager.h"
#include "IMGGameplayAbility_AutoRespawn.generated.h"

class AController;
class UIMGHealthComponent;

UCLASS()
class SHOOTERCORERUNTIME_API UIMGGameplayAbility_AutoRespawn : public UIMGGameplayAbility
{
	GENERATED_BODY()

public:
	UIMGGameplayAbility_AutoRespawn(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	virtual void OnPawnAvatarSet() override;

	UPROPERTY(EditDefaultsOnly, Category = "Respawn", meta = (ClampMin = "0.0"))
	float RespawnDelaySeconds = 5.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Respawn|Messages")
	FGameplayTag RespawnStartedMessageTag;

	UPROPERTY(EditDefaultsOnly, Category = "Respawn|Messages")
	FGameplayTag RespawnCompletedMessageTag;

private:
	void BindHealthComponent();
	void UnbindHealthComponent();
	void RestartPlayer();
	void BroadcastRespawnMessage(FGameplayTag MessageTag, double Magnitude) const;

	UFUNCTION()
	void OnDeathStarted(AActor* DyingActor);

	TWeakObjectPtr<UIMGHealthComponent> BoundHealthComponent;
	TWeakObjectPtr<AController> ControllerToRestart;
	TWeakObjectPtr<AActor> DyingAvatar;
	FTimerHandle RespawnTimerHandle;
};
