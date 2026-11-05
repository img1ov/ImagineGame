#include "AbilitySystem/Attributes/IMGLocomotionSet.h"

#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGLocomotionSet)

namespace
{
	constexpr float DefaultWalkSpeed = 165.f;
	constexpr float DefaultRunSpeed = 380.f;
	constexpr float DefaultSprintSpeed = 585.f;
	constexpr float DefaultCrouchSpeed = 170.f;
}

UIMGLocomotionSet::UIMGLocomotionSet()
{
	InitWalkSpeed(DefaultWalkSpeed);
	InitRunSpeed(DefaultRunSpeed);
	InitSprintSpeed(DefaultSprintSpeed);
	InitCrouchSpeed(DefaultCrouchSpeed);
}

void UIMGLocomotionSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UIMGLocomotionSet, WalkSpeed, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UIMGLocomotionSet, RunSpeed, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UIMGLocomotionSet, SprintSpeed, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UIMGLocomotionSet, CrouchSpeed, COND_None, REPNOTIFY_Always);
}

void UIMGLocomotionSet::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const
{
	Super::PreAttributeBaseChange(Attribute, NewValue);
	ClampAttribute(Attribute, NewValue);
}

void UIMGLocomotionSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	ClampAttribute(Attribute, NewValue);
}

void UIMGLocomotionSet::ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const
{
	if (Attribute == GetWalkSpeedAttribute()
		|| Attribute == GetRunSpeedAttribute()
		|| Attribute == GetSprintSpeedAttribute()
		|| Attribute == GetCrouchSpeedAttribute())
	{
		NewValue = FMath::IsFinite(NewValue) ? FMath::Max(NewValue, 0.0f) : 0.f;
	}
}

void UIMGLocomotionSet::OnRep_WalkSpeed(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UIMGLocomotionSet, WalkSpeed, OldValue);
}

void UIMGLocomotionSet::OnRep_RunSpeed(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UIMGLocomotionSet, RunSpeed, OldValue);
}

void UIMGLocomotionSet::OnRep_SprintSpeed(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UIMGLocomotionSet, SprintSpeed, OldValue);
}

void UIMGLocomotionSet::OnRep_CrouchSpeed(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UIMGLocomotionSet, CrouchSpeed, OldValue);
}
