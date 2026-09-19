#pragma once

#include "AnimationModifiers/IMGOneShotAnimationModifier.h"

#include "IMGAnimationModifier_RemoveAnimationNotifiers.generated.h"

class UAnimNotify;
class UAnimNotifyState;

/** Removes selected animation notifies and notify states, then deletes tracks with no remaining notifiers. */
UCLASS(DisplayName = "AnimationModifier_RemoveAnimationNotifiers")
class UIMGAnimationModifier_RemoveAnimationNotifiers : public UIMGOneShotAnimationModifier
{
	GENERATED_BODY()

public:
	virtual void OnApply_Implementation(UAnimSequence* Animation) override;

private:
	/** Remove every animation notify and notify state, ignoring the filters below. */
	UPROPERTY(EditAnywhere, Category = "Notifiers", meta = (DisplayName = "Remove All Notifiers"))
	bool bRemoveAllNotifiers = true;

	/** Removes animation notifies matching any selected class, including derived classes. */
	UPROPERTY(EditAnywhere, Category = "Notifiers", meta = (EditCondition = "!bRemoveAllNotifiers", EditConditionHides))
	TArray<TSubclassOf<UAnimNotify>> AnimationNotifyClasses;

	/** Removes animation notify states matching any selected class, including derived classes. */
	UPROPERTY(EditAnywhere, Category = "Notifiers", meta = (EditCondition = "!bRemoveAllNotifiers", EditConditionHides))
	TArray<TSubclassOf<UAnimNotifyState>> AnimationNotifyStateClasses;

	/** Removes AN or ANS events whose displayed notify name matches, ignoring letter case. */
	UPROPERTY(EditAnywhere, Category = "Notifiers", meta = (EditCondition = "!bRemoveAllNotifiers", EditConditionHides))
	TArray<FName> NotifierNames;
};
