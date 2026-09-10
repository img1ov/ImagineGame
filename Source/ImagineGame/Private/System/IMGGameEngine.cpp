// Copyright Epic Games, Inc. All Rights Reserved.

#include "System/IMGGameEngine.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGGameEngine)

class IEngineLoop;


UIMGGameEngine::UIMGGameEngine(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UIMGGameEngine::Init(IEngineLoop* InEngineLoop)
{
	Super::Init(InEngineLoop);
}
