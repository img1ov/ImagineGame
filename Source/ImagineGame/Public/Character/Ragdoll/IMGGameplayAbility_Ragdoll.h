#pragma once

#include "AbilitySystem/Abilities/IMGGameplayAbility.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Character/Ragdoll/IMGRagdollTypes.h"
#include "Engine/TimerHandle.h"
#include "IMGGameplayAbility_Ragdoll.generated.h"

class UIMGRagdollComponent;
class UIMGCharacterMovementComponent;
class UIMGHealthComponent;
class UAbilityTask_PlayMontageAndWait;
class UAnimInstance;
class USkeletalMesh;
class UPhysicsAsset;

/** A client suggestion tied to the current GAS activation, impact and accepted movement sample. */
USTRUCT()
struct FIMGRagdollRecoveryTargetData : public FGameplayAbilityTargetData
{
	GENERATED_BODY()

	UPROPERTY()
	uint16 Session = 0;
	UPROPERTY()
	uint32 Attempt = 0;
	UPROPERTY()
	uint32 ImpactRevision = 0;
	UPROPERTY()
	uint32 BodyReferenceSequence = 0;
	UPROPERTY()
	double SampleServerTime = 0.0;
	UPROPERTY()
	float StableSeconds = 0.f;
	UPROPERTY()
	FVector_NetQuantize10 LinearVelocity = FVector::ZeroVector;
	UPROPERTY()
	FVector_NetQuantize10 AngularVelocityRadians = FVector::ZeroVector;
	UPROPERTY()
	FIMGRagdollRecoverySelection Selection;

	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
	virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }
};

template<> struct TStructOpsTypeTraits<FIMGRagdollRecoveryTargetData> : TStructOpsTypeTraitsBase2<FIMGRagdollRecoveryTargetData>
{
	enum { WithNetSerializer = true };
};

/** Server-initiated ragdoll action. Body mechanics, pose handoff and CMC movement remain in their existing owners. */
UCLASS(Blueprintable)
class IMAGINEGAME_API UIMGGameplayAbility_Ragdoll : public UIMGGameplayAbility
{
	GENERATED_BODY()

public:
	UIMGGameplayAbility_Ragdoll(const FObjectInitializer& ObjectInitializer);

protected:
	virtual bool ShouldAbilityRespondToEvent(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayEventData* Payload) const override;
	virtual bool CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	virtual void OnPawnAvatarSet() override;
	virtual void OnRemoveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override;

	UFUNCTION(BlueprintImplementableEvent, Category = "IMG|Ragdoll")
	void PreSelectRecoveryAnimation(const FIMGRagdollRecoveryContext& Context);
	/** Override with CHT/PoseMatch. Native fallback selects the first matching configured entry. */
	UFUNCTION(BlueprintNativeEvent, Category = "IMG|Ragdoll")
	FIMGRagdollRecoverySelection SelectRecoveryAnimation(const FIMGRagdollRecoveryContext& Context) const;
	virtual FIMGRagdollRecoverySelection SelectRecoveryAnimation_Implementation(const FIMGRagdollRecoveryContext& Context) const;

	UPROPERTY(EditDefaultsOnly, Category = "IMG|Ragdoll|Recovery", meta = (ClampMin = "0", ForceUnits = "s"))
	float MinimumRagdollTime = 0.5f;
	UPROPERTY(EditDefaultsOnly, Category = "IMG|Ragdoll|Recovery", meta = (ClampMin = "0.01", ForceUnits = "s"))
	float RequiredStableTime = 0.25f;
	UPROPERTY(EditDefaultsOnly, Category = "IMG|Ragdoll|Recovery", meta = (ClampMin = "0", ForceUnits = "cm/s"))
	float MaxStableLinearSpeed = 40.f;
	UPROPERTY(EditDefaultsOnly, Category = "IMG|Ragdoll|Recovery", meta = (ClampMin = "0"))
	float MaxStableAngularSpeedRadians = 1.f;
	UPROPERTY(EditDefaultsOnly, Category = "IMG|Ragdoll|Recovery", meta = (ClampMin = "0.01", ForceUnits = "s"))
	float CheckInterval = 0.05f;
	UPROPERTY(EditDefaultsOnly, Category = "IMG|Ragdoll|Recovery", meta = (ClampMin = "0.01", ForceUnits = "s"))
	float SelectionRetryInterval = 0.25f;
	UPROPERTY(EditDefaultsOnly, Category = "IMG|Ragdoll|Recovery", meta = (ClampMin = "0.1", ForceUnits = "s"))
	float MaxRagdollTime = 15.f;
	UPROPERTY(EditDefaultsOnly, Category = "IMG|Ragdoll|Recovery", meta = (ClampMin = "0.1", ForceUnits = "s"))
	float RecoveryStartTimeout = 2.f;
	/** Test fallback still uses authority placement and normal lifecycle completion. */
	UPROPERTY(EditDefaultsOnly, Category = "IMG|Ragdoll|Recovery")
	bool bAllowRecoveryWithoutMontage = true;
	UPROPERTY(EditDefaultsOnly, Category = "IMG|Ragdoll|Entry")
	FVector AdditionalVelocity = FVector::ZeroVector;

private:
	enum class EActionStage : uint8 { Entering, Settling, SelectingRecovery, Recovering, Ending };
	struct FActionState
	{
		TWeakObjectPtr<UIMGRagdollComponent> Body;
		TWeakObjectPtr<UIMGCharacterMovementComponent> Movement;
		TWeakObjectPtr<UIMGHealthComponent> Health;
		TWeakObjectPtr<UIMGAbilitySystemComponent> ASC;
		TWeakObjectPtr<AActor> Avatar;
		FGameplayAbilitySpecHandle Spec;
		FPredictionKey ActivationKey;
		EActionStage Stage = EActionStage::Entering;
		uint16 Session = 0;
		uint32 ImpactRevision = 0;
		uint32 NextAttempt = 0;
		uint32 LastRemoteAttempt = 0;
		uint32 PlayingAttempt = 0;
		uint64 CompletionFrame = MAX_uint64;
		double ActivatedTime = 0.0;
		double PreviousCheckTime = 0.0;
		double NextSelectionTime = 0.0;
		double RecoveryDeadline = 0.0;
		double StableStartTime = -1.0;
		bool bTransferred = false;
		bool bCompleted = false;
		bool bMontageFinished = false;
		bool bBodyFailedDuringEntry = false;

		void Reset() { *this = FActionState(); }
	};
	bool IsActionCurrent(uint16 ExpectedSession = 0) const;
	bool IsDeathHandoff() const;
	bool AreSettingsValid() const;
	void CheckRagdoll();
	void CheckSettling();
	void CheckRecovery();
	bool BuildRecoveryRequest(FIMGRagdollRecoveryTargetData& OutRequest, float StableSeconds);
	bool ResolveRecoveryRequest(const FIMGRagdollRecoveryTargetData& Request, bool bRemote);
	void StartRecoveryMontage();
	void AbortAction(const TCHAR* Reason);
	void OnRecoveryTargetData(const FGameplayAbilityTargetDataHandle& Data, FGameplayTag ApplicationTag);
	void OnRecoveryTargetDataCancelled();
	void OnImpact(FGameplayTag Tag, const FGameplayEventData* Payload);
	void RemoveActionBindings();
	UFUNCTION()
	void HandleBodyEnded(int32 Session, EIMGRagdollEndReason Reason);
	UFUNCTION()
	void HandleBodyUnavailable(int32 Session, EIMGRagdollBodyIssue Issue);
	UFUNCTION()
	void OnRecoveryCompleted();
	UFUNCTION()
	void OnRecoveryInterrupted();

	FActionState Action;
	FTimerHandle CheckTimer;
	FDelegateHandle TargetDataHandle;
	FDelegateHandle TargetCancelledHandle;
	FDelegateHandle ImpactHandle;
	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask = nullptr;
	bool bProcessingAction = false;
};
