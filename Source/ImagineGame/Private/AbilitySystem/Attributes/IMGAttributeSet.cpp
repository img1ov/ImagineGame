// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/Attributes/IMGAttributeSet.h"

#include "AbilitySystem/IMGAbilitySystemComponent.h"

UIMGAttributeSet::UIMGAttributeSet()
{
}

UWorld* UIMGAttributeSet::GetWorld() const
{
	const UObject* Outer = GetOuter();
	check(Outer);

	return Outer->GetWorld();
}

UIMGAbilitySystemComponent* UIMGAttributeSet::GetIMGAbilitySystemComponent() const
{
	return Cast<UIMGAbilitySystemComponent>(GetOwningAbilitySystemComponent());
}
