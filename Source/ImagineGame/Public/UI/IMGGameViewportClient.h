#pragma once

#include "CommonGameViewportClient.h"

#include "IMGGameViewportClient.generated.h"

class UGameInstance;
class UObject;

UCLASS(BlueprintType)
class UIMGGameViewportClient : public UCommonGameViewportClient
{
	GENERATED_BODY()

public:
	UIMGGameViewportClient();

	virtual void Init(struct FWorldContext& WorldContext, UGameInstance* OwningGameInstance, bool bCreateNewAudioDevice = true) override;
};
