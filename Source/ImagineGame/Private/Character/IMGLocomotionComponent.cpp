#include "Character/IMGLocomotionComponent.h"

#include "AbilitySystem/IMGAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/IMGLocomotionSet.h"
#include "Character/IMGCharacter.h"
#include "Character/IMGCharacterMovementComponent.h"
#include "Character/IMGPawnExtensionComponent.h"
#include "GameplayEffectTypes.h"
#include "IMGGameplayTags.h"
#include "IMGLogChannels.h"
#include "Templates/UnrealTemplate.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGLocomotionComponent)

UIMGLocomotionComponent::UIMGLocomotionComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UIMGLocomotionComponent::OnRegister()
{
	Super::OnRegister();

	if (!GetIMGCharacter())
	{
		UE_LOG(LogIMG, Error, TEXT("LocomotionComponent on [%s] requires an IMGCharacter owner."), *GetNameSafe(GetOwner()));
		return;
	}

	if (HasBegunPlay())
	{
		RegisterForGameplayReady();
	}
}

void UIMGLocomotionComponent::OnUnregister()
{
	ClearConnections();
	Super::OnUnregister();
}

void UIMGLocomotionComponent::BeginPlay()
{
	Super::BeginPlay();
	if (IsMovementPrerequisiteReady() && GetIMGCharacter() && !GetIMGCharacterMovementComponent())
	{
		UE_LOG(LogIMG, Error, TEXT("LocomotionComponent on [%s] requires IMGCharacterMovementComponent."), *GetNameSafe(GetOwner()));
	}
	RegisterForGameplayReady();
}

void UIMGLocomotionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearConnections();
	Super::EndPlay(EndPlayReason);
}

bool UIMGLocomotionComponent::CanUpdateLocomotion() const
{
	return IsLocomotionReady();
}

bool UIMGLocomotionComponent::IsLocomotionReady() const
{
	const UIMGPawnExtensionComponent* PawnExtension = PawnExtensionComponent.Get();
	return IsMovementPrerequisiteReady() && GetIMGCharacterMovementComponent()
		&& PawnExtension && PawnExtension->HasReachedInitState(IMGGameplayTags::InitState_GameplayReady)
		&& IsValid(AbilitySystemComponent)
		&& PawnExtension->GetIMGAbilitySystemComponent() == AbilitySystemComponent
		&& AbilitySystemComponent->GetAvatarActor() == GetOwner();
}

AIMGCharacter* UIMGLocomotionComponent::GetIMGCharacter() const
{
	return Cast<AIMGCharacter>(GetOwner());
}

UIMGCharacterMovementComponent* UIMGLocomotionComponent::GetIMGCharacterMovementComponent() const
{
	return Cast<UIMGCharacterMovementComponent>(GetCharacterMovementComponent());
}

void UIMGLocomotionComponent::RegisterForGameplayReady()
{
	if (!IsMovementPrerequisiteReady() || GameplayReadyDelegate.IsBound())
	{
		return;
	}

	const AIMGCharacter* Character = GetIMGCharacter();
	if (!Character)
	{
		return;
	}

	UIMGPawnExtensionComponent* PawnExtension = UIMGPawnExtensionComponent::FindPawnExtensionComponent(Character);
	if (!PawnExtension)
	{
		UE_LOG(LogIMG, Error, TEXT("LocomotionComponent on [%s] requires PawnExtension."), *GetNameSafe(GetOwner()));
		return;
	}

	PawnExtensionComponent = PawnExtension;
	GameplayReadyDelegate.BindDynamic(this, &ThisClass::HandleGameplayReady);
	// Register last: the immediate callback can run Blueprint code that tears down this component.
	if (!PawnExtension->RegisterAndCallForInitStateChange(
		IMGGameplayTags::InitState_GameplayReady,
		GameplayReadyDelegate,
		true))
	{
		UE_LOG(LogIMG, Error, TEXT("LocomotionComponent on [%s] failed to subscribe to PawnExtension GameplayReady."), *GetNameSafe(GetOwner()));
		GameplayReadyDelegate.Unbind();
		PawnExtensionComponent.Reset();
	}
}

void UIMGLocomotionComponent::InitializeGameplayReadyConnections()
{
	UIMGPawnExtensionComponent* PawnExtension = PawnExtensionComponent.Get();
	if (!IsMovementPrerequisiteReady() || !GetIMGCharacterMovementComponent() || !PawnExtension
		|| !PawnExtension->HasReachedInitState(IMGGameplayTags::InitState_GameplayReady))
	{
		return;
	}

	PawnExtension->OnAbilitySystemUninitialized_Register(FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &ThisClass::HandleAbilitySystemUninitialized));
	// Register last: the immediate callback can run Blueprint code that tears down this component.
	PawnExtension->OnAbilitySystemInitialized_RegisterAndCall(FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &ThisClass::HandleAbilitySystemInitialized));
}

void UIMGLocomotionComponent::ClearConnections()
{
	if (UIMGPawnExtensionComponent* PawnExtension = PawnExtensionComponent.Get())
	{
		if (GameplayReadyDelegate.IsBound())
		{
			PawnExtension->UnregisterInitStateDelegate(GameplayReadyDelegate);
		}
		PawnExtension->UnregisterAbilitySystemDelegates(this);
	}
	GameplayReadyDelegate.Unbind();

	PawnExtensionComponent.Reset();
	// Unavailable may re-enter lifecycle code; the old subscription state must already be cleared.
	ClearAbilitySystemConnection();
}

void UIMGLocomotionComponent::HandleGameplayReady(const FActorInitStateChangedParams& Params)
{
	UIMGPawnExtensionComponent* PawnExtension = PawnExtensionComponent.Get();
	if (PawnExtension
		&& Params.FeatureName == UIMGPawnExtensionComponent::NAME_ActorFeatureName
		&& Params.Implementer.Get() == PawnExtension
		&& PawnExtension->HasReachedInitState(IMGGameplayTags::InitState_GameplayReady))
	{
		UE_LOG(LogIMG, Log, TEXT("Locomotion lifecycle: PawnExtension GameplayReady. Owner=[%s]"), *GetNameSafe(GetOwner()));
		InitializeGameplayReadyConnections();
	}
}

void UIMGLocomotionComponent::HandleAbilitySystemInitialized()
{
	RefreshAbilitySystemConnection();
}

void UIMGLocomotionComponent::HandleAbilitySystemUninitialized()
{
	ClearAbilitySystemConnection();
}

void UIMGLocomotionComponent::RefreshAbilitySystemConnection()
{
	if (bRefreshingConnections)
	{
		return;
	}
	TGuardValue<bool> RefreshGuard(bRefreshingConnections, true);

	UIMGAbilitySystemComponent* CurrentASC = nullptr;
	if (IsMovementPrerequisiteReady() && GetIMGCharacterMovementComponent())
	{
		if (const UIMGPawnExtensionComponent* PawnExtension = PawnExtensionComponent.Get())
		{
			if (PawnExtension->HasReachedInitState(IMGGameplayTags::InitState_GameplayReady))
			{
				CurrentASC = PawnExtension->GetIMGAbilitySystemComponent();
				if (!IsValid(CurrentASC) || CurrentASC->GetAvatarActor() != GetOwner())
				{
					CurrentASC = nullptr;
				}
			}
		}
	}

	if (AbilitySystemComponent == CurrentASC)
	{
		RefreshLocomotionSet();
		return;
	}

	ClearAbilitySystemConnection();
	// Unavailable can remove this component or replace the ASC/Set from Blueprint.
	const UIMGPawnExtensionComponent* PawnExtension = PawnExtensionComponent.Get();
	if (!IsMovementPrerequisiteReady() || !GetIMGCharacterMovementComponent() || !PawnExtension
		|| !PawnExtension->HasReachedInitState(IMGGameplayTags::InitState_GameplayReady)
		|| !IsValid(CurrentASC)
		|| PawnExtension->GetIMGAbilitySystemComponent() != CurrentASC
		|| CurrentASC->GetAvatarActor() != GetOwner())
	{
		return;
	}

	AbilitySystemComponent = CurrentASC;
	RefreshLocomotionSet();

	if (LocomotionSet)
	{
		UE_LOG(LogIMG, Log, TEXT("Locomotion lifecycle: Ready. Owner=[%s] ASC=[%s] Set=[%s]"),
			*GetNameSafe(GetOwner()), *GetNameSafe(AbilitySystemComponent), *GetNameSafe(LocomotionSet));
	}
	else
	{
		UE_LOG(LogIMG, Warning,
			TEXT("Locomotion lifecycle: Ready without LocomotionSet. Attribute notifications are disabled and later dynamic Set changes are not observed. Owner=[%s] ASC=[%s]"),
			*GetNameSafe(GetOwner()), *GetNameSafe(AbilitySystemComponent));
	}
	OnLocomotionReady();
}

void UIMGLocomotionComponent::ClearAbilitySystemConnection()
{
	const bool bWasReady = AbilitySystemComponent != nullptr;
	UnbindLocomotionSet();
	AbilitySystemComponent = nullptr;
	if (bWasReady)
	{
		UE_LOG(LogIMG, Log, TEXT("Locomotion lifecycle: Unavailable. Owner=[%s]"), *GetNameSafe(GetOwner()));
		OnLocomotionUnavailable();
	}
}

void UIMGLocomotionComponent::RefreshLocomotionSet()
{
	const UIMGLocomotionSet* CurrentSet = nullptr;
	if (IsLocomotionReady())
	{
		CurrentSet = AbilitySystemComponent->GetSet<UIMGLocomotionSet>();
	}

	if (LocomotionSet == CurrentSet)
	{
		return;
	}

	UnbindLocomotionSet();
	if (!IsValid(CurrentSet) || !IsLocomotionReady()
		|| AbilitySystemComponent->GetSet<UIMGLocomotionSet>() != CurrentSet)
	{
		return;
	}

	LocomotionSet = CurrentSet;
	TArray<FGameplayAttribute> BoundAttributes;
	UAttributeSet::GetAttributesFromSetClass(CurrentSet->GetClass(), BoundAttributes);

	for (const FGameplayAttribute& Attribute : BoundAttributes)
	{
		AttributeChangeHandles.Add(Attribute,
			AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(Attribute).AddUObject(this, &ThisClass::HandleAttributeChanged));
	}
}

void UIMGLocomotionComponent::UnbindLocomotionSet()
{
	if (IsValid(AbilitySystemComponent))
	{
		for (const TPair<FGameplayAttribute, FDelegateHandle>& Binding : AttributeChangeHandles)
		{
			AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(Binding.Key).Remove(Binding.Value);
		}
	}

	AttributeChangeHandles.Reset();
	LocomotionSet = nullptr;
}

void UIMGLocomotionComponent::HandleAttributeChanged(const FOnAttributeChangeData& ChangeData)
{
	if (IsLocomotionReady())
	{
		OnLocomotionAttributeChanged(ChangeData.Attribute, ChangeData.OldValue, ChangeData.NewValue);
	}
}
