#pragma once

#include "Components/ActorComponent.h"
#include "Character/Ragdoll/IMGRagdollTypes.h"
#include "IMGRagdollComponent.generated.h"

class ACharacter;
class UIMGCharacterMovementComponent;
class USkeletalMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FIMGRagdollEvent, int32, Session);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FIMGRagdollEndedEvent, int32, Session, EIMGRagdollEndReason, Reason);

/** Optional execution coordinator. Abilities own business rules and presentation. */
UCLASS(BlueprintType, Blueprintable, ClassGroup = (Movement), meta = (BlueprintSpawnableComponent))
class IMAGINEGAME_API UIMGRagdollComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UIMGRagdollComponent(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintPure, Category = "IMG|Ragdoll", meta = (DefaultToSelf = "Actor"))
	static UIMGRagdollComponent* FindRagdollComponent(const AActor* Actor);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "IMG|Ragdoll")
	bool StartRagdoll(FVector AdditionalVelocity = FVector::ZeroVector);

	/** Approve capsule placement and release the custom mode; does not play or select animation. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "IMG|Ragdoll")
	bool BeginRecovery(int32 ExpectedSession);

	/** BodyLocalOffset locates the matched follow point in the proposed capsule's unit-scale space. */
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "IMG|Ragdoll")
	bool QueryRecovery(int32 ExpectedSession, FTransform& OutTransform, EIMGRagdollIssue& OutIssue, FVector BodyLocalOffset = FVector::ZeroVector) const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "IMG|Ragdoll")
	bool BeginRecoveryAt(int32 ExpectedSession, const FTransform& Transform, FVector BodyLocalOffset = FVector::ZeroVector);

	/** The ability must complete or abort its session; there is no automatic recovery timer. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "IMG|Ragdoll")
	void CompleteRecovery(int32 ExpectedSession, bool bInterrupted = false);

	/** Forced cleanup ends the session even if safe placement fails, leaving movement disabled. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "IMG|Ragdoll")
	bool AbortRagdoll(int32 ExpectedSession, bool bForce = false);

	UFUNCTION(BlueprintPure, Category = "IMG|Ragdoll")
	bool IsRagdollAvailable() const;

	UFUNCTION(BlueprintPure, Category = "IMG|Ragdoll")
	EIMGRagdollPhase GetRagdollPhase() const { return ActiveState.Phase; }

	UFUNCTION(BlueprintPure, Category = "IMG|Ragdoll")
	bool IsFaceUp() const;

	UFUNCTION(BlueprintPure, Category = "IMG|Ragdoll")
	int32 GetRagdollSession() const { return ActiveState.Session; }

	UFUNCTION(BlueprintPure, Category = "IMG|Ragdoll")
	FVector GetRagdollInitialVelocity() const { return ActiveState.InitialVelocity; }

	/** Read-only sampling seam for a custom target. The native implementation reads the simulated pelvis. */
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "IMG|Ragdoll")
	bool GetRagdollFollowTarget(FTransform& OutTarget, bool& bFaceUp) const;
	virtual bool GetRagdollFollowTarget_Implementation(FTransform& OutTarget, bool& bFaceUp) const;

	/** Optional body facts only. GetUp thresholds and settled time belong to the caller. */
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "IMG|Ragdoll")
	bool GetRagdollBodyMotion(FVector& OutLinearVelocity, FVector& OutAngularVelocityRadians) const;
	virtual bool GetRagdollBodyMotion_Implementation(FVector& OutLinearVelocity, FVector& OutAngularVelocityRadians) const;

	UFUNCTION(BlueprintPure, Category = "IMG|Ragdoll")
	EIMGRagdollIssue GetRagdollMovementIssue() const;

	UPROPERTY(BlueprintAssignable, Category = "IMG|Ragdoll")
	FIMGRagdollEvent OnRagdollStarted;

	UPROPERTY(BlueprintAssignable, Category = "IMG|Ragdoll")
	FIMGRagdollEvent OnRecoveryStarted;

	UPROPERTY(BlueprintAssignable, Category = "IMG|Ragdoll")
	FIMGRagdollEndedEvent OnRagdollEnded;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void Activate(bool bReset = false) override;
	virtual void Deactivate() override;
	virtual void OnRep_IsActive() override;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Ragdoll|Movement")
	FIMGRagdollSettings Settings;

	UPROPERTY(EditDefaultsOnly, Replicated, BlueprintReadOnly, Category = "IMG|Ragdoll|Target")
	FName PelvisBone = TEXT("pelvis");

	UPROPERTY(EditDefaultsOnly, Replicated, BlueprintReadOnly, Category = "IMG|Ragdoll|Target")
	FName OrientationBone = TEXT("spine_05");

	/** Local axis on OrientationBone pointing out of the character's chest. */
	UPROPERTY(EditDefaultsOnly, Replicated, BlueprintReadOnly, Category = "IMG|Ragdoll|Target")
	FVector FaceUpAxis = FVector::YAxisVector;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

private:
	bool Initialize();
	void TearDown();
	UFUNCTION()
	void HandleMovementUpdated();
	void ReconcileState();
	void SampleTarget();
	void EndLocalSession(EIMGRagdollEndReason Reason);

	// Observed movement state only; CMC owns its single replicated movement contract.
	UPROPERTY(Transient)
	FIMGRagdollRepState ActiveState;

	TWeakObjectPtr<ACharacter> Character;
	TWeakObjectPtr<UIMGCharacterMovementComponent> Movement;
	TWeakObjectPtr<USkeletalMeshComponent> SampledMesh;
	uint16 LocalSession = 0;
	uint16 OwnedSession = 0;
	bool bStartedNotified = false;
	bool bRecoveryNotified = false;
	bool bTransitioning = false;
	bool bReconciling = false;
	bool bTearingDown = false;
};
