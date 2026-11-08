#pragma once

#include "Character/IMGLocomotionComponent.h"
#include "Engine/TimerHandle.h"
#include "GameplayTagContainer.h"
#include "IMGHeroLocomotionComponent.generated.h"

class UCurveFloat;
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
	UIMGHeroLocomotionComponent(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Locomotion")
	FVector WalkDirectionScale = FVector(1.f, 150.f / 165.f, 130.f / 165.f);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Locomotion")
	FVector RunDirectionScale = FVector(1.f, 350.f / 380.f, 300.f / 380.f);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Locomotion")
	FVector SprintDirectionScale = FVector(1.f);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Locomotion")
	FVector CrouchDirectionScale = FVector(1.f, 160.f / 170.f, 150.f / 170.f);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Locomotion")
	TObjectPtr<UCurveFloat> StrafeSpeedMapCurve;

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
	virtual void BeginPlay() override;
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
	UFUNCTION()
	void HandleLanded(const FHitResult& Hit);

	TWeakObjectPtr<UIMGAbilitySystemComponent> BoundASC;
	TMap<FGameplayTag, FDelegateHandle> IntentTagHandles;
	uint8 CurrentRequest = 0;
	FTimerHandle LandingTimer;

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion", meta = (AllowPrivateAccess = "true"))
	bool bJustLanded = false;

	/** World-space movement velocity at the last landing, before switching movement mode. */
	UPROPERTY(BlueprintReadOnly, Category = "Locomotion", meta = (AllowPrivateAccess = "true"))
	FVector LandVelocity = FVector::ZeroVector;
};
