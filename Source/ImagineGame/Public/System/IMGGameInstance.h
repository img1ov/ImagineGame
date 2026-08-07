// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Engine/GameInstance.h"

#include "IMGGameInstance.generated.h"

#define UE_API IMAGINEGAME_API

UCLASS(MinimalAPI, Config = Game)
class UIMGGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:

	UE_API UIMGGameInstance(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	
protected:

	UE_API virtual void Init() override;
	
};

#undef UE_API
