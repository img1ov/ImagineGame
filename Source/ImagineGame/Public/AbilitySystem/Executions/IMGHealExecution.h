// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayEffectExecutionCalculation.h"

#include "IMGHealExecution.generated.h"

class UObject;


/**
 * UIMGHealExecution
 *
 *	Execution used by gameplay effects to apply healing to the health attributes.
 */
UCLASS()
class UIMGHealExecution : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()

public:

	UIMGHealExecution();

protected:

	virtual void Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams, FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;
};
