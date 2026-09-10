// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CommonGameInstance.h"

#include "IMGGameInstance.generated.h"

#define UE_API IMAGINEGAME_API

class AIMGPlayerController;
class UObject;

UCLASS(MinimalAPI, Config = Game)
class UIMGGameInstance : public UCommonGameInstance
{
	GENERATED_BODY()

public:

	UE_API UIMGGameInstance(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UE_API AIMGPlayerController* GetPrimaryPlayerController() const;

	UE_API virtual bool CanJoinRequestedSession() const override;
	UE_API virtual void HandlerUserInitialized(const UCommonUserInfo* UserInfo, bool bSuccess, FText Error, ECommonUserPrivilege RequestedPrivilege, ECommonUserOnlineContext OnlineContext) override;

	UE_API virtual void ReceivedNetworkEncryptionToken(const FString& EncryptionToken, const FOnEncryptionKeyResponse& Delegate) override;
	UE_API virtual void ReceivedNetworkEncryptionAck(const FOnEncryptionKeyResponse& Delegate) override;

protected:

	UE_API virtual void Init() override;
	UE_API virtual void Shutdown() override;

	UE_API void OnPreClientTravelToSession(FString& URL);

	/** A hard-coded encryption key used to try out the encryption code. This is NOT SECURE, do not use this technique in production! */
	TArray<uint8> DebugTestEncryptionKey;
};

#undef UE_API
