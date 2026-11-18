#pragma once

#include "Character/IMGLocomotionComponent.h"
#include "Camera/IMGCameraShake.h"
#include "Engine/TimerHandle.h"
#include "GameplayTagContainer.h"
#include "UObject/SoftObjectPtr.h"
#include "IMGHeroLocomotionComponent.generated.h"

class UCurveFloat;
class UIMGCameraComponent;
class UIMGCameraShakeInstance;
class UIMGCameraShakeDefinition;
struct FHitResult;

UENUM(BlueprintType)
enum class EGait : uint8
{
	Walk,
	Run,
	Sprint
};

/** Shared by the character's animation/traversal output, not an additional movement authority. */
USTRUCT(BlueprintType)
struct IMAGINEGAME_API FCharacterInputState
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bWantsToSprint = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bWantsToWalk = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bWantsToStrafe = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bWantsToAim = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bWantsToCrouch = false;
};

/** Hero movement rules. CMC transports requests without knowing their gameplay meaning. */
UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent))
class IMAGINEGAME_API UIMGHeroLocomotionComponent : public UIMGLocomotionComponent
{
	GENERATED_BODY()
public:
	UIMGHeroLocomotionComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Locomotion")
	FVector WalkDirectionScale = FVector(1.f, 150.f / 165.f, 130.f / 165.f);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Locomotion")
	FVector RunDirectionScale = FVector(1.f, 350.f / 380.f, 300.f / 380.f);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Locomotion")
	FVector SprintDirectionScale = FVector(1.f);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Locomotion")
	FVector CrouchDirectionScale = FVector(1.f, 160.f / 170.f, 150.f / 170.f);

	/** Optional angle-to-direction curve. Missing curves use the forward speed scale. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Locomotion")
	TSoftObjectPtr<UCurveFloat> StrafeSpeedMapCurve;

	/** Rotation rate in degrees per second when not falling. Negative axes turn instantly. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Locomotion")
	FRotator GroundRotationRate = FRotator(0.f, -1.f, 0.f);

	/** Rotation rate in degrees per second while falling. Negative axes turn instantly. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Locomotion")
	FRotator AirRotationRate = FRotator(0.f, 720.f, 0.f);

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	EGait Gait = EGait::Run;
	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	FCharacterInputState CharacterInputState;

	UFUNCTION(BlueprintPure, Category = "Locomotion")
	float CalculateBrakingFriction() const;

protected:
	/** Shared waveform and envelopes for running and sprinting. Playback instances are owned by the camera. */
	UPROPERTY(EditDefaultsOnly, Category = "Locomotion|Running Camera Shake", meta = (DisplayName = "Pattern"))
	FIMGCameraShakeSettings RunningCameraShakePattern;

	/** Pattern amplitude multiplier at run ratio zero. Zero disables this effect; sprint amplitude must be greater. */
	UPROPERTY(EditDefaultsOnly, Category = "Locomotion|Running Camera Shake|Run", meta = (DisplayName = "Amplitude Scale", ClampMin = "0", ClampMax = "1"))
	float RunningCameraShakeMinAmplitude = 0.6f;

	/** Pattern amplitude multiplier at run ratio one; intermediate movement blends from the running value. */
	UPROPERTY(EditDefaultsOnly, Category = "Locomotion|Running Camera Shake|Sprint", meta = (DisplayName = "Amplitude Scale", ClampMin = "0", ClampMax = "2"))
	float RunningCameraShakeMaxAmplitude = 1.f;

	/** Extra frequency growth, as in GTA: multiplier = 1 + RunRatio * Increase. A value of 0.25 reaches 1.25x. */
	UPROPERTY(EditDefaultsOnly, Category = "Locomotion|Running Camera Shake|Sprint", meta = (DisplayName = "Frequency Increase", ClampMin = "0", ClampMax = "2"))
	float RunningCameraShakeMaxFrequencyScale = 0.25f;

	/** Smooths the shared run ratio driving amplitude and frequency. Unit-mass k, not Hz; zero follows immediately. */
	UPROPERTY(EditDefaultsOnly, Category = "Locomotion|Running Camera Shake|Response", meta = (DisplayName = "Run Ratio Spring Constant", ClampMin = "0", ClampMax = "1000"))
	float RunningCameraShakeSpringConstant = 5.f;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void OnLocomotionReadyNative() override;
	virtual void OnLocomotionUnavailableNative() override;
	virtual uint8 GetMovementRequest() const override { return CurrentRequest; }
	virtual void ExtendMovementContext(FMovementSimulationContext& Context) const override;
	virtual bool CalculateMovementParameters(uint8 Request, const FMovementSimulationContext& Context,
		const FVector& Input, bool bReplay, FCharacterMovementParameters& OutParameters) override;

private:
	void ReleaseBindings();
	void HandleIntentTagChanged(FGameplayTag Tag, int32 NewCount);
	void RefreshRequestFromTags();
	void ClearLandingBraking();
	float GetDirectionMap(bool bControllerFacing) const;
	float GetDirectionScale(const FVector& Scales, float Map) const;
	UIMGCameraComponent* GetRunningCameraShakeCamera() const;
	bool IsRunningCameraShakeMovementValid() const;
	void UpdateRunningCameraShake(float DeltaTime);
	void StopRunningCameraShake(bool bImmediately);
	UFUNCTION()
	void HandleRunningCameraShakeMovementUpdated(float DeltaSeconds, FVector OldLocation, FVector OldVelocity);
	UFUNCTION()
	void HandleLanded(const FHitResult& Hit);

	UPROPERTY(Transient, DuplicateTransient)
	TObjectPtr<UCurveFloat> ResolvedStrafeSpeedMapCurve;

	TWeakObjectPtr<UIMGAbilitySystemComponent> BoundASC;
	TMap<FGameplayTag, FDelegateHandle> IntentTagHandles;
	uint8 CurrentRequest = 0;
	FTimerHandle LandingTimer;

	TWeakObjectPtr<UIMGCameraComponent> RunningShakeCamera;
	TWeakObjectPtr<UIMGCameraShakeInstance> RunningShakeInstance;
	UPROPERTY(Transient, DuplicateTransient)
	TObjectPtr<UIMGCameraShakeDefinition> RunningShakeDefinition;
	double RunningShakeRatio = 0.0;
	double RunningShakeRatioVelocity = 0.0;
	uint64 LastRunningShakeUpdateFrame = MAX_uint64;
	bool bRunningShakeRatioInitialized = false;
	bool bWarnedInvalidRunningShakeSettings = false;

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion", meta = (AllowPrivateAccess = "true"))
	bool bJustLanded = false;

	/** World-space movement velocity at the last landing, before switching movement mode. */
	UPROPERTY(BlueprintReadOnly, Category = "Locomotion", meta = (AllowPrivateAccess = "true"))
	FVector LandVelocity = FVector::ZeroVector;
};
