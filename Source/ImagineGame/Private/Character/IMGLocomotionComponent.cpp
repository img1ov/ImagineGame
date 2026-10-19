#include "Character/IMGLocomotionComponent.h"

#include "AbilitySystem/IMGAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/IMGLocomotionSet.h"
#include "Character/IMGCharacterMovementComponent.h"
#include "Character/IMGPawnExtensionComponent.h"
#include "Components/GameFrameworkComponentManager.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayEffectTypes.h"
#include "IMGGameplayTags.h"
#include "IMGLogChannels.h"
#include "Templates/UnrealTemplate.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGLocomotionComponent)

UIMGLocomotionComponent::UIMGLocomotionComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UIMGLocomotionComponent::OnRegister()
{
	Super::OnRegister();

	if (HasBegunPlay())
	{
		InitializeConnections();
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
	UE_LOG(LogIMG, Log, TEXT("Locomotion lifecycle: BeginPlay. Component=[%s] Owner=[%s] TickEnabled=[%s]"),
		*GetNameSafe(this), *GetNameSafe(GetOwner()), IsComponentTickEnabled() ? TEXT("true") : TEXT("false"));
	InitializeConnections();
}

void UIMGLocomotionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UE_LOG(LogIMG, Log, TEXT("Locomotion lifecycle: EndPlay. Component=[%s] Owner=[%s] Reason=[%d]"),
		*GetNameSafe(this), *GetNameSafe(GetOwner()), static_cast<int32>(EndPlayReason));
	ClearConnections();
	Super::EndPlay(EndPlayReason);
}

bool UIMGLocomotionComponent::IsLocomotionReady() const
{
	const UIMGPawnExtensionComponent* PawnExtension = PawnExtensionComponent.Get();
	return bGameplayReady && IsValid(MovementComponent) && IsValid(AbilitySystemComponent) && IsValid(LocomotionSet)
		&& PawnExtension && PawnExtension->GetIMGAbilitySystemComponent() == AbilitySystemComponent
		&& AbilitySystemComponent->GetAvatarActor() == GetOwner()
		&& AbilitySystemComponent->GetSet<UIMGLocomotionSet>() == LocomotionSet;
}

void UIMGLocomotionComponent::InitializeConnections()
{
	if (MovementComponent || !IsRegistered() || !IsValid(this))
	{
		return;
	}

	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		UE_LOG(LogIMG, Error, TEXT("LocomotionComponent on [%s] requires a Character owner."), *GetNameSafe(GetOwner()));
		return;
	}

	UIMGPawnExtensionComponent* PawnExtension = UIMGPawnExtensionComponent::FindPawnExtensionComponent(Character);
	UGameFrameworkComponentManager* Manager = UGameFrameworkComponentManager::GetForActor(Character);
	if (!PawnExtension || !Manager)
	{
		UE_LOG(LogIMG, Error, TEXT("LocomotionComponent on [%s] requires PawnExtension and GameFrameworkComponentManager."), *GetNameSafe(GetOwner()));
		return;
	}

	UIMGCharacterMovementComponent* IMGMovementComponent = Cast<UIMGCharacterMovementComponent>(Character->GetCharacterMovement());
	if (!IMGMovementComponent)
	{
		UE_LOG(LogIMG, Error, TEXT("LocomotionComponent on [%s] requires IMGCharacterMovementComponent."), *GetNameSafe(GetOwner()));
		return;
	}

	MovementComponent = IMGMovementComponent;
	PreMovementUpdateHandle = IMGMovementComponent->OnPreMovementUpdate().AddUObject(this, &ThisClass::HandlePreMovementUpdate);
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
	UE_LOG(LogIMG, Log, TEXT("Locomotion lifecycle: Connections initialized. Component=[%s] Owner=[%s] GameplayReady=[%s]"),
		*GetNameSafe(this), *GetNameSafe(GetOwner()), bGameplayReady ? TEXT("true") : TEXT("false"));
	// Register last: the immediate callback can run Blueprint code that tears down this component.
	PawnExtension->OnAbilitySystemInitialized_RegisterAndCall(FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &ThisClass::HandleAbilitySystemInitialized));
}

void UIMGLocomotionComponent::ClearConnections()
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
		if (UIMGCharacterMovementComponent* IMGMovementComponent = Cast<UIMGCharacterMovementComponent>(MovementComponent))
		{
			if (PreMovementUpdateHandle.IsValid())
			{
				IMGMovementComponent->OnPreMovementUpdate().Remove(PreMovementUpdateHandle);
			}
		}
		MovementComponent = nullptr;
	}
	PreMovementUpdateHandle.Reset();
}

void UIMGLocomotionComponent::HandleGameplayReady(const FActorInitStateChangedParams& Params)
{
	bGameplayReady = true;
	UE_LOG(LogIMG, Log, TEXT("Locomotion lifecycle: GameplayReady. Component=[%s] Owner=[%s]"),
		*GetNameSafe(this), *GetNameSafe(GetOwner()));
	RefreshLocomotionSet();
}

void UIMGLocomotionComponent::HandleAbilitySystemInitialized()
{
	UE_LOG(LogIMG, Log, TEXT("Locomotion lifecycle: ASC initialized. Component=[%s] Owner=[%s]"),
		*GetNameSafe(this), *GetNameSafe(GetOwner()));
	RefreshLocomotionSet();
}

void UIMGLocomotionComponent::HandleAbilitySystemUninitialized()
{
	UE_LOG(LogIMG, Log, TEXT("Locomotion lifecycle: ASC uninitialized. Component=[%s] Owner=[%s]"),
		*GetNameSafe(this), *GetNameSafe(GetOwner()));
	UnbindLocomotionSet();
}

void UIMGLocomotionComponent::RefreshLocomotionSet()
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

	UE_LOG(LogIMG, Log, TEXT("Locomotion lifecycle: Ready. Component=[%s] Owner=[%s] ASC=[%s] Set=[%s] Attributes=[%d]"),
		*GetNameSafe(this), *GetNameSafe(GetOwner()), *GetNameSafe(AbilitySystemComponent), *GetNameSafe(LocomotionSet), BoundAttributes.Num());
	OnLocomotionReady();
}

void UIMGLocomotionComponent::UnbindLocomotionSet()
{
	const bool bWasBound = LocomotionSet != nullptr;
	const FString PreviousASCName = GetNameSafe(AbilitySystemComponent);
	const FString PreviousSetName = GetNameSafe(LocomotionSet);
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
		UE_LOG(LogIMG, Log, TEXT("Locomotion lifecycle: Unavailable. Component=[%s] Owner=[%s] PreviousASC=[%s] PreviousSet=[%s]"),
			*GetNameSafe(this), *GetNameSafe(GetOwner()), *PreviousASCName, *PreviousSetName);
		OnLocomotionUnavailable();
	}
}

void UIMGLocomotionComponent::HandleAttributeChanged(const FOnAttributeChangeData& ChangeData)
{
	if (IsLocomotionReady())
	{
		OnLocomotionAttributeChanged(ChangeData.Attribute, ChangeData.OldValue, ChangeData.NewValue);
	}
}

void UIMGLocomotionComponent::HandlePreMovementUpdate(float DeltaTime)
{
	// AttributeSets can be added or replaced after ASC initialization.
	RefreshLocomotionSet();

	if (IsLocomotionReady())
	{
		UpdateLocomotionPreCMC(DeltaTime);
	}
}
