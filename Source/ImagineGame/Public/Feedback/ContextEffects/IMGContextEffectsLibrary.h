// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayTagContainer.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/WeakObjectPtr.h"

#include "IMGContextEffectsLibrary.generated.h"



class UNiagaraSystem;
class USoundBase;
struct FFrame;

/**
 *
 */
UENUM()
enum class EContextEffectsLibraryLoadState : uint8 {
	Unloaded = 0,
	Loading = 1,
	Loaded = 2
};

/**
 *
 */
USTRUCT(BlueprintType)
struct IMAGINEGAME_API FIMGContextEffects
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FGameplayTag EffectTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FGameplayTagContainer Context;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowedClasses = "/Script/Engine.SoundBase, /Script/Niagara.NiagaraSystem"))
	TArray<FSoftObjectPath> Effects;

};

/**
 *
 */
UCLASS()
class IMAGINEGAME_API UIMGActiveContextEffects : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere)
	FGameplayTag EffectTag;

	UPROPERTY(VisibleAnywhere)
	FGameplayTagContainer Context;

	UPROPERTY(VisibleAnywhere)
	TArray<TObjectPtr<USoundBase>> Sounds;

	UPROPERTY(VisibleAnywhere)
	TArray<TObjectPtr<UNiagaraSystem>> NiagaraSystems;
};

DECLARE_DYNAMIC_DELEGATE_OneParam(FIMGContextEffectLibraryLoadingComplete, TArray<UIMGActiveContextEffects*>, IMGActiveContextEffects);

/**
 *
 */
UCLASS( BlueprintType)
class IMAGINEGAME_API UIMGContextEffectsLibrary : public UObject
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FIMGContextEffects> ContextEffects;

	UFUNCTION(BlueprintCallable)
	void GetEffects(const FGameplayTag Effect, const FGameplayTagContainer Context, TArray<USoundBase*>& Sounds, TArray<UNiagaraSystem*>& NiagaraSystems);

	UFUNCTION(BlueprintCallable)
	void LoadEffects();

	EContextEffectsLibraryLoadState GetContextEffectsLibraryLoadState();

private:
	void LoadEffectsInternal();

	void IMGContextEffectLibraryLoadingComplete(TArray<UIMGActiveContextEffects*> IMGActiveContextEffects);

	UPROPERTY(Transient)
	TArray< TObjectPtr<UIMGActiveContextEffects>> ActiveContextEffects;

	UPROPERTY(Transient)
	EContextEffectsLibraryLoadState EffectsLoadState = EContextEffectsLibraryLoadState::Unloaded;
};
