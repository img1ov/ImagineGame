#include "Character/IMGCharacterWithAbilities.h"

#include "AbilitySystem/Attributes/IMGCombatSet.h"
#include "AbilitySystem/Attributes/IMGHealthSet.h"
#include "AbilitySystem/IMGAbilitySystemComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGCharacterWithAbilities)

AIMGCharacterWithAbilities::AIMGCharacterWithAbilities(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	AbilitySystemComponent = ObjectInitializer.CreateDefaultSubobject<UIMGAbilitySystemComponent>(this, TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	// The ability system discovers these sets during initialization. Keep references so they remain alive until then.
	HealthSet = CreateDefaultSubobject<UIMGHealthSet>(TEXT("HealthSet"));
	CombatSet = CreateDefaultSubobject<UIMGCombatSet>(TEXT("CombatSet"));

	SetNetUpdateFrequency(100.0f);
}

void AIMGCharacterWithAbilities::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	check(AbilitySystemComponent);
	AbilitySystemComponent->InitAbilityActorInfo(this, this);
}

UAbilitySystemComponent* AIMGCharacterWithAbilities::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}
