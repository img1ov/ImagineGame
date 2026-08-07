#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "NativeGameplayTags.h"

#include "IMGInteractionDurationMessage.generated.h"

UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_INTERACTION_DURATION_MESSAGE);

USTRUCT(BlueprintType)
struct FIMGInteractionDurationMessage
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<AActor> Instigator = nullptr;
	
	UPROPERTY(BlueprintReadWrite)
	float Duration = 0;
};
