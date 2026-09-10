// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystem/Abilities/IMGGameplayAbility.h"

#include "IMGGameplayAbility_Reset.generated.h"

#define UE_API IMAGINEGAME_API

class AActor;
class UObject;
struct FGameplayAbilityActorInfo;
struct FGameplayEventData;

/**
 * UIMGGameplayAbility_Reset
 *
 *	Gameplay ability used for handling quickly resetting the player back to initial spawn state.
 *	Ability is activated automatically via the "GameplayEvent.RequestReset" ability trigger tag (server only).
 */
UCLASS(MinimalAPI)
class UIMGGameplayAbility_Reset : public UIMGGameplayAbility
{
	GENERATED_BODY()

public:
	UE_API UIMGGameplayAbility_Reset(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:

	UE_API virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};



USTRUCT(BlueprintType)
struct FIMGPlayerResetMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<AActor> OwnerPlayerState = nullptr;
};

#undef UE_API
