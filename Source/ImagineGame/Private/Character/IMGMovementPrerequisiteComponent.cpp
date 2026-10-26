#include "Character/IMGMovementPrerequisiteComponent.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "IMGLogChannels.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGMovementPrerequisiteComponent)

UIMGMovementPrerequisiteComponent::UIMGMovementPrerequisiteComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UIMGMovementPrerequisiteComponent::OnRegister()
{
	Super::OnRegister();

	if (HasBegunPlay())
	{
		bPreMovementUpdatesActive = PrepareMovementPrerequisite();
	}
}

void UIMGMovementPrerequisiteComponent::OnUnregister()
{
	ReleaseMovementPrerequisite();
	Super::OnUnregister();
}

void UIMGMovementPrerequisiteComponent::BeginPlay()
{
	// Blueprint BeginPlay needs the references; dispatch starts only after it returns.
	PrepareMovementPrerequisite();
	Super::BeginPlay();
	bPreMovementUpdatesActive = PrepareMovementPrerequisite();
}

void UIMGMovementPrerequisiteComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bPreMovementUpdatesActive = false;
	SetComponentTickEnabled(false);
	// Keep the CMC available to Blueprint EndPlay cleanup.
	Super::EndPlay(EndPlayReason);
	ReleaseMovementPrerequisite();
}

void UIMGMovementPrerequisiteComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (IsMovementPrerequisiteReady() && CanUpdateLocomotion())
	{
		UpdateLocomotionPreCMC(DeltaTime);
	}
}

ACharacter* UIMGMovementPrerequisiteComponent::GetCharacterOwner() const
{
	return IsValid(CharacterOwner) ? CharacterOwner.Get() : nullptr;
}

UCharacterMovementComponent* UIMGMovementPrerequisiteComponent::GetCharacterMovementComponent() const
{
	return IsValid(CharacterMovementComponent) ? CharacterMovementComponent.Get() : nullptr;
}

bool UIMGMovementPrerequisiteComponent::IsMovementPrerequisiteReady() const
{
	return bPreMovementUpdatesActive && HasBegunPlay() && IsRegistered() && !IsBeingDestroyed()
		&& IsValid(CharacterOwner) && !CharacterOwner->IsActorBeingDestroyed()
		&& IsValid(CharacterMovementComponent) && !CharacterMovementComponent->IsBeingDestroyed()
		&& CharacterOwner->GetCharacterMovement() == CharacterMovementComponent;
}

void UIMGMovementPrerequisiteComponent::UpdateLocomotionPreCMC_Implementation(float DeltaTime)
{
}

bool UIMGMovementPrerequisiteComponent::CanUpdateLocomotion() const
{
	return true;
}

bool UIMGMovementPrerequisiteComponent::PrepareMovementPrerequisite()
{
	if (!IsRegistered() || IsBeingDestroyed() || !PrimaryComponentTick.bCanEverTick)
	{
		ReleaseMovementPrerequisite();
		return false;
	}

	ACharacter* Character = Cast<ACharacter>(GetOwner());
	UCharacterMovementComponent* Movement = IsValid(Character) ? Character->GetCharacterMovement() : nullptr;
	if (!IsValid(Character) || Character->IsActorBeingDestroyed()
		|| !IsValid(Movement) || Movement->IsBeingDestroyed() || !Movement->PrimaryComponentTick.bCanEverTick)
	{
		UE_LOG(LogIMG, Error, TEXT("MovementPrerequisiteComponent on [%s] requires a live Character with a tick-capable CharacterMovementComponent."), *GetNameSafe(GetOwner()));
		ReleaseMovementPrerequisite();
		return false;
	}

	if (CharacterMovementComponent != Movement)
	{
		ReleaseMovementPrerequisite();
		CharacterOwner = Character;
		CharacterMovementComponent = Movement;
	}

	Character->ReceiveControllerChangedDelegate.AddUniqueDynamic(this, &ThisClass::HandlePawnControllerChanged);
	// CMC waits for both ticks; PreCMC must also wait for this frame's controller input.
	SetControllerTickPrerequisite(Character->GetController());
	Movement->AddTickPrerequisiteComponent(this);
	SetComponentTickEnabled(true);
	return true;
}

void UIMGMovementPrerequisiteComponent::ReleaseMovementPrerequisite()
{
	bPreMovementUpdatesActive = false;
	SetComponentTickEnabled(false);
	if (IsValid(CharacterOwner))
	{
		CharacterOwner->ReceiveControllerChangedDelegate.RemoveDynamic(this, &ThisClass::HandlePawnControllerChanged);
	}
	SetControllerTickPrerequisite(nullptr);
	if (IsValid(CharacterMovementComponent))
	{
		CharacterMovementComponent->RemoveTickPrerequisiteComponent(this);
	}
	CharacterMovementComponent = nullptr;
	CharacterOwner = nullptr;
}

void UIMGMovementPrerequisiteComponent::SetControllerTickPrerequisite(AController* Controller)
{
	if (AController* PreviousController = TickPrerequisiteController.Get())
	{
		RemoveTickPrerequisiteActor(PreviousController);
	}
	TickPrerequisiteController.Reset();

	if (IsValid(Controller) && !Controller->IsActorBeingDestroyed())
	{
		AddTickPrerequisiteActor(Controller);
		TickPrerequisiteController = Controller;
	}
}

void UIMGMovementPrerequisiteComponent::HandlePawnControllerChanged(APawn* Pawn, AController*, AController* NewController)
{
	if (Pawn == CharacterOwner && bPreMovementUpdatesActive)
	{
		SetControllerTickPrerequisite(NewController);
	}
}
