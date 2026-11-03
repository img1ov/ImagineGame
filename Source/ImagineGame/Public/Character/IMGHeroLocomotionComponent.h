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
	virtual void Activate(bool bReset = false) override;
	virtual void Deactivate() override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Locomotion")
	FVector WalkSpeeds = FVector(165.f, 150.f, 130.f);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Locomotion")
	FVector RunSpeeds = FVector(380.f, 350.f, 300.f);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Locomotion")
	FVector SprintSpeeds = FVector(585.f);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Locomotion")
	FVector CrouchSpeeds = FVector(170.f, 160.f, 150.f);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Locomotion")
	TObjectPtr<UCurveFloat> StrafeSpeedMapCurve;

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	EGait Gait = EGait::Run;
	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	FCharacterInputState CharacterInputState;

	UFUNCTION(BlueprintPure, Category = "Locomotion")
	float CalculateBrakingFriction() const;

protected:
	virtual bool UsesLocomotionSet() const override { return false; }
	virtual void OnLocomotionReadyNative() override;
	virtual void OnLocomotionUnavailableNative() override;
	virtual void UpdateLocomotionPreCMC_Implementation(float DeltaTime) override;

private:
	void ReleaseBindings();
	void HandleIntentTagChanged(FGameplayTag Tag, int32 NewCount);
	void RefreshRequestFromTags();
	uint8 CaptureRequest();
	void ApplyRequest(uint8 Request, const FVector& Input);
	void WriteMovementParameters();
	void ClearLandingBraking();
	float DirectionalSpeed(const FVector& Speeds) const;
	UFUNCTION()
	void HandleLanded(const FHitResult& Hit);

	TWeakObjectPtr<UIMGAbilitySystemComponent> BoundASC;
	TWeakObjectPtr<UIMGCharacterMovementComponent> BoundMovement;
	TMap<FGameplayTag, FDelegateHandle> IntentTagHandles;
	uint8 CurrentRequest = 0;
	FTimerHandle LandingTimer;
	float OriginalMaxAcceleration = 0.f;
	float OriginalMaxWalkSpeed = 0.f;
	float OriginalMaxCrouchSpeed = 0.f;
	float OriginalGroundFriction = 0.f;
	float OriginalBrakingDeceleration = 0.f;
	FRotator OriginalRotationRate = FRotator::ZeroRotator;
	bool bOriginalOrientToMovement = false;
	bool bOriginalControllerDesiredRotation = false;
	bool bJustLanded = false;
	bool bLocomotionDisabled = false;
};
