#pragma once

#include "Animation/AnimInstance.h"
#include "GameplayEffectTypes.h"

#include "IMGAnimInstance.generated.h"

class UAbilitySystemComponent;
struct FBlendedCurve;

/** Base animation instance that mirrors gameplay tags and exposes character ground distance. */
UCLASS(Config = Game)
class IMAGINEGAME_API UIMGAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UIMGAnimInstance(const FObjectInitializer& ObjectInitializer);

	virtual void InitializeWithAbilitySystem(UAbilitySystemComponent* ASC);

	/** AnimGraph evaluation only; BaseCurve and OutCurve must be separate containers. */
	void EvaluateSparseMontageCurves(FName SlotName, const FBlendedCurve& BaseCurve, FBlendedCurve& OutCurve) const;

protected:
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	// Derived proxy factories must return FIMGAnimInstanceProxy or a subclass.
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

	UPROPERTY(EditDefaultsOnly, Category = "GameplayTags")
	FGameplayTagBlueprintPropertyMap GameplayTagPropertyMap;

	UPROPERTY(BlueprintReadOnly, Category = "Character State Data")
	float GroundDistance = -1.0f;
};
