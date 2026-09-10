// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/GameSession.h"

#include "IMGGameSession.generated.h"

class UObject;


UCLASS(Config = Game)
class AIMGGameSession : public AGameSession
{
	GENERATED_BODY()

public:

	AIMGGameSession(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:

	/** Override to disable the default behavior */
	virtual bool ProcessAutoLogin() override;

	virtual void HandleMatchHasStarted() override;
	virtual void HandleMatchHasEnded() override;
};
