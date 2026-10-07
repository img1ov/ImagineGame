#include "Character/IMGCharacterLocomotionComponent.h"

#include "AbilitySystem/IMGAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/IMGLocomotionSet.h"
#include "Character/IMGPawnExtensionComponent.h"
#include "Components/GameFrameworkComponentManager.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayEffectTypes.h"
#include "IMGGameplayTags.h"
#include "IMGLogChannels.h"
#include "Templates/UnrealTemplate.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGCharacterLocomotionComponent)

UIMGCharacterLocomotionComponent::UIMGCharacterLocomotionComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UIMGCharacterLocomotionComponent::OnRegister()
{
	Super::OnRegister();

	if (HasBegunPlay())
	{
		InitializeConnections();
	}
}

void UIMGCharacterLocomotionComponent::OnUnregister()
{
	ClearConnections();
	Super::OnUnregister();
}

void UIMGCharacterLocomotionComponent::BeginPlay()
{
	Super::BeginPlay();
	InitializeConnections();
}

void UIMGCharacterLocomotionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearConnections();
	Super::EndPlay(EndPlayReason);
}

bool UIMGCharacterLocomotionComponent::IsLocomotionReady() const
{
	const UIMGPawnExtensionComponent* PawnExtension = PawnExtensionComponent.Get();
	return bGameplayReady && IsValid(MovementComponent) && IsValid(AbilitySystemComponent) && IsValid(LocomotionSet)
		&& PawnExtension && PawnExtension->GetIMGAbilitySystemComponent() == AbilitySystemComponent
		&& AbilitySystemComponent->GetAvatarActor() == GetOwner()
		&& AbilitySystemComponent->GetSet<UIMGLocomotionSet>() == LocomotionSet;
}

void UIMGCharacterLocomotionComponent::InitializeConnections()
{
	if (MovementComponent || !IsRegistered() || !IsValid(this))
	{
		return;
	}

	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		UE_LOG(LogIMG, Error, TEXT("CharacterLocomotionComponent on [%s] requires a Character owner."), *GetNameSafe(GetOwner()));
		return;
	}

	UIMGPawnExtensionComponent* PawnExtension = UIMGPawnExtensionComponent::FindPawnExtensionComponent(Character);
	UGameFrameworkComponentManager* Manager = UGameFrameworkComponentManager::GetForActor(Character);
	if (!PawnExtension || !Manager)
	{
		UE_LOG(LogIMG, Error, TEXT("CharacterLocomotionComponent on [%s] requires PawnExtension and GameFrameworkComponentManager."), *GetNameSafe(GetOwner()));
		return;
	}

	MovementComponent = Character->GetCharacterMovement();
	if (!MovementComponent)
	{
		return;
	}

	MovementComponent->AddTickPrerequisiteComponent(this);
	PawnExtensionComponent = PawnExtension;
	ComponentManager = Manager;
	GameplayReadyHandle = Manager->RegisterAndCallForActorInitState(
		GetOwner(),
		UIMGPawnExtensionComponent::NAME_ActorFeatureName,
		IMGGameplayTags::InitState_GameplayReady,
		FActorInitStateChangedDelegate::CreateUObject(this, &ThisClass::HandleGameplayReady),
		false);
	bGameplayReady = Manager->HasFeatureReachedInitState(GetOwner(), UIMGPawnExtensionComponent::NAME_ActorFeatureName, IMGGameplayTags::InitState_GameplayReady);

	PawnExtension->OnAbilitySystemUninitialized_Register(FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &ThisClass::HandleAbilitySystemUninitialized));
	// Register last: the immediate callback can run Blueprint code that tears down this component.
	PawnExtension->OnAbilitySystemInitialized_RegisterAndCall(FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &ThisClass::HandleAbilitySystemInitialized));
}

void UIMGCharacterLocomotionComponent::ClearConnections()
{
	if (UGameFrameworkComponentManager* Manager = ComponentManager.Get())
	{
		if (GameplayReadyHandle.IsValid())
		{
			Manager->UnregisterActorInitStateDelegate(GetOwner(), GameplayReadyHandle);
		}
	}
	GameplayReadyHandle.Reset();
	ComponentManager.Reset();
	bGameplayReady = false;

	if (UIMGPawnExtensionComponent* PawnExtension = PawnExtensionComponent.Get())
	{
		PawnExtension->UnregisterAbilitySystemDelegates(this);
	}
	PawnExtensionComponent.Reset();

	UnbindLocomotionSet();

	if (MovementComponent)
	{
		MovementComponent->RemoveTickPrerequisiteComponent(this);
		MovementComponent = nullptr;
	}
}

void UIMGCharacterLocomotionComponent::HandleGameplayReady(const FActorInitStateChangedParams& Params)
{
	bGameplayReady = true;
	RefreshLocomotionSet();
}

void UIMGCharacterLocomotionComponent::HandleAbilitySystemInitialized()
{
	RefreshLocomotionSet();
}

void UIMGCharacterLocomotionComponent::HandleAbilitySystemUninitialized()
{
	UnbindLocomotionSet();
}

void UIMGCharacterLocomotionComponent::RefreshLocomotionSet()
{
	if (bRefreshingLocomotionSet)
	{
		return;
	}
	TGuardValue<bool> RefreshGuard(bRefreshingLocomotionSet, true);

	UIMGAbilitySystemComponent* CurrentASC = nullptr;
	const UIMGLocomotionSet* CurrentSet = nullptr;
	if (bGameplayReady && IsValid(MovementComponent))
	{
		if (const UIMGPawnExtensionComponent* PawnExtension = PawnExtensionComponent.Get())
		{
			CurrentASC = PawnExtension->GetIMGAbilitySystemComponent();
			if (IsValid(CurrentASC) && CurrentASC->GetAvatarActor() == GetOwner())
			{
				CurrentSet = CurrentASC->GetSet<UIMGLocomotionSet>();
			}
		}
	}
	if (!IsValid(CurrentSet))
	{
		CurrentASC = nullptr;
		CurrentSet = nullptr;
	}

	if (AbilitySystemComponent == CurrentASC && LocomotionSet == CurrentSet)
	{
		return;
	}

	UnbindLocomotionSet();
	// Unavailable can remove this component or replace the ASC/Set from Blueprint.
	const UIMGPawnExtensionComponent* PawnExtension = PawnExtensionComponent.Get();
	if (!bGameplayReady || !IsValid(MovementComponent) || !PawnExtension
		|| !IsValid(CurrentASC) || !IsValid(CurrentSet)
		|| PawnExtension->GetIMGAbilitySystemComponent() != CurrentASC
		|| CurrentASC->GetAvatarActor() != GetOwner()
		|| CurrentASC->GetSet<UIMGLocomotionSet>() != CurrentSet)
	{
		return;
	}

	AbilitySystemComponent = CurrentASC;
	LocomotionSet = CurrentSet;
	TArray<FGameplayAttribute> BoundAttributes;
	UAttributeSet::GetAttributesFromSetClass(CurrentSet->GetClass(), BoundAttributes);

	for (const FGameplayAttribute& Attribute : BoundAttributes)
	{
		AttributeChangeHandles.Add(Attribute,
			AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(Attribute).AddUObject(this, &ThisClass::HandleAttributeChanged));
	}

	OnLocomotionReady();
}

void UIMGCharacterLocomotionComponent::UnbindLocomotionSet()
{
	const bool bWasBound = LocomotionSet != nullptr;
	if (IsValid(AbilitySystemComponent))
	{
		for (const TPair<FGameplayAttribute, FDelegateHandle>& Binding : AttributeChangeHandles)
		{
			AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(Binding.Key).Remove(Binding.Value);
		}
	}

	AttributeChangeHandles.Reset();
	LocomotionSet = nullptr;
	AbilitySystemComponent = nullptr;
	if (bWasBound)
	{
		OnLocomotionUnavailable();
	}
}

void UIMGCharacterLocomotionComponent::HandleAttributeChanged(const FOnAttributeChangeData& ChangeData)
{
	if (IsLocomotionReady())
	{
		OnLocomotionAttributeChanged(ChangeData.Attribute, ChangeData.OldValue, ChangeData.NewValue);
	}
}

void UIMGCharacterLocomotionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// AttributeSets can be added or replaced after ASC initialization.
	RefreshLocomotionSet();

	if (IsLocomotionReady())
	{
		UpdateLocomotionPreCMC(DeltaTime);
	}
}
