#pragma once

#include "Engine/RuntimeOptionsBase.h"

#include "IMGRuntimeOptions.generated.h"

/** Runtime feature switches and configuration values exposed through the ro console namespace. */
UCLASS(MinimalAPI, config = RuntimeOptions, BlueprintType)
class UIMGRuntimeOptions : public URuntimeOptionsBase
{
	GENERATED_BODY()

public:
	static IMAGINEGAME_API const UIMGRuntimeOptions& Get();

	IMAGINEGAME_API UIMGRuntimeOptions();

	UFUNCTION(BlueprintPure, Category = Options)
	static IMAGINEGAME_API UIMGRuntimeOptions* GetRuntimeOptions();
};
