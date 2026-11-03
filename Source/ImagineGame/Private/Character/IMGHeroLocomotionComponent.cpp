#include "Character/IMGHeroLocomotionComponent.h"

#include "AbilitySystem/IMGAbilitySystemComponent.h"
#include "Character/IMGCharacter.h"
#include "Character/IMGCharacterMovementComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/World.h"
#include "IMGGameplayTags.h"
#include "IMGLogChannels.h"
#include "Math/RotationMatrix.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGHeroLocomotionComponent)

namespace HeroMovement
{
	constexpr uint8 Sprint = 1 << 0;
	constexpr uint8 Walk = 1 << 1;
	constexpr uint8 Aim = 1 << 2;
	constexpr uint8 Strafe = 1 << 3;
	constexpr uint8 Mask = Sprint | Walk | Aim | Strafe;
}

UIMGHeroLocomotionComponent::UIMGHeroLocomotionComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UCurveFloat> Curve(TEXT("/Game/Characters/Heroes/Mannequin/Animations/Data/Curve_StrafeSpeedMap.Curve_StrafeSpeedMap"));
	StrafeSpeedMapCurve = Curve.Object;
}

void UIMGHeroLocomotionComponent::OnLocomotionReadyNative()
{
	ReleaseBindings();
	if (bLocomotionDisabled) { return; }
	UIMGCharacterMovementComponent* Movement = GetIMGCharacterMovementComponent();
	UIMGAbilitySystemComponent* ASC = GetIMGAbilitySystemComponent();
	if (!Movement || !ASC || !IsLocomotionReady()) { return; }
	OriginalMaxAcceleration = Movement->MaxAcceleration;
	OriginalMaxWalkSpeed = Movement->MaxWalkSpeed;
	OriginalMaxCrouchSpeed = Movement->MaxWalkSpeedCrouched;
	OriginalGroundFriction = Movement->GroundFriction;
	OriginalBrakingDeceleration = Movement->BrakingDecelerationWalking;
	OriginalRotationRate = Movement->RotationRate;
	bOriginalOrientToMovement = Movement->bOrientRotationToMovement;
	bOriginalControllerDesiredRotation = Movement->bUseControllerDesiredRotation;
	if (!Movement->RegisterMovementRequestProvider(this,
		FMovementRequestCapture::CreateUObject(this, &ThisClass::CaptureRequest),
		FMovementRequestApply::CreateUObject(this, &ThisClass::ApplyRequest)))
	{
		UE_LOG(LogIMG, Error, TEXT("HeroLocomotion on %s cannot claim the movement request provider."), *GetNameSafe(GetOwner()));
		return;
	}
	BoundMovement = Movement;
	BoundASC = ASC;
	for (const FGameplayTag Tag : { FGameplayTag(IMGGameplayTags::Event_Movement_Gait_Sprint), FGameplayTag(IMGGameplayTags::Event_Movement_Gait_Walk),
		FGameplayTag(IMGGameplayTags::Event_Movement_RotationMode_Aim), FGameplayTag(IMGGameplayTags::Event_Movement_RotationMode_Strafe) })
	{
		IntentTagHandles.Add(Tag, ASC->RegisterGameplayTagEvent(Tag, EGameplayTagEventType::NewOrRemoved)
			.AddUObject(this, &ThisClass::HandleIntentTagChanged));
	}
	RefreshRequestFromTags();
	GetIMGCharacter()->LandedDelegate.AddDynamic(this, &ThisClass::HandleLanded);
}

void UIMGHeroLocomotionComponent::ReleaseBindings()
{
	if (UIMGAbilitySystemComponent* ASC = BoundASC.Get())
	{
		for (const auto& Binding : IntentTagHandles)
		{
			ASC->RegisterGameplayTagEvent(Binding.Key, EGameplayTagEventType::NewOrRemoved).Remove(Binding.Value);
		}
	}
	IntentTagHandles.Reset();
	BoundASC.Reset();
	if (AIMGCharacter* Character = GetIMGCharacter())
	{
		Character->LandedDelegate.RemoveDynamic(this, &ThisClass::HandleLanded);
	}
	if (UIMGCharacterMovementComponent* Movement = BoundMovement.Get(); Movement && (Movement->IsMovementRequestProvider(this) || !Movement->HasMovementRequestProvider()))
	{
		Movement->UnregisterMovementRequestProvider(this);
		Movement->MaxAcceleration = OriginalMaxAcceleration;
		Movement->MaxWalkSpeed = OriginalMaxWalkSpeed;
		Movement->MaxWalkSpeedCrouched = OriginalMaxCrouchSpeed;
		Movement->GroundFriction = OriginalGroundFriction;
		Movement->BrakingDecelerationWalking = OriginalBrakingDeceleration;
		Movement->RotationRate = OriginalRotationRate;
		Movement->bOrientRotationToMovement = bOriginalOrientToMovement;
		Movement->bUseControllerDesiredRotation = bOriginalControllerDesiredRotation;
	}
	BoundMovement.Reset();
	CurrentRequest = 0;
	if (UWorld* World = GetWorld()) { World->GetTimerManager().ClearTimer(LandingTimer); }
	bJustLanded = false;
	Gait = EGait::Run;
	CharacterInputState = FCharacterInputState();
}

void UIMGHeroLocomotionComponent::OnLocomotionUnavailableNative()
{
	ReleaseBindings();
}

void UIMGHeroLocomotionComponent::Activate(bool bReset)
{
	const bool bWasDisabled = bLocomotionDisabled;
	bLocomotionDisabled = false;
	Super::Activate(bReset);
	if (bWasDisabled && IsLocomotionReady()) { OnLocomotionReadyNative(); }
}

void UIMGHeroLocomotionComponent::Deactivate()
{
	bLocomotionDisabled = true;
	ReleaseBindings();
	Super::Deactivate();
}

void UIMGHeroLocomotionComponent::HandleIntentTagChanged(FGameplayTag, int32)
{
	RefreshRequestFromTags();
}

uint8 UIMGHeroLocomotionComponent::CaptureRequest()
{
	return !bLocomotionDisabled && IsLocomotionReady() ? CurrentRequest : 0;
}

void UIMGHeroLocomotionComponent::RefreshRequestFromTags()
{
	UIMGAbilitySystemComponent* ASC = BoundASC.Get();
	CurrentRequest = 0;
	if (ASC && IsLocomotionReady())
	{
		if (ASC->HasMatchingGameplayTag(IMGGameplayTags::Event_Movement_Gait_Sprint)) { CurrentRequest |= HeroMovement::Sprint; }
		if (ASC->HasMatchingGameplayTag(IMGGameplayTags::Event_Movement_Gait_Walk)) { CurrentRequest |= HeroMovement::Walk; }
		if (ASC->HasMatchingGameplayTag(IMGGameplayTags::Event_Movement_RotationMode_Aim)) { CurrentRequest |= HeroMovement::Aim; }
		if (ASC->HasMatchingGameplayTag(IMGGameplayTags::Event_Movement_RotationMode_Strafe)) { CurrentRequest |= HeroMovement::Strafe; }
	}
}

void UIMGHeroLocomotionComponent::ApplyRequest(uint8 Request, const FVector& Input)
{
	UIMGCharacterMovementComponent* Movement = BoundMovement.Get();
	if (!Movement || !IsLocomotionReady())
	{
		ReleaseBindings();
		return;
	}
	if (Movement->IsRagdollMovementMode() || Movement->IsRagdollRecovering())
	{
		return;
	}
	Request &= HeroMovement::Mask;
	if (bLocomotionDisabled) { return; }
	CharacterInputState.bWantsToSprint = (Request & HeroMovement::Sprint) != 0;
	CharacterInputState.bWantsToWalk = (Request & HeroMovement::Walk) != 0;
	CharacterInputState.bWantsToAim = (Request & HeroMovement::Aim) != 0;
	CharacterInputState.bWantsToStrafe = (Request & HeroMovement::Strafe) != 0;
	const bool bControllerFacing = CharacterInputState.bWantsToAim || CharacterInputState.bWantsToStrafe;
	const float InputYaw = Input.Rotation().Yaw;
	const bool bCanSprint = CharacterInputState.bWantsToSprint && (!bControllerFacing
		|| FMath::Abs(FMath::FindDeltaAngleDegrees(GetIMGCharacter()->GetActorRotation().Yaw, InputYaw)) < 50.f);
	Gait = bCanSprint ? EGait::Sprint : (CharacterInputState.bWantsToWalk ? EGait::Walk : EGait::Run);
	WriteMovementParameters();
}

float UIMGHeroLocomotionComponent::DirectionalSpeed(const FVector& Speeds) const
{
	const UIMGCharacterMovementComponent* Movement = BoundMovement.Get();
	float Map = 0.f;
	if (Movement && (CharacterInputState.bWantsToAim || CharacterInputState.bWantsToStrafe) && StrafeSpeedMapCurve)
	{
		const FVector Forward = FRotationMatrix(GetIMGCharacter()->GetActorRotation()).GetScaledAxis(EAxis::X);
		const float Cosine = static_cast<float>(FVector::DotProduct(Forward, Movement->Velocity.GetSafeNormal2D()));
		// Match CalculateDirection, including purely vertical velocity; the curve consumes absolute angle.
		const float Angle = Movement->Velocity.IsNearlyZero() ? 0.f : FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Cosine, -1.f, 1.f)));
		const float Value = StrafeSpeedMapCurve->GetFloatValue(FMath::Abs(Angle));
		Map = FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.f, 2.f) : 0.f;
	}
	return FMath::Max(0.f, static_cast<float>(Map < 1.f ? FMath::Lerp(Speeds.X, Speeds.Y, Map) : FMath::Lerp(Speeds.Y, Speeds.Z, Map - 1.f)));
}

void UIMGHeroLocomotionComponent::WriteMovementParameters()
{
	UIMGCharacterMovementComponent* Movement = BoundMovement.Get();
	if (!Movement) { return; }
	if (Movement->IsRagdollMovementMode() || Movement->IsRagdollRecovering()) { return; }
	const bool bControllerFacing = CharacterInputState.bWantsToAim || CharacterInputState.bWantsToStrafe;
	Movement->bOrientRotationToMovement = !bControllerFacing;
	Movement->bUseControllerDesiredRotation = bControllerFacing;
	Movement->RotationRate = Movement->IsFalling() ? FRotator(0.f, 720.f, 0.f) : FRotator(0.f, -1.f, 0.f);
	const float Speed = Movement->Velocity.Size2D();
	Movement->MaxAcceleration = Gait == EGait::Sprint ? static_cast<float>(FMath::GetMappedRangeValueClamped(FVector2D(300.f, 700.f), FVector2D(800.f, 300.f), Speed))
		: (Gait == EGait::Walk ? 500.f : 800.f);
	Movement->GroundFriction = Gait == EGait::Sprint ? static_cast<float>(FMath::GetMappedRangeValueClamped(FVector2D(0.f, 500.f), FVector2D(1.f, 2.f), Speed))
		: (Gait == EGait::Walk ? 1.f : 2.f);
	Movement->BrakingDecelerationWalking = bJustLanded ? 2000.f : 800.f;
	Movement->MaxWalkSpeed = DirectionalSpeed(Gait == EGait::Walk ? WalkSpeeds : (Gait == EGait::Sprint ? SprintSpeeds : RunSpeeds));
	Movement->MaxWalkSpeedCrouched = DirectionalSpeed(CrouchSpeeds);
}

void UIMGHeroLocomotionComponent::UpdateLocomotionPreCMC_Implementation(float DeltaTime)
{
	if (!bLocomotionDisabled && IsLocomotionReady())
	{
		if (const UIMGCharacterMovementComponent* Movement = BoundMovement.Get())
		{
			const AIMGCharacter* Character = GetIMGCharacter();
			const FVector Input = Character->IsLocallyControlled()
				? Character->GetPendingMovementInputVector() : Movement->GetCurrentAcceleration();
			ApplyRequest(CurrentRequest, Input);
		}
	}
}

void UIMGHeroLocomotionComponent::HandleLanded(const FHitResult&)
{
	if (IsLocomotionReady() && !bLocomotionDisabled && !GetIMGCharacter()->bClientUpdating)
	{
		bJustLanded = true;
		GetWorld()->GetTimerManager().SetTimer(LandingTimer, this, &ThisClass::ClearLandingBraking, 0.3f, false);
	}
}

void UIMGHeroLocomotionComponent::ClearLandingBraking()
{
	bJustLanded = false;
}

float UIMGHeroLocomotionComponent::CalculateBrakingFriction() const
{
	const UIMGCharacterMovementComponent* Movement = BoundMovement.Get();
	return Movement && Movement->GetCurrentAcceleration().IsZero() ? 3.f : 0.f;
}
