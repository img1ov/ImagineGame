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
		UE_LOG(LogIMGLocomotion, Error, TEXT("%s: Requires an IMGCharacter owner."), *GetPathName());
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
		UE_LOG(LogIMGLocomotion, Error, TEXT("%s: Requires IMGCharacterMovementComponent, found %s."),
			*GetPathName(), *GetPathNameSafe(GetCharacterMovementComponent()));
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
	const UIMGCharacterMovementComponent* Movement = GetBoundMovement();
	return !bLocomotionDisabled && !bLocomotionRefreshPending && IsValid(LocomotionSet) && IsValid(AbilitySystemComponent)
		&& Movement && Movement->IsMovementRequestProvider(this) && AbilitySystemComponent == GetReadyAbilitySystem();
}

void UIMGLocomotionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	if (bLocomotionRefreshPending)
	{
		RefreshLocomotionSet();
	}
	if (IsLocomotionReady())
	{
		Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	}
	else if (!AttributeChangeHandles.IsEmpty() && !bLocomotionRefreshPending)
	{
		ClearLocomotionConnection(AbilitySystemComponent != GetReadyAbilitySystem());
	}
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
	SetComponentTickEnabled(false);

	const AIMGCharacter* Character = GetIMGCharacter();
	if (!Character)
	{
		return;
	}

	UIMGPawnExtensionComponent* PawnExtension = UIMGPawnExtensionComponent::FindPawnExtensionComponent(Character);
	if (!PawnExtension)
	{
		UE_LOG(LogIMGLocomotion, Error, TEXT("%s: Requires PawnExtension."), *GetPathName());
		return;
	}

	PawnExtensionComponent = PawnExtension;
	GameplayReadyDelegate.BindDynamic(this, &ThisClass::HandleGameplayReady);
	if (!PawnExtension->RegisterAndCallForInitStateChange(
		IMGGameplayTags::InitState_GameplayReady,
		GameplayReadyDelegate,
		true))
	{
		UE_LOG(LogIMGLocomotion, Error, TEXT("%s: Failed to subscribe to GameplayReady on %s."),
			*GetPathName(), *GetPathNameSafe(PawnExtension));
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

	if (!GetReadyAbilitySystem())
	{
		const UIMGAbilitySystemComponent* ASC = PawnExtension->GetIMGAbilitySystemComponent();
		UE_LOG(LogIMGLocomotion, Log, TEXT("%s: Waiting for ASC avatar pairing. Role=%s ASC=%s Avatar=%s"),
			*GetPathName(), *UEnum::GetValueAsString(GetOwnerRole()), *GetPathNameSafe(ASC),
			*GetPathNameSafe(IsValid(ASC) ? ASC->GetAvatarActor() : nullptr));
	}

	PawnExtension->OnAbilitySystemUninitialized_Register(FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &ThisClass::HandleAbilitySystemUninitialized));
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
	ClearLocomotionConnection(true);
}

void UIMGLocomotionComponent::HandleGameplayReady(const FActorInitStateChangedParams& Params)
{
	UIMGPawnExtensionComponent* PawnExtension = PawnExtensionComponent.Get();
	if (PawnExtension
		&& Params.FeatureName == UIMGPawnExtensionComponent::NAME_ActorFeatureName
		&& Params.Implementer.Get() == PawnExtension
		&& PawnExtension->HasReachedInitState(IMGGameplayTags::InitState_GameplayReady))
	{
		UE_LOG(LogIMGLocomotion, Verbose, TEXT("%s: PawnExtension reached GameplayReady."), *GetPathName());
		InitializeGameplayReadyConnections();
	}
}

void UIMGLocomotionComponent::HandleAbilitySystemInitialized()
{
	RefreshAbilitySystemConnection();
}

void UIMGLocomotionComponent::HandleAbilitySystemUninitialized()
{
	ClearLocomotionConnection(true);
}

UIMGAbilitySystemComponent* UIMGLocomotionComponent::GetReadyAbilitySystem() const
{
	const UIMGPawnExtensionComponent* PawnExtension = PawnExtensionComponent.Get();
	if (!IsMovementPrerequisiteReady() || !GetIMGCharacterMovementComponent() || !PawnExtension
		|| !PawnExtension->HasReachedInitState(IMGGameplayTags::InitState_GameplayReady))
	{
		return nullptr;
	}
	UIMGAbilitySystemComponent* ASC = PawnExtension->GetIMGAbilitySystemComponent();
	return IsValid(ASC) && ASC->GetAvatarActor() == GetOwner() ? ASC : nullptr;
}

void UIMGLocomotionComponent::RefreshAbilitySystemConnection()
{
	if (bRefreshingConnections || bLocomotionDisabled)
	{
		return;
	}
	TGuardValue<bool> RefreshGuard(bRefreshingConnections, true);

	if (AbilitySystemComponent != GetReadyAbilitySystem())
	{
		ClearLocomotionConnection(true);
		// Unavailable can change the ASC pairing or unregister this component.
		if (!bLocomotionDisabled)
		{
			AbilitySystemComponent = GetReadyAbilitySystem();
			if (AbilitySystemComponent)
			{
				AttributeSetsUpdatedHandle = AbilitySystemComponent->OnAttributeSetsUpdated().AddUObject(this, &ThisClass::HandleAttributeSetsUpdated);
				if (!IsValid(AbilitySystemComponent->GetSet<UIMGLocomotionSet>()))
				{
					if (GetOwnerRole() == ROLE_Authority)
					{
						UE_LOG(LogIMGLocomotion, Warning, TEXT("%s: Authority ASC %s has no LocomotionSet. Check the AbilitySet or runtime grant. Using native CMC movement until the Set is granted."),
							*GetPathName(), *GetPathNameSafe(AbilitySystemComponent.Get()));
					}
					else
					{
						UE_LOG(LogIMGLocomotion, Log, TEXT("%s: Waiting for LocomotionSet replication. Role=%s ASC=%s"),
							*GetPathName(), *UEnum::GetValueAsString(GetOwnerRole()), *GetPathNameSafe(AbilitySystemComponent.Get()));
					}
				}
			}
		}
	}
	HandleAttributeSetsUpdated();
}

void UIMGLocomotionComponent::HandleAttributeSetsUpdated()
{
	if (bLocomotionDisabled || !IsValid(AbilitySystemComponent) || AbilitySystemComponent != GetReadyAbilitySystem())
	{
		return;
	}
	const UIMGLocomotionSet* CurrentSet = AbilitySystemComponent->GetSet<UIMGLocomotionSet>();
	if (!IsValid(CurrentSet))
	{
		CurrentSet = nullptr;
	}
	if (LocomotionSet != CurrentSet)
	{
		UE_CLOG(!bLocomotionRefreshPending, LogIMGLocomotion, Verbose, TEXT("%s: Queued LocomotionSet change. Previous=%s Current=%s"),
			*GetPathName(), *GetPathNameSafe(LocomotionSet.Get()), *GetPathNameSafe(CurrentSet));
		bLocomotionRefreshPending = true;
		// Keep the frozen parameters through the end of a physics step.
		if (const UIMGCharacterMovementComponent* Movement = GetBoundMovement(); Movement && Movement->IsMovementInProgress())
		{
			GetIMGCharacter()->OnCharacterMovementUpdated.AddUniqueDynamic(this, &ThisClass::HandleMovementUpdated);
		}
		else
		{
			ReleaseMovementConnection();
		}
		// Run lifecycle callbacks after grants and replication settle.
		SetComponentTickEnabled(true);
	}
}

void UIMGLocomotionComponent::HandleMovementUpdated(float, FVector, FVector)
{
	if (const UIMGCharacterMovementComponent* Movement = GetBoundMovement(); bLocomotionRefreshPending && Movement && !Movement->IsMovementInProgress())
	{
		ReleaseMovementConnection();
	}
}

void UIMGLocomotionComponent::RefreshLocomotionSet()
{
	ClearLocomotionConnection(AbilitySystemComponent != GetReadyAbilitySystem());
	if (bLocomotionDisabled || !IsValid(AbilitySystemComponent) || AbilitySystemComponent != GetReadyAbilitySystem())
	{
		return;
	}

	const UIMGLocomotionSet* CurrentSet = AbilitySystemComponent->GetSet<UIMGLocomotionSet>();
	if (!IsValid(CurrentSet))
	{
		if (GetOwnerRole() == ROLE_Authority)
		{
			UE_LOG(LogIMGLocomotion, Warning, TEXT("%s: Authority ASC %s has no LocomotionSet after a Set change. Using native CMC movement until the Set is granted."),
				*GetPathName(), *GetPathNameSafe(AbilitySystemComponent.Get()));
		}
		else
		{
			UE_LOG(LogIMGLocomotion, Log, TEXT("%s: LocomotionSet is unavailable after a Set change. Using native CMC movement. Role=%s ASC=%s"),
				*GetPathName(), *UEnum::GetValueAsString(GetOwnerRole()), *GetPathNameSafe(AbilitySystemComponent.Get()));
		}
		return;
	}

	LocomotionSet = CurrentSet;
	InitializeMovementConnection();
	if (!BoundMovement.IsValid())
	{
		return;
	}
	InitializeMovementContext();

	TArray<FGameplayAttribute> BoundAttributes;
	UAttributeSet::GetAttributesFromSetClass(UIMGLocomotionSet::StaticClass(), BoundAttributes);

	for (const FGameplayAttribute& Attribute : BoundAttributes)
	{
		AttributeChangeHandles.Add(Attribute,
			AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(Attribute).AddUObject(this, &ThisClass::HandleAttributeChanged));
	}
	bLocomotionRefreshPending = false;
	SetComponentTickEnabled(true);
	OnLocomotionReadyNative();
	if (IsLocomotionReady())
	{
		bLocomotionReadyNotified = true;
		UE_LOG(LogIMGLocomotion, Log, TEXT("%s: Ready. Role=%s ASC=%s Set=%s Walk=%g Run=%g Sprint=%g Crouch=%g"),
			*GetPathName(), *UEnum::GetValueAsString(GetOwnerRole()), *GetPathNameSafe(AbilitySystemComponent.Get()), *GetPathNameSafe(LocomotionSet.Get()),
			LocomotionSet->GetWalkSpeed(), LocomotionSet->GetRunSpeed(), LocomotionSet->GetSprintSpeed(), LocomotionSet->GetCrouchSpeed());
		OnLocomotionReady();
	}
}

void UIMGLocomotionComponent::ClearLocomotionConnection(bool bReleaseAbilitySystem)
{
	const bool bHadBindings = !AttributeChangeHandles.IsEmpty();
	const bool bWasReady = bLocomotionReadyNotified;
	UE_CLOG(bHadBindings, LogIMGLocomotion, Verbose, TEXT("%s: Releasing bindings. ASC=%s Set=%s DetachASC=%d Disabled=%d"),
		*GetPathName(), *GetPathNameSafe(AbilitySystemComponent.Get()), *GetPathNameSafe(LocomotionSet.Get()), bReleaseAbilitySystem, bLocomotionDisabled);
	bLocomotionReadyNotified = false;
	bLocomotionRefreshPending = false;
	bWarnedInvalidMovementContext = false;
	bWarnedInvalidMovementParameters = false;
	SetComponentTickEnabled(false);
	ReleaseMovementConnection();

	if (IsValid(AbilitySystemComponent))
	{
		for (const TPair<FGameplayAttribute, FDelegateHandle>& Binding : AttributeChangeHandles)
		{
			AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(Binding.Key).Remove(Binding.Value);
		}
		if (bReleaseAbilitySystem)
		{
			AbilitySystemComponent->OnAttributeSetsUpdated().Remove(AttributeSetsUpdatedHandle);
		}
	}

	AttributeChangeHandles.Reset();
	LocomotionSet = nullptr;
	PendingMovementContext = FMovementSimulationContext();
	if (bReleaseAbilitySystem)
	{
		AttributeSetsUpdatedHandle.Reset();
		AbilitySystemComponent = nullptr;
	}
	if (bHadBindings)
	{
		OnLocomotionUnavailableNative();
	}
	if (bWasReady)
	{
		OnLocomotionUnavailable();
	}
}

void UIMGLocomotionComponent::HandleAttributeChanged(const FOnAttributeChangeData& ChangeData)
{
	if (IsLocomotionReady() && AbilitySystemComponent->GetSet<UIMGLocomotionSet>() == LocomotionSet)
	{
		const bool bLocalPrediction = AbilitySystemComponent->GetPredictionKeyForNewAction().IsValidForMorePrediction();
		const bool bPreserveAuthorityInputs = GetOwner()->GetLocalRole() == ROLE_AutonomousProxy && !bLocalPrediction;
		const FGameplayAttribute& Attribute = ChangeData.Attribute;
		int32 Index = INDEX_NONE;
		if (Attribute == UIMGLocomotionSet::GetWalkSpeedAttribute()) { Index = WalkSpeedParameter; }
		else if (Attribute == UIMGLocomotionSet::GetRunSpeedAttribute()) { Index = RunSpeedParameter; }
		else if (Attribute == UIMGLocomotionSet::GetSprintSpeedAttribute()) { Index = SprintSpeedParameter; }
		else if (Attribute == UIMGLocomotionSet::GetCrouchSpeedAttribute()) { Index = CrouchSpeedParameter; }
		if (PendingMovementContext.Parameters.IsValidIndex(Index))
		{
			const float CurrentValue = Attribute.GetNumericValue(LocomotionSet.Get());
			PendingMovementContext.Parameters[Index] = FMath::IsFinite(CurrentValue) ? FMath::Max(0.f, CurrentValue) : 0.f;
			AdvanceMovementContextRevision();
			// Replicated GAS values can lag a CMC response; only a fresh local prediction supersedes that authority input.
			if (!bPreserveAuthorityInputs) { PendingMovementContext.ParameterRevisions[Index] = PendingMovementContext.Revision; }
			OnLocomotionAttributeChanged(Attribute, ChangeData.OldValue, CurrentValue);
		}
	}
}

void UIMGLocomotionComponent::InitializeMovementContext()
{
	PendingMovementContext.Parameters = {
		LocomotionSet->GetWalkSpeed(), LocomotionSet->GetRunSpeed(), LocomotionSet->GetSprintSpeed(), LocomotionSet->GetCrouchSpeed()
	};
	AdvanceMovementContextRevision();
	PendingMovementContext.ParameterRevisions.Init(PendingMovementContext.Revision, LocomotionParameterCount);
}

void UIMGLocomotionComponent::AdvanceMovementContextRevision()
{
	if (++MovementContextRevision == 0) { ++MovementContextRevision; }
	PendingMovementContext.Revision = MovementContextRevision;
}

void UIMGLocomotionComponent::InitializeMovementConnection()
{
	UIMGCharacterMovementComponent* Movement = GetIMGCharacterMovementComponent();
	const FCharacterMovementParameters PreviousParameters = FCharacterMovementParameters::Capture(*Movement);
	if (!Movement->RegisterMovementRequestProvider(this,
		FMovementRequestCapture::CreateUObject(this, &ThisClass::CaptureMovementRequest),
		FMovementRequestApply::CreateUObject(this, &ThisClass::ApplyMovementRequest)))
	{
		return;
	}
	OriginalMovementParameters = PreviousParameters;
	BoundMovement = Movement;
}

void UIMGLocomotionComponent::ReleaseMovementConnection()
{
	if (AIMGCharacter* Character = GetIMGCharacter(); IsValid(Character))
	{
		Character->OnCharacterMovementUpdated.RemoveDynamic(this, &ThisClass::HandleMovementUpdated);
	}
	if (UIMGCharacterMovementComponent* Movement = BoundMovement.Get(); Movement &&
		(Movement->IsMovementRequestProvider(this) || !Movement->HasMovementRequestProvider()))
	{
		Movement->UnregisterMovementRequestProvider(this);
		OriginalMovementParameters.Apply(*Movement);
	}
	BoundMovement.Reset();
}

void UIMGLocomotionComponent::Activate(bool bReset)
{
	bLocomotionDisabled = false;
	Super::Activate(bReset);
	if (!bLocomotionDisabled)
	{
		RefreshAbilitySystemConnection();
		SetComponentTickEnabled(bLocomotionRefreshPending || IsLocomotionReady());
	}
}

void UIMGLocomotionComponent::Deactivate()
{
	bLocomotionDisabled = true;
	ClearLocomotionConnection(true);
	if (bLocomotionDisabled)
	{
		Super::Deactivate();
	}
}

bool UIMGLocomotionComponent::CaptureMovementRequest(uint8& OutRequest, FMovementSimulationContext& OutContext)
{
	if (!IsLocomotionReady()) { return false; }
	FMovementSimulationContext Candidate = PendingMovementContext;
	ExtendMovementContext(Candidate);
	if (!Candidate.HasSameParameters(PendingMovementContext))
	{
		AdvanceMovementContextRevision();
		Candidate.ParameterRevisions.SetNumZeroed(Candidate.Parameters.Num());
		for (int32 Index = 0; Index < Candidate.Parameters.Num(); ++Index)
		{
			if (!PendingMovementContext.Parameters.IsValidIndex(Index)
				|| Candidate.Parameters[Index] != PendingMovementContext.Parameters[Index])
			{
				Candidate.ParameterRevisions[Index] = PendingMovementContext.Revision;
			}
		}
		PendingMovementContext.Parameters = MoveTemp(Candidate.Parameters);
		PendingMovementContext.ParameterRevisions = MoveTemp(Candidate.ParameterRevisions);
	}
	OutRequest = GetMovementRequest();
	OutContext = PendingMovementContext;
	const bool bContextValid = OutContext.IsValid();
	if (!bContextValid && !bWarnedInvalidMovementContext)
	{
		bWarnedInvalidMovementContext = true;
		const int32 InvalidValueIndex = OutContext.Parameters.IndexOfByPredicate([](float Value)
		{
			return !FMath::IsFinite(Value) || Value < 0.f;
		});
		UE_LOG(LogIMGLocomotion, Warning, TEXT("%s: Rejected captured movement context. Revision=%u Values=%d ValueRevisions=%d InvalidValueIndex=%d InvalidValue=%g"),
			*GetPathName(), OutContext.Revision, OutContext.Parameters.Num(), OutContext.ParameterRevisions.Num(), InvalidValueIndex,
			InvalidValueIndex != INDEX_NONE ? OutContext.Parameters[InvalidValueIndex] : 0.f);
	}
	return bContextValid;
}

bool UIMGLocomotionComponent::ApplyMovementRequest(uint8 Request, const FMovementSimulationContext& Context,
	const FVector& Input, bool bReplay)
{
	UIMGCharacterMovementComponent* Movement = BoundMovement.Get();
	if (!Movement || !IsLocomotionReady() || !Context.IsValid()
		|| Movement->IsRagdollMovementMode() || Movement->IsRagdollRecovering()) { return false; }
	FCharacterMovementParameters Parameters = OriginalMovementParameters;
	const bool bCalculated = CalculateMovementParameters(Request, Context, Input, bReplay, Parameters);
	if (!bCalculated || !Parameters.IsValid())
	{
		if (!bWarnedInvalidMovementParameters)
		{
			bWarnedInvalidMovementParameters = true;
			if (bCalculated)
			{
				UE_LOG(LogIMGLocomotion, Warning, TEXT("%s: Invalid movement parameters. Revision=%u Request=%u Replay=%d Walk=%g Crouch=%g Acceleration=%g GroundFriction=%g BrakingDeceleration=%g RotationRate=%s"),
					*GetPathName(), Context.Revision, static_cast<uint32>(Request), bReplay, Parameters.MaxWalkSpeed, Parameters.MaxWalkSpeedCrouched,
					Parameters.MaxAcceleration, Parameters.GroundFriction, Parameters.BrakingDecelerationWalking, *Parameters.RotationRate.ToCompactString());
			}
			else
			{
				UE_LOG(LogIMGLocomotion, Warning, TEXT("%s: Movement rules rejected the context. Revision=%u Values=%d Request=%u Replay=%d"),
					*GetPathName(), Context.Revision, Context.Parameters.Num(), static_cast<uint32>(Request), bReplay);
			}
		}
		return false;
	}
	if (BoundMovement.Get() != Movement || !IsLocomotionReady()) { return false; }
	Parameters.Apply(*Movement);
	return true;
}

bool UIMGLocomotionComponent::CalculateMovementParameters(uint8 Request, const FMovementSimulationContext& Context,
	const FVector& Input, bool bReplay, FCharacterMovementParameters& OutParameters)
{
	if (Context.Parameters.Num() < LocomotionParameterCount) { return false; }
	OutParameters.MaxWalkSpeed = Context.Parameters[RunSpeedParameter];
	OutParameters.MaxWalkSpeedCrouched = Context.Parameters[CrouchSpeedParameter];
	OutParameters.MaxAcceleration = BaseMaxAcceleration;
	OutParameters.BrakingDecelerationWalking = BaseBrakingDecelerationWalking;
	OutParameters.GroundFriction = BaseGroundFriction;
	return true;
}

void UIMGLocomotionComponent::UpdateLocomotionPreCMC_Implementation(float DeltaTime)
{
	uint8 Request = 0;
	FMovementSimulationContext Context;
	if (CaptureMovementRequest(Request, Context))
	{
		if (const UIMGCharacterMovementComponent* Movement = GetBoundMovement(); Movement && IsLocomotionReady())
		{
			const AIMGCharacter* Character = GetIMGCharacter();
			const FVector Input = Character->IsLocallyControlled() ? Character->GetPendingMovementInputVector()
				: Movement->GetCurrentAcceleration();
			ApplyMovementRequest(Request, Context, Input, false);
		}
	}
}

UIMGCharacterMovementComponent* UIMGLocomotionComponent::GetBoundMovement() const
{
	return BoundMovement.Get();
}
