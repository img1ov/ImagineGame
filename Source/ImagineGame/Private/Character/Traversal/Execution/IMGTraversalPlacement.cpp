#include "Character/Traversal/Execution/IMGTraversalPlacement.h"

#include "Animation/AnimMontage.h"
#include "AnimationWarpingLibrary.h"
#include "Character/IMGCharacter.h"
#include "Character/IMGCharacterMovementComponent.h"
#include "Character/Traversal/IMGTraversalDetector.h"
#include "Character/Traversal/IMGTraversalTypes.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "RootMotionModifier.h"
#include "RootMotionModifier_SkewWarp.h"
#include "UObject/UnrealType.h"

#include "Character/Traversal/IMGTraversalDebug.h"
#include "Character/Traversal/Execution/IMGTraversalPlaybackPath.h"
#include "Character/Traversal/Execution/IMGGroundPlacement.h"
#include "Character/Traversal/Execution/IMGTraversalWarpPrediction.h"

namespace
{
constexpr double PlacementUprightTolerance = 1.e-3;

const FFloatProperty* GetSkewWarpSpeedClampProperty()
{
	// TWeakFieldPtr stays safe across editor Live Coding class reinstancing.
	static TWeakFieldPtr<const FFloatProperty> CachedProperty;
	if (!CachedProperty.IsValid())
	{
		CachedProperty = FindFProperty<FFloatProperty>(
		    URootMotionModifier_SkewWarp::StaticClass(), TEXT("MaxSpeedClampRatio"));
	}
	return CachedProperty.Get();
}

struct FPlacementContext
{
	AIMGCharacter& Character;
	const FIMGTraversalExecutionSettings& Settings;
	const FIMGTraversalDetectorSettings& Geometry;
	TConstArrayView<FIMGTraversalWarpWindow> Windows;
	FIMGTraversalCandidate& Candidate;
	const FIMGTraversalAnimationSelection& Selection;
	bool bVerboseDiagnostics = false;
	TFunctionRef<void(const TCHAR*, const FString&, bool)> LogEvent;

	FIMGTraversalPlaybackPath Playback;
	TSet<FName> BindingNames;
	TSet<FName> UsedTargets;
	TArray<const FIMGTraversalWarpWindow*, TInlineAllocator<4>> PlacementWindows;
	const FIMGTraversalWarpWindow* FinalTranslationWindow = nullptr;
	const FIMGTraversalWarpWindow* LandingWindow = nullptr;
	const FIMGTraversalWarpWindow* EvaluationWindow = nullptr;
	const FIMGTraversalWarpWindow* CompensationWindow = nullptr;
	bool bProjectSequence = false;
	FString PredictionUnavailableReason;

	FTransform InitialPose = FTransform::Identity;
	FTransform PathAnchor = FTransform::Identity;
	TMap<FName, FTransform> WorldTargets;
	TOptional<FVector> CompensatedPlacementCenter;
	UPrimitiveComponent* PlacementSupportToIgnore = nullptr;
	const TCHAR* CompensationSkipReason = TEXT("No supported height-adjusting window");

	void Log(const TCHAR* Event, const FString& Detail, bool bWarning = false) const
	{
		LogEvent(Event, Detail, bWarning);
	}

	bool Reject(const FString& Reason) const
	{
		Log(TEXT("WarpTargetsRejected"),
		    FString::Printf(TEXT("Montage=%s | %s"), *GetNameSafe(Selection.Montage.Get()), *Reason), true);
		return false;
	}

	bool GetAnchor(EIMGTraversalAnchor Anchor, FTransform& OutTransform, UPrimitiveComponent*& OutComponent) const
	{
		switch (Anchor)
		{
			case EIMGTraversalAnchor::FrontLedge:
				OutTransform = Candidate.FrontLedge;
				OutComponent = Candidate.FrontLedgeComponent;
				return Candidate.bHasFrontLedge && IsValid(OutComponent);
			case EIMGTraversalAnchor::BackLedge:
				OutTransform = Candidate.BackLedge;
				OutComponent = Candidate.BackLedgeComponent;
				return Candidate.bHasBackLedge && IsValid(OutComponent);
			case EIMGTraversalAnchor::BackFloor:
				OutTransform = Candidate.BackFloor;
				OutComponent = Candidate.BackFloorComponent;
				return Candidate.bHasBackFloor && IsValid(OutComponent);
			default:
				return false;
		}
	}

	const FIMGTraversalWarpBinding* FindBinding(FName Name) const
	{
		return Settings.WarpBindings.FindByPredicate(
		    [Name](const FIMGTraversalWarpBinding& Binding) { return Binding.TargetName == Name; });
	}

	const FIMGTraversalWarpWindow* SelectWindow(FName Name, int32 Index, float MinEnd, float MaxEnd) const
	{
		return Playback.SelectTranslationWindow(Windows, Name, Index, MinEnd, MaxEnd);
	}

	float AlignmentTime(const FIMGTraversalWarpWindow& Window) const
	{
		return Window.Modifier->bSubtractRemainingRootMotion ? Selection.Montage->GetPlayLength() : Window.EndTime;
	}

	bool IsBeyondBackLedge(const FVector& Location) const
	{
		return Candidate.bHasBackLedge &&
		    FVector::DotProduct(Location - Candidate.BackLedge.GetLocation(),
		        Candidate.BackLedge.GetUnitAxis(EAxis::X).GetSafeNormal2D()) >= 0.0;
	}
};

bool ValidateBindingsAndCollectWindows(FPlacementContext& Ctx)
{
	for (const FIMGTraversalWarpBinding& Binding : Ctx.Settings.WarpBindings)
	{
		if (Binding.TargetName.IsNone() || Ctx.BindingNames.Contains(Binding.TargetName) || !Binding.Offset.IsValid())
		{
			return Ctx.Reject(
			    FString::Printf(TEXT("Warp binding invalid or duplicated: %s"), *Binding.TargetName.ToString()));
		}
		Ctx.BindingNames.Add(Binding.TargetName);
	}

	FString PlaybackFailure;
	if (!Ctx.Playback.Build(Ctx.Selection, Ctx.Windows, Ctx.bVerboseDiagnostics, Ctx.LogEvent, PlaybackFailure))
	{
		return Ctx.Reject(PlaybackFailure);
	}

	const auto& WindowPlaybackTimes = Ctx.Playback.GetWindowTimes();
	const float PlaybackStart = Ctx.Playback.GetStartTime();
	const float PlacementTime = Ctx.Playback.GetPlacementTime();
	const bool bNeedPredictionDetails =
	    Ctx.bVerboseDiagnostics || Ctx.Settings.Landing.Mode != EIMGTraversalLandingMode::None;
	const FFloatProperty* SpeedClampProperty = GetSkewWarpSpeedClampProperty();

	bool bSequentialSkewWindows = true;
	const FIMGTraversalWarpWindow* LatestEndingWindow = nullptr;
	float LatestWindowEnd = -FLT_MAX;
	TArray<FString> PredictionLimitations;

	for (const FIMGTraversalWarpWindow* WindowPtr : Ctx.Playback.GetReachableWindows())
	{
		const FIMGTraversalWarpWindow& Window = *WindowPtr;
		const FVector2f& Times = WindowPlaybackTimes.FindChecked(WindowPtr);
		if (Times.Y <= PlaybackStart || Times.X >= PlacementTime)
		{
			continue;
		}
		if (!Window.Modifier)
		{
			bSequentialSkewWindows = false;
			if (bNeedPredictionDetails)
			{
				PredictionLimitations.Add(
				    FString::Printf(TEXT("Non-Warp modifier at %.6f..%.6f"), Window.StartTime, Window.EndTime));
			}
			continue;
		}
		const FName Name = Window.Modifier->WarpTargetName;
		if (!Ctx.BindingNames.Contains(Name))
		{
			return Ctx.Reject(FString::Printf(TEXT("Warp target has no geometry binding: %s"), *Name.ToString()));
		}
		Ctx.UsedTargets.Add(Name);
		if (Window.Modifier->bWarpTranslation &&
		    (!Ctx.FinalTranslationWindow || Times.Y >= WindowPlaybackTimes.FindChecked(Ctx.FinalTranslationWindow).Y))
		{
			Ctx.FinalTranslationWindow = &Window;
		}
		if (Window.Modifier->bWarpTranslation || Window.Modifier->bWarpRotation)
		{
			const bool bNativeSkew = Window.Modifier->GetClass() == URootMotionModifier_SkewWarp::StaticClass();
			const float SpeedClamp = bNativeSkew && SpeedClampProperty
			    ? SpeedClampProperty->GetPropertyValue_InContainer(Window.Modifier)
			    : 1.f;
			const bool bOverlapsPrevious = LatestEndingWindow != nullptr && LatestWindowEnd > Times.X;
			bool bWindowPredictable = bNativeSkew && SpeedClampProperty && FMath::IsFinite(SpeedClamp) &&
			    SpeedClamp <= 0.f && Times.Y <= PlacementTime && !bOverlapsPrevious;
			if (Window.Modifier->bWarpRotation)
			{
				bWindowPredictable &= Window.Modifier->RotationType == EMotionWarpRotationType::Default &&
				    Window.Modifier->RotationMethod == EMotionWarpRotationMethod::Slerp &&
				    FMath::IsFinite(Window.Modifier->WarpRotationTimeMultiplier) &&
				    Window.Modifier->WarpRotationTimeMultiplier > 0.f &&
				    Window.Modifier->WarpRotationTimeMultiplier <= 1.f;
			}
			if (!bWindowPredictable && bNeedPredictionDetails)
			{
				PredictionLimitations.Add(FString::Printf(
				    TEXT(
				        "Target=%s Window=%.6f..%.6f Class=%s ClampAccessor=%d SpeedClamp=%.3f Complete=%d Overlap=%d "
				        "Translation=%d IgnoreZ=%d WarpRotation=%d RotationType=%s RotationMethod=%s "
				        "RotationTimeMultiplier=%.3f"),
				    *Name.ToString(), Window.StartTime, Window.EndTime, *GetNameSafe(Window.Modifier->GetClass()),
				    SpeedClampProperty != nullptr, SpeedClamp, Times.Y <= PlacementTime, bOverlapsPrevious,
				    Window.Modifier->bWarpTranslation, Window.Modifier->bIgnoreZAxis, Window.Modifier->bWarpRotation,
				    *UEnum::GetValueAsString(Window.Modifier->RotationType),
				    *UEnum::GetValueAsString(Window.Modifier->RotationMethod),
				    Window.Modifier->WarpRotationTimeMultiplier));
				if (bOverlapsPrevious)
				{
					const FIMGTraversalWarpWindow& Previous = *LatestEndingWindow;
					PredictionLimitations.Add(FString::Printf(
					    TEXT("OverlapWith=%s Window=%.6f..%.6f Translation=%d IgnoreZ=%d Rotation=%d"),
					    *Previous.Modifier->WarpTargetName.ToString(), Previous.StartTime, Previous.EndTime,
					    Previous.Modifier->bWarpTranslation, Previous.Modifier->bIgnoreZAxis,
					    Previous.Modifier->bWarpRotation));
				}
			}
			bSequentialSkewWindows &= bWindowPredictable;
			Ctx.PlacementWindows.Add(&Window);
			if (Times.Y > LatestWindowEnd)
			{
				LatestWindowEnd = Times.Y;
				LatestEndingWindow = &Window;
			}
		}
	}

	const bool bSupportedTransform =
	    Ctx.Character.GetActorScale3D().Equals(FVector::OneVector, UE_KINDA_SMALL_NUMBER) &&
	    Ctx.Character.GetActorUpVector().Equals(FVector::UpVector, PlacementUprightTolerance) &&
	    Ctx.Character.GetMesh() &&
	    Ctx.Character.GetMesh()->GetComponentScale().Equals(FVector::OneVector, UE_KINDA_SMALL_NUMBER);
	if (!bSupportedTransform && bNeedPredictionDetails)
	{
		PredictionLimitations.Add(FString::Printf(TEXT("ActorScale=%s MeshScale=%s ActorUp=%s"),
		    *Ctx.Character.GetActorScale3D().ToCompactString(),
		    Ctx.Character.GetMesh() ? *Ctx.Character.GetMesh()->GetComponentScale().ToCompactString() : TEXT("NoMesh"),
		    *Ctx.Character.GetActorUpVector().ToCompactString()));
	}
	if (Ctx.PlacementWindows.IsEmpty() && bNeedPredictionDetails)
	{
		PredictionLimitations.Add(TEXT("No remaining placement window"));
	}
	Ctx.PredictionUnavailableReason = FString::Join(PredictionLimitations, TEXT("; "));
	Ctx.bProjectSequence = bSequentialSkewWindows && bSupportedTransform && !Ctx.PlacementWindows.IsEmpty();
	if (!Ctx.bProjectSequence)
	{
		Ctx.Log(TEXT("PlacementPredictionUnavailable"), Ctx.PredictionUnavailableReason);
	}
	for (FName Required : Ctx.Settings.RequiredTargets)
	{
		if (!Ctx.UsedTargets.Contains(Required))
		{
			return Ctx.Reject(FString::Printf(TEXT("Required warp target %s has no remaining window at %.3f"),
			    *Required.ToString(), Ctx.Selection.StartTime));
		}
	}
	return true;
}

bool ResolveExplicitLanding(FPlacementContext& Ctx)
{
	const FIMGTraversalLandingSettings& Landing = Ctx.Settings.Landing;
	if (Ctx.Selection.Action != EIMGTraversalAction::Hurdle)
	{
		if (Landing.Mode != EIMGTraversalLandingMode::None)
		{
			return Ctx.Reject(TEXT("Rear-floor landing requires Hurdle; Mantle climbs the top and Vault exits airborne"));
		}
		for (const FIMGTraversalWarpBinding& Binding : Ctx.Settings.WarpBindings)
		{
			if (Binding.Anchor == EIMGTraversalAnchor::BackFloor && Ctx.UsedTargets.Contains(Binding.TargetName))
			{
				return Ctx.Reject(FString::Printf(TEXT("Only Hurdle can consume a BackFloor warp target: %s"),
				    *Binding.TargetName.ToString()));
			}
		}
		return true;
	}
	if (static_cast<uint8>(Landing.Mode) > static_cast<uint8>(EIMGTraversalLandingMode::DistanceCurve))
	{
		return Ctx.Reject(TEXT("Landing mode is invalid"));
	}
	if (Landing.Mode == EIMGTraversalLandingMode::None)
	{
		return true;
	}
	if (!Ctx.Candidate.bHasBackLedge)
	{
		return Ctx.Reject(TEXT("Explicit rear-floor landing requires a confirmed rear ledge"));
	}
	if (!FMath::IsFinite(Landing.CapsuleClearance) || Landing.CapsuleClearance < 0.f)
	{
		return Ctx.Reject(TEXT("Landing capsule clearance is invalid"));
	}

	const auto& WindowPlaybackTimes = Ctx.Playback.GetWindowTimes();
	const FIMGTraversalWarpBinding* LandingBinding = Ctx.FindBinding(Landing.TargetName);
	Ctx.LandingWindow = Ctx.SelectWindow(
	    Landing.TargetName, Landing.LandingWindowIndex, Ctx.Playback.GetStartTime(), Ctx.Playback.GetPlacementTime());
	if (!LandingBinding || LandingBinding->Anchor != EIMGTraversalAnchor::BackFloor || !Ctx.LandingWindow)
	{
		return Ctx.Reject(TEXT("Landing needs a BackFloor binding and a remaining translation window"));
	}
	if (!Ctx.bProjectSequence)
	{
		return Ctx.Reject(FString::Printf(
		    TEXT("Selected landing needs a supported, complete sequence of Skew Warp windows: %s"),
		    *Ctx.PredictionUnavailableReason));
	}

	FVector LandingPoint = Ctx.Candidate.BackFloor.GetLocation();
	if (Landing.Mode == EIMGTraversalLandingMode::DistanceCurve)
	{
		const float LandingEnd = WindowPlaybackTimes.FindChecked(Ctx.LandingWindow).Y;
		const FIMGTraversalWarpWindow* AnchorWindow =
		    Ctx.SelectWindow(Landing.AnchorTargetName, Landing.AnchorWindowIndex, -1.f, LandingEnd);
		if (!AnchorWindow && !Ctx.SelectWindow(Landing.AnchorTargetName, -1, -1.f, Ctx.Playback.GetPlacementTime()))
		{
			AnchorWindow =
			    Ctx.SelectWindow(Landing.FallbackAnchorTargetName, Landing.AnchorWindowIndex, -1.f, LandingEnd);
		}
		const FIMGTraversalWarpBinding* AnchorBinding =
		    AnchorWindow ? Ctx.FindBinding(AnchorWindow->Modifier->WarpTargetName) : nullptr;
		UPrimitiveComponent* AnchorComponent = nullptr;
		if (!AnchorBinding || AnchorBinding->Anchor == EIMGTraversalAnchor::BackFloor ||
		    !Ctx.GetAnchor(AnchorBinding->Anchor, Ctx.PathAnchor, AnchorComponent) ||
		    WindowPlaybackTimes.FindChecked(AnchorWindow).Y >= LandingEnd)
		{
			return Ctx.Reject(TEXT("Landing contact window is missing, ambiguous in order, or has no geometry"));
		}

		float CurveDistance = 0.f;
		auto AccumulateCurveDistance = [&](float From, float To, float Direction)
		{
			if (From == To)
			{
				return true;
			}
			float FromValue = 0.f;
			float ToValue = 0.f;
			if (!UAnimationWarpingLibrary::GetCurveValueFromAnimation(
			        Ctx.Selection.Montage, Landing.DistanceCurveName, From, FromValue) ||
			    !UAnimationWarpingLibrary::GetCurveValueFromAnimation(
			        Ctx.Selection.Montage, Landing.DistanceCurveName, To, ToValue) ||
			    !FMath::IsFinite(FromValue) || !FMath::IsFinite(ToValue))
			{
				return false;
			}
			CurveDistance += Direction * (ToValue - FromValue);
			return FMath::IsFinite(CurveDistance);
		};

		const float ContactTime = WindowPlaybackTimes.FindChecked(AnchorWindow).Y;
		bool bValidCurve = Ctx.Playback.ForEachInterval(ContactTime, LandingEnd,
		    [&AccumulateCurveDistance](float AssetFrom, float AssetTo)
		    { return AccumulateCurveDistance(AssetFrom, AssetTo, 1.f); });
		// Preserve the target's native tail-subtraction reference independently of the played path.
		bValidCurve &= AccumulateCurveDistance(AnchorWindow->EndTime, Ctx.AlignmentTime(*AnchorWindow), -1.f);
		bValidCurve &= AccumulateCurveDistance(Ctx.LandingWindow->EndTime, Ctx.AlignmentTime(*Ctx.LandingWindow), 1.f);
		if (!bValidCurve)
		{
			return Ctx.Reject(FString::Printf(
			    TEXT("Landing curve %s is missing or non-finite"), *Landing.DistanceCurveName.ToString()));
		}
		const float Distance = Landing.bAbsoluteCurveDistance ? FMath::Abs(CurveDistance) : CurveDistance;
		LandingPoint = Ctx.PathAnchor.GetLocation() + Ctx.PathAnchor.GetUnitAxis(EAxis::X).GetSafeNormal2D() * Distance;
#if IMG_TRAVERSAL_DEBUG_ENABLED
		if (IMGTraversalDebug::IsEnabled())
		{
			IMGTraversalDebug::MessageAt(LandingPoint + FVector::UpVector * 25.f,
			    FString::Printf(TEXT("Landing %s %.3f -> %s %.3f | Path curve delta %.1f"),
			        *AnchorWindow->Modifier->WarpTargetName.ToString(), AnchorWindow->EndTime,
			        *Landing.TargetName.ToString(), Ctx.LandingWindow->EndTime, CurveDistance),
			    IMGTraversalDebug::EStage::Execution);
		}
#endif
	}
	else if (!Ctx.Candidate.bHasBackFloor)
	{
		return Ctx.Reject(TEXT("Detected-floor landing has no sampled walkable support"));
	}

	const UCapsuleComponent* Capsule = Ctx.Character.GetCapsuleComponent();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const float TopZ = FMath::Max(Ctx.Candidate.FrontLedge.GetLocation().Z, Ctx.Candidate.BackLedge.GetLocation().Z);
	const FVector TraceStart(LandingPoint.X, LandingPoint.Y, TopZ + HalfHeight);
	const FVector TraceEnd(LandingPoint.X, LandingPoint.Y, Ctx.Candidate.FrontLedge.GetLocation().Z - Ctx.Geometry.MaxDrop);
	FHitResult Floor;
	FCollisionQueryParams FloorParams(SCENE_QUERY_STAT(IMGTraversalSelectedLanding), false, &Ctx.Character);
	FCollisionResponseParams Responses;
	Ctx.Character.GetIMGMovementComponent()->InitCollisionParams(FloorParams, Responses);
	const bool bHit = IMG_TRAVERSAL_QUERY(Ctx.Character.GetWorld()->LineTraceSingleByChannel(
	    Floor, TraceStart, TraceEnd, Ctx.Geometry.FloorTraceChannel, FloorParams, Responses));
#if IMG_TRAVERSAL_DEBUG_ENABLED
	IMGTraversalDebug::Line(
	    Ctx.Character.GetWorld(), TraceStart, bHit ? Floor.ImpactPoint : TraceEnd, bHit ? FColor::Green : FColor::Red);
	if (bHit)
	{
		IMGTraversalDebug::Intersection(Ctx.Character.GetWorld(), Floor,
		    Ctx.IsBeyondBackLedge(Floor.ImpactPoint) && Ctx.Character.GetIMGMovementComponent()->IsWalkable(Floor) &&
		        FIMGTraversalDetector::CanUseFloor(Floor.GetComponent(), Ctx.Geometry),
		    TEXT("Animation-selected floor"), IMGTraversalDebug::EStage::Execution);
	}
#endif
	UPrimitiveComponent* FloorComponent = Floor.GetComponent();
	if (!bHit || !Ctx.IsBeyondBackLedge(Floor.ImpactPoint) ||
	    !Ctx.Character.GetIMGMovementComponent()->IsWalkable(Floor) ||
	    !FIMGTraversalDetector::CanUseFloor(FloorComponent, Ctx.Geometry))
	{
		return Ctx.Reject(TEXT("Selected landing floor is unusable or is not beyond the rear ledge"));
	}
	const float Drop = Ctx.Candidate.FrontLedge.GetLocation().Z - Floor.ImpactPoint.Z;
	if (Drop < 0.f || Drop > Ctx.Geometry.MaxDrop)
	{
		return Ctx.Reject(TEXT("Selected landing exceeds the configured drop range"));
	}
	LandingPoint.Z = Floor.ImpactPoint.Z;
	Ctx.Candidate.BackFloor.SetRotation(Ctx.PathAnchor.GetRotation());
	Ctx.Candidate.BackFloor.SetLocation(LandingPoint);
	Ctx.Candidate.BackFloorComponent = FloorComponent;
	Ctx.Candidate.bHasBackFloor = true;
	Ctx.Candidate.DropHeight = Drop;
	Ctx.Candidate.BackLedgeHeight = Ctx.Candidate.BackLedge.GetLocation().Z - LandingPoint.Z;
	return true;
}

bool BuildInitialWorldTargets(FPlacementContext& Ctx)
{
	for (const FIMGTraversalWarpBinding& Binding : Ctx.Settings.WarpBindings)
	{
		if (!Ctx.UsedTargets.Contains(Binding.TargetName))
		{
			continue;
		}
		FTransform Anchor;
		UPrimitiveComponent* Component = nullptr;
		if (!Ctx.GetAnchor(Binding.Anchor, Anchor, Component))
		{
			return Ctx.Reject(
			    FString::Printf(TEXT("Warp target %s has no valid geometric anchor"), *Binding.TargetName.ToString()));
		}
		const FTransform Transform = Binding.Offset * Anchor;
		if (!Transform.IsValid())
		{
			return Ctx.Reject(TEXT("Resolved warp transform is invalid"));
		}
		Ctx.WorldTargets.Add(Binding.TargetName, Transform);
	}
	return true;
}

bool SelectEvaluationAndCompensationWindows(FPlacementContext& Ctx, float& OutGroundHandoffTime)
{
	const auto& WindowPlaybackTimes = Ctx.Playback.GetWindowTimes();
	Ctx.EvaluationWindow = Ctx.LandingWindow;
	if (Ctx.Selection.Action == EIMGTraversalAction::Mantle)
	{
		Ctx.EvaluationWindow = Ctx.FinalTranslationWindow;
		if (!Ctx.EvaluationWindow || WindowPlaybackTimes.FindChecked(Ctx.EvaluationWindow).Y > Ctx.Playback.GetPlacementTime() ||
		    !FMath::IsFinite(Ctx.Settings.MaxGroundAdjustment) || Ctx.Settings.MaxGroundAdjustment < 0.f)
		{
			return Ctx.Reject(TEXT("Mantle requires a reachable ground handoff window and finite recovery limit"));
		}
		OutGroundHandoffTime = WindowPlaybackTimes.FindChecked(Ctx.EvaluationWindow).Y;
	}
	else if (Ctx.Selection.Action == EIMGTraversalAction::Hurdle && !Ctx.LandingWindow)
	{
		for (const FIMGTraversalWarpWindow* Window : Ctx.PlacementWindows)
		{
			const FIMGTraversalWarpBinding* Binding = Ctx.FindBinding(Window->Modifier->WarpTargetName);
			if (Window->Modifier->bWarpTranslation && Binding && Binding->Anchor == EIMGTraversalAnchor::BackFloor)
			{
				Ctx.EvaluationWindow = Window;
			}
		}
	}
	if (Ctx.bProjectSequence && Ctx.EvaluationWindow)
	{
		// A later height window cannot correct an earlier contact, regardless of action or target name.
		const float EvaluationEnd = WindowPlaybackTimes.FindChecked(Ctx.EvaluationWindow).Y;
		for (const FIMGTraversalWarpWindow* Window : Ctx.PlacementWindows)
		{
			if (Window->Modifier->bWarpTranslation && !Window->Modifier->bIgnoreZAxis &&
			    WindowPlaybackTimes.FindChecked(Window).Y <= EvaluationEnd)
			{
				Ctx.CompensationWindow = Window;
			}
		}
	}
	return true;
}

void CompensateSupportHeight(FPlacementContext& Ctx, FIMGTraversalWarpPrediction& Prediction,
    const FIMGTraversalWarpWindow& Window, const FIMGTraversalWarpWindow& EvaluationWindow,
    const FTransform& Anchor, const UPrimitiveComponent& Carrier)
{
	// Support belongs to the action's contact window; playback may continue with locomotion afterward.
	FVector Center;
	if (!Prediction.Project(EvaluationWindow, Ctx.WorldTargets, Center))
	{
		Ctx.CompensationSkipReason = TEXT("Placement projection is unavailable");
		return;
	}
	if (Ctx.bVerboseDiagnostics)
	{
		Ctx.Log(TEXT("PlacementEvaluation"),
		    FString::Printf(
		        TEXT("Endpoint=%s StartTime=%.6f CorrectionTarget=%s ReferenceTarget=%s AssetWindowEnd=%.6f "
		             "EvaluationEnd=%.6f IncludeTail=0 Center=%s"),
		        Ctx.Selection.Action == EIMGTraversalAction::Mantle ? TEXT("TopContactWindow")
		            : Ctx.LandingWindow                             ? TEXT("LandingWindow")
		                                                            : TEXT("BackFloorWindow"),
		        Ctx.Selection.StartTime, *Window.Modifier->WarpTargetName.ToString(),
		        *EvaluationWindow.Modifier->WarpTargetName.ToString(), EvaluationWindow.EndTime,
		        Ctx.Playback.GetWindowTimes().FindChecked(&EvaluationWindow).Y, *Center.ToString()));
	}

	const UCapsuleComponent* Capsule = Ctx.Character.GetCapsuleComponent();
	const UIMGCharacterMovementComponent* Movement = Ctx.Character.GetIMGMovementComponent();
	const bool bRequireTopSupport = Ctx.Selection.Action == EIMGTraversalAction::Mantle;
	const double TopTolerance =
	    FMath::Max(static_cast<double>(Ctx.Geometry.DepthHeightTolerance), UE_DOUBLE_KINDA_SMALL_NUMBER);
	const double TopMinZ = Ctx.Candidate.TopSupportHeightRange.X - TopTolerance;
	const double TopMaxZ = Ctx.Candidate.TopSupportHeightRange.Y + TopTolerance;
	auto IsPlacementSupport = [&](const FHitResult& Hit)
	{
		UPrimitiveComponent* Component = Hit.GetComponent();
		const bool bKnownSurface = Component &&
		    (Component == &Carrier || (bRequireTopSupport && Ctx.Candidate.ObstacleComponents.Contains(Component)));
		// One collision component can contain both the obstacle top and the lower floor.
		const double Drop = Ctx.Candidate.FrontLedge.GetLocation().Z - Hit.ImpactPoint.Z;
		const bool bInSupportLayer = bRequireTopSupport
		    ? Hit.ImpactPoint.Z >= TopMinZ && Hit.ImpactPoint.Z <= TopMaxZ
		    : Ctx.IsBeyondBackLedge(Hit.ImpactPoint) && Drop >= 0.0 && Drop <= Ctx.Geometry.MaxDrop;
		return bKnownSurface && bInSupportLayer && Hit.IsValidBlockingHit() && Movement->IsWalkable(Hit) &&
		    (bRequireTopSupport
		        ? FIMGTraversalDetector::CanUseSurface(Component, Ctx.Geometry, Ctx.Candidate.RequestSource)
		        : FIMGTraversalDetector::CanUseFloor(Component, Ctx.Geometry));
	};

	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const float Radius = Capsule->GetScaledCapsuleRadius();
#if IMG_TRAVERSAL_DEBUG_ENABLED
	IMGTraversalDebug::PlacementCapsule(
	    Ctx.Character.GetWorld(), Center, HalfHeight, Radius, FColor::Orange, TEXT("Prepared placement before compensation"));
#endif
	const FVector Up = FVector::UpVector;
	const FVector Feet = Center - Up * HalfHeight;
	const float FloorGap = IMGGroundPlacement::FloorGap();
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius, HalfHeight);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IMGTraversalPlacementCompensation), false, &Ctx.Character);
	FCollisionResponseParams Responses;
	Movement->InitCollisionParams(Params, Responses);
	const ECollisionChannel Channel = Capsule->GetCollisionObjectType();

	double SupportTopZ = TopMaxZ;
	double SupportBottomZ = TopMinZ;
	if (!bRequireTopSupport)
	{
		// Reach the anchor's platform even when the projected feet are above it.
		const float WalkableZ = Carrier.GetWalkableSlopeOverride().ModifyWalkableFloorZ(Movement->GetWalkableFloorZ());
		const double MinNormalZ =
		    FMath::Clamp(static_cast<double>(WalkableZ), static_cast<double>(UE_KINDA_SMALL_NUMBER), 1.0);
		const FVector AnchorLocation = Anchor.GetLocation();
		const double MaxGrade = FMath::Sqrt(1.0 - FMath::Square(MinNormalZ)) / MinNormalZ;
		const double SupportHeightRange = (FVector::Dist2D(Feet, AnchorLocation) + Radius) * MaxGrade;
		const double RoundBottomReach = Radius * (1.0 / MinNormalZ - 1.0);
		SupportTopZ = AnchorLocation.Z + SupportHeightRange;
		SupportBottomZ = FMath::Min(Feet.Z - RoundBottomReach, AnchorLocation.Z - SupportHeightRange);
	}

	FHitResult Plane;
	const FVector TraceStart(Feet.X, Feet.Y, FMath::Max(Center.Z, SupportTopZ) + FloorGap);
	const FVector TraceEnd(Feet.X, Feet.Y, SupportBottomZ - UCharacterMovementComponent::MAX_FLOOR_DIST);
	if (TraceStart.ContainsNaN() || TraceEnd.ContainsNaN())
	{
		Ctx.CompensationSkipReason = TEXT("Support trace is non-finite");
		return;
	}
	const ECollisionChannel SupportChannel = bRequireTopSupport ? Channel : Ctx.Geometry.FloorTraceChannel.GetValue();
	const bool bHit = IMG_TRAVERSAL_QUERY(Ctx.Character.GetWorld()->LineTraceSingleByChannel(
	    Plane, TraceStart, TraceEnd, SupportChannel, Params, Responses));
	const bool bHasPlaneSupport = bHit && IsPlacementSupport(Plane);
#if IMG_TRAVERSAL_DEBUG_ENABLED
	IMGTraversalDebug::Line(Ctx.Character.GetWorld(), TraceStart, bHit ? Plane.ImpactPoint : TraceEnd,
	    bHasPlaneSupport ? FColor::Green : FColor::Red);
#endif
	if (!bHasPlaneSupport)
	{
		Ctx.CompensationSkipReason = TEXT("No usable plane support on the selected layer");
		if (Ctx.bVerboseDiagnostics)
		{
			Ctx.Log(TEXT("PlacementPlane"),
			    FString::Printf(
			        TEXT("Center=%s Feet=%s Hit=%d Component=%s Impact=%s Normal=%s Penetrating=%d "
			             "TopRequired=%d TopRange=%.2f..%.2f"),
			        *Center.ToCompactString(), *Feet.ToCompactString(), bHit, *GetNameSafe(Plane.GetComponent()),
			        *Plane.ImpactPoint.ToCompactString(), *Plane.ImpactNormal.ToCompactString(), Plane.bStartPenetrating,
			        bRequireTopSupport, TopMinZ, TopMaxZ));
		}
		return;
	}

	// Estimate where the lower hemisphere clears the plane before sweeping the full capsule.
	double PlaneAdjustment = 0.0;
	if (!IMGGroundPlacement::PlaneAdjustment(Center, HalfHeight, Radius, Plane, PlaneAdjustment))
	{
		Ctx.CompensationSkipReason = TEXT("Support plane normal or adjustment is invalid");
		return;
	}

	FHitResult Support;
	// Start at the original center on downward corrections so intervening blockers are swept.
	const FVector SweepStart = Center + Up * FMath::Max(0.0, PlaneAdjustment);
	const FVector SweepEnd = Plane.ImpactPoint + Up * (HalfHeight - UCharacterMovementComponent::MAX_FLOOR_DIST);
	const bool bHasSupport = IMG_TRAVERSAL_QUERY(Ctx.Character.GetWorld()->SweepSingleByChannel(
	    Support, SweepStart, SweepEnd, FQuat::Identity, Channel, Shape, Params, Responses));
	if (!bHasSupport || !IsPlacementSupport(Support) || !IMGGroundPlacement::IsUsableSupport(*Movement, Support, Radius))
	{
		Ctx.CompensationSkipReason = TEXT("Full capsule sweep has no usable support or fails edge tolerance");
		if (Ctx.bVerboseDiagnostics)
		{
			Ctx.Log(TEXT("PlacementCapsuleSupport"),
			    FString::Printf(
			        TEXT("Center=%s SweepStart=%s SweepEnd=%s Hit=%d Component=%s Impact=%s Normal=%s Penetrating=%d"),
			        *Center.ToCompactString(), *SweepStart.ToCompactString(), *SweepEnd.ToCompactString(), bHasSupport,
			        *GetNameSafe(Support.GetComponent()), *Support.ImpactPoint.ToCompactString(),
			        *Support.ImpactNormal.ToCompactString(), Support.bStartPenetrating));
		}
		return;
	}

	const FVector SafeCenter = Support.Location + Up * FloorGap;
	const double HeightAdjustment = SafeCenter.Z - Center.Z;
	if (!FMath::IsFinite(HeightAdjustment) ||
	    IMG_TRAVERSAL_QUERY(Ctx.Character.GetWorld()->OverlapBlockingTestByChannel(
	        SafeCenter, FQuat::Identity, Channel, Shape, Params, Responses)))
	{
#if IMG_TRAVERSAL_DEBUG_ENABLED
		IMGTraversalDebug::PlacementCapsule(Ctx.Character.GetWorld(), SafeCenter, HalfHeight, Radius, FColor::Red,
		    TEXT("Rejected placement after compensation"));
#endif
		Ctx.CompensationSkipReason = TEXT("Corrected capsule is blocked or its height is non-finite");
		return;
	}

	FTransform& WarpTarget = Ctx.WorldTargets.FindChecked(Window.Modifier->WarpTargetName);
	const FVector OriginalLocation = WarpTarget.GetLocation();
	FVector TargetLocation = OriginalLocation;
	const double CenterOffsetZ = Center.Z - TargetLocation.Z;
	TargetLocation.Z = SafeCenter.Z - CenterOffsetZ;
	WarpTarget.SetLocation(TargetLocation);
	if (Ctx.bProjectSequence && FMath::Abs(HeightAdjustment) > UE_DOUBLE_SMALL_NUMBER)
	{
		// A named target can affect several windows, including ones before the height window.
		FVector ReprojectedCenter;
		if (!Prediction.Project(EvaluationWindow, Ctx.WorldTargets, ReprojectedCenter) ||
		    !ReprojectedCenter.Equals(SafeCenter, UE_KINDA_SMALL_NUMBER))
		{
			WarpTarget.SetLocation(OriginalLocation);
#if IMG_TRAVERSAL_DEBUG_ENABLED
			IMGTraversalDebug::PlacementCapsule(Ctx.Character.GetWorld(), SafeCenter, HalfHeight, Radius, FColor::Red,
			    TEXT("Rejected placement after compensation"));
#endif
			Ctx.CompensationSkipReason = TEXT("Corrected target fails placement-path reprojection");
			Ctx.Log(TEXT("PlacementReprojectionRejected"), TEXT("Height correction changes the predicted landing"));
			return;
		}
	}

	Ctx.CompensatedPlacementCenter = SafeCenter;
	Ctx.CompensationSkipReason = nullptr;
	if (HeightAdjustment > UE_DOUBLE_SMALL_NUMBER)
	{
		Ctx.PlacementSupportToIgnore = Support.GetComponent();
	}
#if IMG_TRAVERSAL_DEBUG_ENABLED
	IMGTraversalDebug::PlacementCapsule(Ctx.Character.GetWorld(), SafeCenter, HalfHeight, Radius, FColor::Green,
	    TEXT("Validated placement after compensation"));
	if (IMGTraversalDebug::IsEnabled())
	{
		IMGTraversalDebug::MessageAt(SafeCenter + Up * HalfHeight,
		    FString::Printf(TEXT("Safe capsule Z %.2f | WarpTarget Z %.2f"), SafeCenter.Z, TargetLocation.Z),
		    IMGTraversalDebug::EStage::Execution);
	}
#endif
}

bool ValidateExplicitLanding(FPlacementContext& Ctx, FIMGTraversalWarpPrediction& Prediction)
{
	if (!Ctx.LandingWindow)
	{
		return true;
	}
	const FIMGTraversalLandingSettings& Landing = Ctx.Settings.Landing;
	FVector PlacementCenter;
	if (Ctx.CompensatedPlacementCenter.IsSet())
	{
		PlacementCenter = Ctx.CompensatedPlacementCenter.GetValue();
	}
	else if (!Prediction.Project(*Ctx.LandingWindow, Ctx.WorldTargets, PlacementCenter))
	{
		return Ctx.Reject(TEXT("Landing warp-point or root transform is invalid"));
	}
	if (Ctx.bVerboseDiagnostics)
	{
		Ctx.Log(TEXT("LandingEvaluation"),
		    FString::Printf(TEXT("Target=%s StartTime=%.6f AssetWindowEnd=%.6f EvaluationEnd=%.6f IncludeTail=0 Center=%s"),
		        *Ctx.LandingWindow->Modifier->WarpTargetName.ToString(), Ctx.Selection.StartTime,
		        Ctx.LandingWindow->EndTime, Ctx.Playback.GetWindowTimes().FindChecked(Ctx.LandingWindow).Y,
		        *PlacementCenter.ToString()));
	}

	const UCapsuleComponent* Capsule = Ctx.Character.GetCapsuleComponent();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const float Radius = Capsule->GetScaledCapsuleRadius();
	FCollisionQueryParams FloorParams(SCENE_QUERY_STAT(IMGTraversalSelectedLanding), false, &Ctx.Character);
	FCollisionResponseParams Responses;
	Ctx.Character.GetIMGMovementComponent()->InitCollisionParams(FloorParams, Responses);
	const FVector LandingCenter = PlacementCenter + FVector::UpVector * Landing.CapsuleClearance;
	const bool bBlocked = IMG_TRAVERSAL_QUERY(Ctx.Character.GetWorld()->OverlapBlockingTestByChannel(
	    LandingCenter, FQuat::Identity, Capsule->GetCollisionObjectType(),
	    FCollisionShape::MakeCapsule(Radius, HalfHeight), FloorParams, Responses));
#if IMG_TRAVERSAL_DEBUG_ENABLED
	IMGTraversalDebug::PlacementCapsule(Ctx.Character.GetWorld(), LandingCenter, HalfHeight, Radius,
	    bBlocked ? FColor::Red : FColor::Green, TEXT("Selected landing clearance"));
#endif
	if (bBlocked)
	{
		return Ctx.Reject(TEXT("Selected landing capsule is blocked"));
	}

	// An animation warp-point offset may place the capsule beyond the floor traced at the contact point.
	FFindFloorResult RootFloor;
	const float FloorDistance = Landing.CapsuleClearance + UCharacterMovementComponent::MAX_FLOOR_DIST;
	Ctx.Character.GetIMGMovementComponent()->ComputeFloorDist(LandingCenter, FloorDistance, FloorDistance, RootFloor, Radius);
	UPrimitiveComponent* RootSupport = RootFloor.HitResult.GetComponent();
	const float RootDrop = Ctx.Candidate.FrontLedge.GetLocation().Z - RootFloor.HitResult.ImpactPoint.Z;
#if IMG_TRAVERSAL_DEBUG_ENABLED
	IMGTraversalDebug::Line(Ctx.Character.GetWorld(), Ctx.Candidate.BackFloor.GetLocation(), PlacementCenter, FColor::Yellow);
	if (IMGTraversalDebug::IsEnabled())
	{
		IMGTraversalDebug::MessageAt(LandingCenter + FVector::UpVector * HalfHeight,
		    FString::Printf(TEXT("Landing contact -> binding/warp-point -> capsule | floor reach %.1f"), FloorDistance),
		    IMGTraversalDebug::EStage::Execution);
	}
	if (RootFloor.bBlockingHit)
	{
		IMGTraversalDebug::Intersection(Ctx.Character.GetWorld(), RootFloor.HitResult,
		    RootFloor.IsWalkableFloor() && RootDrop >= 0.f && RootDrop <= Ctx.Geometry.MaxDrop &&
		        Ctx.IsBeyondBackLedge(RootFloor.HitResult.ImpactPoint) &&
		        FIMGTraversalDetector::CanUseFloor(RootSupport, Ctx.Geometry),
		    TEXT("Actual capsule support"), IMGTraversalDebug::EStage::Execution);
	}
#endif
	if (!RootFloor.IsWalkableFloor() || RootDrop < 0.f || RootDrop > Ctx.Geometry.MaxDrop ||
	    !Ctx.IsBeyondBackLedge(RootFloor.HitResult.ImpactPoint) ||
	    !FIMGTraversalDetector::CanUseFloor(RootSupport, Ctx.Geometry))
	{
		return Ctx.Reject(TEXT("Selected landing capsule has no usable floor support beyond the rear ledge"));
	}

	if (Landing.bCheckStraightPath)
	{
		FCollisionQueryParams PathParams(FloorParams);
		for (const UPrimitiveComponent* Component : Ctx.Candidate.ObstacleComponents)
		{
			PathParams.AddIgnoredComponent(Component);
		}
		if (Ctx.LandingWindow == Ctx.CompensationWindow && Ctx.PlacementSupportToIgnore)
		{
			PathParams.AddIgnoredComponent(Ctx.PlacementSupportToIgnore);
		}
		FHitResult PathHit;
		const FVector Start = Ctx.PathAnchor.GetLocation() + FVector::UpVector * (HalfHeight + Landing.CapsuleClearance);
		const bool bPathBlocked = IMG_TRAVERSAL_QUERY(Ctx.Character.GetWorld()->SweepSingleByChannel(PathHit, Start,
		    LandingCenter, FQuat::Identity, Capsule->GetCollisionObjectType(),
		    FCollisionShape::MakeCapsule(Radius, HalfHeight), PathParams, Responses));
#if IMG_TRAVERSAL_DEBUG_ENABLED
		IMGTraversalDebug::Capsule(Ctx.Character.GetWorld(), Start, HalfHeight, Radius, FColor::Cyan);
		IMGTraversalDebug::Line(
		    Ctx.Character.GetWorld(), Start, LandingCenter, bPathBlocked ? FColor::Red : FColor::Cyan);
		if (bPathBlocked)
		{
			IMGTraversalDebug::Intersection(Ctx.Character.GetWorld(), PathHit, false,
			    TEXT("Optional straight capsule path"), IMGTraversalDebug::EStage::Execution);
		}
#endif
		if (bPathBlocked)
		{
			return Ctx.Reject(TEXT("Configured straight landing path is blocked"));
		}
	}

	Ctx.Candidate.BackFloorComponent = RootSupport;
	Ctx.Candidate.LocalBackFloor = Ctx.Candidate.BackFloor.GetRelativeTransform(RootSupport->GetComponentTransform());
	return true;
}

bool CommitOutputTargets(FPlacementContext& Ctx, TArray<FIMGTraversalWarpTarget>& OutTargets)
{
	if (!Ctx.LandingWindow && Ctx.EvaluationWindow)
	{
		if (!Ctx.bProjectSequence && Ctx.bVerboseDiagnostics)
		{
			// Optional compensation limits do not invalidate the authored traversal trajectory.
			Ctx.Log(TEXT("PlacementPredictionFallback"),
			    FString::Printf(TEXT("Montage=%s Start=%.3f: retaining authored targets without placement "
			                         "compensation/contact prediction; %s"),
			        *GetNameSafe(Ctx.Selection.Montage.Get()), Ctx.Selection.StartTime,
			        Ctx.PredictionUnavailableReason.IsEmpty() ? TEXT("No resolvable contact window")
			                                                  : *Ctx.PredictionUnavailableReason));
		}
		else if (Ctx.CompensationSkipReason && Ctx.bVerboseDiagnostics)
		{
			Ctx.Log(TEXT("PlacementCompensationSkipped"),
			    FString::Printf(TEXT("Montage=%s Start=%.3f: retaining authored targets; %s | %s"),
			        *GetNameSafe(Ctx.Selection.Montage.Get()), Ctx.Selection.StartTime, Ctx.CompensationSkipReason,
			        *Ctx.PredictionUnavailableReason));
		}
	}

	if (Ctx.PlacementSupportToIgnore)
	{
		if (Ctx.Selection.Action == EIMGTraversalAction::Hurdle &&
		    !Ctx.Candidate.ObstacleComponents.Contains(Ctx.PlacementSupportToIgnore))
		{
			Ctx.Candidate.FloorOnlyComponents.AddUnique(Ctx.PlacementSupportToIgnore);
		}
		Ctx.Candidate.ObstacleComponents.AddUnique(Ctx.PlacementSupportToIgnore);
	}

	for (const FIMGTraversalWarpBinding& Binding : Ctx.Settings.WarpBindings)
	{
		const FTransform* Transform = Ctx.WorldTargets.Find(Binding.TargetName);
		if (!Transform)
		{
			continue;
		}
		FTransform Anchor;
		UPrimitiveComponent* Component = nullptr;
		if (!Ctx.GetAnchor(Binding.Anchor, Anchor, Component))
		{
			return Ctx.Reject(
			    FString::Printf(TEXT("Warp target %s has no valid geometric anchor"), *Binding.TargetName.ToString()));
		}
		FIMGTraversalWarpTarget& Target = OutTargets.AddDefaulted_GetRef();
		if (Binding.Anchor == EIMGTraversalAnchor::BackFloor && !Ctx.Candidate.ObstacleComponents.Contains(Component))
		{
			Ctx.Candidate.FloorOnlyComponents.AddUnique(Component);
		}
		Target.Name = Binding.TargetName;
		Target.Component = Component;
		Target.LocalTransform = Transform->GetRelativeTransform(Component->GetComponentTransform());
		if (!Target.LocalTransform.IsValid())
		{
			return Ctx.Reject(TEXT("Warp carrier transform is invalid"));
		}
#if IMG_TRAVERSAL_DEBUG_ENABLED
		const FVector UncompensatedLocation = (Binding.Offset * Anchor).GetLocation();
		if (!Transform->GetLocation().Equals(UncompensatedLocation, KINDA_SMALL_NUMBER))
		{
			IMGTraversalDebug::Point(Ctx.Character.GetWorld(), UncompensatedLocation, FColor::Orange);
			IMGTraversalDebug::Point(Ctx.Character.GetWorld(), Transform->GetLocation(), FColor::Cyan);
			IMGTraversalDebug::Line(
			    Ctx.Character.GetWorld(), UncompensatedLocation, Transform->GetLocation(), FColor::Cyan, 3.f);
			if (IMGTraversalDebug::IsEnabled())
			{
				IMGTraversalDebug::MessageAt(Transform->GetLocation() + FVector::UpVector * 15.f,
				    FString::Printf(TEXT("%s placement %+.2f cm"), *Binding.TargetName.ToString(),
				        Transform->GetLocation().Z - UncompensatedLocation.Z),
				    IMGTraversalDebug::EStage::Execution);
			}
		}
		IMGTraversalDebug::Line(
		    Ctx.Character.GetWorld(), Anchor.GetLocation(), Transform->GetLocation(), FColor::Yellow);
		IMGTraversalDebug::Arrow(Ctx.Character.GetWorld(), Transform->GetLocation(),
		    Transform->GetLocation() + Transform->GetUnitAxis(EAxis::X) * 30.f, FColor::Yellow);
		if (IMGTraversalDebug::IsEnabled())
		{
			IMGTraversalDebug::MessageAt(Transform->GetLocation() + FVector::UpVector * 15.f,
			    FString::Printf(TEXT("%s <- %s + offset | %s"), *Binding.TargetName.ToString(),
			        *StaticEnum<EIMGTraversalAnchor>()->GetNameStringByValue(static_cast<int64>(Binding.Anchor)),
			        *GetNameSafe(Component)),
			    IMGTraversalDebug::EStage::Execution);
		}
#endif
	}
#if IMG_TRAVERSAL_DEBUG_ENABLED
	IMGTraversalDebug::PrepareHeightCapture(&Ctx.Character, Ctx.Selection, Ctx.InitialPose, Ctx.Playback.GetRanges());
#endif
	return true;
}
} // namespace

bool IMGTraversalPlacement::PrepareTargets(AIMGCharacter& InCharacter,
    const FIMGTraversalExecutionSettings& Settings, const FIMGTraversalDetectorSettings& Geometry,
    TConstArrayView<FIMGTraversalWarpWindow> Windows, FIMGTraversalCandidate& Candidate,
    const FIMGTraversalAnimationSelection& Selection, TArray<FIMGTraversalWarpTarget>& OutTargets,
    float& OutGroundHandoffTime, bool bVerboseDiagnostics,
    TFunctionRef<void(const TCHAR*, const FString&, bool)> LogEvent)
{
#if IMG_TRAVERSAL_DEBUG_ENABLED
	IMGTraversalDebug::FScope DebugScope(&InCharacter, IMGTraversalDebug::EPhase::Placement);
#endif
	OutTargets.Reset();
	OutGroundHandoffTime = -1.f;

	FPlacementContext Ctx{InCharacter, Settings, Geometry, Windows, Candidate, Selection, bVerboseDiagnostics, LogEvent};
	Ctx.InitialPose = InCharacter.GetActorTransform();
	Ctx.PathAnchor = Candidate.BackLedge;

	if (!ValidateBindingsAndCollectWindows(Ctx) || !ResolveExplicitLanding(Ctx) || !BuildInitialWorldTargets(Ctx) ||
	    !SelectEvaluationAndCompensationWindows(Ctx, OutGroundHandoffTime))
	{
		return false;
	}

	FIMGTraversalWarpPrediction Prediction(InCharacter, Selection, Ctx.Playback, Ctx.PlacementWindows, Ctx.InitialPose,
	    Ctx.bProjectSequence, bVerboseDiagnostics, Ctx.PredictionUnavailableReason, LogEvent);

	if (Ctx.CompensationWindow &&
	    (Ctx.EvaluationWindow == Ctx.CompensationWindow ||
	        Ctx.CompensationWindow->Modifier->WarpTargetName != Ctx.EvaluationWindow->Modifier->WarpTargetName ||
	        Ctx.Playback.GetWindowTimes().FindChecked(Ctx.EvaluationWindow).X >=
	            Ctx.Playback.GetWindowTimes().FindChecked(Ctx.CompensationWindow).Y))
	{
		// The contact owns support geometry even when an earlier target controls the height correction.
		const FIMGTraversalWarpBinding* Binding = Ctx.FindBinding(Ctx.EvaluationWindow->Modifier->WarpTargetName);
		FTransform Anchor;
		UPrimitiveComponent* Component = nullptr;
		if (Binding && Ctx.GetAnchor(Binding->Anchor, Anchor, Component))
		{
			CompensateSupportHeight(Ctx, Prediction, *Ctx.CompensationWindow, *Ctx.EvaluationWindow, Anchor, *Component);
		}
	}

	return ValidateExplicitLanding(Ctx, Prediction) && CommitOutputTargets(Ctx, OutTargets);
}
