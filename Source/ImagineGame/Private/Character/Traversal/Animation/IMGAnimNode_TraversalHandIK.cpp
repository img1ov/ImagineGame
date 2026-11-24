#include "Character/Traversal/Animation/IMGAnimNode_TraversalHandIK.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "AbilitySystem/IMGAbilitySystemComponent.h"
#include "Character/IMGPawnExtensionComponent.h"
#include "Animation/AnimInstanceProxy.h"
#include "Character/IMGCharacter.h"
#include "Character/Traversal/IMGTraversalComponent.h"
#include "CollisionQueryParams.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Character/Traversal/IMGTraversalDebug.h"
#include "TwoBoneIK.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGAnimNode_TraversalHandIK)

namespace
{
constexpr double TraversalHandSampleMaxAge = 0.15;

bool IsSupportedHandAlphaInput(EAnimAlphaInputType InputType)
{
	return InputType == EAnimAlphaInputType::Float || InputType == EAnimAlphaInputType::Curve;
}

bool IsAlphaModifierValid(const FInputScaleBiasClamp& Modifier)
{
	return FMath::IsFinite(Modifier.Scale) && FMath::IsFinite(Modifier.Bias) &&
	    (!Modifier.bMapRange || (FMath::IsFinite(Modifier.InRange.Min) && FMath::IsFinite(Modifier.InRange.Max) &&
	        FMath::IsFinite(Modifier.OutRange.Min) && FMath::IsFinite(Modifier.OutRange.Max))) &&
	    (!Modifier.bClampResult || (FMath::IsFinite(Modifier.ClampMin) && FMath::IsFinite(Modifier.ClampMax) &&
	        Modifier.ClampMin <= Modifier.ClampMax)) &&
	    (!Modifier.bInterpResult || (FMath::IsFinite(Modifier.InterpSpeedIncreasing) && Modifier.InterpSpeedIncreasing >= 0.f &&
	        FMath::IsFinite(Modifier.InterpSpeedDecreasing) && Modifier.InterpSpeedDecreasing >= 0.f));
}

const FIMGTraversalWarpTarget* ResolveContactReference(const UIMGTraversalComponent& Traversal,
    const UAnimInstance* AnimInstance, FName TargetName, int32& OutMontageInstanceId)
{
	OutMontageInstanceId = INDEX_NONE;
	if (!Traversal.IsTraversalReady()) { return nullptr; }
	const FIMGTraversalWarpTarget* Target = Traversal.FindWarpTarget(EIMGTraversalAnchor::FrontLedge, TargetName);
	const FIMGTraversalRepState& State = Traversal.GetActiveTraversalState();
	const AActor* Owner = Traversal.GetOwner();
	const UWorld* World = Traversal.GetWorld();
	const UPrimitiveComponent* Surface = Target ? Target->Component.Get() : nullptr;
	if (!Target || !IsValid(AnimInstance) || !IsValid(State.Montage) || !IsValid(Owner) ||
	    Owner->IsActorBeingDestroyed() || !World || World->GetNetMode() == NM_DedicatedServer ||
	    !IsValid(Surface) || !Surface->IsRegistered() || Surface->GetWorld() != World ||
	    !Target->LocalTransform.IsValid() || !Surface->GetComponentTransform().IsValid())
	{
		return nullptr;
	}
	if (Owner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		const UIMGPawnExtensionComponent* Extension = UIMGPawnExtensionComponent::FindPawnExtensionComponent(Owner);
		const UIMGAbilitySystemComponent* ASC = Extension ? Extension->GetIMGAbilitySystemComponent() : nullptr;
		if (!ASC || ASC->GetAvatarActor() != Owner ||
		    !ASC->MatchesReplicatedMontageInstance(State.Montage, State.MontagePlayInstanceId)) { return nullptr; }
	}
	const FAnimMontageInstance* Instance = AnimInstance->GetActiveInstanceForMontage(State.Montage);
	if (!Instance) { Instance = AnimInstance->GetInstanceForMontage(State.Montage); }
	if (!Instance) { return nullptr; }
	OutMontageInstanceId = Instance->GetInstanceID();
	return Target;
}

TOptional<FVector> ResolvePalmOffsetLocal(const USkeletalMeshComponent& Mesh, const FBoneReference& Hand, FName Socket)
{
	const int32 HandBoneIndex = Mesh.GetBoneIndex(Hand.BoneName);
	if (HandBoneIndex == INDEX_NONE)
	{
		return {};
	}
	if (Socket.IsNone())
	{
		return FVector::ZeroVector;
	}
	FTransform Offset;
	int32 SocketBoneIndex = INDEX_NONE;
	if (!Mesh.GetSocketInfoByName(Socket, Offset, SocketBoneIndex) || SocketBoneIndex != HandBoneIndex ||
	    !Offset.IsValid())
	{
		return {};
	}
	return Offset.GetLocation();
}
}

FIMGAnimNode_TraversalHandIK::FIMGAnimNode_TraversalHandIK()
{
	LeftHand.BoneName = TEXT("hand_l");
	RightHand.BoneName = TEXT("hand_r");
}

void FIMGAnimNode_TraversalHandIK::Reset()
{
	LeftHandState = FHandState();
	RightHandState = FHandState();
	LeftAlphaScaleBiasClamp.Reinitialize();
	RightAlphaScaleBiasClamp.Reinitialize();
	ContactSource.Reset();
	MontageAnimInstance.Reset();
	MontageInstanceId = INDEX_NONE;
	ReferenceCarrier.Reset();
	ReferenceSurfaces.Reset();
	ReferenceName = NAME_None;
	ReferenceLocal = FTransform::Identity;
	LastPreUpdateTime = -1.0;
}

void FIMGAnimNode_TraversalHandIK::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_SkeletalControlBase::Initialize_AnyThread(Context);
	Reset();
	AlphaUpdateCounter = 0;
	UpdateDeltaTime = 0.f;
}

void FIMGAnimNode_TraversalHandIK::UpdateInternal(const FAnimationUpdateContext& Context)
{
	FAnimNode_SkeletalControlBase::UpdateInternal(Context);
	++AlphaUpdateCounter;
	UpdateDeltaTime = FMath::IsFinite(Context.GetDeltaTime()) ? FMath::Max(0.f, Context.GetDeltaTime()) : 0.f;
}

void FIMGAnimNode_TraversalHandIK::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	InitializeAndValidateBoneRef(LeftHand, RequiredBones);
	InitializeAndValidateBoneRef(RightHand, RequiredBones);
	LeftHandState.ResetContact();
	RightHandState.ResetContact();
	LeftHandState.LastAlphaUpdateCounter = RightHandState.LastAlphaUpdateCounter = MAX_uint64;
	LeftAlphaScaleBiasClamp.Reinitialize();
	RightAlphaScaleBiasClamp.Reinitialize();
}

bool FIMGAnimNode_TraversalHandIK::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	return (IsSupportedHandAlphaInput(LeftAlphaInputType) && LeftHand.IsValidToEvaluate(RequiredBones)) ||
	    (IsSupportedHandAlphaInput(RightAlphaInputType) && RightHand.IsValidToEvaluate(RequiredBones));
}

void FIMGAnimNode_TraversalHandIK::PreUpdate(const UAnimInstance* InAnimInstance)
{
	if (!IsSupportedHandAlphaInput(LeftAlphaInputType) && !IsSupportedHandAlphaInput(RightAlphaInputType))
	{
		Reset();
		return;
	}
	const USkeletalMeshComponent* Mesh = InAnimInstance->GetSkelMeshComponent();
	const AIMGCharacter* Character = Mesh ? Cast<AIMGCharacter>(Mesh->GetOwner()) : nullptr;
	const UWorld* World = Character ? Character->GetWorld() : nullptr;
	if (!World || World->GetNetMode() == NM_DedicatedServer || Mesh != Character->GetMesh() ||
	    (GetLODThreshold() != INDEX_NONE && Mesh->GetPredictedLODLevel() > GetLODThreshold()))
	{
		Reset();
		return;
	}

	const UIMGTraversalComponent* Traversal = UIMGTraversalComponent::Get(Character);
	const UAnimInstance* MainAnimInstance = Mesh->GetAnimInstance();
	int32 NextMontageId = INDEX_NONE;
	const FIMGTraversalWarpTarget* Reference = Traversal
	    ? ResolveContactReference(*Traversal, MainAnimInstance, ContactTargetName, NextMontageId) : nullptr;
	if (!Reference)
	{
		Reset();
		return;
	}
	const FTransform FrontEdge = Reference->LocalTransform * Reference->Component->GetComponentTransform();
	if (!FrontEdge.IsValid())
	{
		Reset();
		return;
	}

	const TArray<TObjectPtr<UPrimitiveComponent>>& Surfaces = Traversal->GetActiveTraversalState().ObstacleComponents;
	const bool bSurfacesChanged = ReferenceSurfaces.Num() != Surfaces.Num() ||
	    Surfaces.ContainsByPredicate([this](const TObjectPtr<UPrimitiveComponent>& Surface) {
		    return !ReferenceSurfaces.Contains(TWeakObjectPtr<UPrimitiveComponent>(Surface.Get()));
	    });
	const double Now = World->GetTimeSeconds();
	const bool bSampleExpired = LastPreUpdateTime >= 0.0 &&
	    (Now < LastPreUpdateTime || Now - LastPreUpdateTime > TraversalHandSampleMaxAge);
	if (ContactSource.Get() != Traversal || MontageAnimInstance.Get() != MainAnimInstance ||
	    MontageInstanceId != NextMontageId || bSurfacesChanged || bSampleExpired ||
	    ReferenceCarrier.Get() != Reference->Component.Get() || ReferenceName != Reference->Name ||
	    !ReferenceLocal.Equals(Reference->LocalTransform))
	{
		// Neither a probe sample nor its result may cross a montage or a contact reference change.
		Reset();
		ContactSource = Traversal;
		MontageAnimInstance = MainAnimInstance;
		MontageInstanceId = NextMontageId;
		ReferenceCarrier = Reference->Component.Get();
		ReferenceName = Reference->Name;
		ReferenceLocal = Reference->LocalTransform;
		ReferenceSurfaces.Reserve(Surfaces.Num());
		for (const TObjectPtr<UPrimitiveComponent>& Surface : Surfaces)
		{
			ReferenceSurfaces.Emplace(Surface.Get());
		}
	}
	LastPreUpdateTime = Now;

	const FIMGTraversalDetectorSettings& DetectorSettings = Traversal->GetDetectorSettings();
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(IMGTraversalHandContact), false, Character);
	const bool bRefineFrontEdge = Character->IsLocallyControlled() && Character->IsPlayerControlled();
	auto UpdateHandContact = [&](FHandState& Hand, const FBoneReference& Bone, EAnimAlphaInputType InputType,
	                             FName Socket, FInputScaleBiasClamp& ScaleBiasClamp,
	                             [[maybe_unused]] const FColor& Color) {
		const TOptional<FVector> Offset = IsSupportedHandAlphaInput(InputType)
		    ? ResolvePalmOffsetLocal(*Mesh, Bone, Socket) : TOptional<FVector>();
		if (Hand.HandBone != Bone.BoneName || Hand.PalmSocket != Socket || Hand.PalmOffsetLocal != Offset)
		{
			Hand = FHandState();
			ScaleBiasClamp.Reinitialize();
			Hand.PalmOffsetLocal = Offset;
			Hand.HandBone = Bone.BoneName;
			Hand.PalmSocket = Socket;
		}
		Hand.PalmTargetWorld.Reset();
		if (!Hand.PreIKPalmWorld)
		{
			Hand.Contact.Reset();
			ScaleBiasClamp.Reinitialize();
			return;
		}
		const FVector Sample = Hand.PreIKPalmWorld.GetValue();
		Hand.PreIKPalmWorld.Reset();
		FVector Target;
		if (Hand.Contact.UpdateContact(*World, Params, DetectorSettings.TraceChannel, Surfaces, FrontEdge,
		        Sample, DetectorSettings.HandholdMinNormalZ, VerticalTraceHalfHeight, VerticalTraceRadius,
		        FrontEdgeTraceDistance, FrontEdgeTraceRadius, MaxHandCorrectionDistance, ContactBreakDistance,
		        bRefineFrontEdge, Target))
		{
			Hand.PalmTargetWorld = Target;
#if IMG_TRAVERSAL_DEBUG_ENABLED
			IMGTraversalDebug::HandContact(Character, Sample, Target, Color);
#endif
		}
	};
	UpdateHandContact(LeftHandState, LeftHand, LeftAlphaInputType, LeftPalmSocket, LeftAlphaScaleBiasClamp, FColor::Green);
	UpdateHandContact(RightHandState, RightHand, RightAlphaInputType, RightPalmSocket, RightAlphaScaleBiasClamp, FColor::Magenta);
}

float FIMGAnimNode_TraversalHandIK::EvaluateHandAlpha(const FComponentSpacePoseContext& Output, FHandState& Hand,
    EAnimAlphaInputType InputType, float InputAlpha, FName CurveName, const FInputScaleBias& ScaleBias,
    FInputScaleBiasClamp& ScaleBiasClamp)
{
	const FName ActiveCurveName = InputType == EAnimAlphaInputType::Curve ? CurveName : NAME_None;
	if (Hand.AlphaInputType != InputType || Hand.AlphaCurveName != ActiveCurveName)
	{
		// Exposed inputs are updated after PreUpdate. Never reuse a probe across an alpha-source change.
		Hand.ResetContact();
		Hand.AlphaInputType = InputType;
		Hand.AlphaCurveName = ActiveCurveName;
		Hand.LastAlphaUpdateCounter = MAX_uint64;
		ScaleBiasClamp.Reinitialize();
	}
	const bool bFirstEvaluationSinceUpdate = Hand.LastAlphaUpdateCounter != AlphaUpdateCounter;
	Hand.LastAlphaUpdateCounter = AlphaUpdateCounter;
	Hand.EffectiveAlpha = 0.f;
	bool bHasInput = InputType == EAnimAlphaInputType::Float;
	const float Value = InputType == EAnimAlphaInputType::Curve
	    ? Output.Curve.Get(ActiveCurveName, bHasInput) : InputAlpha;
	if (!bHasInput || !FMath::IsFinite(Value) || !IsAlphaModifierValid(ScaleBiasClamp) ||
	    (InputType == EAnimAlphaInputType::Float && (!FMath::IsFinite(ScaleBias.Scale) || !FMath::IsFinite(ScaleBias.Bias))))
	{
		ScaleBiasClamp.Reinitialize();
		return 0.f;
	}
	float Weight = ScaleBiasClamp.ApplyTo(Value, bFirstEvaluationSinceUpdate ? UpdateDeltaTime : 0.f);
	if (FMath::IsFinite(Weight) && InputType == EAnimAlphaInputType::Float)
	{
		Weight = Weight * ScaleBias.Scale + ScaleBias.Bias;
	}
	if (FMath::IsFinite(Weight))
	{
		Hand.EffectiveAlpha = FMath::Clamp(Weight, 0.f, 1.f);
	}
	else
	{
		ScaleBiasClamp.Reinitialize();
	}
	return Hand.EffectiveAlpha;
}

void FIMGAnimNode_TraversalHandIK::EvaluateHandIK(FComponentSpacePoseContext& Output,
    TArray<FBoneTransform>& OutBoneTransforms, FHandState& Hand, const FBoneReference& Bone, float HandAlpha)
{
	Hand.PreIKPalmWorld.Reset();
	if (!Hand.PalmOffsetLocal || !FAnimWeight::IsRelevant(HandAlpha) ||
	    !FMath::IsFinite(MaxHandCorrectionDistance) || MaxHandCorrectionDistance <= 0.f ||
	    !FMath::IsFinite(MaxArmReachRatio) || MaxArmReachRatio <= 0.f || MaxArmReachRatio > 1.f)
	{
		return;
	}
	const FBoneContainer& Bones = Output.Pose.GetPose().GetBoneContainer();
	if (!Bone.IsValidToEvaluate(Bones))
	{
		return;
	}
	const FCompactPoseBoneIndex WristIndex = Bone.GetCompactPoseIndex(Bones);
	if (!WristIndex.IsValid())
	{
		return;
	}
	const FCompactPoseBoneIndex ElbowIndex = Bones.GetParentBoneIndex(WristIndex);
	const FCompactPoseBoneIndex ShoulderIndex = ElbowIndex.IsValid() ? Bones.GetParentBoneIndex(ElbowIndex)
	                                                               : FCompactPoseBoneIndex(INDEX_NONE);
	if (!ShoulderIndex.IsValid())
	{
		return;
	}
	const FTransform& ComponentToWorld = Output.AnimInstanceProxy->GetComponentTransform();
	FTransform Wrist = Output.Pose.GetComponentSpaceTransform(WristIndex);
	if (!ComponentToWorld.IsValid() || !Wrist.IsValid())
	{
		return;
	}
	const FVector Palm = Wrist.TransformPosition(Hand.PalmOffsetLocal.GetValue());
	const FVector Sample = ComponentToWorld.TransformPosition(Palm);
	if (Sample.ContainsNaN())
	{
		return;
	}
	Hand.PreIKPalmWorld = Sample;
	if (!Hand.PalmTargetWorld ||
	    FVector::DistSquared(Sample, Hand.PalmTargetWorld.GetValue()) >= FMath::Square(MaxHandCorrectionDistance) ||
	    OutBoneTransforms.ContainsByPredicate([&](const FBoneTransform& Transform) {
		    return Transform.BoneIndex == WristIndex || Transform.BoneIndex == ElbowIndex ||
		        Transform.BoneIndex == ShoulderIndex;
	    }))
	{
		return;
	}

	FTransform Shoulder = Output.Pose.GetComponentSpaceTransform(ShoulderIndex);
	FTransform Elbow = Output.Pose.GetComponentSpaceTransform(ElbowIndex);
	const FVector Root = Shoulder.GetLocation();
	const FVector Joint = Elbow.GetLocation();
	const FVector End = Wrist.GetLocation();
	const double UpperLength = FVector::Distance(Root, Joint);
	const double LowerLength = FVector::Distance(Joint, End);
	if (!Shoulder.IsValid() || !Elbow.IsValid() || UpperLength <= UE_SMALL_NUMBER || LowerLength <= UE_SMALL_NUMBER)
	{
		return;
	}
	const FVector PalmTarget = ComponentToWorld.InverseTransformPosition(Hand.PalmTargetWorld.GetValue());
	const FVector WristTarget = End + (PalmTarget - Palm);
	const FVector TargetOffset = WristTarget - Root;
	if (TargetOffset.ContainsNaN() || TargetOffset.IsNearlyZero())
	{
		return;
	}

	const FVector GoalDirection = TargetOffset.GetSafeNormal();
	const FVector RefDirection = (End - Root).GetSafeNormal();
	FVector Bend = FVector::VectorPlaneProject(Joint - Root, RefDirection).GetSafeNormal();
	if (!RefDirection.IsNearlyZero() && !Bend.IsNearlyZero())
	{
		Bend = FQuat::FindBetweenNormals(RefDirection, GoalDirection).RotateVector(Bend);
	}
	else
	{
		Bend = FVector::VectorPlaneProject(Shoulder.GetUnitAxis(EAxis::Y), GoalDirection).GetSafeNormal();
		if (Bend.IsNearlyZero())
		{
			FVector UnusedAxis;
			GoalDirection.FindBestAxisVectors(Bend, UnusedAxis);
		}
	}
	const FVector Pole = Root + Bend * (UpperLength + LowerLength);
	const FVector Goal = Root + TargetOffset.GetClampedToMaxSize((UpperLength + LowerLength) * MaxArmReachRatio);
	AnimationCore::SolveTwoBoneIK(Shoulder, Elbow, Wrist, Pole, Goal, UpperLength, LowerLength, false, 1.0, 1.0);
	if (HandAlpha < 1.f && Shoulder.IsValid() && Elbow.IsValid() && Wrist.IsValid())
	{
		// Blend the solved chain in local space, as SkeletalControlBase does, to preserve arm lengths.
		const FTransform& InputShoulder = Output.Pose.GetComponentSpaceTransform(ShoulderIndex);
		const FTransform& InputElbow = Output.Pose.GetComponentSpaceTransform(ElbowIndex);
		const FTransform& InputWrist = Output.Pose.GetComponentSpaceTransform(WristIndex);
		const FCompactPoseBoneIndex ParentIndex = Bones.GetParentBoneIndex(ShoulderIndex);
		const FTransform Parent = ParentIndex.IsValid()
		    ? Output.Pose.GetComponentSpaceTransform(ParentIndex) : FTransform::Identity;
		FTransform BlendedShoulder;
		FTransform BlendedElbow;
		FTransform BlendedWrist;
		BlendedShoulder.Blend(InputShoulder.GetRelativeTransform(Parent), Shoulder.GetRelativeTransform(Parent), HandAlpha);
		BlendedElbow.Blend(InputElbow.GetRelativeTransform(InputShoulder), Elbow.GetRelativeTransform(Shoulder), HandAlpha);
		BlendedWrist.Blend(InputWrist.GetRelativeTransform(InputElbow), Wrist.GetRelativeTransform(Elbow), HandAlpha);
		Shoulder = BlendedShoulder * Parent;
		Elbow = BlendedElbow * Shoulder;
		Wrist = BlendedWrist * Elbow;
	}
	if (Shoulder.IsValid() && Elbow.IsValid() && Wrist.IsValid())
	{
		OutBoneTransforms.Emplace(ShoulderIndex, Shoulder);
		OutBoneTransforms.Emplace(ElbowIndex, Elbow);
		OutBoneTransforms.Emplace(WristIndex, Wrist);
	}
}

void FIMGAnimNode_TraversalHandIK::EvaluateSkeletalControl_AnyThread(
    FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
	// Both arms sample the same input pose; the base class applies transforms after this call.
	const float LeftWeight = EvaluateHandAlpha(Output, LeftHandState, LeftAlphaInputType, LeftAlpha, LeftAlphaCurveName,
	    LeftAlphaScaleBias, LeftAlphaScaleBiasClamp);
	const float RightWeight = EvaluateHandAlpha(Output, RightHandState, RightAlphaInputType, RightAlpha, RightAlphaCurveName,
	    RightAlphaScaleBias, RightAlphaScaleBiasClamp);
	EvaluateHandIK(Output, OutBoneTransforms, LeftHandState, LeftHand, LeftWeight);
	EvaluateHandIK(Output, OutBoneTransforms, RightHandState, RightHand, RightWeight);
	OutBoneTransforms.Sort(FCompareBoneTransformIndex());
}

void FIMGAnimNode_TraversalHandIK::GatherDebugData(FNodeDebugData& DebugData)
{
	FString Label = DebugData.GetNodeName(this);
	AddDebugNodeData(Label);
	Label += FString::Printf(TEXT(" Left: %.1f%% Right: %.1f%%"), LeftHandState.EffectiveAlpha * 100.f, RightHandState.EffectiveAlpha * 100.f);
	DebugData.AddDebugItem(Label);
	ComponentPose.GatherDebugData(DebugData);
}
