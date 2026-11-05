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
	UIMGAbilitySystemComponent* ASC = GetIMGAbilitySystemComponent();
	if (!GetBoundMovement() || !ASC || !IsLocomotionReady()) { return; }
	if (!StrafeSpeedMapCurve)
	{
		UE_LOG(LogIMGLocomotion, Warning, TEXT("%s: StrafeSpeedMapCurve is missing. Aim and Strafe use the forward speed scale."), *GetPathName());
	}
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
	CurrentRequest = 0;
	if (UWorld* World = GetWorld()) { World->GetTimerManager().ClearTimer(LandingTimer); }
	bJustLanded = false;
	LandVelocity = FVector::ZeroVector;
	Gait = EGait::Run;
	CharacterInputState = FCharacterInputState();
}

void UIMGHeroLocomotionComponent::OnLocomotionUnavailableNative()
{
	ReleaseBindings();
}

void UIMGHeroLocomotionComponent::HandleIntentTagChanged(FGameplayTag, int32)
{
	RefreshRequestFromTags();
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

void UIMGHeroLocomotionComponent::ExtendMovementContext(FMovementSimulationContext& Context) const
{
	Context.Parameters.SetNum(LocomotionParameterCount + 1);
	Context.Parameters[LocomotionParameterCount] = bJustLanded ? 1.f : 0.f;
}

bool UIMGHeroLocomotionComponent::CalculateMovementParameters(uint8 Request, const FMovementSimulationContext& Context,
	const FVector& Input, bool bReplay, FCharacterMovementParameters& OutParameters)
{
	UIMGCharacterMovementComponent* Movement = GetBoundMovement();
	if (!Movement || Context.Parameters.Num() != LocomotionParameterCount + 1
		|| !Super::CalculateMovementParameters(Request, Context, Input, bReplay, OutParameters)) { return false; }
	Request &= HeroMovement::Mask;
	FCharacterInputState InputState;
	InputState.bWantsToSprint = (Request & HeroMovement::Sprint) != 0;
	InputState.bWantsToWalk = (Request & HeroMovement::Walk) != 0;
	InputState.bWantsToAim = (Request & HeroMovement::Aim) != 0;
	InputState.bWantsToStrafe = (Request & HeroMovement::Strafe) != 0;
	const bool bControllerFacing = InputState.bWantsToAim || InputState.bWantsToStrafe;
	const bool bCanSprint = InputState.bWantsToSprint && (!bControllerFacing
		|| FMath::Abs(FMath::FindDeltaAngleDegrees(GetIMGCharacter()->GetActorRotation().Yaw, Input.Rotation().Yaw)) < 50.f);
	const EGait MoveGait = bCanSprint ? EGait::Sprint : (InputState.bWantsToWalk ? EGait::Walk : EGait::Run);
	const float Speed = Movement->Velocity.Size2D();
	const float Map = GetDirectionMap(bControllerFacing);
	const int32 SpeedParameter = MoveGait == EGait::Walk ? WalkSpeedParameter : (MoveGait == EGait::Sprint ? SprintSpeedParameter : RunSpeedParameter);
	const FVector& Scale = MoveGait == EGait::Walk ? WalkDirectionScale : (MoveGait == EGait::Sprint ? SprintDirectionScale : RunDirectionScale);
	OutParameters.MaxWalkSpeed = Context.Parameters[SpeedParameter] * GetDirectionScale(Scale, Map);
	OutParameters.MaxWalkSpeedCrouched *= GetDirectionScale(CrouchDirectionScale, Map);
	OutParameters.bOrientRotationToMovement = !bControllerFacing;
	OutParameters.bUseControllerDesiredRotation = bControllerFacing;
	OutParameters.RotationRate = Movement->IsFalling() ? FRotator(0.f, 720.f, 0.f) : FRotator(0.f, -1.f, 0.f);
	// Component baselines retain the original gait/speed relationships at their default values.
	const float AccelerationScale = MoveGait == EGait::Sprint
		? static_cast<float>(FMath::GetMappedRangeValueClamped(FVector2D(300.f, 700.f), FVector2D(1.f, 0.375f), Speed))
		: (MoveGait == EGait::Walk ? 0.625f : 1.f);
	const float FrictionScale = MoveGait == EGait::Sprint
		? static_cast<float>(FMath::GetMappedRangeValueClamped(FVector2D(0.f, 500.f), FVector2D(0.5f, 1.f), Speed))
		: (MoveGait == EGait::Walk ? 0.5f : 1.f);
	OutParameters.MaxAcceleration *= AccelerationScale;
	OutParameters.GroundFriction *= FrictionScale;
	OutParameters.BrakingDecelerationWalking *= Context.Parameters[LocomotionParameterCount] > 0.f ? 2.5f : 1.f;
	if (!bReplay)
	{
		Gait = MoveGait;
		CharacterInputState = InputState;
	}
	return true;
}

float UIMGHeroLocomotionComponent::GetDirectionMap(bool bControllerFacing) const
{
	const UIMGCharacterMovementComponent* Movement = GetBoundMovement();
	if (!Movement || !bControllerFacing || !StrafeSpeedMapCurve) { return 0.f; }
	const FVector Forward = FRotationMatrix(GetIMGCharacter()->GetActorRotation()).GetScaledAxis(EAxis::X);
	const float Cosine = static_cast<float>(FVector::DotProduct(Forward, Movement->Velocity.GetSafeNormal2D()));
	const float Angle = Movement->Velocity.IsNearlyZero() ? 0.f : FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Cosine, -1.f, 1.f)));
	const float Value = StrafeSpeedMapCurve->GetFloatValue(FMath::Abs(Angle));
	return FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.f, 2.f) : 0.f;
}

float UIMGHeroLocomotionComponent::GetDirectionScale(const FVector& Scales, float Map) const
{
	return FMath::Max(0.f, static_cast<float>(Map < 1.f ? FMath::Lerp(Scales.X, Scales.Y, Map) : FMath::Lerp(Scales.Y, Scales.Z, Map - 1.f)));
}

void UIMGHeroLocomotionComponent::HandleLanded(const FHitResult&)
{
	if (const UIMGCharacterMovementComponent* Movement = GetBoundMovement(); Movement && IsLocomotionReady() && !GetIMGCharacter()->bClientUpdating)
	{
		bJustLanded = true;
		LandVelocity = Movement->Velocity;
		GetWorld()->GetTimerManager().SetTimer(LandingTimer, this, &ThisClass::ClearLandingBraking, 0.3f, false);
	}
}

void UIMGHeroLocomotionComponent::ClearLandingBraking()
{
	bJustLanded = false;
}

float UIMGHeroLocomotionComponent::CalculateBrakingFriction() const
{
	const UIMGCharacterMovementComponent* Movement = GetBoundMovement();
	return Movement && Movement->GetCurrentAcceleration().IsZero() ? 3.f : 0.f;
}
