#pragma once

#include "Animation/AnimInstance.h"
#include "GameplayEffectTypes.h"

#include "IMGAnimInstance.generated.h"

class UAbilitySystemComponent;

/** Base animation instance that mirrors gameplay tags and exposes character ground distance. */
UCLASS(Config = Game)
class IMAGINEGAME_API UIMGAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UIMGAnimInstance(const FObjectInitializer& ObjectInitializer);

	virtual void InitializeWithAbilitySystem(UAbilitySystemComponent* ASC);

protected:
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	UPROPERTY(EditDefaultsOnly, Category = "GameplayTags")
	FGameplayTagBlueprintPropertyMap GameplayTagPropertyMap;

	UPROPERTY(BlueprintReadOnly, Category = "Character State Data")
	float GroundDistance = -1.0f;
};
