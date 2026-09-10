// Copyright Epic Games, Inc. All Rights Reserved.

#include "System/IMGGameSession.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGGameSession)


AIMGGameSession::AIMGGameSession(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool AIMGGameSession::ProcessAutoLogin()
{
	// This is actually handled in IMGGameMode::TryDedicatedServerLogin
	return true;
}

void AIMGGameSession::HandleMatchHasStarted()
{
	Super::HandleMatchHasStarted();
}

void AIMGGameSession::HandleMatchHasEnded()
{
	Super::HandleMatchHasEnded();
}
