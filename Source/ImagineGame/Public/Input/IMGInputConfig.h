// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "GameplayTagContainer.h"
#include "Engine/DataAsset.h"
#include "InputTriggers.h"

#include "IMGInputConfig.generated.h"

class UInputAction;
class UObject;
struct FFrame;

/**
 * FActInputTagAction
 *
 *	Struct used to map a input action to a gameplay input tag.
 */
USTRUCT(BlueprintType)
struct FActInputAction
{
	GENERATED_BODY()
	
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (Categories = "InputTag"))
	FGameplayTag InputTag;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TObjectPtr<const UInputAction> InputAction = nullptr;

	// Press event to bind for ability input dispatch.
	// Use Triggered for hold IA with one-shot hold trigger, Started for tap.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	ETriggerEvent PressedTriggerEvent = ETriggerEvent::Started;

	// Whether this action should also bind Completed as a release event.
	// Keep true for button-like actions that should update held-state in analyzer.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	bool bBindReleaseEvent = true;
};

/**
 * UIMGInputConfig
 *
 *	Non-mutable data asset that contains input configuration properties.
 */
UCLASS(BlueprintType, Const)
class IMAGINEGAME_API UIMGInputConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	
	UIMGInputConfig(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintCallable, Category = "IMG|Pawn")
	const UInputAction* FindNativeInputActionForTag(const FGameplayTag& InputTag, bool bLogNotFound = true) const;

	UFUNCTION(BlueprintCallable, Category = "IMG|Pawn")
	const UInputAction* FindAbilityInputActionForTag(const FGameplayTag& InputTag, bool bLogNotFound = true) const;
	
public:
	
	// List of input actions used by the owner.  These input actions are mapped to a gameplay tag and must be manually bound.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Meta = (TitleProperty = "InputTagAction"))
	TArray<FActInputAction> NativeInputTagActions;

	// List of input actions used by the owner.  These input actions are mapped to a gameplay tag and are automatically bound to abilities with matching input tags.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Meta = (TitleProperty = "InputAction"))
	TArray<FActInputAction> AbilityInputActions;
};
