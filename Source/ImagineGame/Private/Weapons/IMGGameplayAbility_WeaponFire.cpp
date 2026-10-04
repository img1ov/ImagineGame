#include "Weapons/IMGGameplayAbility_WeaponFire.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/IMGGameplayAbilityTargetData_SingleTargetHit.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameplayCueFunctionLibrary.h"
#include "Inventory/IMGInventoryItemInstance.h"
#include "Physics/IMGCollisionChannels.h"
#include "TimerManager.h"
#include "Weapons/IMGWeaponStateComponent.h"
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
	UIMGGameplayAbility_FromEquipment::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!ASC || !GetWeaponInstance())
	{
		K2_EndAbility();
		return;
	}
	TargetDataDelegateHandle = ASC->AbilityTargetDataSetDelegate(Handle, ActivationInfo.GetActivationPredictionKey()).AddUObject(this, &ThisClass::OnTargetDataReady);
	GetWeaponInstance()->UpdateFiringTime();
	if (FireMontage)
	{
		UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this, NAME_None, FireMontage, FireMontagePlayRate, NAME_None, true);
		MontageTask->ReadyForActivation();
	}
	if (ActorInfo->IsLocallyControlled())
	{
		StartTargeting();
	}
}

void UIMGGameplayAbility_WeaponFire::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FireTimerHandle);
	}
	if (ActorInfo && ActorInfo->AbilitySystemComponent.IsValid())
	{
		UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
		ASC->AbilityTargetDataSetDelegate(Handle, ActivationInfo.GetActivationPredictionKey()).Remove(TargetDataDelegateHandle);
		ASC->ConsumeClientReplicatedTargetData(Handle, ActivationInfo.GetActivationPredictionKey());
	}
	UIMGGameplayAbility_FromEquipment::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UIMGGameplayAbility_WeaponFire::ApplyCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo) const
{
	Super::ApplyCost(Handle, ActorInfo, ActivationInfo);
	if (ActorInfo && ActorInfo->IsNetAuthority())
	{
		if (UIMGInventoryItemInstance* Item = GetAssociatedItem())
		{
			Item->RemoveStatTagStack(MagazineAmmoTag, 1);
		}
	}
}

void UIMGGameplayAbility_WeaponFire::StartTargeting()
{
	TArray<FHitResult> FoundHits;
	PerformLocalTargeting(FoundHits);

	FGameplayAbilityTargetDataHandle TargetData;
	if (const AController* Controller = GetControllerFromActorInfo())
	{
		if (const UIMGWeaponStateComponent* WeaponState = Controller->FindComponentByClass<UIMGWeaponStateComponent>())
		{
			TargetData.UniqueId = WeaponState->GetUnconfirmedServerSideHitMarkerCount();
		}
	}
	const int32 CartridgeID = FMath::Rand();
	for (const FHitResult& Hit : FoundHits)
	{
		FIMGGameplayAbilityTargetData_SingleTargetHit* Data = new FIMGGameplayAbilityTargetData_SingleTargetHit();
		Data->HitResult = Hit;
		Data->CartridgeID = CartridgeID;
		TargetData.Add(Data);
	}
	if (AController* Controller = GetControllerFromActorInfo())
	{
		if (UIMGWeaponStateComponent* WeaponState = Controller->FindComponentByClass<UIMGWeaponStateComponent>())
		{
			WeaponState->AddUnconfirmedServerSideHitMarkers(TargetData, FoundHits);
		}
	}
	OnTargetDataReady(TargetData, FGameplayTag());
}

void UIMGGameplayAbility_WeaponFire::OnTargetDataReady(const FGameplayAbilityTargetDataHandle& TargetData, FGameplayTag ApplicationTag)
{
	if (!IsActive() || !CurrentActorInfo || !CurrentActorInfo->AbilitySystemComponent.IsValid())
	{
		return;
	}
	UAbilitySystemComponent* ASC = CurrentActorInfo->AbilitySystemComponent.Get();
	FScopedPredictionWindow PredictionWindow(ASC);
	FGameplayAbilityTargetDataHandle LocalTargetData(TargetData);
	const bool bValid = ValidateTargetData(LocalTargetData);
	if (bValid && CurrentActorInfo->IsLocallyControlled() && !CurrentActorInfo->IsNetAuthority())
	{
		ASC->CallServerSetReplicatedTargetData(CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey(),
			LocalTargetData, ApplicationTag, ASC->ScopedPredictionKey);
	}

	const bool bCommitted = bValid && CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo);
	if (CurrentActorInfo->IsNetAuthority())
	{
		if (AController* Controller = GetControllerFromActorInfo())
		{
			if (UIMGWeaponStateComponent* WeaponState = Controller->FindComponentByClass<UIMGWeaponStateComponent>())
			{
				WeaponState->ClientConfirmTargetData(LocalTargetData.UniqueId, bCommitted, {});
			}
		}
	}
	if (bCommitted)
	{
		GetWeaponInstance()->AddSpread();
		HandleTargetData(LocalTargetData);
	}
	else
	{
		K2_EndAbility();
	}
	ASC->ConsumeClientReplicatedTargetData(CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey());
}

bool UIMGGameplayAbility_WeaponFire::ValidateTargetData(FGameplayAbilityTargetDataHandle& TargetData) const
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

		if (!CurrentActorInfo->IsNetAuthority())
		{
			continue;
		}
		AActor* Target = Hit->GetActor();
		while (Target && !UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target))
		{
			Target = Target->GetAttachParentActor();
		}
		if (!Target)
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

void UIMGGameplayAbility_WeaponFire::HandleTargetData(const FGameplayAbilityTargetDataHandle& TargetData)
{
	LastShotTime = GetWorld()->GetTimeSeconds();

	if (CurrentActorInfo->IsNetAuthority())
	{
		ApplyGameplayEffectToTarget(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, TargetData, DamageEffect,
			GetAbilityLevel(CurrentSpecHandle, CurrentActorInfo));
		for (int32 Index = 0; Index < TargetData.Num(); ++Index)
		{
			const FGameplayAbilityTargetData* Data = TargetData.Get(Index);
			const FHitResult* Hit = Data ? Data->GetHitResult() : nullptr;
			AActor* HitActor = Hit ? Hit->GetActor() : nullptr;
			if (!HitActor || UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(HitActor))
			{
				continue;
			}
			for (AActor* Parent = HitActor->GetAttachParentActor(); Parent; Parent = Parent->GetAttachParentActor())
			{
				if (UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Parent))
				{
					FGameplayAbilityTargetDataHandle ParentData;
					FGameplayAbilityTargetData_ActorArray* ActorData = new FGameplayAbilityTargetData_ActorArray();
					ActorData->TargetActorArray.Add(Parent);
					ParentData.Add(ActorData);
					ApplyGameplayEffectToTarget(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, ParentData, DamageEffect,
						GetAbilityLevel(CurrentSpecHandle, CurrentActorInfo));
					break;
				}
			}
		}
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
