// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Components/GameFrameworkComponent.h"

#include "IMGHealthComponent.generated.h"

#define UE_API IMAGINEGAME_API

class UIMGHealthSet;
class UIMGAbilitySystemComponent;
class UObject;
struct FFrame;
struct FGameplayEffectSpec;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FIMGHealth_DeathEvent, AActor*, OwningActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FIMGHealth_AttributeChanged, UIMGHealthComponent*, HealthComponent, float, OldValue, float, NewValue, AActor*, Instigator);

/**
 * EActDeathState
 *
 *	Defines current state of death.
 */
UENUM(BlueprintType)
enum class EIMGDeathState : uint8
{
	NotDead = 0,
	DeathStarted,
	DeathFinished
};

/**
 * UIMGHealthComponent
 *
 *	An actor component used to handle anything related to health.
 */
UCLASS(MinimalAPI, Blueprintable, Meta=(BlueprintSpawnableComponent))
class UIMGHealthComponent : public UGameFrameworkComponent
{
	GENERATED_BODY()

public:
	
	UE_API UIMGHealthComponent(const FObjectInitializer& ObjectInitializer);
	
	// Returns the health component if one exists on the specified actor.
	UFUNCTION(BlueprintPure, Category = "IMG|Health")
	static UIMGHealthComponent* FindHealthComponent(const AActor* Actor) { return (Actor ? Actor->FindComponentByClass<UIMGHealthComponent>() : nullptr); }

	// Initialize the component using an ability system component.
	UFUNCTION(BlueprintCallable, Category = "IMG|Health")
	UE_API void InitializeWithAbilitySystem(UIMGAbilitySystemComponent* InASC);

	// Uninitialize the component, clearing any references to the ability system.
	UFUNCTION(BlueprintCallable, Category = "IMG|Health")
	UE_API void UninitializeFromAbilitySystem();
	
	// Returns the current health value.
	UFUNCTION(BlueprintCallable, Category = "IMG|Health")
	UE_API float GetHealth() const;

	// Returns the current maximum health value.
	UFUNCTION(BlueprintCallable, Category = "IMG|Health")
	UE_API float GetMaxHealth() const;
	
	// Returns the current health in the range [0.0, 1.0].
	UFUNCTION(BlueprintCallable, Category = "IMG|Health")
	UE_API float GetHealthNormalized() const;

	UFUNCTION(BlueprintCallable, Category = "IMG|Health")
	EIMGDeathState GetDeathState() const { return DeathState; }

	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "IMG|Health", Meta = (ExpandBoolAsExecs = "ReturnValue"))
	bool IsDeadOrDying() const { return (DeathState > EIMGDeathState::NotDead); }

	// Begins the death sequence for the owner.
	UE_API virtual void StartDeath();

	// Ends the death sequence for the owner.
	UE_API virtual void FinishDeath();

	// Applies enough damage to kill the owner.
	UE_API virtual void DamageSelfDestruct(bool bFellOutOfWorld = false);
	
public:
	
	// Delegate fired when the health value has changed. This is called on the client but the instigator may not be valid
	UPROPERTY(BlueprintAssignable)
	FIMGHealth_AttributeChanged OnHealthChanged;

	// Delegate fired when the max health value has changed. This is called on the client but the instigator may not be valid
	UPROPERTY(BlueprintAssignable)
	FIMGHealth_AttributeChanged OnMaxHealthChanged;
	
	// Delegate fired when the death sequence has started.
	UPROPERTY(BlueprintAssignable)
	FIMGHealth_DeathEvent OnDeathStarted;

	// Delegate fired when the death sequence has finished.
	UPROPERTY(BlueprintAssignable)
	FIMGHealth_DeathEvent OnDeathFinished;
	
protected:
	
	UE_API virtual void OnUnregister() override;

	UE_API void ClearGameplayTags();

	UE_API virtual void HandleHealthChanged(AActor* DamageInstigator, AActor* DamageCauser, const FGameplayEffectSpec* DamageEffectSpec, float DamageMagnitude, float OldValue, float NewValue);
	UE_API virtual void HandleMaxHealthChanged(AActor* DamageInstigator, AActor* DamageCauser, const FGameplayEffectSpec* DamageEffectSpec, float DamageMagnitude, float OldValue, float NewValue);
	UE_API virtual void HandleOutOfHealth(AActor* DamageInstigator, AActor* DamageCauser, const FGameplayEffectSpec* DamageEffectSpec, float DamageMagnitude, float OldValue, float NewValue);

	UFUNCTION()
	UE_API virtual void OnRep_DeathState(EIMGDeathState OldDeathState);

protected:

	// Ability system used by this component.
	UPROPERTY()
	TObjectPtr<UIMGAbilitySystemComponent> AbilitySystemComponent;
	
	// Health set used by this component.
	UPROPERTY()
	TObjectPtr<const UIMGHealthSet> HealthSet;
	
	// Replicated state used to handle dying.
	UPROPERTY(ReplicatedUsing = OnRep_DeathState)
	EIMGDeathState DeathState;
};

#undef UE_API 