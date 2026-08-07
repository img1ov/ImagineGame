#pragma once

#include "Logging/LogMacros.h"

class UObject;

IMAGINEGAME_API DECLARE_LOG_CATEGORY_EXTERN(LogIMG, Log, All);
IMAGINEGAME_API DECLARE_LOG_CATEGORY_EXTERN(LogIMGExperience, Log, All);
IMAGINEGAME_API DECLARE_LOG_CATEGORY_EXTERN(LogIMGAbilitySystem, Log, All);
IMAGINEGAME_API DECLARE_LOG_CATEGORY_EXTERN(LogIMGTeams, Log, All);


IMAGINEGAME_API FString GetClientServerContextString(UObject* ContextObject = nullptr);
