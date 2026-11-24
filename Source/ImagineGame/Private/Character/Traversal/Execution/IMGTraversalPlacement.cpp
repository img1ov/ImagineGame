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
#include "Character/Traversal/Execution/IMGTraversalWarpPrediction.h"

bool IMGTraversalPlacement::PrepareTargets(AIMGCharacter& InCharacter,
    const FIMGTraversalExecutionSettings& Settings, const FIMGTraversalDetectorSettings& Geometry,
    TConstArrayView<FIMGTraversalWarpWindow> Windows, FIMGTraversalCandidate& Candidate,
    const FIMGTraversalAnimationSelection& Selection, TArray<FIMGTraversalWarpTarget>& OutTargets,
    bool bVerboseDiagnostics, TFunctionRef<void(const TCHAR*, const FString&, bool)> LogEvent)
{
#if IMG_TRAVERSAL_DEBUG_ENABLED
	IMGTraversalDebug::FScope DebugScope(&InCharacter, IMGTraversalDebug::EPhase::Placement);
#endif
	OutTargets.Reset();
	AIMGCharacter* Character = &InCharacter;
	constexpr double UprightTolerance = 1.e-3;
	auto LogTraversalEvent = [&LogEvent](const TCHAR* Event, const FString& Detail, bool bWarning = false) {
		LogEvent(Event, Detail, bWarning);
	};
	auto Reject = [&LogTraversalEvent, &Selection](const FString& Reason) {
		LogTraversalEvent(TEXT("WarpTargetsRejected"),
		    FString::Printf(TEXT("Montage=%s | %s"), *GetNameSafe(Selection.Montage.Get()), *Reason), true);
		return false;
	};
	auto GetAnchor = [&Candidate](
	                     EIMGTraversalAnchor Anchor, FTransform& Transform, UPrimitiveComponent*& Component) {
		switch (Anchor)
		{
			case EIMGTraversalAnchor::FrontLedge:
				Transform = Candidate.FrontLedge;
				Component = Candidate.FrontLedgeComponent;
				return Candidate.bHasFrontLedge && IsValid(Component);
			case EIMGTraversalAnchor::BackLedge:
				Transform = Candidate.BackLedge;
				Component = Candidate.BackLedgeComponent;
				return Candidate.bHasBackLedge && IsValid(Component);
			case EIMGTraversalAnchor::BackFloor:
				Transform = Candidate.BackFloor;
				Component = Candidate.BackFloorComponent;
				return Candidate.bHasBackFloor && IsValid(Component);
			default:
				return false;
		}
	};
	TSet<FName> BindingNames;
	for (const FIMGTraversalWarpBinding& Binding : Settings.WarpBindings)
	{
		if (Binding.TargetName.IsNone() || BindingNames.Contains(Binding.TargetName) ||
		    !Binding.Offset.IsValid())
		{
			return Reject(FString::Printf(
			    TEXT("Warp binding invalid or duplicated: %s"), *Binding.TargetName.ToString()));
		}
		BindingNames.Add(Binding.TargetName);
	}
	FIMGTraversalPlaybackPath Playback;
	FString PlaybackFailure;
	if (!Playback.Build(Selection, Windows, bVerboseDiagnostics, LogEvent, PlaybackFailure))
	{
		return Reject(PlaybackFailure);
	}
	const TArray<FVector2f>& PlaybackRanges = Playback.GetRanges();
	const TArray<float>& PlaybackOffsets = Playback.GetOffsets();
	const auto& WindowPlaybackTimes = Playback.GetWindowTimes();
	const auto& ReachableWindows = Playback.GetReachableWindows();
	const float PlaybackStart = Playback.GetStartTime();
	const float PlacementTime = Playback.GetPlacementTime();
	TSet<FName> UsedTargets;
	TArray<const FIMGTraversalWarpWindow*, TInlineAllocator<4>> PlacementWindows;
	const FIMGTraversalWarpWindow* FinalTranslationWindow = nullptr;
	bool bSequentialSkewWindows = true;
	const bool bNeedPredictionDetails =
	    bVerboseDiagnostics || Settings.Landing.Mode != EIMGTraversalLandingMode::None;
	TArray<FString> PredictionLimitations;
	// The native speed limit is reflected but has no public C++ getter.
	static const FFloatProperty* SpeedClampProperty = FindFProperty<FFloatProperty>(
	    URootMotionModifier_SkewWarp::StaticClass(), TEXT("MaxSpeedClampRatio"));
	for (const FIMGTraversalWarpWindow* WindowPtr : ReachableWindows)
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
				PredictionLimitations.Add(FString::Printf(
				    TEXT("Non-Warp modifier at %.6f..%.6f"), Window.StartTime, Window.EndTime));
			}
			continue;
		}
		const FName Name = Window.Modifier->WarpTargetName;
		if (!BindingNames.Contains(Name))
		{
			return Reject(FString::Printf(TEXT("Warp target has no geometry binding: %s"), *Name.ToString()));
		}
		UsedTargets.Add(Name);
		if (Window.Modifier->bWarpTranslation &&
		    (!FinalTranslationWindow || Times.Y >= WindowPlaybackTimes.FindChecked(FinalTranslationWindow).Y))
		{
			FinalTranslationWindow = &Window;
		}
		if (Window.Modifier->bWarpTranslation || Window.Modifier->bWarpRotation)
		{
			const bool bNativeSkew =
			    Window.Modifier->GetClass() == URootMotionModifier_SkewWarp::StaticClass();
			const float SpeedClamp = bNativeSkew && SpeedClampProperty
			    ? SpeedClampProperty->GetPropertyValue_InContainer(Window.Modifier)
			    : 1.f;
			bool bWindowPredictable = bNativeSkew && SpeedClampProperty && FMath::IsFinite(SpeedClamp) &&
			    SpeedClamp <= 0.f && Times.Y <= PlacementTime &&
			    (PlacementWindows.IsEmpty() ||
			        WindowPlaybackTimes.FindChecked(PlacementWindows.Last()).Y <= Times.X);
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
				    *Name.ToString(), Window.StartTime, Window.EndTime,
				    *GetNameSafe(Window.Modifier->GetClass()), SpeedClampProperty != nullptr, SpeedClamp,
				    Times.Y <= PlacementTime,
				    !PlacementWindows.IsEmpty() &&
				        WindowPlaybackTimes.FindChecked(PlacementWindows.Last()).Y > Times.X,
				    Window.Modifier->bWarpTranslation, Window.Modifier->bIgnoreZAxis,
				    Window.Modifier->bWarpRotation, *UEnum::GetValueAsString(Window.Modifier->RotationType),
				    *UEnum::GetValueAsString(Window.Modifier->RotationMethod),
				    Window.Modifier->WarpRotationTimeMultiplier));
				if (!PlacementWindows.IsEmpty() &&
				    WindowPlaybackTimes.FindChecked(PlacementWindows.Last()).Y > Times.X)
				{
					const FIMGTraversalWarpWindow& Previous = *PlacementWindows.Last();
					PredictionLimitations.Add(FString::Printf(
					    TEXT("OverlapWith=%s Window=%.6f..%.6f Translation=%d IgnoreZ=%d Rotation=%d"),
					    *Previous.Modifier->WarpTargetName.ToString(), Previous.StartTime, Previous.EndTime,
					    Previous.Modifier->bWarpTranslation, Previous.Modifier->bIgnoreZAxis,
					    Previous.Modifier->bWarpRotation));
				}
			}
			bSequentialSkewWindows &= bWindowPredictable;
			PlacementWindows.Add(&Window);
		}
	}
	const bool bSupportedTransform =
	    Character->GetActorScale3D().Equals(FVector::OneVector, UE_KINDA_SMALL_NUMBER) &&
	    Character->GetActorUpVector().Equals(FVector::UpVector, UprightTolerance) && Character->GetMesh() &&
	    Character->GetMesh()->GetComponentScale().Equals(FVector::OneVector, UE_KINDA_SMALL_NUMBER);
	if (!bSupportedTransform && bNeedPredictionDetails)
	{
		PredictionLimitations.Add(FString::Printf(TEXT("ActorScale=%s MeshScale=%s ActorUp=%s"),
		    *Character->GetActorScale3D().ToCompactString(),
		    Character->GetMesh() ? *Character->GetMesh()->GetComponentScale().ToCompactString()
		                         : TEXT("NoMesh"),
		    *Character->GetActorUpVector().ToCompactString()));
	}
	if (PlacementWindows.IsEmpty() && bNeedPredictionDetails)
	{
		PredictionLimitations.Add(TEXT("No remaining placement window"));
	}
	FString PredictionUnavailableReason = FString::Join(PredictionLimitations, TEXT("; "));
	const bool bProjectSequence =
	    bSequentialSkewWindows && bSupportedTransform && !PlacementWindows.IsEmpty();
	if (!bProjectSequence)
	{
		LogTraversalEvent(TEXT("PlacementPredictionUnavailable"), PredictionUnavailableReason);
	}
	for (FName Required : Settings.RequiredTargets)
	{
		if (!UsedTargets.Contains(Required))
		{
			return Reject(FString::Printf(TEXT("Required warp target %s has no remaining window at %.3f"),
			    *Required.ToString(), Selection.StartTime));
		}
	}
	auto FindBinding = [&Settings](FName Name) {
		return Settings.WarpBindings.FindByPredicate(
		    [Name](const FIMGTraversalWarpBinding& Binding) { return Binding.TargetName == Name; });
	};
	auto SelectWindow = [&Playback, Windows](FName Name, int32 Index, float MinEnd, float MaxEnd) {
		return Playback.SelectTranslationWindow(Windows, Name, Index, MinEnd, MaxEnd);
	};
	auto AlignmentTime = [&Selection](const FIMGTraversalWarpWindow& Window) {
		return Window.Modifier->bSubtractRemainingRootMotion ? Selection.Montage->GetPlayLength()
		                                                     : Window.EndTime;
	};
	const FTransform InitialPose = Character->GetActorTransform();
	FIMGTraversalWarpPrediction Prediction(*Character, Selection, Playback, PlacementWindows, InitialPose,
	    bProjectSequence, bVerboseDiagnostics, PredictionUnavailableReason, LogEvent);
	TMap<FName, FTransform> WorldTargets;
	const FIMGTraversalLandingSettings& Landing = Settings.Landing;
	if (Selection.Action != EIMGTraversalAction::Hurdle)
	{
		if (Landing.Mode != EIMGTraversalLandingMode::None)
		{
			return Reject(TEXT("Rear-floor landing requires Hurdle; Mantle climbs the top and Vault exits airborne"));
		}
		for (const FIMGTraversalWarpBinding& Binding : Settings.WarpBindings)
		{
			if (Binding.Anchor == EIMGTraversalAnchor::BackFloor && UsedTargets.Contains(Binding.TargetName))
			{
				return Reject(FString::Printf(TEXT("Only Hurdle can consume a BackFloor warp target: %s"),
				    *Binding.TargetName.ToString()));
			}
		}
	}
	auto IsBeyondBackLedge = [&Candidate](const FVector& Location) {
		return FVector::DotProduct(Location - Candidate.BackLedge.GetLocation(),
		    Candidate.BackLedge.GetUnitAxis(EAxis::X).GetSafeNormal2D()) >= 0.0;
	};
	TOptional<FVector> CompensatedPlacementCenter;
	const FIMGTraversalWarpWindow* LandingWindow = nullptr;
	UPrimitiveComponent* PlacementSupportToIgnore = nullptr;
	const TCHAR* CompensationSkipReason = TEXT("No supported height-adjusting window");
	auto CompensatePlacement = [&](const FIMGTraversalWarpWindow& Window,
	                               const FIMGTraversalWarpWindow& EvaluationWindow,
	                               const FTransform& Anchor, const UPrimitiveComponent& Carrier) {
		// Support belongs to the action's contact window; playback may continue with locomotion afterward.
		FVector Center;
		if (!Prediction.Project(EvaluationWindow, WorldTargets, Center))
		{
			CompensationSkipReason = TEXT("Placement projection is unavailable");
			return;
		}
		if (bVerboseDiagnostics)
		{
			LogTraversalEvent(TEXT("PlacementEvaluation"),
			    FString::Printf(
			        TEXT(
			            "Endpoint=%s StartTime=%.6f CorrectionTarget=%s ReferenceTarget=%s AssetWindowEnd=%.6f "
			            "EvaluationEnd=%.6f IncludeTail=0 Center=%s"),
			        Selection.Action == EIMGTraversalAction::Mantle ? TEXT("TopContactWindow")
			            : LandingWindow ? TEXT("LandingWindow") : TEXT("BackFloorWindow"),
			        Selection.StartTime,
			        *Window.Modifier->WarpTargetName.ToString(),
			        *EvaluationWindow.Modifier->WarpTargetName.ToString(), EvaluationWindow.EndTime,
			        WindowPlaybackTimes.FindChecked(&EvaluationWindow).Y, *Center.ToString()));
		}
		const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		const UIMGCharacterMovementComponent* Movement = Character->GetIMGMovementComponent();
		const bool bRequireTopSupport = Selection.Action == EIMGTraversalAction::Mantle;
		const double TopTolerance =
		    FMath::Max(static_cast<double>(Geometry.DepthHeightTolerance), UE_DOUBLE_KINDA_SMALL_NUMBER);
		const double TopMinZ = Candidate.TopSupportHeightRange.X - TopTolerance;
		const double TopMaxZ = Candidate.TopSupportHeightRange.Y + TopTolerance;
		auto IsPlacementSupport = [&](const FHitResult& Hit) {
			UPrimitiveComponent* Component = Hit.GetComponent();
			const bool bKnownSurface = Component &&
			    (Component == &Carrier || (bRequireTopSupport && Candidate.ObstacleComponents.Contains(Component)));
			// One collision component can contain both the obstacle top and the lower floor.
			const double Drop = Candidate.FrontLedge.GetLocation().Z - Hit.ImpactPoint.Z;
			const bool bInSupportLayer = bRequireTopSupport
			    ? Hit.ImpactPoint.Z >= TopMinZ && Hit.ImpactPoint.Z <= TopMaxZ
			    : IsBeyondBackLedge(Hit.ImpactPoint) && Drop >= 0.0 && Drop <= Geometry.MaxDrop;
			return bKnownSurface && bInSupportLayer && Hit.IsValidBlockingHit() &&
			    Movement->IsWalkable(Hit) &&
			    FIMGTraversalDetector::CanUseSurface(Component, Geometry, Candidate.RequestSource);
		};
		const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		const float Radius = Capsule->GetScaledCapsuleRadius();
#if IMG_TRAVERSAL_DEBUG_ENABLED
		IMGTraversalDebug::PlacementCapsule(Character->GetWorld(), Center, HalfHeight, Radius, FColor::Orange,
		    TEXT("Prepared placement before compensation"));
#endif
		const FVector Up = FVector::UpVector;
		const FVector Feet = Center - Up * HalfHeight;
		const float FloorGap =
		    (UCharacterMovementComponent::MIN_FLOOR_DIST + UCharacterMovementComponent::MAX_FLOOR_DIST) *
		    0.5f;
		const FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius, HalfHeight);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(IMGTraversalPlacementCompensation), false, Character);
		FCollisionResponseParams Responses;
		Movement->InitCollisionParams(Params, Responses);
		const ECollisionChannel Channel = Capsule->GetCollisionObjectType();
		auto GetMinNormalZ = [Movement](const UPrimitiveComponent& Surface) {
			const float WalkableZ =
			    Surface.GetWalkableSlopeOverride().ModifyWalkableFloorZ(Movement->GetWalkableFloorZ());
			return FMath::Clamp(
			    static_cast<double>(WalkableZ), static_cast<double>(UE_KINDA_SMALL_NUMBER), 1.0);
		};
		auto GetSupportHeightRange = [&Feet, Radius](const FVector& SupportLocation, double MinNormalZ) {
			const double MaxGrade = FMath::Sqrt(1.0 - FMath::Square(MinNormalZ)) / MinNormalZ;
			return (FVector::Dist2D(Feet, SupportLocation) + Radius) * MaxGrade;
		};
		double SupportTopZ = TopMaxZ;
		double SupportBottomZ = TopMinZ;
		if (!bRequireTopSupport)
		{
			// Reach the anchor's platform even when the projected feet are above it.
			const double MinNormalZ = GetMinNormalZ(Carrier);
			const FVector AnchorLocation = Anchor.GetLocation();
			const double SupportHeightRange = GetSupportHeightRange(AnchorLocation, MinNormalZ);
			const double RoundBottomReach = Radius * (1.0 / MinNormalZ - 1.0);
			SupportTopZ = AnchorLocation.Z + SupportHeightRange;
			SupportBottomZ = FMath::Min(Feet.Z - RoundBottomReach, AnchorLocation.Z - SupportHeightRange);
		}
		FHitResult Plane;
		const FVector TraceStart(Feet.X, Feet.Y, FMath::Max(Center.Z, SupportTopZ) + FloorGap);
		const FVector TraceEnd(Feet.X, Feet.Y, SupportBottomZ - UCharacterMovementComponent::MAX_FLOOR_DIST);
		if (TraceStart.ContainsNaN() || TraceEnd.ContainsNaN())
		{
			CompensationSkipReason = TEXT("Support trace is non-finite");
			return;
		}
		const bool bHit = IMG_TRAVERSAL_QUERY(Character->GetWorld()->LineTraceSingleByChannel(
		    Plane, TraceStart, TraceEnd, Channel, Params, Responses));
		const bool bHasPlaneSupport = bHit && IsPlacementSupport(Plane);
#if IMG_TRAVERSAL_DEBUG_ENABLED
		IMGTraversalDebug::Line(Character->GetWorld(), TraceStart, bHit ? Plane.ImpactPoint : TraceEnd,
		    bHasPlaneSupport ? FColor::Green : FColor::Red);
#endif
		if (!bHasPlaneSupport)
		{
			CompensationSkipReason = TEXT("No usable plane support on the selected layer");
			if (bVerboseDiagnostics)
			{
				LogTraversalEvent(TEXT("PlacementPlane"),
				    FString::Printf(
				        TEXT("Center=%s Feet=%s Hit=%d Component=%s Impact=%s Normal=%s Penetrating=%d "
				             "TopRequired=%d TopRange=%.2f..%.2f"),
				        *Center.ToCompactString(), *Feet.ToCompactString(), bHit,
				        *GetNameSafe(Plane.GetComponent()), *Plane.ImpactPoint.ToCompactString(),
				        *Plane.ImpactNormal.ToCompactString(), Plane.bStartPenetrating, bRequireTopSupport,
				        TopMinZ, TopMaxZ));
			}
			return;
		}
		const FVector Normal = Plane.ImpactNormal.GetSafeNormal();
		if (Normal.Z <= UE_KINDA_SMALL_NUMBER)
		{
			CompensationSkipReason = TEXT("Support plane has no usable upward normal");
			return;
		}
		// Estimate where the lower hemisphere clears the plane before sweeping the full capsule.
		const FVector BottomSphere = Center - Up * (HalfHeight - Radius);
		const double PlaneAdjustment =
		    (Radius - FVector::DotProduct(BottomSphere - Plane.ImpactPoint, Normal)) / Normal.Z + FloorGap;
		if (!FMath::IsFinite(PlaneAdjustment))
		{
			CompensationSkipReason = TEXT("Plane adjustment is non-finite");
			return;
		}
		FHitResult Support;
		// Start at the original center on downward corrections so intervening blockers are swept.
		const FVector SweepStart = Center + Up * FMath::Max(0.0, PlaneAdjustment);
		const FVector SweepEnd =
		    Plane.ImpactPoint + Up * (HalfHeight - UCharacterMovementComponent::MAX_FLOOR_DIST);
		const bool bHasSupport = IMG_TRAVERSAL_QUERY(Character->GetWorld()->SweepSingleByChannel(
		    Support, SweepStart, SweepEnd, FQuat::Identity, Channel, Shape, Params, Responses));
		if (!bHasSupport || !IsPlacementSupport(Support) ||
		    !Movement->IsWithinEdgeTolerance(Support.Location, Support.ImpactPoint, Radius))
		{
			CompensationSkipReason = TEXT("Full capsule sweep has no usable support or fails edge tolerance");
			if (bVerboseDiagnostics)
			{
				LogTraversalEvent(TEXT("PlacementCapsuleSupport"),
				    FString::Printf(
				        TEXT("Center=%s SweepStart=%s SweepEnd=%s Hit=%d Component=%s Impact=%s Normal=%s "
				             "Penetrating=%d"),
				        *Center.ToCompactString(), *SweepStart.ToCompactString(), *SweepEnd.ToCompactString(),
				        bHasSupport, *GetNameSafe(Support.GetComponent()),
				        *Support.ImpactPoint.ToCompactString(), *Support.ImpactNormal.ToCompactString(),
				        Support.bStartPenetrating));
			}
			return;
		}
		const FVector SafeCenter = Support.Location + Up * FloorGap;
		const double HeightAdjustment = SafeCenter.Z - Center.Z;
		if (!FMath::IsFinite(HeightAdjustment) ||
		    IMG_TRAVERSAL_QUERY(Character->GetWorld()->OverlapBlockingTestByChannel(
		        SafeCenter, FQuat::Identity, Channel, Shape, Params, Responses)))
		{
#if IMG_TRAVERSAL_DEBUG_ENABLED
			IMGTraversalDebug::PlacementCapsule(Character->GetWorld(), SafeCenter, HalfHeight, Radius,
			    FColor::Red, TEXT("Rejected placement after compensation"));
#endif
			CompensationSkipReason = TEXT("Corrected capsule is blocked or its height is non-finite");
			return;
		}
		FTransform& WarpTarget = WorldTargets.FindChecked(Window.Modifier->WarpTargetName);
		const FVector OriginalLocation = WarpTarget.GetLocation();
		FVector TargetLocation = OriginalLocation;
		const double CenterOffsetZ = Center.Z - TargetLocation.Z;
		TargetLocation.Z = SafeCenter.Z - CenterOffsetZ;
		WarpTarget.SetLocation(TargetLocation);
		if (bProjectSequence && HeightAdjustment != 0.0)
		{
			// A named target can affect several windows, including ones before the height window.
			FVector ReprojectedCenter;
			if (!Prediction.Project(EvaluationWindow, WorldTargets, ReprojectedCenter) ||
			    !ReprojectedCenter.Equals(SafeCenter, UE_KINDA_SMALL_NUMBER))
			{
				WarpTarget.SetLocation(OriginalLocation);
#if IMG_TRAVERSAL_DEBUG_ENABLED
				IMGTraversalDebug::PlacementCapsule(Character->GetWorld(), SafeCenter, HalfHeight, Radius,
				    FColor::Red, TEXT("Rejected placement after compensation"));
#endif
				CompensationSkipReason = TEXT("Corrected target fails placement-path reprojection");
				LogTraversalEvent(TEXT("PlacementReprojectionRejected"),
				    TEXT("Height correction changes the predicted landing"));
				return;
			}
		}
		CompensatedPlacementCenter = SafeCenter;
		CompensationSkipReason = nullptr;
		if (HeightAdjustment > UE_DOUBLE_SMALL_NUMBER)
		{
			PlacementSupportToIgnore = Support.GetComponent();
		}
#if IMG_TRAVERSAL_DEBUG_ENABLED

		IMGTraversalDebug::PlacementCapsule(Character->GetWorld(), SafeCenter, HalfHeight, Radius,
		    FColor::Green, TEXT("Validated placement after compensation"));
		if (IMGTraversalDebug::IsEnabled())
		{
			IMGTraversalDebug::MessageAt(SafeCenter + Up * HalfHeight,
			    FString::Printf(
			        TEXT("Safe capsule Z %.2f | WarpTarget Z %.2f"), SafeCenter.Z, TargetLocation.Z),
			    IMGTraversalDebug::EStage::Execution);
		}
#endif
	};
	FTransform PathAnchor = Candidate.BackLedge;
	if (static_cast<uint8>(Landing.Mode) > static_cast<uint8>(EIMGTraversalLandingMode::DistanceCurve))
	{
		return Reject(TEXT("Landing mode is invalid"));
	}
	if (Landing.Mode != EIMGTraversalLandingMode::None)
	{
		if (!FMath::IsFinite(Landing.CapsuleClearance) || Landing.CapsuleClearance < 0.f)
		{
			return Reject(TEXT("Landing capsule clearance is invalid"));
		}
		const FIMGTraversalWarpBinding* LandingBinding = FindBinding(Landing.TargetName);
		LandingWindow =
		    SelectWindow(Landing.TargetName, Landing.LandingWindowIndex, PlaybackStart, PlacementTime);
		if (!LandingBinding || LandingBinding->Anchor != EIMGTraversalAnchor::BackFloor || !LandingWindow)
		{
			return Reject(TEXT("Landing needs a BackFloor binding and a remaining translation window"));
		}
		if (!bProjectSequence)
		{
			return Reject(FString::Printf(
			    TEXT("Selected landing needs a supported, complete sequence of Skew Warp windows: %s"),
			    *PredictionUnavailableReason));
		}
		FVector LandingPoint = Candidate.BackFloor.GetLocation();
		if (Landing.Mode == EIMGTraversalLandingMode::DistanceCurve)
		{
			const FIMGTraversalWarpWindow* AnchorWindow = SelectWindow(Landing.AnchorTargetName,
			    Landing.AnchorWindowIndex, -1.f, WindowPlaybackTimes.FindChecked(LandingWindow).Y);
			if (!AnchorWindow && !SelectWindow(Landing.AnchorTargetName, -1, -1.f, PlacementTime))
			{
				AnchorWindow = SelectWindow(Landing.FallbackAnchorTargetName, Landing.AnchorWindowIndex, -1.f,
				    WindowPlaybackTimes.FindChecked(LandingWindow).Y);
			}
			const FIMGTraversalWarpBinding* AnchorBinding =
			    AnchorWindow ? FindBinding(AnchorWindow->Modifier->WarpTargetName) : nullptr;
			UPrimitiveComponent* AnchorComponent = nullptr;
			if (!AnchorBinding || AnchorBinding->Anchor == EIMGTraversalAnchor::BackFloor ||
			    !GetAnchor(AnchorBinding->Anchor, PathAnchor, AnchorComponent) ||
			    WindowPlaybackTimes.FindChecked(AnchorWindow).Y >=
			        WindowPlaybackTimes.FindChecked(LandingWindow).Y)
			{
				return Reject(
				    TEXT("Landing contact window is missing, ambiguous in order, or has no geometry"));
			}
			float CurveDistance = 0.f;
			auto AccumulateCurveDistance = [&](float From, float To, float Direction) {
				if (From == To)
				{
					return true;
				}
				float FromValue = 0.f;
				float ToValue = 0.f;
				if (!UAnimationWarpingLibrary::GetCurveValueFromAnimation(
				        Selection.Montage, Landing.DistanceCurveName, From, FromValue) ||
				    !UAnimationWarpingLibrary::GetCurveValueFromAnimation(
				        Selection.Montage, Landing.DistanceCurveName, To, ToValue) ||
				    !FMath::IsFinite(FromValue) || !FMath::IsFinite(ToValue))
				{
					return false;
				}
				CurveDistance += Direction * (ToValue - FromValue);
				return FMath::IsFinite(CurveDistance);
			};
			const float ContactTime = WindowPlaybackTimes.FindChecked(AnchorWindow).Y;
			const float LandingTime = WindowPlaybackTimes.FindChecked(LandingWindow).Y;
			bool bValidCurve = true;
			for (int32 RangeIndex = 0; RangeIndex < PlaybackRanges.Num(); ++RangeIndex)
			{
				const FVector2f& Range = PlaybackRanges[RangeIndex];
				const float Offset = PlaybackOffsets[RangeIndex];
				const float From = FMath::Max(ContactTime, Offset);
				const float To = FMath::Min(LandingTime, Offset + Range.Y - Range.X);
				if (To > From)
				{
					bValidCurve &=
					    AccumulateCurveDistance(FMath::Clamp(Range.X + From - Offset, Range.X, Range.Y),
					        FMath::Clamp(Range.X + To - Offset, Range.X, Range.Y), 1.f);
				}
			}
			// Preserve the target's native tail-subtraction reference independently of the played path.
			bValidCurve &= AccumulateCurveDistance(AnchorWindow->EndTime, AlignmentTime(*AnchorWindow), -1.f);
			bValidCurve &=
			    AccumulateCurveDistance(LandingWindow->EndTime, AlignmentTime(*LandingWindow), 1.f);
			if (!bValidCurve)
			{
				return Reject(FString::Printf(TEXT("Landing curve %s is missing or non-finite"),
				    *Landing.DistanceCurveName.ToString()));
			}
			const float Distance = Landing.bAbsoluteCurveDistance ? FMath::Abs(CurveDistance) : CurveDistance;
			LandingPoint =
			    PathAnchor.GetLocation() + PathAnchor.GetUnitAxis(EAxis::X).GetSafeNormal2D() * Distance;
#if IMG_TRAVERSAL_DEBUG_ENABLED
			if (IMGTraversalDebug::IsEnabled())
			{
				IMGTraversalDebug::MessageAt(LandingPoint + FVector::UpVector * 25.f,
				    FString::Printf(TEXT("Landing %s %.3f -> %s %.3f | Path curve delta %.1f"),
				        *AnchorWindow->Modifier->WarpTargetName.ToString(), AnchorWindow->EndTime,
				        *Landing.TargetName.ToString(), LandingWindow->EndTime, CurveDistance),
				    IMGTraversalDebug::EStage::Execution);
			}
#endif
		}
		else if (!Candidate.bHasBackFloor)
		{
			return Reject(TEXT("Detected-floor landing has no sampled walkable support"));
		}
		const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		const float TopZ =
		    FMath::Max(Candidate.FrontLedge.GetLocation().Z, Candidate.BackLedge.GetLocation().Z);
		const FVector TraceStart(LandingPoint.X, LandingPoint.Y, TopZ + HalfHeight);
		const FVector TraceEnd(
		    LandingPoint.X, LandingPoint.Y, Candidate.FrontLedge.GetLocation().Z - Geometry.MaxDrop);
		FHitResult Floor;
		FCollisionQueryParams FloorParams(SCENE_QUERY_STAT(IMGTraversalSelectedLanding), false, Character);
		FCollisionResponseParams Responses;
		Character->GetIMGMovementComponent()->InitCollisionParams(FloorParams, Responses);
		const bool bHit = IMG_TRAVERSAL_QUERY(Character->GetWorld()->LineTraceSingleByChannel(
		    Floor, TraceStart, TraceEnd, Geometry.TraceChannel, FloorParams, Responses));
#if IMG_TRAVERSAL_DEBUG_ENABLED
		IMGTraversalDebug::Line(Character->GetWorld(), TraceStart, bHit ? Floor.ImpactPoint : TraceEnd,
		    bHit ? FColor::Green : FColor::Red);
		if (bHit)
		{
			IMGTraversalDebug::Intersection(Character->GetWorld(), Floor,
			    IsBeyondBackLedge(Floor.ImpactPoint) && Character->GetIMGMovementComponent()->IsWalkable(Floor) &&
			        FIMGTraversalDetector::CanUseSurface(
			            Floor.GetComponent(), Geometry, Candidate.RequestSource),
			    TEXT("Animation-selected floor"), IMGTraversalDebug::EStage::Execution);
		}
#endif
		UPrimitiveComponent* FloorComponent = Floor.GetComponent();
		if (!bHit || !IsBeyondBackLedge(Floor.ImpactPoint) ||
		    !Character->GetIMGMovementComponent()->IsWalkable(Floor) ||
		    !FIMGTraversalDetector::CanUseSurface(FloorComponent, Geometry, Candidate.RequestSource))
		{
			return Reject(TEXT("Selected landing floor is unusable or is not beyond the rear ledge"));
		}
		const float Drop = Candidate.FrontLedge.GetLocation().Z - Floor.ImpactPoint.Z;
		if (Drop < 0.f || Drop > Geometry.MaxDrop)
		{
			return Reject(TEXT("Selected landing exceeds the configured drop range"));
		}
		LandingPoint.Z = Floor.ImpactPoint.Z;
		Candidate.BackFloor.SetRotation(PathAnchor.GetRotation());
		Candidate.BackFloor.SetLocation(LandingPoint);
		Candidate.BackFloorComponent = FloorComponent;
		Candidate.bHasBackFloor = true;
		Candidate.DropHeight = Drop;
		Candidate.BackLedgeHeight =
		    Candidate.bHasBackLedge ? Candidate.BackLedge.GetLocation().Z - LandingPoint.Z : 0.f;
	}
	for (const FIMGTraversalWarpBinding& Binding : Settings.WarpBindings)
	{
		if (!UsedTargets.Contains(Binding.TargetName))
		{
			continue;
		}
		FTransform Anchor;
		UPrimitiveComponent* Component = nullptr;
		if (!GetAnchor(Binding.Anchor, Anchor, Component))
		{
			return Reject(FString::Printf(
			    TEXT("Warp target %s has no valid geometric anchor"), *Binding.TargetName.ToString()));
		}
		const FTransform Transform = Binding.Offset * Anchor;
		if (!Transform.IsValid())
		{
			return Reject(TEXT("Resolved warp transform is invalid"));
		}
		WorldTargets.Add(Binding.TargetName, Transform);
	}
	const FIMGTraversalWarpWindow* EvaluationWindow = LandingWindow;
	if (Selection.Action == EIMGTraversalAction::Mantle)
	{
		EvaluationWindow = FinalTranslationWindow;
	}
	else if (Selection.Action == EIMGTraversalAction::Hurdle && !LandingWindow)
	{
		for (const FIMGTraversalWarpWindow* Window : PlacementWindows)
		{
			const FIMGTraversalWarpBinding* Binding = FindBinding(Window->Modifier->WarpTargetName);
			if (Window->Modifier->bWarpTranslation && Binding && Binding->Anchor == EIMGTraversalAnchor::BackFloor)
			{
				EvaluationWindow = Window;
			}
		}
	}
	const FIMGTraversalWarpWindow* CompensationWindow = nullptr;
	if (bProjectSequence && EvaluationWindow)
	{
		// A later height window cannot correct an earlier contact, regardless of action or target name.
		const float EvaluationEnd = WindowPlaybackTimes.FindChecked(EvaluationWindow).Y;
		for (const FIMGTraversalWarpWindow* Window : PlacementWindows)
		{
			if (Window->Modifier->bWarpTranslation && !Window->Modifier->bIgnoreZAxis &&
			    WindowPlaybackTimes.FindChecked(Window).Y <= EvaluationEnd)
			{
				CompensationWindow = Window;
			}
		}
	}
	if (CompensationWindow &&
	    (EvaluationWindow == CompensationWindow ||
	        CompensationWindow->Modifier->WarpTargetName != EvaluationWindow->Modifier->WarpTargetName ||
	        WindowPlaybackTimes.FindChecked(EvaluationWindow).X >=
	            WindowPlaybackTimes.FindChecked(CompensationWindow).Y))
	{
		// The contact owns support geometry even when an earlier target controls the height correction.
		const FIMGTraversalWarpBinding* Binding = FindBinding(EvaluationWindow->Modifier->WarpTargetName);
		FTransform Anchor;
		UPrimitiveComponent* Component = nullptr;
		if (Binding && GetAnchor(Binding->Anchor, Anchor, Component))
		{
			CompensatePlacement(*CompensationWindow, *EvaluationWindow, Anchor, *Component);
		}
	}
	if (LandingWindow)
	{
		FVector PlacementCenter;
		if (CompensatedPlacementCenter.IsSet())
		{
			PlacementCenter = CompensatedPlacementCenter.GetValue();
		}
		else if (!Prediction.Project(*LandingWindow, WorldTargets, PlacementCenter))
		{
			return Reject(TEXT("Landing warp-point or root transform is invalid"));
		}
		if (bVerboseDiagnostics)
		{
			LogTraversalEvent(TEXT("LandingEvaluation"),
			    FString::Printf(
			        TEXT(
			            "Target=%s StartTime=%.6f AssetWindowEnd=%.6f EvaluationEnd=%.6f IncludeTail=0 Center=%s"),
			        *LandingWindow->Modifier->WarpTargetName.ToString(), Selection.StartTime,
			        LandingWindow->EndTime, WindowPlaybackTimes.FindChecked(LandingWindow).Y,
			        *PlacementCenter.ToString()));
		}
		const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		const float Radius = Capsule->GetScaledCapsuleRadius();
		FCollisionQueryParams FloorParams(SCENE_QUERY_STAT(IMGTraversalSelectedLanding), false, Character);
		FCollisionResponseParams Responses;
		Character->GetIMGMovementComponent()->InitCollisionParams(FloorParams, Responses);
		const FVector LandingCenter = PlacementCenter + FVector::UpVector * Landing.CapsuleClearance;
		const bool bBlocked = IMG_TRAVERSAL_QUERY(Character->GetWorld()->OverlapBlockingTestByChannel(
		    LandingCenter, FQuat::Identity, Capsule->GetCollisionObjectType(),
		    FCollisionShape::MakeCapsule(Radius, HalfHeight), FloorParams, Responses));
#if IMG_TRAVERSAL_DEBUG_ENABLED
		IMGTraversalDebug::PlacementCapsule(Character->GetWorld(), LandingCenter, HalfHeight, Radius,
		    bBlocked ? FColor::Red : FColor::Green, TEXT("Selected landing clearance"));
#endif
		if (bBlocked)
		{
			return Reject(TEXT("Selected landing capsule is blocked"));
		}
		// An animation warp-point offset may place the capsule beyond the floor traced at the contact point.
		FFindFloorResult RootFloor;
		const float FloorDistance = Landing.CapsuleClearance + UCharacterMovementComponent::MAX_FLOOR_DIST;
		Character->GetIMGMovementComponent()->ComputeFloorDist(
		    LandingCenter, FloorDistance, FloorDistance, RootFloor, Radius);
		UPrimitiveComponent* RootSupport = RootFloor.HitResult.GetComponent();
		const float RootDrop = Candidate.FrontLedge.GetLocation().Z - RootFloor.HitResult.ImpactPoint.Z;
#if IMG_TRAVERSAL_DEBUG_ENABLED
		IMGTraversalDebug::Line(
		    Character->GetWorld(), Candidate.BackFloor.GetLocation(), PlacementCenter, FColor::Yellow);
		if (IMGTraversalDebug::IsEnabled())
		{
			IMGTraversalDebug::MessageAt(LandingCenter + FVector::UpVector * HalfHeight,
			    FString::Printf(TEXT("Landing contact -> binding/warp-point -> capsule | floor reach %.1f"),
			        FloorDistance),
			    IMGTraversalDebug::EStage::Execution);
		}
		if (RootFloor.bBlockingHit)
		{
			IMGTraversalDebug::Intersection(Character->GetWorld(), RootFloor.HitResult,
			    RootFloor.IsWalkableFloor() && RootDrop >= 0.f && RootDrop <= Geometry.MaxDrop &&
			        IsBeyondBackLedge(RootFloor.HitResult.ImpactPoint) &&
			        FIMGTraversalDetector::CanUseSurface(RootSupport, Geometry, Candidate.RequestSource),
			    TEXT("Actual capsule support"), IMGTraversalDebug::EStage::Execution);
		}
#endif
		if (!RootFloor.IsWalkableFloor() || RootDrop < 0.f || RootDrop > Geometry.MaxDrop ||
		    !IsBeyondBackLedge(RootFloor.HitResult.ImpactPoint) ||
		    !FIMGTraversalDetector::CanUseSurface(RootSupport, Geometry, Candidate.RequestSource))
		{
			return Reject(TEXT("Selected landing capsule has no usable floor support beyond the rear ledge"));
		}
		if (Landing.bCheckStraightPath)
		{
			FCollisionQueryParams PathParams(FloorParams);
			for (const UPrimitiveComponent* Component : Candidate.ObstacleComponents)
			{
				PathParams.AddIgnoredComponent(Component);
			}
			if (LandingWindow == CompensationWindow && PlacementSupportToIgnore)
			{
				PathParams.AddIgnoredComponent(PlacementSupportToIgnore);
			}
			FHitResult PathHit;
			const FVector Start =
			    PathAnchor.GetLocation() + FVector::UpVector * (HalfHeight + Landing.CapsuleClearance);
			const bool bPathBlocked = IMG_TRAVERSAL_QUERY(Character->GetWorld()->SweepSingleByChannel(PathHit,
			    Start, LandingCenter, FQuat::Identity, Capsule->GetCollisionObjectType(),
			    FCollisionShape::MakeCapsule(Radius, HalfHeight), PathParams, Responses));
#if IMG_TRAVERSAL_DEBUG_ENABLED
			IMGTraversalDebug::Capsule(Character->GetWorld(), Start, HalfHeight, Radius, FColor::Cyan);
			IMGTraversalDebug::Line(
			    Character->GetWorld(), Start, LandingCenter, bPathBlocked ? FColor::Red : FColor::Cyan);
			if (bPathBlocked)
			{
				IMGTraversalDebug::Intersection(Character->GetWorld(), PathHit, false,
				    TEXT("Optional straight capsule path"), IMGTraversalDebug::EStage::Execution);
			}
#endif
			if (bPathBlocked)
			{
				return Reject(TEXT("Configured straight landing path is blocked"));
			}
		}
		Candidate.BackFloorComponent = RootSupport;
		Candidate.LocalBackFloor =
		    Candidate.BackFloor.GetRelativeTransform(RootSupport->GetComponentTransform());
	}
	if (!LandingWindow && (Selection.Action == EIMGTraversalAction::Mantle || EvaluationWindow))
	{
		if (!bProjectSequence && bVerboseDiagnostics)
		{
			// Optional compensation limits do not invalidate the authored traversal trajectory.
			LogTraversalEvent(TEXT("PlacementPredictionFallback"),
			    FString::Printf(TEXT("Montage=%s Start=%.3f: retaining authored targets without placement "
			                         "compensation/contact prediction; %s"),
			        *GetNameSafe(Selection.Montage.Get()), Selection.StartTime,
			        PredictionUnavailableReason.IsEmpty() ? TEXT("No resolvable contact window")
			                                              : *PredictionUnavailableReason));
		}
		else if (CompensationSkipReason && bVerboseDiagnostics)
		{
			LogTraversalEvent(TEXT("PlacementCompensationSkipped"),
			    FString::Printf(TEXT("Montage=%s Start=%.3f: retaining authored targets; %s | %s"),
			        *GetNameSafe(Selection.Montage.Get()), Selection.StartTime, CompensationSkipReason,
			        *PredictionUnavailableReason));
		}
	}
	if (PlacementSupportToIgnore)
	{
		Candidate.ObstacleComponents.AddUnique(PlacementSupportToIgnore);
	}
	for (const FIMGTraversalWarpBinding& Binding : Settings.WarpBindings)
	{
		const FTransform* Transform = WorldTargets.Find(Binding.TargetName);
		if (!Transform)
		{
			continue;
		}
		FTransform Anchor;
		UPrimitiveComponent* Component = nullptr;
		if (!GetAnchor(Binding.Anchor, Anchor, Component))
		{
			return Reject(FString::Printf(
			    TEXT("Warp target %s has no valid geometric anchor"), *Binding.TargetName.ToString()));
		}
		FIMGTraversalWarpTarget& Target = OutTargets.AddDefaulted_GetRef();
		Target.Name = Binding.TargetName;
		Target.Component = Component;
		Target.LocalTransform = Transform->GetRelativeTransform(Component->GetComponentTransform());
		if (!Target.LocalTransform.IsValid())
		{
			return Reject(TEXT("Warp carrier transform is invalid"));
		}
#if IMG_TRAVERSAL_DEBUG_ENABLED
		const FVector UncompensatedLocation = (Binding.Offset * Anchor).GetLocation();
		if (!Transform->GetLocation().Equals(UncompensatedLocation, KINDA_SMALL_NUMBER))
		{
			IMGTraversalDebug::Point(Character->GetWorld(), UncompensatedLocation, FColor::Orange);
			IMGTraversalDebug::Point(Character->GetWorld(), Transform->GetLocation(), FColor::Cyan);
			IMGTraversalDebug::Line(
			    Character->GetWorld(), UncompensatedLocation, Transform->GetLocation(), FColor::Cyan, 3.f);
			if (IMGTraversalDebug::IsEnabled())
			{
				IMGTraversalDebug::MessageAt(Transform->GetLocation() + FVector::UpVector * 15.f,
				    FString::Printf(TEXT("%s placement %+.2f cm"), *Binding.TargetName.ToString(),
				        Transform->GetLocation().Z - UncompensatedLocation.Z),
				    IMGTraversalDebug::EStage::Execution);
			}
		}
		IMGTraversalDebug::Line(
		    Character->GetWorld(), Anchor.GetLocation(), Transform->GetLocation(), FColor::Yellow);
		IMGTraversalDebug::Arrow(Character->GetWorld(), Transform->GetLocation(),
		    Transform->GetLocation() + Transform->GetUnitAxis(EAxis::X) * 30.f, FColor::Yellow);
		if (IMGTraversalDebug::IsEnabled())
		{
			IMGTraversalDebug::MessageAt(Transform->GetLocation() + FVector::UpVector * 15.f,
			    FString::Printf(TEXT("%s <- %s + offset | %s"), *Binding.TargetName.ToString(),
			        *StaticEnum<EIMGTraversalAnchor>()->GetNameStringByValue(
			            static_cast<int64>(Binding.Anchor)),
			        *GetNameSafe(Component)),
			    IMGTraversalDebug::EStage::Execution);
		}
#endif
	}
#if IMG_TRAVERSAL_DEBUG_ENABLED
	IMGTraversalDebug::PrepareHeightCapture(Character, Selection, InitialPose, PlaybackRanges);
#endif
	return true;
}
