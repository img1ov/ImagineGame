#include "Weapons/IMGGameplayAbility_AutoReload.h"

#include "AbilitySystem/IMGAbilitySystemComponent.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Engine/World.h"
#include "Inventory/IMGInventoryItemInstance.h"
#include "IMGGameplayTags.h"
#include "Weapons/IMGWeaponInstance.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGGameplayAbility_AutoReload)

UIMGGameplayAbility_AutoReload::UIMGGameplayAbility_AutoReload(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ActivationPolicy = EIMGAbilityActivationPolicy::OnSpawn;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalOnly;
}

void UIMGGameplayAbility_AutoReload::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!MagazineAmmoTag.IsValid() || !SpareAmmoTag.IsValid() || !ReloadEventTag.IsValid())
	{
		K2_EndAbility();
		return;
	}
	GetWorld()->GetTimerManager().SetTimer(PollTimerHandle, this, &ThisClass::CheckAutoReload,
		FMath::Max(PollIntervalSeconds, 0.05f), true);
}

void UIMGGameplayAbility_AutoReload::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PollTimerHandle);
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UIMGGameplayAbility_AutoReload::CheckAutoReload()
{
	const UIMGInventoryItemInstance* Item = GetAssociatedItem();
	const UIMGWeaponInstance* Weapon = Cast<UIMGWeaponInstance>(GetAssociatedEquipment());
	UIMGAbilitySystemComponent* ASC = GetIMGAbilitySystemComponentFromActorInfo();
	if (!Item || !Weapon || !ASC || Item->GetStatTagStackCount(MagazineAmmoTag) != 0 ||
		Item->GetStatTagStackCount(SpareAmmoTag) <= 0 || Weapon->GetTimeSinceLastInteractedWith() < IdleDelaySeconds ||
		ASC->HasMatchingGameplayTag(IMGGameplayTags::Ability_Weapon_NoFiring))
	{
		return;
	}

	FGameplayEventData EventData;
	EventData.EventTag = ReloadEventTag;
	EventData.Instigator = GetAvatarActorFromActorInfo();
	EventData.Target = GetAvatarActorFromActorInfo();
	ASC->HandleGameplayEvent(ReloadEventTag, &EventData);
}
