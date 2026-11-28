
#include "Character/IMGHeroComponent.h"
#include "Camera/IMGCameraComponent.h"
#include "Camera/IMGCameraMode.h"

#include "Components/GameFrameworkComponentDelegates.h"
#include "Logging/MessageLog.h"
#include "IMGLogChannels.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Player/IMGPlayerController.h"
#include "Player/IMGPlayerState.h"
#include "Character/IMGPawnExtensionComponent.h"
#include "Character/IMGPawnData.h"
#include "Character/IMGCharacter.h"
#include "AbilitySystem/IMGAbilitySystemComponent.h"
#include "Input/IMGInputConfig.h"
#include "Input/IMGInputComponent.h"
#include "IMGGameplayTags.h"
#include "Components/GameFrameworkComponentManager.h"
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

	static FVector MakePlanarMovementIntent(float AngleRadians, float Magnitude)
	{
		float SinAngle = 0.0f;
		float CosAngle = 1.0f;
		FMath::SinCos(&SinAngle, &CosAngle, AngleRadians);
		return FVector(CosAngle * Magnitude, SinAngle * Magnitude, 0.0f);
	}
};

const FName UIMGHeroComponent::NAME_BindInputsNow("BindInputsNow");
const FName UIMGHeroComponent::NAME_ActorFeatureName("IMG");

FVector UIMGHeroComponent::FMovementIntentProcessor::UpdateMovementIntent(
	const FVector& DesiredMovementIntent,
	const FVector& ActorFacing,
	float DeltaSeconds,
	const FIMGMovementIntentSettings& Settings,
	bool& bOutTriggeredPivot)
{
	bOutTriggeredPivot = false;

	const FVector PlanarDesiredIntent(DesiredMovementIntent.X, DesiredMovementIntent.Y, 0.0f);
	const FVector ClampedDesiredIntent = PlanarDesiredIntent.GetClampedToMaxSize(1.0f);
	const float DesiredMagnitude = ClampedDesiredIntent.Size2D();
	if (DesiredMagnitude <= UE_KINDA_SMALL_NUMBER)
	{
		Reset();
		return FVector::ZeroVector;
	}

	const FVector DesiredDirection = ClampedDesiredIntent / DesiredMagnitude;
	const float DesiredAngleRadians = FMath::Atan2(DesiredDirection.Y, DesiredDirection.X);
	const float TurningStrength = FMath::Clamp(Settings.TurningStrength, 0.0f, 1.0f);

	if (!bIsInitialized)
	{
		Initialize(DesiredDirection, ActorFacing);
	}

	// A zero time step must not advance either the intermediate heading or the
	// pivot cooldown. Preserve the current heading while retaining input magnitude.
	if (DeltaSeconds <= 0.0f)
	{
		return IMGHero::MakePlanarMovementIntent(SmoothedAngleRadians, DesiredMagnitude);
	}

	// Evaluate pivots before the unsmoothed path so startup pivots still emit
	// their configured gameplay event at full turning strength.
	if (EvaluatePivot(DesiredAngleRadians, DeltaSeconds, Settings))
	{
		bOutTriggeredPivot = true;
		SmoothedAngleRadians = DesiredAngleRadians;
		return ClampedDesiredIntent;
	}

	if (TurningStrength >= 1.0f)
	{
		SmoothedAngleRadians = DesiredAngleRadians;
		return ClampedDesiredIntent;
	}

	const float TurnAlpha = CalculateFrameIndependentAlpha(TurningStrength, DeltaSeconds);
	SmoothedAngleRadians += FMath::FindDeltaAngleRadians(
		SmoothedAngleRadians,
		DesiredAngleRadians) * TurnAlpha;

	return IMGHero::MakePlanarMovementIntent(SmoothedAngleRadians, DesiredMagnitude);
}

void UIMGHeroComponent::FMovementIntentProcessor::Initialize(
	const FVector& DesiredDirection,
	const FVector& ActorFacing)
{
	const FVector ActorFacing2D = FVector(
		ActorFacing.X,
		ActorFacing.Y,
		0.0f).GetSafeNormal();
	const FVector& InitialDirection = !ActorFacing2D.IsNearlyZero()
		? ActorFacing2D
		: DesiredDirection;

	SmoothedAngleRadians = FMath::Atan2(InitialDirection.Y, InitialDirection.X);
	bIsInitialized = true;
}

bool UIMGHeroComponent::FMovementIntentProcessor::EvaluatePivot(
	float DesiredAngleRadians,
	float DeltaSeconds,
	const FIMGMovementIntentSettings& Settings)
{
	if (Settings.PivotAngleThreshold < 0.0f)
	{
		PivotTimer = 0.0f;
		return false;
	}

	// A bypass is a one-shot response. While its retrigger time is active,
	// subsequent direction changes deliberately use the normal smoothing path.
	PivotTimer = FMath::Max(
		PivotTimer - DeltaSeconds,
		0.0f);
	if (PivotTimer > 0.0f)
	{
		return false;
	}

	// SmoothedAngleRadians is the processor's persistent intermediate heading.
	// It is seeded from actor facing so a startup redirection can trigger a pivot.
	const float DirectionChangeDegrees = FMath::Abs(FMath::RadiansToDegrees(
		FMath::FindDeltaAngleRadians(SmoothedAngleRadians, DesiredAngleRadians)));
	if (DirectionChangeDegrees <= Settings.PivotAngleThreshold)
	{
		return false;
	}

	PivotTimer = FMath::Max(
		Settings.PivotRetriggerInterval,
		0.0f);
	return true;
}

float UIMGHeroComponent::FMovementIntentProcessor::CalculateFrameIndependentAlpha(
	float TurningStrength,
	float DeltaSeconds)
{
	if (TurningStrength <= 0.0f || DeltaSeconds <= 0.0f)
	{
		return 0.0f;
	}

	// TurningStrength is the fraction of the remaining angular error removed
	// per frame at 60 Hz. Adjust the blend factor to preserve that response over time.
	constexpr float ReferenceFrameRate = 60.0f;
	return 1.0f - FMath::Pow(1.0f - TurningStrength, DeltaSeconds * ReferenceFrameRate);
}

void UIMGHeroComponent::FMovementIntentProcessor::Reset()
{
	SmoothedAngleRadians = 0.0f;
	PivotTimer = 0.0f;
	bIsInitialized = false;
}

UIMGHeroComponent::UIMGHeroComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bReadyToBindInputs = false;
}

void UIMGHeroComponent::AddMovementIntent(FVector WorldDirection, float ScaleValue, bool bForce)
{
	APawn* Pawn = GetPawn<APawn>();
	if (!IsValid(Pawn) || Pawn->IsActorBeingDestroyed() || !IsRegistered() || IsBeingDestroyed())
	{
		ResetMovementIntent();
		return;
	}

	const FVector DesiredMovementIntent = WorldDirection * ScaleValue;
	if (!ensureMsgf(!DesiredMovementIntent.ContainsNaN(),
		TEXT("AddMovementIntent received a non-finite movement request for %s."), *GetNameSafe(Pawn)))
	{
		ResetMovementIntent();
		return;
	}

	const uint32 RequestSerial = ++MovementIntentSerial;
	const AController* RequestController = Pawn->GetController();
	const UWorld* World = GetWorld();
	bool bTriggeredPivot = false;
	const FVector MovementIntent = MovementIntentProcessor.UpdateMovementIntent(
		DesiredMovementIntent,
		Pawn->GetActorForwardVector(),
		World ? World->GetDeltaSeconds() : 0.0f,
		MovementIntentSettings,
		bTriggeredPivot);

	if (bTriggeredPivot && MovementIntentSettings.PivotGameplayEventTag.IsValid())
	{
		const UIMGPawnExtensionComponent* PawnExtComp = UIMGPawnExtensionComponent::FindPawnExtensionComponent(Pawn);
		UIMGAbilitySystemComponent* IMGASC = PawnExtComp ? PawnExtComp->GetIMGAbilitySystemComponent() : nullptr;
		if (IMGASC && IMGASC->GetAvatarActor() == Pawn)
		{
			FGameplayEventData Payload;
			Payload.EventTag = MovementIntentSettings.PivotGameplayEventTag;
			Payload.Instigator = Pawn;
			Payload.Target = Pawn;
			IMGASC->HandleGameplayEvent(Payload.EventTag, &Payload);
		}
	}

	// The event can cancel movement, replace control or destroy this pawn/component synchronously.
	if (RequestSerial != MovementIntentSerial) { return; }
	if (!IsValid(Pawn) || Pawn->IsActorBeingDestroyed() || !IsRegistered() || IsBeingDestroyed()
		|| GetPawn<APawn>() != Pawn || Pawn->GetController() != RequestController)
	{
		ResetMovementIntent();
		return;
	}

	if (!MovementIntent.IsNearlyZero())
	{
		Pawn->AddMovementInput(MovementIntent, 1.0f, bForce);
	}
}

void UIMGHeroComponent::ResetMovementIntent()
{
	++MovementIntentSerial;
	MovementIntentProcessor.Reset();
}

void UIMGHeroComponent::AddAdditionalInputConfig(const UIMGInputConfig* InputConfig, const UObject* Source)
{
	const APawn* Pawn = GetPawn<APawn>();
	UIMGInputComponent* InputComponent = BoundInputComponent.Get();
	if (!IsValid(InputConfig) || !IsValid(Source) || !bReadyToBindInputs || !Pawn || !Pawn->IsLocallyControlled() ||
		!IsRegistered() || IsBeingDestroyed() || !InputComponent || Pawn->InputComponent != InputComponent)
	{
		return;
	}
	FAdditionalInputConfigBinding& Binding = InputConfigBindings.FindOrAdd(InputConfig);
	if (Binding.Sources.Contains(Source))
	{
		return;
	}
	Binding.Sources.Add(Source);
	if (Binding.Sources.Num() == 1)
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = BoundInputSubsystem.Get())
		{
			InputComponent->AddInputMappings(InputConfig, Subsystem);
		}
		InputComponent->BindAbilityActions(InputConfig, this, &ThisClass::Input_AbilityInputTagPressed,
			&ThisClass::Input_AbilityInputTagReleased, Binding.BindHandles);
	}
}

void UIMGHeroComponent::RemoveAdditionalInputConfig(const UIMGInputConfig* InputConfig, const UObject* Source)
{
	FAdditionalInputConfigBinding* Binding = InputConfigBindings.Find(InputConfig);
	if (!Binding || Binding->Sources.Remove(Source) == 0 || !Binding->Sources.IsEmpty())
	{
		return;
	}
	if (UIMGInputComponent* InputComponent = BoundInputComponent.Get())
	{
		InputComponent->RemoveBinds(Binding->BindHandles);
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = BoundInputSubsystem.Get())
		{
			InputComponent->RemoveInputMappings(InputConfig, Subsystem);
		}
	}
	InputConfigBindings.Remove(InputConfig);
	ReleaseInputConfigTags(InputConfig);
}

void UIMGHeroComponent::ReleaseInputConfigTags(const UIMGInputConfig* InputConfig)
{
	if (!IsValid(InputConfig))
	{
		return;
	}
	const APawn* Pawn = GetPawn<APawn>();
	const UIMGPawnExtensionComponent* PawnExt = UIMGPawnExtensionComponent::FindPawnExtensionComponent(Pawn);
	UIMGAbilitySystemComponent* ASC = PawnExt ? PawnExt->GetIMGAbilitySystemComponent() : nullptr;
	if (!IsValid(ASC) || ASC->GetAvatarActor() != Pawn)
	{
		return;
	}
	for (const FIMGInputAction& Action : InputConfig->AbilityInputActions)
	{
		if (!Action.InputAction || !Action.InputTag.IsValid())
		{
			continue;
		}
		bool bStillBound = false;
		for (const auto& Entry : InputConfigBindings)
		{
			const UIMGInputConfig* Config = Entry.Key.Get();
			if (Config && Config->AbilityInputActions.ContainsByPredicate([&Action](const FIMGInputAction& Other)
			{
				return Other.InputAction && Other.InputTag == Action.InputTag;
			}))
			{
				bStillBound = true;
				break;
			}
		}
		if (!bStillBound)
		{
			// Teardown must release held input even after this Hero starts being destroyed.
			ASC->AbilityInputTagReleased(Action.InputTag);
		}
	}
}

void UIMGHeroComponent::ReleasePlayerInput()
{
	bReadyToBindInputs = false;
	const auto PreviousBindings = MoveTemp(InputConfigBindings);
	for (const auto& Entry : PreviousBindings)
	{
		ReleaseInputConfigTags(Entry.Key.Get());
	}
	if (UIMGInputComponent* InputComponent = BoundInputComponent.Get())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = BoundInputSubsystem.Get())
		{
			for (const auto& Entry : PreviousBindings)
			{
				if (const UIMGInputConfig* Config = Entry.Key.Get())
				{
					InputComponent->RemoveInputMappings(Config, Subsystem);
				}
			}
		}
		InputComponent->ClearBindingsForObject(this);
	}
	BoundInputComponent.Reset();
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = BoundInputSubsystem.Get())
	{
		for (const auto& Mapping : AppliedDefaultMappings)
		{
			if (const UInputMappingContext* Context = Mapping.Get())
			{
				Subsystem->RemoveMappingContext(Context);
			}
		}
	}
	AppliedDefaultMappings.Reset();
	BoundInputSubsystem.Reset();
}

bool UIMGHeroComponent::IsReadyToBindInputs() const
{
	const APawn* Pawn = GetPawn<APawn>();
	return bReadyToBindInputs && IsRegistered() && !IsBeingDestroyed() && Pawn && Pawn->IsLocallyControlled() &&
		BoundInputComponent.IsValid() && Pawn->InputComponent == BoundInputComponent.Get();
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

		// Bind for all pawns so their camera modes also work when spectating.
		if (UIMGCameraComponent* CameraComponent = UIMGCameraComponent::FindCameraComponent(Pawn))
		{
			CameraComponent->DetermineCameraModeDelegate.BindUObject(this, &ThisClass::DetermineCameraMode);
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
	const APawn* Pawn = GetPawn<APawn>();
	if (GetInitState() == IMGGameplayTags::InitState_GameplayReady && Pawn &&
		Pawn->IsLocallyControlled() && !Pawn->IsBotControlled() && Pawn->InputComponent &&
		(!bReadyToBindInputs || BoundInputComponent.Get() != Pawn->InputComponent))
	{
		InitializePlayerInput(Pawn->InputComponent);
	}
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
		if (HasBegunPlay())
		{
			CheckDefaultInitialization();
		}
	}
}

void UIMGHeroComponent::BeginPlay()
{
	Super::BeginPlay();
	if (APawn* Pawn = GetPawn<APawn>())
	{
		Pawn->ReceiveControllerChangedDelegate.AddUniqueDynamic(this, &ThisClass::HandlePawnControllerChanged);
	}

	BindOnActorInitStateChanged(UIMGPawnExtensionComponent::NAME_ActorFeatureName, FGameplayTag(), false);
	ensure(TryToChangeInitState(IMGGameplayTags::InitState_Spawned));
	CheckDefaultInitialization();
}

void UIMGHeroComponent::OnUnregister()
{
	ReleasePlayerInput();
	Super::OnUnregister();
}

void UIMGHeroComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleasePlayerInput();
	if (APawn* Pawn = GetPawn<APawn>())
	{
		Pawn->ReceiveControllerChangedDelegate.RemoveDynamic(this, &ThisClass::HandlePawnControllerChanged);
		if (UIMGCameraComponent* Camera = UIMGCameraComponent::FindCameraComponent(Pawn))
		{
			if (Camera->DetermineCameraModeDelegate.IsBoundToObject(this))
			{
				Camera->DetermineCameraModeDelegate.Unbind();
				Camera->ResetInterpolation();
			}
		}
	}
	AbilityCameraMode = nullptr;
	AbilityCameraModeOwningSpecHandle = FGameplayAbilitySpecHandle();

	ResetMovementIntent();
	UnregisterInitStateFeature();

	Super::EndPlay(EndPlayReason);
}

void UIMGHeroComponent::HandlePawnControllerChanged(APawn*, AController* OldController, AController* NewController)
{
	ResetMovementIntent();
	if (OldController != NewController)
	{
		ReleasePlayerInput();
	}
}

void UIMGHeroComponent::InitializePlayerInput(UInputComponent* PlayerInputComponent)
{
	const APawn* Pawn = GetPawn<APawn>();
	const APlayerController* PC = GetController<APlayerController>();
	const ULocalPlayer* LP = PC ? PC->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LP ? LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	UIMGInputComponent* IMGIC = Cast<UIMGInputComponent>(PlayerInputComponent);
	if (bInitializingPlayerInput || !IsRegistered() || IsBeingDestroyed() || !Pawn ||
		!Pawn->IsLocallyControlled() || !InputSubsystem || !InputSubsystem->GetPlayerInput() ||
		!ensureMsgf(IMGIC, TEXT("Player input must use UIMGInputComponent or a subclass.")))
	{
		return;
	}
	TGuardValue<bool> InitializingInput(bInitializingPlayerInput, true);
	ReleasePlayerInput();
	ResetMovementIntent();
	BoundInputComponent = IMGIC;
	BoundInputSubsystem = InputSubsystem;
	bReadyToBindInputs = true;

	for (const FInputMappingContextAndPriority& Mapping : DefaultInputMappings)
	{
		if (UInputMappingContext* IMC = Mapping.InputMapping.LoadSynchronous())
		{
			if (!IsReadyToBindInputs() || Pawn->GetController() != PC)
			{
				return;
			}
			if (Mapping.bRegisterWithSettings)
			{
				if (UEnhancedInputUserSettings* Settings = InputSubsystem->GetUserSettings())
				{
					Settings->RegisterInputMappingContext(IMC);
				}
			}
			if (!IsReadyToBindInputs() || Pawn->GetController() != PC)
			{
				return;
			}
			FModifyContextOptions Options = {};
			Options.bIgnoreAllPressedKeysUntilRelease = false;
			InputSubsystem->AddMappingContext(IMC, Mapping.Priority, Options);
			AppliedDefaultMappings.Add(IMC);
		}
	}

	if (!IsReadyToBindInputs() || Pawn->GetController() != PC)
	{
		return;
	}
	const UIMGPawnExtensionComponent* PawnExtComp = UIMGPawnExtensionComponent::FindPawnExtensionComponent(Pawn);
	const UIMGPawnData* PawnData = PawnExtComp ? PawnExtComp->GetPawnData<UIMGPawnData>() : nullptr;
	if (const UIMGInputConfig* InputConfig = PawnData ? PawnData->InputConfig.Get() : nullptr)
	{
		AddAdditionalInputConfig(InputConfig, this);
		IMGIC->BindNativeAction(InputConfig, IMGGameplayTags::InputTag_Move, ETriggerEvent::Triggered, this, &ThisClass::Input_Move, false);
		IMGIC->BindNativeAction(InputConfig, IMGGameplayTags::InputTag_Move, ETriggerEvent::Completed, this, &ThisClass::Input_MoveCompleted, false);
		IMGIC->BindNativeAction(InputConfig, IMGGameplayTags::InputTag_Move, ETriggerEvent::Canceled, this, &ThisClass::Input_MoveCompleted, false);
		IMGIC->BindNativeAction(InputConfig, IMGGameplayTags::InputTag_Look_Mouse, ETriggerEvent::Triggered, this, &ThisClass::Input_LookMouse, false);
		IMGIC->BindNativeAction(InputConfig, IMGGameplayTags::InputTag_Look_Stick, ETriggerEvent::Triggered, this, &ThisClass::Input_LookStick, false);
		IMGIC->BindNativeAction(InputConfig, IMGGameplayTags::InputTag_Crouch, ETriggerEvent::Triggered, this, &ThisClass::Input_Crouch, false);
		IMGIC->BindNativeAction(InputConfig, IMGGameplayTags::InputTag_Crawl, ETriggerEvent::Triggered, this, &ThisClass::Input_Crawl, false);
		IMGIC->BindNativeAction(InputConfig, IMGGameplayTags::InputTag_AutoRun, ETriggerEvent::Triggered, this, &ThisClass::Input_AutoRun, false);
	}

	UGameFrameworkComponentManager::SendGameFrameworkComponentExtensionEvent(const_cast<APlayerController*>(PC), NAME_BindInputsNow);
	if (bReadyToBindInputs && BoundInputComponent.Get() == IMGIC && Pawn->GetController() == PC && IsRegistered() && !IsBeingDestroyed())
	{
		UGameFrameworkComponentManager::SendGameFrameworkComponentExtensionEvent(const_cast<APawn*>(Pawn), NAME_BindInputsNow);
	}
}

void UIMGHeroComponent::Input_AbilityInputTagPressed(FGameplayTag InputTag)
{
	if (!IsRegistered() || IsBeingDestroyed()) { return; }
	if (const APawn* Pawn = GetPawn<APawn>())
	{
		if (const UIMGPawnExtensionComponent* PawnExtComp = UIMGPawnExtensionComponent::FindPawnExtensionComponent(Pawn))
		{
			if (UIMGAbilitySystemComponent* IMGASC = PawnExtComp->GetIMGAbilitySystemComponent(); IMGASC && IMGASC->GetAvatarActor() == Pawn)
			{
				IMGASC->AbilityInputTagPressed(InputTag);
			}
		}
	}
}

void UIMGHeroComponent::Input_AbilityInputTagReleased(FGameplayTag InputTag)
{
	if (!IsRegistered() || IsBeingDestroyed()) { return; }
	if (const APawn* Pawn = GetPawn<APawn>())
	{
		if (const UIMGPawnExtensionComponent* PawnExtComp = UIMGPawnExtensionComponent::FindPawnExtensionComponent(Pawn))
		{
			if (UIMGAbilitySystemComponent* IMGASC = PawnExtComp->GetIMGAbilitySystemComponent(); IMGASC && IMGASC->GetAvatarActor() == Pawn)
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
	if (!IsValid(Pawn) || Pawn->IsActorBeingDestroyed() || !IsValid(Controller) || Controller->IsActorBeingDestroyed()
		|| !IsRegistered() || IsBeingDestroyed())
	{
		ResetMovementIntent();
		return;
	}

	// If the player has attempted to move again then cancel auto running
	if (AIMGPlayerController* IMGController = Cast<AIMGPlayerController>(Controller))
	{
		IMGController->SetIsAutoRunning(false);
	}
	if (!IsValid(Pawn) || Pawn->IsActorBeingDestroyed() || !IsValid(Controller) || Controller->IsActorBeingDestroyed()
		|| GetPawn<APawn>() != Pawn || Pawn->GetController() != Controller || !IsRegistered() || IsBeingDestroyed())
	{
		ResetMovementIntent();
		return;
	}

	const FVector2D MoveInput = InputActionValue.Get<FVector2D>();
	const FRotator MovementRotation(0.0f, Controller->GetControlRotation().Yaw, 0.0f);

	// Input mapping ends here. The shared movement-intent entry point owns all
	// shaping and submission for player, AI, and scripted callers.
	const FVector DesiredMovementIntent =
		MovementRotation.RotateVector(FVector::RightVector) * MoveInput.X
		+ MovementRotation.RotateVector(FVector::ForwardVector) * MoveInput.Y;

	AddMovementIntent(DesiredMovementIntent);
}

void UIMGHeroComponent::Input_MoveCompleted(const FInputActionValue&)
{
	// Direction has no meaning while input magnitude is zero. Discard the old
	// intent so the next movement starts from its configured initial direction.
	ResetMovementIntent();
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

void UIMGHeroComponent::Input_Crawl(const FInputActionValue& InputActionValue)
{
	if (AIMGCharacter* Character = GetPawn<AIMGCharacter>())
	{
		Character->ToggleCrawl();
	}
}

TSubclassOf<UIMGCameraMode> UIMGHeroComponent::DetermineCameraMode() const
{
	if (AbilityCameraMode)
	{
		return AbilityCameraMode;
	}

	const APawn* Pawn = GetPawn<APawn>();
	if (!Pawn)
	{
		return nullptr;
	}

	if (UIMGPawnExtensionComponent* PawnExtComp = UIMGPawnExtensionComponent::FindPawnExtensionComponent(Pawn))
	{
		if (const UIMGPawnData* PawnData = PawnExtComp->GetPawnData<UIMGPawnData>())
		{
			return PawnData->DefaultCameraMode;
		}
	}

	return nullptr;
}

void UIMGHeroComponent::SetAbilityCameraMode(TSubclassOf<UIMGCameraMode> CameraMode, const FGameplayAbilitySpecHandle& OwningSpecHandle)
{
	if (CameraMode)
	{
		AbilityCameraMode = CameraMode;
		AbilityCameraModeOwningSpecHandle = OwningSpecHandle;
	}
	else
	{
		ClearAbilityCameraMode(OwningSpecHandle);
	}
}

void UIMGHeroComponent::ClearAbilityCameraMode(const FGameplayAbilitySpecHandle& OwningSpecHandle)
{
	if (AbilityCameraModeOwningSpecHandle == OwningSpecHandle)
	{
		AbilityCameraMode = nullptr;
		AbilityCameraModeOwningSpecHandle = FGameplayAbilitySpecHandle();
	}
}


void UIMGHeroComponent::Input_AutoRun(const FInputActionValue& InputActionValue)
{
	if (APawn* Pawn = GetPawn<APawn>())
	{
		if (AIMGPlayerController* Controller = Cast<AIMGPlayerController>(Pawn->GetController()))
		{
			// Toggle auto running
			Controller->SetIsAutoRunning(!Controller->GetIsAutoRunning());
		}
	}
}
