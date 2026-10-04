#include "Weapons/IMGGameplayAbility_WeaponReload.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Animation/AnimMontage.h"
#include "Engine/World.h"
#include "Inventory/IMGInventoryItemInstance.h"
#include "IMGGameplayTags.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGGameplayAbility_WeaponReload)

UIMGGameplayAbility_WeaponReload::UIMGGameplayAbility_WeaponReload(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bServerRespectsRemoteAbilityCancellation = false;
	ActivationOwnedTags.AddTag(IMGGameplayTags::Ability_Weapon_NoFiring);
}

bool UIMGGameplayAbility_WeaponReload::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags) ||
		!MagazineAmmoTag.IsValid() || !SpareAmmoTag.IsValid() || !MagazineSizeTag.IsValid())
	{
		return false;
	}

	const UIMGInventoryItemInstance* Item = GetAssociatedItem();
	return Item && Item->GetStatTagStackCount(SpareAmmoTag) > 0 &&
		Item->GetStatTagStackCount(MagazineAmmoTag) < Item->GetStatTagStackCount(MagazineSizeTag);
}

void UIMGGameplayAbility_WeaponReload::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	bReloadStarted = false;
	bReloadCompleted = false;
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		K2_EndAbility();
		return;
	}
	bReloadStarted = true;
	if (ActorInfo->IsLocallyControlled())
	{
		K2_OnReloadStarted();
	}

	if (ReloadMontage)
	{
		if (ReloadCompleteEventTag.IsValid())
		{
			UAbilityTask_WaitGameplayEvent* EventTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, ReloadCompleteEventTag, nullptr, true, true);
			EventTask->EventReceived.AddDynamic(this, &ThisClass::OnReloadEvent);
			EventTask->ReadyForActivation();
		}

		UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this, NAME_None, ReloadMontage, 1.0f, NAME_None, true);
		MontageTask->OnCompleted.AddDynamic(this, &ThisClass::OnMontageCompleted);
		MontageTask->OnInterrupted.AddDynamic(this, &ThisClass::OnMontageInterrupted);
		MontageTask->OnCancelled.AddDynamic(this, &ThisClass::OnMontageInterrupted);
		MontageTask->ReadyForActivation();
	}
	else
	{
		GetWorld()->GetTimerManager().SetTimer(ReloadTimerHandle, this, &ThisClass::CompleteReload, FMath::Max(ReloadDurationSeconds, 0.01f));
	}
}

void UIMGGameplayAbility_WeaponReload::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReloadTimerHandle);
	}
	if (bReloadStarted && ActorInfo && ActorInfo->IsLocallyControlled())
	{
		K2_OnReloadEnded(bReloadCompleted && !bWasCancelled);
	}
	bReloadStarted = false;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UIMGGameplayAbility_WeaponReload::OnReloadEvent(FGameplayEventData Payload)
{
	CompleteReload();
}

void UIMGGameplayAbility_WeaponReload::OnMontageCompleted()
{
	CompleteReload();
}

void UIMGGameplayAbility_WeaponReload::OnMontageInterrupted()
{
	if (IsActive() && !bReloadCompleted)
	{
		K2_EndAbility();
	}
}

void UIMGGameplayAbility_WeaponReload::CompleteReload()
{
	if (!IsActive())
	{
		return;
	}

	if (CurrentActorInfo->IsNetAuthority())
	{
		if (UIMGInventoryItemInstance* Item = GetAssociatedItem())
		{
			const int32 MagazineAmmo = Item->GetStatTagStackCount(MagazineAmmoTag);
			const int32 SpareAmmo = Item->GetStatTagStackCount(SpareAmmoTag);
			const int32 MagazineSize = Item->GetStatTagStackCount(MagazineSizeTag);
			const int32 AmmoToLoad = FMath::Min(FMath::Max(MagazineSize - MagazineAmmo, 0), SpareAmmo);
			if (AmmoToLoad > 0)
			{
				Item->RemoveStatTagStack(SpareAmmoTag, AmmoToLoad);
				Item->AddStatTagStack(MagazineAmmoTag, AmmoToLoad);
			}
		}
	}
	bReloadCompleted = true;
	K2_EndAbility();
}
