#include "Character/IMGHeroLocomotionComponent.h"

#include "AbilitySystem/Attributes/IMGLocomotionSet.h"
#include "AbilitySystem/IMGAbilitySystemComponent.h"
#include "Camera/IMGCameraComponent.h"
#include "Camera/IMGCameraDamping.h"
#include "Camera/IMGCameraShakeInstance.h"
#include "Camera/IMGCameraShakeDefinition.h"
#include "Character/IMGCharacter.h"
#include "Character/IMGCharacterMovementComponent.h"
#include "CoreGlobals.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "IMGGameplayTags.h"
#include "IMGLogChannels.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_HeroLocomotion_RunningCameraShake, "IMG.CameraShake.Running");

UIMGHeroLocomotionComponent::UIMGHeroLocomotionComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	RunningCameraShakePattern.Envelope.AttackTime = 0.15f;
	RunningCameraShakePattern.Envelope.HoldTime = -1.f;
	RunningCameraShakePattern.Envelope.ReleaseTime = 0.2f;

	FIMGCameraShakeOscillatorSettings Vertical;
	Vertical.Component = EIMGCameraShakeComponent::CameraLocationZ;
	Vertical.Amplitude = 0.5f;
	Vertical.Frequency = 2.8f;
	Vertical.Phase = 0.f;

	FIMGCameraShakeOscillatorSettings Pitch;
	Pitch.Component = EIMGCameraShakeComponent::Pitch;
	Pitch.Amplitude = 0.12f;
	Pitch.Frequency = 2.8f;
	Pitch.Phase = 0.25f;

	FIMGCameraShakeOscillatorSettings Roll;
	Roll.Component = EIMGCameraShakeComponent::Roll;
	Roll.Amplitude = 0.08f;
	Roll.Frequency = 1.4f;
	Roll.Phase = 0.f;
	RunningCameraShakePattern.Components = { Vertical, Pitch, Roll };
}

UIMGCameraComponent* UIMGHeroLocomotionComponent::GetRunningCameraShakeCamera() const
{
	const AIMGCharacter* Character = GetIMGCharacter();
	const UIMGCharacterMovementComponent* Movement = GetBoundMovement();
	const APlayerController* Controller = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	const UIMGAbilitySystemComponent* ASC = GetIMGAbilitySystemComponent();
	if (!IsLocomotionReady() || !HasBegunPlay() || !IsRegistered() || !IsComponentTickEnabled() || IsBeingDestroyed()
		|| !IsValid(Character) || Character->IsActorBeingDestroyed() || !Character->IsLocallyControlled()
		|| !IsValid(Controller) || Controller->IsActorBeingDestroyed() || !Controller->IsLocalController()
		|| Controller->GetViewTarget() != Character || !GetWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer
		|| !Movement || !Movement->IsActive() || !Movement->IsRegistered() || !Movement->IsComponentTickEnabled()
		|| (ASC && ASC->HasMatchingGameplayTag(IMGGameplayTags::Status_Death)))
	{
		return nullptr;
	}
	UIMGCameraComponent* Camera = UIMGCameraComponent::FindCameraComponent(Character);
	return Camera && Camera->IsRegistered() && Camera->IsActive() && Camera->GetTargetActor() == Character ? Camera : nullptr;
}

bool UIMGHeroLocomotionComponent::IsRunningCameraShakeMovementValid() const
{
	const UIMGCharacterMovementComponent* Movement = GetBoundMovement();
	return Movement && Movement->IsMovingOnGround() && !Movement->IsCrouching() && !Movement->IsCrawling()
		&& !Movement->IsRagdollRecovering() && !Movement->HasAnimRootMotion() && !Movement->HasRootMotionSources() && Gait != EGait::Walk;
}

void UIMGHeroLocomotionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Cleanup must still run when the CMC no longer sends movement updates.
	if (GetRunningCameraShakeCamera() != RunningShakeCamera.Get())
	{
		StopRunningCameraShake(true);
	}
	else if (!IsRunningCameraShakeMovementValid())
	{
		StopRunningCameraShake(false);
	}
}

void UIMGHeroLocomotionComponent::HandleRunningCameraShakeMovementUpdated(float, FVector, FVector)
{
	const AIMGCharacter* Character = GetIMGCharacter();
	if (!Character || Character->bClientUpdating) { return; }
	// Saved moves can replay this delegate with simulation delta times. Presentation uses the local frame clock.
	if (const UWorld* World = GetWorld()) { UpdateRunningCameraShake(World->GetDeltaSeconds()); }
}

void UIMGHeroLocomotionComponent::UpdateRunningCameraShake(float DeltaTime)
{
	UIMGCameraComponent* Camera = GetRunningCameraShakeCamera();
	if (Camera != RunningShakeCamera.Get()) { StopRunningCameraShake(true); }
	if (!Camera)
	{
		StopRunningCameraShake(true);
		return;
	}
	RunningShakeCamera = Camera;
	if (!RunningShakeDefinition)
	{
		RunningShakeDefinition = NewObject<UIMGCameraShakeDefinition>(this, NAME_None, RF_Transient);
		RunningShakeDefinition->Settings = RunningCameraShakePattern;
		RunningShakeDefinition->ShakeCategory = TAG_HeroLocomotion_RunningCameraShake;
	}

	const UIMGCameraShakeInstance* Instance = RunningShakeInstance.Get();
	const bool bValidSourceSettings = FMath::IsFinite(RunningCameraShakeMinAmplitude) && RunningCameraShakeMinAmplitude >= 0.f
		&& FMath::IsFinite(RunningCameraShakeMaxAmplitude) && RunningCameraShakeMaxAmplitude >= 0.f
		&& FMath::IsFinite(RunningCameraShakeMaxFrequencyScale) && RunningCameraShakeMaxFrequencyScale >= 0.f
		&& FMath::IsFinite(RunningCameraShakeSpringConstant) && RunningCameraShakeSpringConstant >= 0.f;
	const bool bResponseEnabled = RunningCameraShakeMinAmplitude > 0.f && RunningCameraShakeMaxAmplitude > RunningCameraShakeMinAmplitude;
	const bool bValidDefinition = !bResponseEnabled || (Instance && Instance->IsPlaying()) || RunningShakeDefinition->IsValidForPlayback(true, false);
	if (!bValidSourceSettings || !bValidDefinition)
	{
		if (!bWarnedInvalidRunningShakeSettings)
		{
			UE_LOG(LogIMGLocomotion, Warning, TEXT("%s: Running Camera Shake rejected: %s"),
				*GetPathName(), bValidDefinition
					? TEXT("source settings require finite nonnegative amplitude, frequency growth, and spring constant values.")
					: TEXT("configure at least one oscillator and valid envelopes under Running Camera Shake Pattern on HeroLocomotion."));
			bWarnedInvalidRunningShakeSettings = true;
		}
		StopRunningCameraShake(true);
		return;
	}
	bWarnedInvalidRunningShakeSettings = false;
	if (!bResponseEnabled)
	{
		StopRunningCameraShake(false);
		return;
	}

	const UIMGCharacterMovementComponent* Movement = GetBoundMovement();
	const UIMGLocomotionSet* Locomotion = GetLocomotionSet();
	if (!IsRunningCameraShakeMovementValid())
	{
		StopRunningCameraShake(false);
		return;
	}
	// GTA uses a fixed physical-speed gate of 1.5 m/s independently of its configurable shake response.
	constexpr float MinRunningSpeed = 150.f;
	const float Speed = static_cast<float>(Movement->ProjectToGravityFloor(Movement->Velocity).Size());
	if (!FMath::IsFinite(Speed) || Speed < MinRunningSpeed)
	{
		StopRunningCameraShake(false);
		return;
	}

	const float Map = GetDirectionMap(CharacterInputState.bWantsToAim || CharacterInputState.bWantsToStrafe);
	const float RunSpeed = Locomotion->GetRunSpeed() * GetDirectionScale(RunDirectionScale, Map);
	const float SprintSpeed = Locomotion->GetSprintSpeed() * GetDirectionScale(SprintDirectionScale, Map);
	if (!FMath::IsFinite(RunSpeed) || !FMath::IsFinite(SprintSpeed) || RunSpeed <= UE_SMALL_NUMBER
		|| !FMath::IsFinite(DeltaTime) || DeltaTime < 0.f)
	{
		StopRunningCameraShake(false);
		return;
	}
	const double DesiredRatio = SprintSpeed > RunSpeed + UE_SMALL_NUMBER
		? FMath::Clamp((Speed - RunSpeed) / (SprintSpeed - RunSpeed), 0.f, 1.f) : 0.f;
	if (!bRunningShakeRatioInitialized || !Instance || !Instance->IsPlaying())
	{
		RunningShakeRatio = DesiredRatio;
		RunningShakeRatioVelocity = 0.0;
		bRunningShakeRatioInitialized = true;
	}
	else if (LastRunningShakeUpdateFrame != GFrameCounter)
	{
		IMGCameraDamping::AdvanceSpring(DesiredRatio, RunningCameraShakeSpringConstant, 1.0, DeltaTime,
			RunningShakeRatio, RunningShakeRatioVelocity);
	}
	LastRunningShakeUpdateFrame = GFrameCounter;

	const float Ratio = static_cast<float>(FMath::Clamp(RunningShakeRatio, 0.0, 1.0));
	const float Amplitude = FMath::Lerp(RunningCameraShakeMinAmplitude, RunningCameraShakeMaxAmplitude, Ratio);
	const float Frequency = 1.f + Ratio * RunningCameraShakeMaxFrequencyScale;
	if (!Camera->UpdateCameraShake(RunningShakeInstance.Get(), Amplitude, Frequency))
	{
		RunningShakeInstance = Camera->StartCameraShake(RunningShakeDefinition, this, Amplitude, Frequency);
	}
}

void UIMGHeroLocomotionComponent::StopRunningCameraShake(bool bImmediately)
{
	if (UIMGCameraComponent* Camera = RunningShakeCamera.Get())
	{
		if (bImmediately) { Camera->StopCameraShakesFromSource(this, true); }
		else if (UIMGCameraShakeInstance* Instance = RunningShakeInstance.Get(); Instance && !Instance->IsReleasing())
		{
			Camera->StopCameraShake(Instance, false);
		}
	}
	if (bImmediately)
	{
		RunningShakeCamera.Reset();
		RunningShakeInstance.Reset();
	}
	RunningShakeRatio = 0.0;
	RunningShakeRatioVelocity = 0.0;
	bRunningShakeRatioInitialized = false;
	LastRunningShakeUpdateFrame = MAX_uint64;
}
