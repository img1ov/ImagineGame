#pragma once

#include "Kismet/BlueprintAsyncActionBase.h"
#include "UObject/ObjectPtr.h"

#include "AsyncAction_QueryReplays.generated.h"

class APlayerController;
class INetworkReplayStreamer;
class UIMGReplayList;
struct FEnumerateStreamsResult;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FQueryReplayAsyncDelegate, UIMGReplayList*, Results);

/** Asynchronously enumerates the replays available to a player. */
UCLASS()
class UAsyncAction_QueryReplays : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	UAsyncAction_QueryReplays(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true"))
	static UAsyncAction_QueryReplays* QueryReplays(APlayerController* PlayerController);

	virtual void Activate() override;

	UPROPERTY(BlueprintAssignable)
	FQueryReplayAsyncDelegate QueryComplete;

private:
	void OnEnumerateStreamsComplete(const FEnumerateStreamsResult& Result);

	UPROPERTY()
	TObjectPtr<UIMGReplayList> ResultList;

	TWeakObjectPtr<APlayerController> PlayerController;
	TSharedPtr<INetworkReplayStreamer> ReplayStreamer;
};
