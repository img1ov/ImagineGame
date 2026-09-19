#pragma once

#include "AnimationModifier.h"

#include "IMGOneShotAnimationModifier.generated.h"

class UAnimSequence;

/**
 * Base class for destructive, one-shot animation modifiers.
 *
 * The modifier removes its authored instance after applying and clears only the
 * bookkeeping created by UAnimationModifier. Its animation changes are retained.
 */
UCLASS(Abstract, NotBlueprintable)
class UIMGOneShotAnimationModifier : public UAnimationModifier
{
	GENERATED_BODY()

public:
	virtual void PostInitProperties() override;
	const FGuid& GetInstanceId() const { return InstanceId; }

protected:
	/** Removes this one-shot modifier from the source asset after the current apply completes. */
	void RemoveAfterApply(UAnimSequence* Animation);

	/** One-shot modifiers intentionally keep their output when Unreal clears the applied record. */
	virtual void OnRevert_Implementation(UAnimSequence* Animation) final override;

private:
	/** Stable across Unreal's applied-instance copy, allowing it to find the source instance. */
	UPROPERTY()
	FGuid InstanceId;
};
