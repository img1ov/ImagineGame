// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/GameEngine.h"

#include "IMGGameEngine.generated.h"

class IEngineLoop;
class UObject;


UCLASS()
class UIMGGameEngine : public UGameEngine
{
	GENERATED_BODY()

public:

	UIMGGameEngine(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:

	virtual void Init(IEngineLoop* InEngineLoop) override;
};
