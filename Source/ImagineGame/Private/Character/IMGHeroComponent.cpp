// Fill out your copyright notice in the Description page of Project Settings.

#include "Character/IMGHeroComponent.h"

#include "Components/GameFrameworkComponentDelegates.h"
#include "Logging/MessageLog.h"
#include "IMGLogChannels.h"
#include "EnhancedInputSubsystems.h"
#include "Player/IMGPlayerController.h"
#include "Player/IMGPlayerState.h"
#include "Player/IMGLocalPlayer.h"
#include "Character/IMGPawnExtensionComponent.h"
#include "Character/IMGPawnData.h"
#include "Character/IMGCharacter.h"
#include "AbilitySystem/IMGAbilitySystemComponent.h"
#include "Input/IMGInputConfig.h"
#include "Input/IMGInputComponent.h"
#include "IMGGameplayTags.h"
#include "Components/GameFrameworkComponentManager.h"
#include "PlayerMappableInputConfig.h"
#include "UserSettings/EnhancedInputUserSettings.h"
#include "InputMappingContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGHeroComponent)

#if WITH_EDITOR
#include "Misc/UObjectToken.h"
#endif	// WITH_EDITOR

namespace IMGHero
{
	static const float LookYawRate = 165.0f;
	static const float LookPitchRate = 165.0f;
};

const FName UIMGHeroComponent::NAME_BindInputsNow("BindInputsNow");
const FName UIMGHeroComponent::NAME_ActorFeatureName("IMG");

UIMGHeroComponent::UIMGHeroComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bReadyToBindInputs = false;
}

void UIMGHeroComponent::AddAdditionalInputConfig(const UIMGInputConfig* InputConfig)
{
	TArray<uint32> BindHandles;

	const APawn* Pawn = GetPawn<APawn>();
	if (!Pawn)
	{
		return;
	}
	
	const APlayerController* PC = GetController<APlayerController>();
	check(PC);

	const ULocalPlayer* LP = PC->GetLocalPlayer();
	check(LP);

	UEnhancedInputLocalPlayerSubsystem* Subsystem = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	check(Subsystem);

	if (const UIMGPawnExtensionComponent* PawnExtComp = UIMGPawnExtensionComponent::FindPawnExtensionComponent(Pawn))
	{
		UIMGInputComponent* IMGIC = Pawn->FindComponentByClass<UIMGInputComponent>();
		if (ensureMsgf(IMGIC, TEXT("Unexpected Input Component class! The Gameplay Abilities will not be bound to their inputs. Change the input component to UIMGInputComponent or a subclass of it.")))
		{
			IMGIC->BindAbilityActions(InputConfig, this, &ThisClass::Input_AbilityInputTagPressed, &ThisClass::Input_AbilityInputTagReleased, /*out*/ BindHandles);
		}
	}
}

void UIMGHeroComponent::RemoveAdditionalInputConfig(const UIMGInputConfig* InputConfig)
{
	//@TODO: Implement me!
}

bool UIMGHeroComponent::IsReadyToBindInputs() const
{
	return bReadyToBindInputs;
}

bool UIMGHeroComponent::CanChangeInitState(UGameFrameworkComponentManager* Manager, FGameplayTag CurrentState, FGameplayTag DesiredState) const
{
	check(Manager);

	APawn* Pawn = GetPawn<APawn>();

	if (!CurrentState.IsValid() && DesiredState == IMGGameplayTags::InitState_Spawned)
	{
		return Pawn != nullptr;
	}
	else if (CurrentState == IMGGameplayTags::InitState_Spawned && DesiredState == IMGGameplayTags::InitState_DataAvailable)
	{
		if (!GetPlayerState<AIMGPlayerState>())
		{
			return false;
		}

		if (Pawn->GetLocalRole() != ROLE_SimulatedProxy)
		{
			AController* Controller = GetController<AController>();
			const bool bHasControllerPairedWithPS = (Controller != nullptr)
				&& (Controller->PlayerState != nullptr)
				&& (Controller->PlayerState->GetOwner() == Controller);
			if (!bHasControllerPairedWithPS)
			{
				return false;
			}
		}

		const bool bIsLocallyControlled = Pawn->IsLocallyControlled();
		const bool bIsBot = Pawn->IsBotControlled();
		if (bIsLocallyControlled && !bIsBot)
		{
			AIMGPlayerController* IMGPC = GetController<AIMGPlayerController>();
			if (!Pawn->InputComponent || !IMGPC || !IMGPC->GetLocalPlayer())
			{
				return false;
			}
		}

		return true;
	}
	else if (CurrentState == IMGGameplayTags::InitState_DataAvailable && DesiredState == IMGGameplayTags::InitState_DataInitialized)
	{
		AIMGPlayerState* IMGPS = GetPlayerState<AIMGPlayerState>();
		return IMGPS && Manager->HasFeatureReachedInitState(Pawn, UIMGPawnExtensionComponent::NAME_ActorFeatureName, IMGGameplayTags::InitState_DataInitialized);
	}
	else if (CurrentState == IMGGameplayTags::InitState_DataInitialized && DesiredState == IMGGameplayTags::InitState_GameplayReady)
	{
		return true;
	}

	return false;
}

void UIMGHeroComponent::HandleChangeInitState(UGameFrameworkComponentManager* Manager, FGameplayTag CurrentState, FGameplayTag DesiredState)
{
	if (CurrentState == IMGGameplayTags::InitState_DataAvailable && DesiredState == IMGGameplayTags::InitState_DataInitialized)
	{
		APawn* Pawn = GetPawn<APawn>();
		AIMGPlayerState* IMGPS = GetPlayerState<AIMGPlayerState>();
		if (!ensure(Pawn && IMGPS))
		{
			return;
		}

		if (UIMGPawnExtensionComponent* PawnExtComp = UIMGPawnExtensionComponent::FindPawnExtensionComponent(Pawn))
		{
			PawnExtComp->InitializeAbilitySystem(IMGPS->GetIMGAbilitySystemComponent(), IMGPS);
		}

		if (AIMGPlayerController* IMGPC = GetController<AIMGPlayerController>())
		{
			if (Pawn->InputComponent != nullptr)
			{
				InitializePlayerInput(Pawn->InputComponent);
			}
		}
	}
}

void UIMGHeroComponent::OnActorInitStateChanged(const FActorInitStateChangedParams& Params)
{
	if (Params.FeatureName == UIMGPawnExtensionComponent::NAME_ActorFeatureName
		&& Params.FeatureState == IMGGameplayTags::InitState_DataInitialized)
	{
		CheckDefaultInitialization();
	}
}

void UIMGHeroComponent::CheckDefaultInitialization()
{
	static const TArray<FGameplayTag> StateChain =
	{
		IMGGameplayTags::InitState_Spawned,
		IMGGameplayTags::InitState_DataAvailable,
		IMGGameplayTags::InitState_DataInitialized,
		IMGGameplayTags::InitState_GameplayReady
	};

	ContinueInitStateChain(StateChain);
}

void UIMGHeroComponent::OnRegister()
{
	Super::OnRegister();

	if (!GetPawn<APawn>())
	{
		UE_LOG(LogIMG, Error, TEXT("[UIMGHeroComponent::OnRegister] This component must be placed on a Pawn Blueprint."));

#if WITH_EDITOR
		if (GIsEditor)
		{
			static const FText Message = NSLOCTEXT("IMGHeroComponent", "NotOnPawnError", "This component must be placed on a Pawn Blueprint. This will cause a crash if you PIE!");
			static const FName BattleMessageLogName = TEXT("IMGHeroComponent");

			FMessageLog(BattleMessageLogName).Error()
				->AddToken(FUObjectToken::Create(this, FText::FromString(GetNameSafe(this))))
				->AddToken(FTextToken::Create(Message));

			FMessageLog(BattleMessageLogName).Open();
		}
#endif
	}
	else
	{
		RegisterInitStateFeature();
	}
}

void UIMGHeroComponent::BeginPlay()
{
	Super::BeginPlay();

	BindOnActorInitStateChanged(UIMGPawnExtensionComponent::NAME_ActorFeatureName, FGameplayTag(), false);
	ensure(TryToChangeInitState(IMGGameplayTags::InitState_Spawned));
	CheckDefaultInitialization();
}

void UIMGHeroComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterInitStateFeature();

	Super::EndPlay(EndPlayReason);
}

void UIMGHeroComponent::InitializePlayerInput(UInputComponent* PlayerInputComponent)
{
	check(PlayerInputComponent);
	
	const APawn* Pawn = GetPawn<APawn>();
	if (Pawn == nullptr)
	{
		return;
	}

	const APlayerController* PC = GetController<APlayerController>();
	check(PC);

	const UIMGLocalPlayer* LP = Cast<UIMGLocalPlayer>(PC->GetLocalPlayer());
	check(LP);

	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	check(InputSubsystem);

	InputSubsystem->ClearAllMappings();

	if (const UIMGPawnExtensionComponent* PawnExtComp = UIMGPawnExtensionComponent::FindPawnExtensionComponent(Pawn))
	{
		if (const UIMGPawnData* PawnData = PawnExtComp->GetPawnData<UIMGPawnData>())
		{
			if (const UIMGInputConfig* InputConfig = PawnData->InputConfig)
			{
				for (const FInputMappingContextAndPriority& Mapping : DefaultInputMappings)
				{
					if (UInputMappingContext* IMC = Mapping.InputMapping.LoadSynchronous())
					{
						if (Mapping.bRegisterWithSettings)
						{
							if (UEnhancedInputUserSettings* Settings = InputSubsystem->GetUserSettings())
							{
								Settings->RegisterInputMappingContext(IMC);
							}
							
							FModifyContextOptions Options = {};
							Options.bIgnoreAllPressedKeysUntilRelease = false;
							// Actually add the config to the local player							
							InputSubsystem->AddMappingContext(IMC, Mapping.Priority, Options);
						}
					}
				}
				
				// The IMG Input Component has some additional functions to map Gameplay Tags to an Input Action.
				// If you want this functionality but still want to change your input component class, make it a subclass
				// of the UIMGInputComponent or modify this component accordingly.
				UIMGInputComponent* IMGIC = Cast<UIMGInputComponent>(PlayerInputComponent);
				if (ensureMsgf(IMGIC, TEXT("Unexpected Input Component class! Change the input component to UIMGInputComponent or a subclass of it.")))
				{
					// Add the key mappings that may have been set by the player
					IMGIC->AddInputMappings(InputConfig, InputSubsystem);
					
					// This is where we actually bind and input action to a gameplay tag, which means that Gameplay Ability Blueprints will
					// be triggered directly by these input actions Triggered events. 
					TArray<uint32> BindHandles;
					IMGIC->BindAbilityActions(InputConfig, this, &ThisClass::Input_AbilityInputTagPressed, &ThisClass::Input_AbilityInputTagReleased, BindHandles);
					
					IMGIC->BindNativeAction(InputConfig, IMGGameplayTags::InputTag_Move, ETriggerEvent::Triggered, this, &ThisClass::Input_Move, false);
					IMGIC->BindNativeAction(InputConfig, IMGGameplayTags::InputTag_Look_Mouse, ETriggerEvent::Triggered, this, &ThisClass::Input_LookMouse, false);
					IMGIC->BindNativeAction(InputConfig, IMGGameplayTags::InputTag_Look_Stick, ETriggerEvent::Triggered, this, &ThisClass::Input_LookStick, false);
					IMGIC->BindNativeAction(InputConfig, IMGGameplayTags::InputTag_Crouch, ETriggerEvent::Triggered, this, &ThisClass::Input_Crouch, /*bLogIfNotFound=*/ false);
				}
			}
		}
	}

	if (ensure(!bReadyToBindInputs))
	{
		bReadyToBindInputs = true;
	}

	UGameFrameworkComponentManager::SendGameFrameworkComponentExtensionEvent(const_cast<APlayerController*>(PC), NAME_BindInputsNow);
	UGameFrameworkComponentManager::SendGameFrameworkComponentExtensionEvent(const_cast<APawn*>(Pawn), NAME_BindInputsNow);
}

void UIMGHeroComponent::Input_AbilityInputTagPressed(FGameplayTag InputTag)
{
	if (const APawn* Pawn = GetPawn<APawn>())
	{
		if (const UIMGPawnExtensionComponent* PawnExtComp = UIMGPawnExtensionComponent::FindPawnExtensionComponent(Pawn))
		{
			if (UIMGAbilitySystemComponent* IMGASC = PawnExtComp->GetIMGAbilitySystemComponent())
			{
				IMGASC->AbilityInputTagPressed(InputTag);
			}
		}
	}
}

void UIMGHeroComponent::Input_AbilityInputTagReleased(FGameplayTag InputTag)
{
	if (const APawn* Pawn = GetPawn<APawn>())
	{
		if (const UIMGPawnExtensionComponent* PawnExtComp = UIMGPawnExtensionComponent::FindPawnExtensionComponent(Pawn))
		{
			if (UIMGAbilitySystemComponent* IMGASC = PawnExtComp->GetIMGAbilitySystemComponent())
			{
				IMGASC->AbilityInputTagReleased(InputTag);
			}
		}
	}
}

void UIMGHeroComponent::Input_Move(const FInputActionValue& InputActionValue)
{
	APawn* Pawn = GetPawn<APawn>();
	AController* Controller = Pawn ? Pawn->GetController() : nullptr;

	// If the player has attempted to move again then cancel auto running
	if (AIMGPlayerController* IMGController = Cast<AIMGPlayerController>(Controller))
	{
		//IMGController->SetIsAutoRunning(false);
	}
	
	if (Controller)
	{
		const FVector2D Value = InputActionValue.Get<FVector2D>().GetSafeNormal();
		
		const FRotator MovementRotation(0.0f, Controller->GetControlRotation().Yaw, 0.0f);

		if (Value.X != 0.0f)
		{
			const FVector MovementDirection = MovementRotation.RotateVector(FVector::RightVector);
			Pawn->AddMovementInput(MovementDirection, Value.X);
		}

		if (Value.Y != 0.0f)
		{
			const FVector MovementDirection = MovementRotation.RotateVector(FVector::ForwardVector);
			Pawn->AddMovementInput(MovementDirection, Value.Y);
		}
	}
}

void UIMGHeroComponent::Input_LookMouse(const FInputActionValue& InputActionValue)
{
	APawn* Pawn = GetPawn<APawn>();
	if (!Pawn)
	{
		return;
	}

	const FVector2D Value = InputActionValue.Get<FVector2D>();
	if (Value.X != 0.0f)
	{
		Pawn->AddControllerYawInput(Value.X);
	}

	if (Value.Y != 0.0f)
	{
		Pawn->AddControllerPitchInput(Value.Y);
	}
}

void UIMGHeroComponent::Input_LookStick(const FInputActionValue& InputActionValue)
{
	APawn* Pawn = GetPawn<APawn>();

	if (!Pawn)
	{
		return;
	}
	
	const FVector2D Value = InputActionValue.Get<FVector2D>();

	const UWorld* World = GetWorld();
	check(World);
	
	if (Value.X != 0.0f)
	{
		Pawn->AddControllerYawInput(Value.X * IMGHero::LookYawRate * World->GetDeltaSeconds());
	}

	if (Value.Y != 0.0f)
	{
		Pawn->AddControllerPitchInput(Value.Y * IMGHero::LookPitchRate * World->GetDeltaSeconds());
	}
}

void UIMGHeroComponent::Input_Crouch(const FInputActionValue& InputActionValue)
{
	if (AIMGCharacter* Character = GetPawn<AIMGCharacter>())
	{
		Character->ToggleCrouch();
	}
}
