// Fill out your copyright notice in the Description page of Project Settings.

#include "Input/IMGInputComponent.h"

UIMGInputComponent::UIMGInputComponent(const FObjectInitializer& ObjectInitializer)
{
}

void UIMGInputComponent::AddInputMappings(const UIMGInputConfig* InputConfig,
	UEnhancedInputLocalPlayerSubsystem* InputSubsystem) const
{
	check(InputConfig);
	check(InputSubsystem);

	// Extension point for mode-specific mapping layers (mount/vehicle/UI).
}

void UIMGInputComponent::RemoveInputMappings(const UIMGInputConfig* InputConfig,
	UEnhancedInputLocalPlayerSubsystem* InputSubsystem) const
{
	check(InputConfig);
	check(InputSubsystem);

	// Paired cleanup point for AddInputMappings.
}

void UIMGInputComponent::RemoveBinds(TArray<uint32>& BindHandles)
{
	// Binding ownership is handle-based; centralized removal is the safest approach.
	for (uint32 Handle : BindHandles)
	{
		RemoveBindingByHandle(Handle);
	}
	BindHandles.Reset();
}
