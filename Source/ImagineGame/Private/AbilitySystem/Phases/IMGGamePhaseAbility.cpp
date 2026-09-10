// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Phases/IMGGamePhaseAbility.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "AbilitySystem/Phases/IMGGamePhaseSubsystem.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGGamePhaseAbility)

#define LOCTEXT_NAMESPACE "UIMGGamePhaseAbility"

UIMGGamePhaseAbility::UIMGGamePhaseAbility(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ReplicationPolicy = EGameplayAbilityReplicationPolicy::ReplicateNo;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;
	NetSecurityPolicy = EGameplayAbilityNetSecurityPolicy::ServerOnly;
}

void UIMGGamePhaseAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (ActorInfo->IsNetAuthority())
	{
		UWorld* World = ActorInfo->AbilitySystemComponent->GetWorld();
		UIMGGamePhaseSubsystem* PhaseSubsystem = UWorld::GetSubsystem<UIMGGamePhaseSubsystem>(World);
		PhaseSubsystem->OnBeginPhase(this, Handle);
	}

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

void UIMGGamePhaseAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (ActorInfo->IsNetAuthority())
	{
		UWorld* World = ActorInfo->AbilitySystemComponent->GetWorld();
		UIMGGamePhaseSubsystem* PhaseSubsystem = UWorld::GetSubsystem<UIMGGamePhaseSubsystem>(World);
		PhaseSubsystem->OnEndPhase(this, Handle);
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

#if WITH_EDITOR
EDataValidationResult UIMGGamePhaseAbility::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = CombineDataValidationResults(Super::IsDataValid(Context), EDataValidationResult::Valid);

	if (!GamePhaseTag.IsValid())
	{
		Result = EDataValidationResult::Invalid;
		Context.AddError(LOCTEXT("GamePhaseTagNotSet", "GamePhaseTag must be set to a tag representing the current phase."));
	}

	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
