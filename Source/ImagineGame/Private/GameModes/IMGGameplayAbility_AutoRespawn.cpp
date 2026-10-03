#include "GameModes/IMGGameplayAbility_AutoRespawn.h"

#include "Character/IMGHealthComponent.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameModes/IMGGameMode.h"
#include "IMGGameplayTags.h"
#include "Messages/IMGVerbMessage.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGGameplayAbility_AutoRespawn)

UIMGGameplayAbility_AutoRespawn::UIMGGameplayAbility_AutoRespawn(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ActivationPolicy = EIMGAbilityActivationPolicy::OnSpawn;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;
	FGameplayTagContainer Tags;
	Tags.AddTag(IMGGameplayTags::Ability_Behavior_SurvivesDeath);
	SetAssetTags(Tags);
}

void UIMGGameplayAbility_AutoRespawn::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	BindHealthComponent();
}

void UIMGGameplayAbility_AutoRespawn::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	UnbindHealthComponent();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RespawnTimerHandle);
	}
	ControllerToRestart.Reset();
	DyingAvatar.Reset();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UIMGGameplayAbility_AutoRespawn::OnPawnAvatarSet()
{
	Super::OnPawnAvatarSet();
	if (IsActive())
	{
		BindHealthComponent();
		if (ControllerToRestart.IsValid() && GetAvatarActorFromActorInfo() && GetAvatarActorFromActorInfo() != DyingAvatar.Get())
		{
			BroadcastRespawnMessage(RespawnCompletedMessageTag, 0.0);
			ControllerToRestart.Reset();
			DyingAvatar.Reset();
		}
	}
}

void UIMGGameplayAbility_AutoRespawn::BindHealthComponent()
{
	UIMGHealthComponent* Health = UIMGHealthComponent::FindHealthComponent(GetAvatarActorFromActorInfo());
	if (BoundHealthComponent.Get() == Health)
	{
		return;
	}
	UnbindHealthComponent();
	if (Health)
	{
		Health->OnDeathStarted.AddDynamic(this, &ThisClass::OnDeathStarted);
		BoundHealthComponent = Health;
		if (Health->IsDeadOrDying())
		{
			OnDeathStarted(Health->GetOwner());
		}
	}
}

void UIMGGameplayAbility_AutoRespawn::UnbindHealthComponent()
{
	if (UIMGHealthComponent* Health = BoundHealthComponent.Get())
	{
		Health->OnDeathStarted.RemoveDynamic(this, &ThisClass::OnDeathStarted);
	}
	BoundHealthComponent.Reset();
}

void UIMGGameplayAbility_AutoRespawn::OnDeathStarted(AActor* DyingActor)
{
	if (!IsActive() || ControllerToRestart.IsValid())
	{
		return;
	}
	ControllerToRestart = GetControllerFromActorInfo();
	if (!ControllerToRestart.IsValid())
	{
		return;
	}
	DyingAvatar = DyingActor;

	BroadcastRespawnMessage(RespawnStartedMessageTag, RespawnDelaySeconds);
	if (CurrentActorInfo->IsNetAuthority())
	{
		GetWorld()->GetTimerManager().SetTimer(RespawnTimerHandle, this, &ThisClass::RestartPlayer,
			FMath::Max(RespawnDelaySeconds, 0.01f));
	}
}

void UIMGGameplayAbility_AutoRespawn::RestartPlayer()
{
	AController* Controller = ControllerToRestart.Get();
	const UIMGHealthComponent* Health = Controller ? UIMGHealthComponent::FindHealthComponent(Controller->GetPawn()) : nullptr;
	if (Controller && (!Controller->GetPawn() || (Health && Health->IsDeadOrDying())))
	{
		if (AIMGGameMode* GameMode = GetWorld()->GetAuthGameMode<AIMGGameMode>())
		{
			GameMode->RequestPlayerRestartNextFrame(Controller, true);
		}
	}
}

void UIMGGameplayAbility_AutoRespawn::BroadcastRespawnMessage(FGameplayTag MessageTag, double Magnitude) const
{
	if (!MessageTag.IsValid())
	{
		return;
	}
	FIMGVerbMessage Message;
	Message.Verb = MessageTag;
	Message.Instigator = CurrentActorInfo->OwnerActor.Get();
	Message.Target = ControllerToRestart.Get();
	Message.Magnitude = Magnitude;
	UGameplayMessageSubsystem::Get(this).BroadcastMessage(MessageTag, Message);
}
