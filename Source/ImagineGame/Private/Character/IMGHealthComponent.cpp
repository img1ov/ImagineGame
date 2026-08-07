// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/IMGHealthComponent.h"

#include "IMGLogChannels.h"
#include "System/IMGAssetManager.h"
#include "IMGGameplayTags.h"
#include "Net/UnrealNetwork.h"
#include "AbilitySystem/IMGAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/IMGHealthSet.h"
#include "Messages/IMGVerbMessage.h"
#include "Messages/IMGVerbMessageHelpers.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "System/IMGGameData.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGHealthComponent)

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_IMG_Elimination_Message, "IMG.Elimination.Message");

UIMGHealthComponent::UIMGHealthComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.bCanEverTick = false;

	SetIsReplicatedByDefault(true);

	AbilitySystemComponent = nullptr;
	HealthSet = nullptr;
	DeathState = EIMGDeathState::NotDead;
}

void UIMGHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UIMGHealthComponent, DeathState);
}

void UIMGHealthComponent::OnUnregister()
{
	UninitializeFromAbilitySystem();
	
	Super::OnUnregister();
}

void UIMGHealthComponent::InitializeWithAbilitySystem(UIMGAbilitySystemComponent* InASC)
{
	AActor* Owner = GetOwner();
	check(Owner);
	
	if (AbilitySystemComponent)
	{
		UE_LOG(LogIMG, Error, TEXT("UIMGHealthComponent: Health component for owner [%s] has already been initialized with an ability system."), *GetNameSafe(Owner));
		return;
	}

	AbilitySystemComponent = InASC;
	if (!AbilitySystemComponent)
	{
		UE_LOG(LogIMG, Error, TEXT("UIMGHealthComponent: Cannot initialize health component for owner [%s] with NULL ability system."), *GetNameSafe(Owner));
		return;
	}
	
	HealthSet = AbilitySystemComponent->GetSet<UIMGHealthSet>();
	if (!HealthSet)
	{
		UE_LOG(LogIMG, Error, TEXT("ActHealthComponent: Cannot initialize health component for owner [%s] with NULL health set on the ability system."), *GetNameSafe(Owner));
		return;
	}
	
	// Register to listen for attribute changes.
	HealthSet->OnHealthChanged.AddUObject(this, &ThisClass::HandleHealthChanged);
	HealthSet->OnMaxHealthChanged.AddUObject(this, &ThisClass::HandleMaxHealthChanged);
	HealthSet->OnOutOfHealth.AddUObject(this, &ThisClass::HandleOutOfHealth);

	// TEMP: Reset attributes to default values.  Eventually this will be driven by a spread sheet.
	AbilitySystemComponent->SetNumericAttributeBase(UIMGHealthSet::GetHealthAttribute(), HealthSet->GetMaxHealth());

	ClearGameplayTags();
	
	OnHealthChanged.Broadcast(this, HealthSet->GetHealth(), HealthSet->GetHealth(), nullptr);
	OnMaxHealthChanged.Broadcast(this, HealthSet->GetMaxHealth(), HealthSet->GetMaxHealth(), nullptr);
}

void UIMGHealthComponent::UninitializeFromAbilitySystem()
{
	ClearGameplayTags();

	if (HealthSet)
	{
		HealthSet->OnHealthChanged.RemoveAll(this);
		HealthSet->OnMaxHealthChanged.RemoveAll(this);
		HealthSet->OnOutOfHealth.RemoveAll(this);
	}

	HealthSet = nullptr;
	AbilitySystemComponent = nullptr;
}

void UIMGHealthComponent::ClearGameplayTags()
{
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->SetLooseGameplayTagCount(IMGGameplayTags::Status_Death_Dying, 0);
		AbilitySystemComponent->SetLooseGameplayTagCount(IMGGameplayTags::Status_Death_Dead, 0);
	}
}

float UIMGHealthComponent::GetHealth() const
{
	return (HealthSet ? HealthSet->GetHealth() : 0.0f);
}

float UIMGHealthComponent::GetMaxHealth() const
{
	return (HealthSet ? HealthSet->GetMaxHealth() : 0.0f);
}

float UIMGHealthComponent::GetHealthNormalized() const
{
	if (HealthSet)
	{
		const float Health = HealthSet->GetHealth();
		const float MaxHealth = HealthSet->GetMaxHealth();

		return ((MaxHealth > 0.0f) ? (Health / MaxHealth) : 0.0f);
	}

	return 0.0f;
}

void UIMGHealthComponent::HandleHealthChanged(AActor* DamageInstigator, AActor* DamageCauser,
	const FGameplayEffectSpec* DamageEffectSpec, float DamageMagnitude, float OldValue, float NewValue)
{
	OnHealthChanged.Broadcast(this, OldValue, NewValue, DamageInstigator);
}

void UIMGHealthComponent::HandleMaxHealthChanged(AActor* DamageInstigator, AActor* DamageCauser,
	const FGameplayEffectSpec* DamageEffectSpec, float DamageMagnitude, float OldValue, float NewValue)
{
	OnMaxHealthChanged.Broadcast(this, OldValue, NewValue, DamageInstigator);
}

void UIMGHealthComponent::HandleOutOfHealth(AActor* DamageInstigator, AActor* DamageCauser,
	const FGameplayEffectSpec* DamageEffectSpec, float DamageMagnitude, float OldValue, float NewValue)
{
#if WITH_SERVER_CODE
	// Drive death state on the server. Replication will trigger StartDeath/FinishDeath on clients via OnRep_DeathState.
	if (GetOwnerRole() == ROLE_Authority && DeathState == EIMGDeathState::NotDead)
	{
		StartDeath();

		// If no ability/animation finishes death explicitly, finish next tick so we don't get stuck in "dying".
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimerForNextTick(this, &ThisClass::FinishDeath);
		}
	}

	if (AbilitySystemComponent && DamageEffectSpec)
	{
		// Send the "GameplayEvent.Death" gameplay event through the owner's ability system.  This can be used to trigger a death gameplay ability.
		{
			FGameplayEventData Payload;
			Payload.EventTag = IMGGameplayTags::GameplayEvent_Death;
			Payload.Instigator = DamageInstigator;
			Payload.Target = AbilitySystemComponent->GetAvatarActor();
			Payload.OptionalObject = DamageEffectSpec->Def;
			Payload.ContextHandle = DamageEffectSpec->GetEffectContext();
			Payload.InstigatorTags = *DamageEffectSpec->CapturedSourceTags.GetAggregatedTags();
			Payload.TargetTags = *DamageEffectSpec->CapturedTargetTags.GetAggregatedTags();
			Payload.EventMagnitude = DamageMagnitude;
		
			FScopedPredictionWindow NewScopedWindow(AbilitySystemComponent, true);
			AbilitySystemComponent->HandleGameplayEvent(Payload.EventTag, &Payload);
		}
		
		// Send a standardized verb message that other systems can observe
		{
			FIMGVerbMessage Message;
			Message.Verb = TAG_IMG_Elimination_Message;
			Message.Instigator = DamageInstigator;
			Message.InstigatorTags = *DamageEffectSpec->CapturedSourceTags.GetAggregatedTags();
			Message.Target = UIMGVerbMessageHelpers::GetPlayerStateFromObject(AbilitySystemComponent->GetAvatarActor());
			Message.TargetTags = *DamageEffectSpec->CapturedTargetTags.GetAggregatedTags();
			//@TODO: Fill out context tags, and any non-ability-system source/instigator tags
			//@TODO: Determine if it's an opposing team kill, self-own, team kill, etc...

			UGameplayMessageSubsystem& MessageSystem = UGameplayMessageSubsystem::Get(GetWorld());
			MessageSystem.BroadcastMessage(Message.Verb, Message);
		}
		
		//@TODO: assist messages (could compute from damage dealt elsewhere)?
	}
#endif // #if WITH_SERVER_CODE
}

void UIMGHealthComponent::OnRep_DeathState(EIMGDeathState OldDeathState)
{
	const EIMGDeathState NewDeathState = DeathState;
	// Revert the death state for now since we rely on StartDeath and FinishDeath to change it.
    DeathState = OldDeathState;
	
	if (OldDeathState > NewDeathState)
	{
		// The server is trying to set us back but we've already predicted past the server state.
		UE_LOG(LogIMG, Warning, TEXT("IMGHealthComponent: Predicted past server death state [%d] -> [%d] for owner [%s]."), (uint8)OldDeathState, (uint8)NewDeathState, *GetNameSafe(GetOwner()));
		return;
	}
	
	if (OldDeathState == EIMGDeathState::NotDead)
	{
		if (NewDeathState == EIMGDeathState::DeathStarted)
		{
			StartDeath();
		}
		else if (NewDeathState == EIMGDeathState::DeathFinished)
		{
			StartDeath();
			FinishDeath();
		}
		else
		{
			UE_LOG(LogIMG, Error, TEXT("IMGHealthComponent: Invalid death transition [%d] -> [%d] for owner [%s]."), (uint8)OldDeathState, (uint8)NewDeathState, *GetNameSafe(GetOwner()));
		}
	}
	else if (OldDeathState == EIMGDeathState::DeathStarted)
	{
		if (NewDeathState == EIMGDeathState::DeathFinished)
		{
			FinishDeath();
		}
		else
		{
			UE_LOG(LogIMG, Error, TEXT("IMGHealthComponent: Invalid death transition [%d] -> [%d] for owner [%s]."), (uint8)OldDeathState, (uint8)NewDeathState, *GetNameSafe(GetOwner()));
		}
	}
	
	ensureMsgf((DeathState == NewDeathState), TEXT("IMGHealthComponent: Death transition failed [%d] -> [%d] for owner [%s]."), (uint8)OldDeathState, (uint8)NewDeathState, *GetNameSafe(GetOwner()));
}

void UIMGHealthComponent::StartDeath()
{
	if (DeathState != EIMGDeathState::NotDead)
	{
		return;
	}
	
	DeathState = EIMGDeathState::DeathStarted;
	
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->SetLooseGameplayTagCount(IMGGameplayTags::Status_Death_Dying, 1);
	}
	
	AActor* Owner = GetOwner();
	check(Owner);

	OnDeathStarted.Broadcast(Owner);

	Owner->ForceNetUpdate();
}

void UIMGHealthComponent::FinishDeath()
{
	if (DeathState != EIMGDeathState::DeathStarted)
	{
		return;
	}
	
	DeathState = EIMGDeathState::DeathFinished;
	
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->SetLooseGameplayTagCount(IMGGameplayTags::Status_Death_Dead, 1);
	}
	
	AActor* Owner = GetOwner();
	check(Owner);

	OnDeathFinished.Broadcast(Owner);

	Owner->ForceNetUpdate();
}

void UIMGHealthComponent::DamageSelfDestruct(bool bFellOutOfWorld)
{
	if ((DeathState == EIMGDeathState::NotDead) && AbilitySystemComponent)
	{
		const TSubclassOf<UGameplayEffect> DamageGE = UIMGAssetManager::GetSubclass(UIMGGameData::Get().DamageGameplayEffect_SetByCaller);
		if (!DamageGE)
		{
			UE_LOG(LogIMG, Error, TEXT("UIMGHealthComponent: DamageSelfDestruct failed for owner [%s]. Unable to find gameplay effect [%s]."), *GetNameSafe(GetOwner()), *UIMGGameData::Get().DamageGameplayEffect_SetByCaller.GetAssetName());
			return;
		}
		
		FGameplayEffectSpecHandle SpecHandle = AbilitySystemComponent->MakeOutgoingSpec(DamageGE, 1.0f, AbilitySystemComponent->MakeEffectContext());
		FGameplayEffectSpec* Spec = SpecHandle.Data.Get();
		
		if (!Spec)
		{
			UE_LOG(LogIMG, Error, TEXT("ActHealthComponent: DamageSelfDestruct failed for owner [%s]. Unable to make outgoing spec for [%s]."), *GetNameSafe(GetOwner()), *GetNameSafe(DamageGE));
			return;
		}
		
		Spec->AddDynamicAssetTag(TAG_Gameplay_DamageSelfDestruct);
		
		if (bFellOutOfWorld)
		{
			Spec->AddDynamicAssetTag(TAG_Gameplay_FellOutOfWorld);
		}
		
		const float DamageAmount = GetMaxHealth();
		
		Spec->SetSetByCallerMagnitude(IMGGameplayTags::SetByCaller_Damage, DamageAmount);
		AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*Spec);
	}
}
