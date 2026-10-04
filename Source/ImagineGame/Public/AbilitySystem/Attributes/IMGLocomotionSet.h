#pragma once

#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/IMGAttributeSet.h"

#include "IMGLocomotionSet.generated.h"

#define UE_API IMAGINEGAME_API

/** Gameplay-controlled locomotion targets. Directional tuning belongs to the locomotion component. */
UCLASS(MinimalAPI, BlueprintType)
class UIMGLocomotionSet : public UIMGAttributeSet
{
	GENERATED_BODY()

public:
	ATTRIBUTE_ACCESSORS(UIMGLocomotionSet, WalkSpeed);
	ATTRIBUTE_ACCESSORS(UIMGLocomotionSet, RunSpeed);
	ATTRIBUTE_ACCESSORS(UIMGLocomotionSet, SprintSpeed);
	ATTRIBUTE_ACCESSORS(UIMGLocomotionSet, CrouchSpeed);
	ATTRIBUTE_ACCESSORS(UIMGLocomotionSet, MaxAcceleration);
	ATTRIBUTE_ACCESSORS(UIMGLocomotionSet, BrakingDecelerationWalking);
	ATTRIBUTE_ACCESSORS(UIMGLocomotionSet, GroundFriction);

	UE_API virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UE_API virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;
	UE_API virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;

protected:
	UFUNCTION()
	UE_API void OnRep_WalkSpeed(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	UE_API void OnRep_RunSpeed(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	UE_API void OnRep_SprintSpeed(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	UE_API void OnRep_CrouchSpeed(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	UE_API void OnRep_MaxAcceleration(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	UE_API void OnRep_BrakingDecelerationWalking(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	UE_API void OnRep_GroundFriction(const FGameplayAttributeData& OldValue);

private:
	void ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_WalkSpeed, Category = "IMG|Locomotion", meta = (AllowPrivateAccess = true))
	FGameplayAttributeData WalkSpeed;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_RunSpeed, Category = "IMG|Locomotion", meta = (AllowPrivateAccess = true))
	FGameplayAttributeData RunSpeed;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_SprintSpeed, Category = "IMG|Locomotion", meta = (AllowPrivateAccess = true))
	FGameplayAttributeData SprintSpeed;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_CrouchSpeed, Category = "IMG|Locomotion", meta = (AllowPrivateAccess = true))
	FGameplayAttributeData CrouchSpeed;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxAcceleration, Category = "IMG|Locomotion", meta = (AllowPrivateAccess = true))
	FGameplayAttributeData MaxAcceleration;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_BrakingDecelerationWalking, Category = "IMG|Locomotion", meta = (AllowPrivateAccess = true))
	FGameplayAttributeData BrakingDecelerationWalking;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_GroundFriction, Category = "IMG|Locomotion", meta = (AllowPrivateAccess = true))
	FGameplayAttributeData GroundFriction;
};

#undef UE_API
