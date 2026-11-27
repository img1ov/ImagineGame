#pragma once

#include "Character/Ragdoll/IMGRagdollComponent.h"
#include "IMGRagdollComponent_PhysicsControl.generated.h"

class UPhysicsControlAsset;
class UPhysicsControlComponent;

/** PhysicsControl binding only. Session, sampling, correction and resource sequencing stay in the base executor. */
UCLASS(BlueprintType, Blueprintable, ClassGroup = (Movement), meta = (BlueprintSpawnableComponent))
class IMAGINEGAME_API UIMGRagdollComponent_PhysicsControl : public UIMGRagdollComponent
{
	GENERATED_BODY()

public:
	/** Local execution resource; neither this component nor its controls are a second network authority. */
	UFUNCTION(BlueprintPure, Category = "IMG|Ragdoll|PhysicsControl")
	UPhysicsControlComponent* GetPhysicsControlComponent() const;

protected:
	/** A hard reference makes the setup available with the component class, before the entry frame. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Ragdoll|PhysicsControl")
	TObjectPtr<UPhysicsControlAsset> ControlAsset;

	UPROPERTY(EditAnywhere, Category = "IMG|Ragdoll|PhysicsControl", meta = (UseComponentPicker, AllowedClasses = "/Script/PhysicsControl.PhysicsControlComponent"))
	FComponentReference ControlComponent;

	/** Reuse the sole empty control component when unassigned; otherwise create a dedicated local one. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Ragdoll|PhysicsControl")
	bool bAutoCreateControlComponent = true;

	/** None uses the PCA's initial configuration. This profile is applied before the simulation override below. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Ragdoll|PhysicsControl")
	FName RagdollProfile;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Ragdoll|PhysicsControl")
	FName SimulatedBodyModifierSet = TEXT("All");

	/** Free the body from world pose pins; parent-space controls can retain configured active strength. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Ragdoll|PhysicsControl")
	FName DisabledControlSet = TEXT("WorldSpace");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Ragdoll|PhysicsControl")
	TArray<FName> RequiredControlSets;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Ragdoll|PhysicsControl")
	TArray<FName> RequiredBodyModifierSets;

	virtual bool CanExecuteBody(USkeletalMeshComponent& Mesh) const override;
	virtual bool StartBodySimulation(USkeletalMeshComponent& Mesh) override;
	virtual bool IsBodySimulationValid() const override;
	virtual void UpdateBodySimulation(float DeltaTime) override;
	virtual void StopBodySimulation(USkeletalMeshComponent& Mesh) override;
	virtual void ReleaseBodyResources() override;

private:
	bool ResolveControlComponent();
	void ClearControls();
	bool ValidateCreatedControls() const;
	UPROPERTY(Transient)
	TObjectPtr<UPhysicsControlComponent> RuntimeControlComponent;
	TSoftObjectPtr<UPhysicsControlAsset> PreviousControlAsset;
	TSoftObjectPtr<UPhysicsControlAsset> ActiveControlAsset;
	TArray<FName> OwnedControls;
	TArray<FName> OwnedModifiers;
	bool bCreatedControlComponent = false;
	bool bOwnsControlSetup = false;
	bool bPreviousTickEnabled = false;
};
