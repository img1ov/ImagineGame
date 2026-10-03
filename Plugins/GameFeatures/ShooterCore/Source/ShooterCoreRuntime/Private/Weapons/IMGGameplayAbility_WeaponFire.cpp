#include "Weapons/IMGGameplayAbility_WeaponFire.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Engine/World.h"
#include "GameplayCueFunctionLibrary.h"
#include "Inventory/IMGInventoryItemInstance.h"
#include "Physics/IMGCollisionChannels.h"
#include "TimerManager.h"
#include "Weapons/IMGRangedWeaponInstance.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGGameplayAbility_WeaponFire)

UIMGGameplayAbility_WeaponFire::UIMGGameplayAbility_WeaponFire(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bServerRespectsRemoteAbilityCancellation = false;
}

bool UIMGGameplayAbility_WeaponFire::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags) ||
		!MagazineAmmoTag.IsValid() || !DamageEffect || !ActorInfo || !ActorInfo->AvatarActor.IsValid())
	{
		return false;
	}

	const UIMGInventoryItemInstance* Item = GetAssociatedItem();
	if (!Item || Item->GetStatTagStackCount(MagazineAmmoTag) < 1)
	{
		return false;
	}

	return ActorInfo->AvatarActor->GetWorld()->GetTimeSeconds() - LastShotTime >= FireIntervalSeconds;
}

void UIMGGameplayAbility_WeaponFire::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (FireMontage)
	{
		UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this, NAME_None, FireMontage, FireMontagePlayRate, NAME_None, false);
		MontageTask->ReadyForActivation();
	}
	if (ActorInfo->IsLocallyControlled())
	{
		StartRangedWeaponTargeting();
	}
}

void UIMGGameplayAbility_WeaponFire::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FireTimerHandle);
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

bool UIMGGameplayAbility_WeaponFire::ValidateRangedWeaponTargetData(FGameplayAbilityTargetDataHandle& TargetData) const
{
	const UIMGRangedWeaponInstance* Weapon = GetWeaponInstance();
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	const UIMGInventoryItemInstance* Item = GetAssociatedItem();
	if (!Weapon || !Avatar || !Item || Item->GetStatTagStackCount(MagazineAmmoTag) < 1 ||
		TargetData.Num() == 0 || TargetData.Num() > FMath::Max(1, Weapon->GetBulletsPerCartridge()) * FMath::Max(1, MaxTargetDataPerBullet))
	{
		return false;
	}

	for (int32 Index = 0; Index < TargetData.Num(); ++Index)
	{
		FGameplayAbilityTargetData* Data = TargetData.Get(Index);
		if (!Data || !Data->GetScriptStruct() || !Data->GetScriptStruct()->IsChildOf(FGameplayAbilityTargetData_SingleTargetHit::StaticStruct()))
		{
			return false;
		}
		FGameplayAbilityTargetData_SingleTargetHit* SingleHit = static_cast<FGameplayAbilityTargetData_SingleTargetHit*>(Data);
		const FHitResult* Hit = SingleHit->GetHitResult();
		if (!Hit || FVector::Dist(Hit->TraceStart, Avatar->GetActorLocation()) > MaxTraceOriginDistance ||
			FVector::Dist(Hit->TraceStart, Hit->ImpactPoint) > Weapon->GetMaxDamageRange() + TraceRangeTolerance ||
			FVector::Dist(Hit->TraceStart, Hit->TraceEnd) > Weapon->GetMaxDamageRange() + TraceRangeTolerance)
		{
			return false;
		}

		const TArray<TWeakObjectPtr<AActor>> ResolvedActors = Data->GetActors();
		AActor* Target = ResolvedActors.Num() == 1 ? ResolvedActors[0].Get() : nullptr;
		if (!Target || !UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target))
		{
			continue;
		}

		TArray<FHitResult> ServerHits;
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(WeaponDamageValidation), true, Avatar);
		QueryParams.bReturnPhysicalMaterial = true;
		TArray<AActor*> AttachedActors;
		Avatar->GetAttachedActors(AttachedActors, true, true);
		QueryParams.AddIgnoredActors(AttachedActors);
		const float SweepRadius = Weapon->GetBulletTraceSweepRadius();
		if (SweepRadius > 0.0f)
		{
			GetWorld()->SweepMultiByChannel(ServerHits, Hit->TraceStart, Hit->TraceEnd, FQuat::Identity,
				IMG_TraceChannel_Weapon, FCollisionShape::MakeSphere(SweepRadius), QueryParams);
		}
		else
		{
			GetWorld()->LineTraceMultiByChannel(ServerHits, Hit->TraceStart, Hit->TraceEnd, IMG_TraceChannel_Weapon, QueryParams);
		}
		bool bHitTarget = false;
		for (const FHitResult& ServerHit : ServerHits)
		{
			for (AActor* HitActor = ServerHit.GetActor(); HitActor; HitActor = HitActor->GetAttachParentActor())
			{
				if (HitActor == Target)
				{
					SingleHit->HitResult = ServerHit;
					bHitTarget = true;
					break;
				}
			}
			if (bHitTarget || ServerHit.bBlockingHit)
			{
				break;
			}
		}
		if (!bHitTarget)
		{
			return false;
		}
	}
	return true;
}

void UIMGGameplayAbility_WeaponFire::HandleRangedWeaponTargetData(const FGameplayAbilityTargetDataHandle& TargetData)
{
	LastShotTime = GetWorld()->GetTimeSeconds();

	if (CurrentActorInfo->IsNetAuthority())
	{
		if (UIMGInventoryItemInstance* Item = GetAssociatedItem())
		{
			Item->RemoveStatTagStack(MagazineAmmoTag, 1);
		}
		ApplyGameplayEffectToTarget(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, TargetData, DamageEffect,
			GetAbilityLevel(CurrentSpecHandle, CurrentActorInfo));
	}

	if (FireCueTag.IsValid())
	{
		FGameplayCueParameters CueParams;
		CueParams.Location = GetAvatarActorFromActorInfo()->GetActorLocation();
		CueParams.Instigator = GetAvatarActorFromActorInfo();
		CueParams.EffectCauser = GetAvatarActorFromActorInfo();
		CueParams.SourceObject = GetWeaponInstance();
		K2_ExecuteGameplayCueWithParams(FireCueTag, CueParams);
	}
	if (ImpactCueTag.IsValid())
	{
		for (int32 Index = 0; Index < TargetData.Num(); ++Index)
		{
			const FGameplayAbilityTargetData* Data = TargetData.Get(Index);
			if (const FHitResult* Hit = Data ? Data->GetHitResult() : nullptr)
			{
				if (Hit->bBlockingHit)
				{
					K2_ExecuteGameplayCueWithParams(ImpactCueTag, UGameplayCueFunctionLibrary::MakeGameplayCueParametersFromHitResult(*Hit));
				}
			}
		}
	}

	GetWorld()->GetTimerManager().SetTimer(FireTimerHandle, this, &ThisClass::FinishShot, FMath::Max(FireIntervalSeconds, 0.01f));
}

void UIMGGameplayAbility_WeaponFire::FinishShot()
{
	K2_EndAbility();
}
