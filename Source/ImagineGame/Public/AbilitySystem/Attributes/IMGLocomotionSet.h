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
	UE_API UIMGLocomotionSet();

	ATTRIBUTE_ACCESSORS(UIMGLocomotionSet, WalkSpeed);
	ATTRIBUTE_ACCESSORS(UIMGLocomotionSet, RunSpeed);
	ATTRIBUTE_ACCESSORS(UIMGLocomotionSet, SprintSpeed);
	ATTRIBUTE_ACCESSORS(UIMGLocomotionSet, CrouchSpeed);

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
};

#undef UE_API
