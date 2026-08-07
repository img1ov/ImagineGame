// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"

#include "IMGVerbMessageHelpers.generated.h"

#define UE_API IMAGINEGAME_API

struct FGameplayCueParameters;
struct FIMGVerbMessage;

class APlayerController;
class APlayerState;
class UObject;
struct FFrame;


UCLASS(MinimalAPI)
class UIMGVerbMessageHelpers : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "IMG")
	static UE_API APlayerState* GetPlayerStateFromObject(UObject* Object);

	UFUNCTION(BlueprintCallable, Category = "IMG")
	static UE_API APlayerController* GetPlayerControllerFromObject(UObject* Object);

	UFUNCTION(BlueprintCallable, Category = "IMG")
	static UE_API FGameplayCueParameters VerbMessageToCueParameters(const FIMGVerbMessage& Message);

	UFUNCTION(BlueprintCallable, Category = "IMG")
	static UE_API FIMGVerbMessage CueParametersToVerbMessage(const FGameplayCueParameters& Params);
};

#undef UE_API
