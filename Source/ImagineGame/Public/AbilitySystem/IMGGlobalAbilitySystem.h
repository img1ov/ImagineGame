// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "ActiveGameplayEffectHandle.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayAbilitySpecHandle.h"
#include "Templates/SubclassOf.h"

#include "IMGGlobalAbilitySystem.generated.h"

class UGameplayAbility;
class UGameplayEffect;
class UIMGAbilitySystemComponent;
class UObject;
struct FActiveGameplayEffectHandle;
struct FFrame;
struct FGameplayAbilitySpecHandle;

USTRUCT()
struct FGlobalAppliedAbilityList
{
	GENERATED_BODY()
	
	UPROPERTY()
	TMap<TObjectPtr<UIMGAbilitySystemComponent>, FGameplayAbilitySpecHandle> Handles;
	
	void AddToASC(TSubclassOf<UGameplayAbility> Ability, UIMGAbilitySystemComponent* ASC);
	void RemoveFromASC(UIMGAbilitySystemComponent* ASC);
	void RemoveFromAll();
};

USTRUCT()
struct FGlobalAppliedEffectList
{
	GENERATED_BODY()
	
	UPROPERTY()
	TMap<TObjectPtr<UIMGAbilitySystemComponent>, FActiveGameplayEffectHandle> Handles;
	
	void AddToASC(TSubclassOf<UGameplayEffect> Effect, UIMGAbilitySystemComponent* ASC);
	void RemoveFromASC(UIMGAbilitySystemComponent* ASC);
	void RemoveFromAll();
};

UCLASS()
class IMAGINEGAME_API UIMGGlobalAbilitySystem : public UWorldSubsystem
{
	GENERATED_BODY()
	
public:
	
	UIMGGlobalAbilitySystem();
	
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="IMG")
	void ApplyAbilityToAll(TSubclassOf<UGameplayAbility> Ability);
	
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="IMG")
	void ApplyEffectToAll(TSubclassOf<UGameplayEffect> Effect);
	
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "IMG")
	void RemoveAbilityFromAll(TSubclassOf<UGameplayAbility> Ability);
	
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "IMG")
	void RemoveEffectFromAll(TSubclassOf<UGameplayEffect> Effect);
	
	/** Register an ASC with global system and apply any active global effects/abilities. */
	void RegisterASC(UIMGAbilitySystemComponent* ASC);

	/** Removes an ASC from the global system, along with any active global effects/abilities. */
	void UnregisterASC(UIMGAbilitySystemComponent* ASC);
	
private:
	
	UPROPERTY()
	TMap<TSubclassOf<UGameplayAbility>, FGlobalAppliedAbilityList> AppliedAbilities;

	UPROPERTY()
	TMap<TSubclassOf<UGameplayEffect>, FGlobalAppliedEffectList> AppliedEffects;

	UPROPERTY()
	TArray<TObjectPtr<UIMGAbilitySystemComponent>> RegisteredASCs;
	
};
