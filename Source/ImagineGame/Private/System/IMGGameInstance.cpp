// Fill out your copyright notice in the Description page of Project Settings.


#include "System/IMGGameInstance.h"

#include "IMGGameplayTags.h"
#include "Components/GameFrameworkComponentManager.h"

UIMGGameInstance::UIMGGameInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UIMGGameInstance::Init()
{
	Super::Init();

	// Register our custom init states
	UGameFrameworkComponentManager* ComponentManager = GetSubsystem<UGameFrameworkComponentManager>(this);

	if (ensure(ComponentManager))
	{
		ComponentManager->RegisterInitState(IMGGameplayTags::InitState_Spawned, false, FGameplayTag());
		ComponentManager->RegisterInitState(IMGGameplayTags::InitState_DataAvailable, false, IMGGameplayTags::InitState_Spawned);
		ComponentManager->RegisterInitState(IMGGameplayTags::InitState_DataInitialized, false, IMGGameplayTags::InitState_DataAvailable);
		ComponentManager->RegisterInitState(IMGGameplayTags::InitState_GameplayReady, false, IMGGameplayTags::InitState_DataInitialized);
	}

	//TODO: Initialize the debug key with a set value for AES256. This is not secure and for example purposes only.
}
