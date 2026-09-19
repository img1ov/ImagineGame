#pragma once

#include "Messages/GameplayMessageProcessor.h"

#include "ElimStreakProcessor.generated.h"

class APlayerState;
class UObject;
struct FGameplayTag;
struct FIMGVerbMessage;
template <typename T> struct TObjectPtr;

// Tracks a streak of eliminations (X eliminations without being eliminated)
UCLASS(Abstract)
class UElimStreakProcessor : public UGameplayMessageProcessor
{
	GENERATED_BODY()

public:
	virtual void StartListening() override;

protected:
	// The event to rebroadcast when a user gets a streak of a certain length
	UPROPERTY(EditDefaultsOnly)
	TMap<int32, FGameplayTag> ElimStreakTags;

private:
	void OnEliminationMessage(FGameplayTag Channel, const FIMGVerbMessage& Payload);

private:
	UPROPERTY(Transient)
	TMap<TObjectPtr<APlayerState>, int32> PlayerStreakHistory;
};
