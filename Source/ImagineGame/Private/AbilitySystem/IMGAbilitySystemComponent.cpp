#include "AbilitySystem/IMGAbilitySystemComponent.h"
#include "AbilitySystem/IMGAbilityTagRelationshipMapping.h"
#include "AbilitySystem/IMGGlobalAbilitySystem.h"
#include "Animation/IMGAnimInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "IMGLogChannels.h"
#include "System/IMGAssetManager.h"
#include "System/IMGGameData.h"
#include "Templates/UnrealTemplate.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGAbilitySystemComponent)

UIMGAbilitySystemComponent::UIMGAbilitySystemComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	FMemory::Memset(ActivationGroupCounts, 0, sizeof(ActivationGroupCounts));
	SetMontageRepAnimPositionMethod(ERepAnimPositionMethod::Position);
}

UIMGAbilitySystemComponent::~UIMGAbilitySystemComponent() = default;

void UIMGAbilitySystemComponent::AddRuntimeAttributeSet(UAttributeSet* AttributeSet)
{
	if (IsValid(AttributeSet))
	{
		AddSpawnedAttribute(AttributeSet);
		NotifyAttributeSetsChanged();
	}
}

void UIMGAbilitySystemComponent::RemoveRuntimeAttributeSet(UAttributeSet* AttributeSet)
{
	if (AttributeSet)
	{
		RemoveSpawnedAttribute(AttributeSet);
		NotifyAttributeSetsChanged();
	}
}

void UIMGAbilitySystemComponent::PostRepNotifies()
{
	Super::PostRepNotifies();
	NotifyAttributeSetsChanged();
}

void UIMGAbilitySystemComponent::NotifyAttributeSetsChanged()
{
	const TArray<UAttributeSet*>& CurrentSets = GetSpawnedAttributes();
	bool bChanged = CurrentSets.Num() != ObservedAttributeSets.Num();
	if (!bChanged)
	{
		for (int32 Index = 0; Index < CurrentSets.Num(); ++Index)
		{
			if (ObservedAttributeSets[Index] != CurrentSets[Index])
			{
				bChanged = true;
				break;
			}
		}
	}
	if (!bChanged)
	{
		return;
	}

	// Publish the new identities before listeners can synchronously grant or remove another set.
	ObservedAttributeSets.Reset(CurrentSets.Num());
	for (UAttributeSet* AttributeSet : CurrentSets)
	{
		ObservedAttributeSets.Add(AttributeSet);
	}
	AttributeSetsUpdated.Broadcast();
}

void UIMGAbilitySystemComponent::OnRegister()
{
	Super::OnRegister();
	MontageGate.bSuspending = false;
	RetryReplicatedMontage();
}

void UIMGAbilitySystemComponent::OnUnregister()
{
	MontageGate.bSuspending = true;
	++MontageGate.AvatarGeneration;
	++MontageGate.Revision;
	MontageGate.ResetReady();
	ClearReplicatedMontageWait();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(MontageGate.RetryTimer);
	}
	MontageGate.RetryTimer.Invalidate();
	Super::OnUnregister();
}

void UIMGAbilitySystemComponent::InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor)
{
	FGameplayAbilityActorInfo* ActorInfo = AbilityActorInfo.Get();
	check(ActorInfo);
	check(InOwnerActor);

	const bool bHasNewPawnAvatar = Cast<APawn>(InAvatarActor) && (InAvatarActor != ActorInfo->AvatarActor);
	if (InAvatarActor != ActorInfo->AvatarActor)
	{
		ClearAbilityInput();
		const bool bLeavingAvatar = !ActorInfo->AvatarActor.IsExplicitlyNull();
		ResetReplicatedMontagePrerequisites();
		// Keep data received before the first avatar, but never carry a previous avatar's pending playback across the handoff.
		if (bLeavingAvatar)
		{
			bPendingMontageRep = false;
		}
	}
	const bool bShouldDeferReplicatedMontage = InAvatarActor
		&& InAvatarActor->GetLocalRole() == ROLE_SimulatedProxy;

	{
		TGuardValue<bool> DeferReplicatedMontage(
			MontageGate.bDeferringForActorInfoInitialization, bShouldDeferReplicatedMontage);
		Super::InitAbilityActorInfo(InOwnerActor, InAvatarActor);
	}
	SetMontageRepAnimPositionMethod(ERepAnimPositionMethod::Position);
	if (bShouldDeferReplicatedMontage && bPendingMontageRep)
	{
		RetryReplicatedMontage();
	}

	if (bHasNewPawnAvatar)
	{
		// Notify all abilities that a new pawn avatar has been set
		for (const FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
		{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
			ensureMsgf(AbilitySpec.Ability && AbilitySpec.Ability->GetInstancingPolicy() != EGameplayAbilityInstancingPolicy::NonInstanced, TEXT("InitAbilityActorInfo: All Abilities should be Instanced (NonInstanced is being deprecated due to usability issues)."));
PRAGMA_ENABLE_DEPRECATION_WARNINGS

			TArray<UGameplayAbility*> Instances = AbilitySpec.GetAbilityInstances();
			for (UGameplayAbility* AbilityInstance : Instances)
			{
				UIMGGameplayAbility* IMGAbilityInstance = Cast<UIMGGameplayAbility>(AbilityInstance);
				if (IMGAbilityInstance)
				{
					// Ability instances may be missing for replays
					IMGAbilityInstance->OnPawnAvatarSet();
				}
			}
		}

		// Register with the global system once we actually have a pawn avatar. We wait until this time since some globally-applied effects may require an avatar.
		if (UIMGGlobalAbilitySystem* GlobalAbilitySystem = UWorld::GetSubsystem<UIMGGlobalAbilitySystem>(GetWorld()))
		{
			GlobalAbilitySystem->RegisterASC(this);
		}

		if (UIMGAnimInstance* IMGAnimInst = Cast<UIMGAnimInstance>(ActorInfo->GetAnimInstance()))
		{
			IMGAnimInst->InitializeWithAbilitySystem(this);
		}

		TryActivateAbilitiesOnSpawn();
	}
}

void UIMGAbilitySystemComponent::ClearActorInfo()
{
	ClearAbilityInput();
	ResetReplicatedMontagePrerequisites();
	bPendingMontageRep = false;
	Super::ClearActorInfo();
}

void UIMGAbilitySystemComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearAbilityInput();
	MontageGate.bEnding = true;
	ResetReplicatedMontagePrerequisites();
	bPendingMontageRep = false;
	MontageGate.bDeferringForActorInfoInitialization = false;
	if (UIMGGlobalAbilitySystem* GlobalAbilitySystem = UWorld::GetSubsystem<UIMGGlobalAbilitySystem>(GetWorld()))
	{
		GlobalAbilitySystem->UnregisterASC(this);
	}

	Super::EndPlay(EndPlayReason);
	ObservedAttributeSets.Reset();
}

void UIMGAbilitySystemComponent::SetTagRelationshipMapping(UIMGAbilityTagRelationshipMapping* NewMapping)
{
	TagRelationshipMapping = NewMapping;
}

void UIMGAbilitySystemComponent::GetAdditionalActivationTagRequirements(const FGameplayTagContainer& AbilityTags,
	FGameplayTagContainer& OutActivationRequired, FGameplayTagContainer& OutActivationBlocked) const
{
	if (TagRelationshipMapping)
	{
		TagRelationshipMapping->GetRequiredAndBlockedActivationTags(AbilityTags, &OutActivationRequired, &OutActivationBlocked);
	}
}

void UIMGAbilitySystemComponent::ResetGameplayTagCounts(FGameplayTagContainer TagContainer, const int32 NewCount)
{
	for (const FGameplayTag& Tag : TagContainer)
	{
		SetLooseGameplayTagCount(Tag, NewCount);
	}
}

void UIMGAbilitySystemComponent::AddDynamicTagGameplayEffect(const FGameplayTag& Tag)
{
	const TSubclassOf<UGameplayEffect> DynamicTagGE = UIMGAssetManager::GetSubclass(UIMGGameData::Get().DynamicTagGameplayEffect);
	if (!DynamicTagGE)
	{
		UE_LOG(LogIMGAbilitySystem, Warning, TEXT("AddDynamicTagGameplayEffect: Unable to find DynamicTagGameplayEffect [%s]."), *UIMGGameData::Get().DynamicTagGameplayEffect.GetAssetName());
		return;
	}

	const FGameplayEffectSpecHandle SpecHandle = MakeOutgoingSpec(DynamicTagGE, 1.0f, MakeEffectContext());
	FGameplayEffectSpec* Spec = SpecHandle.Data.Get();

	if (!Spec)
	{
		UE_LOG(LogIMGAbilitySystem, Warning, TEXT("AddDynamicTagGameplayEffect: Unable to make outgoing spec for [%s]."), *GetNameSafe(DynamicTagGE));
		return;
	}

	Spec->DynamicGrantedTags.AddTag(Tag);

	ApplyGameplayEffectSpecToSelf(*Spec);
}

void UIMGAbilitySystemComponent::RemoveDynamicTagGameplayEffect(const FGameplayTag& Tag)
{
	const TSubclassOf<UGameplayEffect> DynamicTagGE = UIMGAssetManager::GetSubclass(UIMGGameData::Get().DynamicTagGameplayEffect);
	if (!DynamicTagGE)
	{
		UE_LOG(LogIMGAbilitySystem, Warning, TEXT("RemoveDynamicTagGameplayEffect: Unable to find gameplay effect [%s]."), *UIMGGameData::Get().DynamicTagGameplayEffect.GetAssetName());
		return;
	}

	FGameplayEffectQuery Query = FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Tag));
	Query.EffectDefinition = DynamicTagGE;

	RemoveActiveEffects(Query);
}

void UIMGAbilitySystemComponent::GetAbilityTargetData(const FGameplayAbilitySpecHandle AbilityHandle, FGameplayAbilityActivationInfo ActivationInfo, FGameplayAbilityTargetDataHandle& OutTargetDataHandle)
{
	OutTargetDataHandle.Clear();
	TSharedPtr<FAbilityReplicatedDataCache> ReplicatedData = AbilityTargetDataMap.Find(FGameplayAbilitySpecHandleAndPredictionKey(AbilityHandle, ActivationInfo.GetActivationPredictionKey()));
	if (ReplicatedData.IsValid())
	{
		OutTargetDataHandle = ReplicatedData->TargetData;
	}
}
