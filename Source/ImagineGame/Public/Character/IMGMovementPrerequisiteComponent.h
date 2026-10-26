#pragma once

#include "Components/PawnComponent.h"

#include "IMGMovementPrerequisiteComponent.generated.h"

#define UE_API IMAGINEGAME_API

class ACharacter;
class AController;
class UCharacterMovementComponent;

/** Owns Character/CMC references and scheduling for updates that must precede character movement. */
UCLASS(Abstract, MinimalAPI, BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class UIMGMovementPrerequisiteComponent : public UPawnComponent
{
	GENERATED_BODY()

public:
	UE_API UIMGMovementPrerequisiteComponent(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintPure, Category = "IMG|Movement")
	UE_API ACharacter* GetCharacterOwner() const;

	UFUNCTION(BlueprintPure, Category = "IMG|Movement")
	UE_API UCharacterMovementComponent* GetCharacterMovementComponent() const;

	/** Scheduling readiness only; does not imply ASC or gameplay initialization. */
	UFUNCTION(BlueprintPure, Category = "IMG|Movement")
	UE_API bool IsMovementPrerequisiteReady() const;

protected:
	UE_API virtual void OnRegister() override;
	UE_API virtual void OnUnregister() override;
	UE_API virtual void BeginPlay() override;
	UE_API virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	UE_API virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Derived adapters can require their own data lifecycle without adding it to the scheduling base. */
	UE_API virtual bool CanUpdateLocomotion() const;

	/** Runs before the CMC when scheduling and the derived data lifecycle permit it. */
	UFUNCTION(BlueprintNativeEvent, Category = "IMG|Movement", meta = (DisplayName = "Update Locomotion Pre CMC"))
	UE_API void UpdateLocomotionPreCMC(float DeltaTime);
	UE_API virtual void UpdateLocomotionPreCMC_Implementation(float DeltaTime);

private:
	bool PrepareMovementPrerequisite();
	void ReleaseMovementPrerequisite();
	void SetControllerTickPrerequisite(AController* Controller);

	UFUNCTION()
	void HandlePawnControllerChanged(APawn* Pawn, AController* OldController, AController* NewController);

	UPROPERTY(Transient)
	TObjectPtr<ACharacter> CharacterOwner;

	UPROPERTY(Transient)
	TObjectPtr<UCharacterMovementComponent> CharacterMovementComponent;

	TWeakObjectPtr<AController> TickPrerequisiteController;

	bool bPreMovementUpdatesActive = false;
};

#undef UE_API
