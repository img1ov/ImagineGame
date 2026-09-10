// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/IMGTaggedActor.h"
#include "UObject/UnrealType.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGTaggedActor)

AIMGTaggedActor::AIMGTaggedActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void AIMGTaggedActor::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
	TagContainer.AppendTags(StaticGameplayTags);
}

#if WITH_EDITOR
bool AIMGTaggedActor::CanEditChange(const FProperty* InProperty) const
{
	// Prevent editing of the other tags property
	if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(AActor, Tags))
	{
		return false;
	}

	return Super::CanEditChange(InProperty);
}
#endif
