#pragma once

#include "Character/Traversal/Animation/IMGTraversalHandContact.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "Misc/Optional.h"

#include "IMGAnimNode_TraversalHandIK.generated.h"

class UPrimitiveComponent;
class UIMGTraversalComponent;

/** Finds surface contacts from the input pose and applies independent two-bone arm solves. */
USTRUCT(BlueprintInternalUseOnly)
struct IMAGINEGAME_API FIMGAnimNode_TraversalHandIK : public FAnimNode_SkeletalControlBase
{
	GENERATED_BODY()

public:
	FIMGAnimNode_TraversalHandIK();

	static bool IsSupportedHandAlphaInput(EAnimAlphaInputType InputType)
	{
		return InputType == EAnimAlphaInputType::Float || InputType == EAnimAlphaInputType::Curve;
	}

	UPROPERTY(EditAnywhere, Category = "Left Hand", meta = (NeverAsPin, DisplayName = "Left Hand Bone"))
	FBoneReference LeftHand;

	/** Optional socket attached directly to the left hand bone. */
	UPROPERTY(EditAnywhere, Category = "Left Hand", meta = (NeverAsPin))
	FName LeftPalmSocket = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Left Hand", meta = (NeverAsPin, DisplayName = "Left Hand Alpha Input Type", ValidEnumValues = "Float,Curve"))
	EAnimAlphaInputType LeftAlphaInputType = EAnimAlphaInputType::Float;

	/** Independent left-arm pose weight. May be bound directly to an animation curve. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Left Hand", meta = (PinShownByDefault, DisplayName = "Left Hand Alpha", ClampMin = "0", ClampMax = "1"))
	float LeftAlpha = 0.f;

	UPROPERTY(EditAnywhere, Category = "Left Hand", meta = (NeverAsPin, DisplayName = "Left Hand Alpha Scale Bias"))
	FInputScaleBias LeftAlphaScaleBias;

	/** Curve mode reads this curve from the current input pose. A missing curve disables this side. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Left Hand", meta = (PinShownByDefault, DisplayName = "Left Hand Alpha Curve Name"))
	FName LeftAlphaCurveName = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Left Hand", meta = (NeverAsPin, DisplayName = "Left Hand Alpha Scale Bias Clamp"))
	FInputScaleBiasClamp LeftAlphaScaleBiasClamp;

	UPROPERTY(EditAnywhere, Category = "Right Hand", meta = (NeverAsPin, DisplayName = "Right Hand Bone"))
	FBoneReference RightHand;

	/** Optional socket attached directly to the right hand bone. */
	UPROPERTY(EditAnywhere, Category = "Right Hand", meta = (NeverAsPin))
	FName RightPalmSocket = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Right Hand", meta = (NeverAsPin, DisplayName = "Right Hand Alpha Input Type", ValidEnumValues = "Float,Curve"))
	EAnimAlphaInputType RightAlphaInputType = EAnimAlphaInputType::Float;

	/** Independent right-arm pose weight. May be bound directly to an animation curve. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Right Hand", meta = (PinShownByDefault, DisplayName = "Right Hand Alpha", ClampMin = "0", ClampMax = "1"))
	float RightAlpha = 0.f;

	UPROPERTY(EditAnywhere, Category = "Right Hand", meta = (NeverAsPin, DisplayName = "Right Hand Alpha Scale Bias"))
	FInputScaleBias RightAlphaScaleBias;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Right Hand", meta = (PinShownByDefault, DisplayName = "Right Hand Alpha Curve Name"))
	FName RightAlphaCurveName = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Right Hand", meta = (NeverAsPin, DisplayName = "Right Hand Alpha Scale Bias Clamp"))
	FInputScaleBiasClamp RightAlphaScaleBiasClamp;

	/** Seconds to ease from the current pre-IK palm into a new contact. Zero attaches immediately. Shared settings, independent hand histories. */
	UPROPERTY(EditAnywhere, Category = "Contact", meta = (NeverAsPin, ClampMin = "0", Units = "s"))
	float AttachBlendTime = 0.1f;

	/** Seconds to fade the last arm correction back to the current animation after contact loss. Zero restores immediately. Alpha still applies once. */
	UPROPERTY(EditAnywhere, Category = "Contact", meta = (NeverAsPin, ClampMin = "0", Units = "s"))
	float DetachBlendTime = 0.1f;

	/** Exponential follow rate for changing targets in contact-surface local space (1/seconds). Higher is faster; zero follows immediately. */
	UPROPERTY(EditAnywhere, Category = "Contact", meta = (NeverAsPin, ClampMin = "0"))
	float TargetInterpSpeed = 20.f;

	/** Vertical sweep extends this far above and below the execution front-edge height, in cm. */
	UPROPERTY(EditAnywhere, Category = "Contact", meta = (NeverAsPin, ClampMin = "0.01", Units = "cm"))
	float VerticalTraceHalfHeight = 30.f;

	/** Maximum 3D distance from the pre-IK palm to an accepted surface target, in cm. */
	UPROPERTY(EditAnywhere, Category = "Contact", meta = (NeverAsPin, ClampMin = "0.01", Units = "cm"))
	float MaxHandCorrectionDistance = 25.f;

	/** Stop this contact if its cached target drifts farther from the pre-IK palm, in cm. Keep at least MaxHandCorrectionDistance. */
	UPROPERTY(EditAnywhere, Category = "Contact", meta = (NeverAsPin, ClampMin = "0.01", Units = "cm"))
	float ContactBreakDistance = 50.f;

	UPROPERTY(EditAnywhere, Category = "Contact", AdvancedDisplay, meta = (NeverAsPin, ClampMin = "0", Units = "cm"))
	float VerticalTraceRadius = 7.5f;

	/** Maximum depth past the front edge and length of the early front-refinement sweep, in cm. */
	UPROPERTY(EditAnywhere, Category = "Contact", AdvancedDisplay, meta = (NeverAsPin, ClampMin = "0.01", Units = "cm"))
	float FrontEdgeTraceDistance = 50.f;

	UPROPERTY(EditAnywhere, Category = "Contact", AdvancedDisplay, meta = (NeverAsPin, ClampMin = "0", Units = "cm"))
	float FrontEdgeTraceRadius = 5.f;

	/** Maximum shoulder-to-wrist reach as a fraction of the combined arm lengths. No bone stretching. */
	UPROPERTY(EditAnywhere, Category = "Solver", meta = (NeverAsPin, ClampMin = "0.01", ClampMax = "1"))
	float MaxArmReachRatio = 0.98f;

	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual bool HasPreUpdate() const override { return true; }
	virtual void PreUpdate(const UAnimInstance* InAnimInstance) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;

protected:
	virtual void UpdateInternal(const FAnimationUpdateContext& Context) override;
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	virtual void EvaluateSkeletalControl_AnyThread(
	    FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;

private:
	class FHandState
	{
	public:
		void ResetContact()
		{
			Contact.Reset();
			PreIKPalmWorld.Reset();
			PalmTargetWorld.Reset();
			ContactSurfaceToWorld = FTransform::Identity;
			ResetPositionSmoothing();
			ResetRecovery();
		}

		void ResetRecovery()
		{
			bHasIKCorrection = bHadContact = bDetaching = false;
			DetachElapsedTime = 0.f;
			LastDetachUpdateCounter = MAX_uint64;
			LastShoulderCorrection = LastElbowCorrection = LastWristCorrection = FQuat::Identity;
			DetachShoulderCorrection = DetachElbowCorrection = DetachWristCorrection = FQuat::Identity;
		}

		void ResetPositionSmoothing()
		{
			AttachStartLocal.Reset();
			SmoothedTargetLocal = FVector::ZeroVector;
			AttachElapsedTime = 0.f;
			LastPositionUpdateCounter = MAX_uint64;
		}

		FIMGTraversalHandContact Contact;
		TOptional<FVector> PalmOffsetLocal;
		TOptional<FVector> PreIKPalmWorld;
		TOptional<FVector> PalmTargetWorld;
		// Value snapshot from the actual contacted surface; evaluation never reads the component.
		FTransform ContactSurfaceToWorld = FTransform::Identity;
		TOptional<FVector> AttachStartLocal;
		FVector SmoothedTargetLocal = FVector::ZeroVector;
		float AttachElapsedTime = 0.f;
		uint64 LastPositionUpdateCounter = MAX_uint64;
		// Unweighted local rotation deltas preserve current animation and bone lengths during recovery.
		FQuat LastShoulderCorrection = FQuat::Identity;
		FQuat LastElbowCorrection = FQuat::Identity;
		FQuat LastWristCorrection = FQuat::Identity;
		FQuat DetachShoulderCorrection = FQuat::Identity;
		FQuat DetachElbowCorrection = FQuat::Identity;
		FQuat DetachWristCorrection = FQuat::Identity;
		float DetachElapsedTime = 0.f;
		uint64 LastDetachUpdateCounter = MAX_uint64;
		bool bHasIKCorrection = false;
		bool bHadContact = false;
		bool bDetaching = false;
		FName HandBone = NAME_None;
		FName PalmSocket = NAME_None;
		EAnimAlphaInputType AlphaInputType = EAnimAlphaInputType::Float;
		FName AlphaCurveName = NAME_None;
		// Repeated evaluations read the current curve but consume the update delta only once.
		uint64 LastAlphaUpdateCounter = MAX_uint64;
		float EffectiveAlpha = 0.f;
	};

	void Reset();
	float EvaluateHandAlpha(const FComponentSpacePoseContext& Output, FHandState& Hand, EAnimAlphaInputType InputType,
	    float InputAlpha, FName CurveName, const FInputScaleBias& ScaleBias, FInputScaleBiasClamp& ScaleBiasClamp);
	FVector InterpolatePalmTarget(FHandState& Hand, const FVector& PalmWorld);
	void EvaluateHandIK(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms,
	    FHandState& Hand, const FBoneReference& Bone, float HandAlpha);

	FHandState LeftHandState;
	FHandState RightHandState;
	// Only PreUpdate reads live objects. Evaluation and the next PreUpdate exchange value samples on this node.
	TWeakObjectPtr<const UIMGTraversalComponent> ContactSource;
	TWeakObjectPtr<const UAnimInstance> MontageAnimInstance;
	TWeakObjectPtr<UPrimitiveComponent> ReferenceCarrier;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> ReferenceSurfaces;
	FTransform ReferenceLocal = FTransform::Identity;
	FName ReferenceName = NAME_None;
	int32 MontageInstanceId = INDEX_NONE;
	double LastPreUpdateTime = -1.0;
	uint64 AnimationUpdateCounter = 0;
	float UpdateDeltaTime = 0.f;
};
