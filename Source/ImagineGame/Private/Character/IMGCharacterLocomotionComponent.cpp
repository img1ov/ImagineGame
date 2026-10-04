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

void UIMGCharacterLocomotionComponent::InitializeConnections()
{
	if (MovementComponent)
	{
		return;
	}

	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		UE_LOG(LogIMG, Error, TEXT("CharacterLocomotionComponent on [%s] requires a Character owner."), *GetNameSafe(GetOwner()));
		return;
	}

	MovementComponent = Character->GetCharacterMovement();
	if (!MovementComponent)
	{
		return;
	}

	MovementComponent->AddTickPrerequisiteComponent(this);

	if (UGameFrameworkComponentManager* Manager = UGameFrameworkComponentManager::GetForActor(Character))
	{
		ComponentManager = Manager;
		GameplayReadyHandle = Manager->RegisterAndCallForActorInitState(
			GetOwner(),
			UIMGPawnExtensionComponent::NAME_ActorFeatureName,
			IMGGameplayTags::InitState_GameplayReady,
			FActorInitStateChangedDelegate::CreateUObject(this, &ThisClass::HandleGameplayReady),
			false);

		if (Manager->HasFeatureReachedInitState(GetOwner(), UIMGPawnExtensionComponent::NAME_ActorFeatureName, IMGGameplayTags::InitState_GameplayReady))
		{
			bGameplayReady = true;
			RefreshLocomotionSet();
		}
	}
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

void UIMGCharacterLocomotionComponent::RefreshLocomotionSet()
{
	UIMGAbilitySystemComponent* CurrentASC = nullptr;
	const UIMGLocomotionSet* CurrentSet = nullptr;
	if (bGameplayReady && MovementComponent)
	{
		if (const UIMGPawnExtensionComponent* PawnExtension = UIMGPawnExtensionComponent::FindPawnExtensionComponent(GetOwner()))
		{
			CurrentASC = PawnExtension->GetIMGAbilitySystemComponent();
			if (CurrentASC)
			{
				CurrentSet = CurrentASC->GetSet<UIMGLocomotionSet>();
			}
		}
	}

	if (AbilitySystemComponent == CurrentASC && LocomotionSet == CurrentSet)
	{
		return;
	}

	UnbindLocomotionSet();
	if (!CurrentASC || !CurrentSet)
	{
		return;
	}

	AbilitySystemComponent = CurrentASC;
	LocomotionSet = CurrentSet;
	UAttributeSet::GetAttributesFromSetClass(CurrentSet->GetClass(), BoundAttributes);

	for (const FGameplayAttribute& Attribute : BoundAttributes)
	{
		AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(Attribute).AddUObject(this, &ThisClass::HandleAttributeChanged);
	}

	OnLocomotionReady();
}

void UIMGCharacterLocomotionComponent::UnbindLocomotionSet()
{
	if (!LocomotionSet)
	{
		return;
	}

	if (AbilitySystemComponent)
	{
		for (const FGameplayAttribute& Attribute : BoundAttributes)
		{
			AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(Attribute).RemoveAll(this);
		}
	}

	BoundAttributes.Reset();
	LocomotionSet = nullptr;
	AbilitySystemComponent = nullptr;
	OnLocomotionUnavailable();
}

void UIMGCharacterLocomotionComponent::HandleAttributeChanged(const FOnAttributeChangeData& ChangeData)
{
	if (AbilitySystemComponent && LocomotionSet
		&& AbilitySystemComponent->GetSet<UIMGLocomotionSet>() == LocomotionSet)
	{
		OnLocomotionAttributeChanged(ChangeData.Attribute, ChangeData.OldValue, ChangeData.NewValue);
	}
}

void UIMGCharacterLocomotionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	RefreshLocomotionSet();

	if (LocomotionSet && MovementComponent)
	{
		UpdateLocomotionPreCMC(DeltaTime);
	}
}
