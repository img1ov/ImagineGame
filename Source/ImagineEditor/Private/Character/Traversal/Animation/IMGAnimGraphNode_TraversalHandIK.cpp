#include "Character/Traversal/Animation/IMGAnimGraphNode_TraversalHandIK.h"

#include "DetailLayoutBuilder.h"
#include "EdGraph/EdGraphPin.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGAnimGraphNode_TraversalHandIK)

#define LOCTEXT_NAMESPACE "IMGAnimGraphNode_TraversalHandIK"

FText UIMGAnimGraphNode_TraversalHandIK::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return GetControllerDescription();
}

FText UIMGAnimGraphNode_TraversalHandIK::GetControllerDescription() const
{
	return LOCTEXT("Title", "Traversal Hand IK");
}

FText UIMGAnimGraphNode_TraversalHandIK::GetTooltipText() const
{
	return LOCTEXT("Tooltip",
	    "Place after foot placement and before any hand IK. Each hand has its own Float or Curve alpha that blends the solved arm chain. "
	    "The game thread probes the previous pre-IK pose sample; this node applies the result to its current input pose.");
}

FString UIMGAnimGraphNode_TraversalHandIK::GetNodeCategory() const
{
	return TEXT("Animation|Traversal");
}

void UIMGAnimGraphNode_TraversalHandIK::ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton,
    FCompilerResultsLog& MessageLog)
{
	Super::ValidateAnimNodeDuringCompilation(ForSkeleton, MessageLog);

	const auto ValidateHandAlpha = [this, &MessageLog](EAnimAlphaInputType InputType, FName CurveProperty,
	                                                 FName CurveName, const TCHAR* Side) {
		if (InputType != EAnimAlphaInputType::Float && InputType != EAnimAlphaInputType::Curve)
		{
			MessageLog.Error(*FString::Printf(TEXT("@@ supports only Float or Curve alpha for the %s hand."), Side), this);
			return false;
		}
		if (InputType == EAnimAlphaInputType::Curve)
		{
			const UEdGraphPin* CurvePin = FindPin(CurveProperty);
			const bool bDynamicCurveName = HasBinding(CurveProperty) || (CurvePin && !CurvePin->LinkedTo.IsEmpty());
			const FName DefaultCurveName = CurvePin ? FName(*CurvePin->GetDefaultAsString()) : CurveName;
			if (!bDynamicCurveName && DefaultCurveName.IsNone())
			{
				MessageLog.Warning(*FString::Printf(TEXT("@@ has no alpha curve name for the %s hand."), Side), this);
			}
		}
		return true;
	};
	const bool bLeftInputSupported = ValidateHandAlpha(Node.LeftAlphaInputType,
	    GET_MEMBER_NAME_CHECKED(FIMGAnimNode_TraversalHandIK, LeftAlphaCurveName), Node.LeftAlphaCurveName, TEXT("left"));
	const bool bRightInputSupported = ValidateHandAlpha(Node.RightAlphaInputType,
	    GET_MEMBER_NAME_CHECKED(FIMGAnimNode_TraversalHandIK, RightAlphaCurveName), Node.RightAlphaCurveName, TEXT("right"));
	if (bLeftInputSupported && bRightInputSupported && !Node.LeftHand.BoneName.IsNone() &&
	    Node.LeftHand.BoneName == Node.RightHand.BoneName)
	{
		MessageLog.Error(TEXT("@@ requires different wrist bones for the left and right hands."), this);
	}

	const auto ValidateDistance = [this, &MessageLog](const TCHAR* Name, float Value, bool bAllowZero = false) {
		if (!FMath::IsFinite(Value) || (bAllowZero ? Value < 0.f : Value <= 0.f))
		{
			MessageLog.Error(*FString::Printf(TEXT("@@ %s must be finite and %s."), Name,
			    bAllowZero ? TEXT("non-negative") : TEXT("positive")), this);
		}
	};
	ValidateDistance(TEXT("Vertical Trace Half Height"), Node.VerticalTraceHalfHeight);
	ValidateDistance(TEXT("Vertical Trace Radius"), Node.VerticalTraceRadius, true);
	ValidateDistance(TEXT("Front Edge Trace Distance"), Node.FrontEdgeTraceDistance);
	ValidateDistance(TEXT("Front Edge Trace Radius"), Node.FrontEdgeTraceRadius, true);
	ValidateDistance(TEXT("Max Hand Correction Distance"), Node.MaxHandCorrectionDistance);
	ValidateDistance(TEXT("Contact Break Distance"), Node.ContactBreakDistance);
	if (Node.ContactBreakDistance < Node.MaxHandCorrectionDistance)
	{
		MessageLog.Warning(TEXT("@@ Contact Break Distance is smaller than Max Hand Correction Distance; a newly acquired contact may break on its next update."), this);
	}
	if (!FMath::IsFinite(Node.MaxArmReachRatio) || Node.MaxArmReachRatio <= 0.f || Node.MaxArmReachRatio > 1.f)
	{
		MessageLog.Error(TEXT("@@ Max Arm Reach Ratio must be finite, greater than zero, and at most one."), this);
	}
}

void UIMGAnimGraphNode_TraversalHandIK::CustomizePinData(UEdGraphPin* Pin, FName SourcePropertyName,
    int32 ArrayIndex) const
{
	Super::CustomizePinData(Pin, SourcePropertyName, ArrayIndex);

	const auto CustomizeHandPin = [Pin](EAnimAlphaInputType InputType, FName AlphaProperty, FName CurveProperty,
	                                    const FInputScaleBias& ScaleBias, const FInputScaleBiasClamp& ScaleBiasClamp) {
		if (Pin->PinName == AlphaProperty)
		{
			Pin->bHidden = InputType != EAnimAlphaInputType::Float;
			if (!Pin->bHidden)
			{
				Pin->PinFriendlyName = ScaleBias.GetFriendlyName(ScaleBiasClamp.GetFriendlyName(Pin->PinFriendlyName));
			}
		}
		else if (Pin->PinName == CurveProperty)
		{
			Pin->bHidden = InputType != EAnimAlphaInputType::Curve;
			if (!Pin->bHidden)
			{
				Pin->PinFriendlyName = ScaleBiasClamp.GetFriendlyName(Pin->PinFriendlyName);
			}
		}
	};
	CustomizeHandPin(Node.LeftAlphaInputType,
	    GET_MEMBER_NAME_CHECKED(FIMGAnimNode_TraversalHandIK, LeftAlpha),
	    GET_MEMBER_NAME_CHECKED(FIMGAnimNode_TraversalHandIK, LeftAlphaCurveName),
	    Node.LeftAlphaScaleBias, Node.LeftAlphaScaleBiasClamp);
	CustomizeHandPin(Node.RightAlphaInputType,
	    GET_MEMBER_NAME_CHECKED(FIMGAnimNode_TraversalHandIK, RightAlpha),
	    GET_MEMBER_NAME_CHECKED(FIMGAnimNode_TraversalHandIK, RightAlphaCurveName),
	    Node.RightAlphaScaleBias, Node.RightAlphaScaleBiasClamp);
}

void UIMGAnimGraphNode_TraversalHandIK::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	Super::CustomizeDetails(DetailBuilder);

	TSharedRef<IPropertyHandle> NodeHandle =
	    DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UIMGAnimGraphNode_TraversalHandIK, Node), GetClass());

	const auto CustomizeHandDetails = [&DetailBuilder, &NodeHandle](EAnimAlphaInputType InputType,
	                                                                FName AlphaProperty, FName ScaleBiasProperty,
	                                                                FName CurveProperty) {
		if (InputType != EAnimAlphaInputType::Float)
		{
			DetailBuilder.HideProperty(NodeHandle->GetChildHandle(AlphaProperty));
			DetailBuilder.HideProperty(NodeHandle->GetChildHandle(ScaleBiasProperty));
		}
		if (InputType != EAnimAlphaInputType::Curve)
		{
			DetailBuilder.HideProperty(NodeHandle->GetChildHandle(CurveProperty));
		}
	};
	CustomizeHandDetails(Node.LeftAlphaInputType,
	    GET_MEMBER_NAME_CHECKED(FIMGAnimNode_TraversalHandIK, LeftAlpha),
	    GET_MEMBER_NAME_CHECKED(FIMGAnimNode_TraversalHandIK, LeftAlphaScaleBias),
	    GET_MEMBER_NAME_CHECKED(FIMGAnimNode_TraversalHandIK, LeftAlphaCurveName));
	CustomizeHandDetails(Node.RightAlphaInputType,
	    GET_MEMBER_NAME_CHECKED(FIMGAnimNode_TraversalHandIK, RightAlpha),
	    GET_MEMBER_NAME_CHECKED(FIMGAnimNode_TraversalHandIK, RightAlphaScaleBias),
	    GET_MEMBER_NAME_CHECKED(FIMGAnimNode_TraversalHandIK, RightAlphaCurveName));
}

void UIMGAnimGraphNode_TraversalHandIK::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	const bool bLeftChanged =
	    PropertyName == GET_MEMBER_NAME_CHECKED(FIMGAnimNode_TraversalHandIK, LeftAlphaInputType);
	const bool bRightChanged =
	    PropertyName == GET_MEMBER_NAME_CHECKED(FIMGAnimNode_TraversalHandIK, RightAlphaInputType);
	if (bLeftChanged || bRightChanged)
	{
		FScopedTransaction Transaction(LOCTEXT("ChangeHandAlphaInputType", "Change Hand Alpha Input Type"));
		Modify();

		const auto BreakUnusedHandAlphaLinks = [this](EAnimAlphaInputType InputType, FName AlphaProperty,
		                                              FName CurveProperty) {
			for (UEdGraphPin* Pin : Pins)
			{
				if (Pin->PinName == AlphaProperty && InputType != EAnimAlphaInputType::Float)
				{
					Pin->BreakAllPinLinks();
					RemoveBindings(Pin->PinName);
				}
				else if (Pin->PinName == CurveProperty && InputType != EAnimAlphaInputType::Curve)
				{
					Pin->BreakAllPinLinks();
					RemoveBindings(Pin->PinName);
				}
			}
		};
		if (bLeftChanged)
		{
			BreakUnusedHandAlphaLinks(Node.LeftAlphaInputType,
			    GET_MEMBER_NAME_CHECKED(FIMGAnimNode_TraversalHandIK, LeftAlpha),
			    GET_MEMBER_NAME_CHECKED(FIMGAnimNode_TraversalHandIK, LeftAlphaCurveName));
		}
		else
		{
			BreakUnusedHandAlphaLinks(Node.RightAlphaInputType,
			    GET_MEMBER_NAME_CHECKED(FIMGAnimNode_TraversalHandIK, RightAlpha),
			    GET_MEMBER_NAME_CHECKED(FIMGAnimNode_TraversalHandIK, RightAlphaCurveName));
		}

		ReconstructNode();
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(GetBlueprint());
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(FIMGAnimNode_TraversalHandIK, LeftAlphaScaleBias) ||
	    PropertyName == GET_MEMBER_NAME_CHECKED(FIMGAnimNode_TraversalHandIK, RightAlphaScaleBias) ||
	    PropertyName == GET_MEMBER_NAME_CHECKED(FIMGAnimNode_TraversalHandIK, LeftAlphaScaleBiasClamp) ||
	    PropertyName == GET_MEMBER_NAME_CHECKED(FIMGAnimNode_TraversalHandIK, RightAlphaScaleBiasClamp))
	{
		ReconstructNode();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#undef LOCTEXT_NAMESPACE
