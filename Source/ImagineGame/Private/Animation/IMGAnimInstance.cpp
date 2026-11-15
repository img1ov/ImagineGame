#include "Animation/IMGAnimInstance.h"

#include "AbilitySystemGlobals.h"
#include "Animation/IMGAnimInstanceProxy.h"
#include "Character/IMGCharacter.h"
#include "Character/IMGCharacterMovementComponent.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGAnimInstance)

UIMGAnimInstance::UIMGAnimInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UIMGAnimInstance::InitializeWithAbilitySystem(UAbilitySystemComponent* ASC)
{
	check(ASC);
	GameplayTagPropertyMap.Initialize(this, ASC);
}

FAnimInstanceProxy* UIMGAnimInstance::CreateAnimInstanceProxy()
{
	return new FIMGAnimInstanceProxy(this);
}

void UIMGAnimInstance::EvaluateSparseMontageCurves(FName SlotName, const FBlendedCurve& BaseCurve, FBlendedCurve& OutCurve) const
{
	GetProxyOnAnyThread<FIMGAnimInstanceProxy>().EvaluateSparseMontageCurves(SlotName, BaseCurve, OutCurve);
}

#if WITH_EDITOR
EDataValidationResult UIMGAnimInstance::IsDataValid(FDataValidationContext& Context) const
{
	Super::IsDataValid(Context);
	GameplayTagPropertyMap.IsDataValid(this, Context);

	return Context.GetNumErrors() > 0 ? EDataValidationResult::Invalid : EDataValidationResult::Valid;
}
#endif

void UIMGAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	if (AActor* OwningActor = GetOwningActor())
	{
		if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OwningActor))
		{
			InitializeWithAbilitySystem(ASC);
		}
	}
}

void UIMGAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	const AIMGCharacter* Character = Cast<AIMGCharacter>(GetOwningActor());
	if (!Character)
	{
		return;
	}

	UIMGCharacterMovementComponent* CharacterMovement = CastChecked<UIMGCharacterMovementComponent>(Character->GetCharacterMovement());
	GroundDistance = CharacterMovement->GetGroundInfo().GroundDistance;
}
