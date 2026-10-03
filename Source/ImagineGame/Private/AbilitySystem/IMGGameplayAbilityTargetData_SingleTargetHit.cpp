

#include "AbilitySystem/IMGGameplayAbilityTargetData_SingleTargetHit.h"

#include "AbilitySystem/IMGGameplayEffectContext.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "GameFramework/Actor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGGameplayAbilityTargetData_SingleTargetHit)

struct FGameplayEffectContextHandle;

//////////////////////////////////////////////////////////////////////

TArray<TWeakObjectPtr<AActor>> FIMGGameplayAbilityTargetData_SingleTargetHit::GetActors() const
{
	TArray<TWeakObjectPtr<AActor>> Actors = FGameplayAbilityTargetData_SingleTargetHit::GetActors();
	if (Actors.Num() == 1)
	{
		for (AActor* Candidate = Actors[0].Get(); Candidate; Candidate = Candidate->GetAttachParentActor())
		{
			if (UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Candidate))
			{
				Actors[0] = Candidate;
				break;
			}
		}
	}
	return Actors;
}

void FIMGGameplayAbilityTargetData_SingleTargetHit::AddTargetDataToContext(FGameplayEffectContextHandle& Context, bool bIncludeActorArray) const
{
	FGameplayAbilityTargetData_SingleTargetHit::AddTargetDataToContext(Context, bIncludeActorArray);

	// Add game-specific data
	if (FIMGGameplayEffectContext* TypedContext = FIMGGameplayEffectContext::ExtractEffectContext(Context))
	{
		TypedContext->CartridgeID = CartridgeID;
	}
}

bool FIMGGameplayAbilityTargetData_SingleTargetHit::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	FGameplayAbilityTargetData_SingleTargetHit::NetSerialize(Ar, Map, bOutSuccess);

	Ar << CartridgeID;

	return true;
}

