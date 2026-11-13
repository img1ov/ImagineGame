#include "IMGTraversalPlacement.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "AnimationWarpingLibrary.h"
#include "BoneContainer.h"
#include "Character/IMGCharacter.h"
#include "Character/IMGCharacterMovementComponent.h"
#include "Character/Traversal/IMGTraversalComponent.h"
#include "Character/Traversal/IMGTraversalDetector.h"
#include "Character/Traversal/IMGTraversalTypes.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Math/RotationMatrix.h"
#include "MotionWarpingComponent.h"
#include "RootMotionModifier.h"
#include "RootMotionModifier_SkewWarp.h"
#include "UObject/UnrealType.h"

#include "IMGTraversalDebug.h"

bool IMGTraversalPlacement::PrepareTargets(AIMGCharacter& InCharacter,
    const FIMGTraversalAnimationSettings& Settings, const FIMGTraversalDetectorSettings& Geometry,
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
	const UAnimMontage& Montage = *Selection.Montage;
	const float MontageLength = Montage.GetPlayLength();
	const float PlaybackRate = Selection.PlayRate * Montage.RateScale;
	if (!FMath::IsFinite(MontageLength) || !FMath::IsFinite(PlaybackRate) || PlaybackRate <= 0.f)
	{
		return Reject(TEXT("Montage length or effective play rate is invalid"));
	}
	if (!Montage.bEnableAutoBlendOut || !FMath::IsFinite(Montage.BlendOutTriggerTime) ||
	    !FMath::IsFinite(Montage.BlendOut.GetBlendTime()) || Montage.BlendOut.GetBlendTime() < 0.f)
	{
		return Reject(
		    TEXT("Traversal needs automatic montage blend-out with finite timing to release movement"));
	}
	TArray<FVector2f> PlaybackRanges;
	TArray<float> PlaybackOffsets;
	TSet<int32> VisitedSections;
	int32 SectionIndex = Montage.GetSectionIndexFromPosition(Selection.StartTime);
	float PlaybackEnd = 0.f;
	float TerminalSectionStart = 0.f;
	while (Montage.IsValidSectionIndex(SectionIndex))
	{
		if (VisitedSections.Contains(SectionIndex))
		{
			return Reject(TEXT("Traversal needs a finite section path; the montage section chain loops"));
		}
		VisitedSections.Add(SectionIndex);
		float SectionStart = 0.f;
		float SectionEnd = 0.f;
		Montage.GetSectionStartAndEndTime(SectionIndex, SectionStart, SectionEnd);
		if (!FMath::IsFinite(SectionStart) || !FMath::IsFinite(SectionEnd) || SectionStart < 0.f ||
		    SectionEnd <= SectionStart || SectionEnd > MontageLength)
		{
			return Reject(TEXT("Montage section has an invalid time range"));
		}
		TerminalSectionStart = PlaybackEnd;
		// Adjacent sections form one extraction range. Jumps retain separate animation intervals.
		if (!PlaybackRanges.IsEmpty() && PlaybackRanges.Last().Y == SectionStart)
		{
			PlaybackRanges.Last().Y = SectionEnd;
		}
		else
		{
			PlaybackRanges.Emplace(SectionStart, SectionEnd);
			PlaybackOffsets.Add(PlaybackEnd);
		}
		if (bVerboseDiagnostics)
		{
			LogTraversalEvent(TEXT("PlaybackSection"),
			    FString::Printf(TEXT("Section=%s Range=%.6f..%.6f PlaybackOffset=%.6f Next=%s"),
			        *Montage.GetSectionName(SectionIndex).ToString(), SectionStart, SectionEnd, PlaybackEnd,
			        *Montage.CompositeSections[SectionIndex].NextSectionName.ToString()));
		}
		PlaybackEnd += SectionEnd - SectionStart;
		const FName NextSection = Montage.CompositeSections[SectionIndex].NextSectionName;
		if (NextSection.IsNone())
		{
			break;
		}
		SectionIndex = Montage.GetSectionIndex(NextSection);
		if (!Montage.IsValidSectionIndex(SectionIndex))
		{
			return Reject(TEXT("Montage section links to an unknown next section"));
		}
	}
	if (PlaybackRanges.IsEmpty() || !FMath::IsFinite(PlaybackEnd))
	{
		return Reject(TEXT("Montage start time has no valid section path"));
	}
	const float PlaybackStart = Selection.StartTime - PlaybackRanges[0].X;
	float PlacementTime = PlaybackEnd;
	if (Selection.Montage->bEnableAutoBlendOut && Selection.Montage->BlendOutTriggerTime >= 0.f)
	{
		const float BlendStart = FMath::Max(FMath::Max(PlaybackStart, TerminalSectionStart),
		    PlaybackEnd - Selection.Montage->BlendOutTriggerTime * PlaybackRate);
		PlacementTime =
		    FMath::Min(PlaybackEnd, BlendStart + Selection.Montage->BlendOut.GetBlendTime() * PlaybackRate);
	}
	if (bVerboseDiagnostics)
	{
		LogTraversalEvent(TEXT("PlaybackPath"),
		    FString::Printf(TEXT("Montage=%s Length=%.6f StartTime=%.6f PlaybackStart=%.6f PlaybackEnd=%.6f "
		                         "PlacementTime=%.6f Ranges=%d"),
		        *GetNameSafe(Selection.Montage.Get()), MontageLength, Selection.StartTime, PlaybackStart,
		        PlaybackEnd, PlacementTime, PlaybackRanges.Num()));
	}
	TMap<const FIMGTraversalWarpWindow*, FVector2f> WindowPlaybackTimes;
	TArray<const FIMGTraversalWarpWindow*> ReachableWindows;
	for (const FIMGTraversalWarpWindow& Window : Windows)
	{
		if (!FMath::IsFinite(Window.StartTime) || !FMath::IsFinite(Window.EndTime) ||
		    Window.EndTime <= Window.StartTime || Window.StartTime < 0.f || Window.EndTime > MontageLength)
		{
			return Reject(TEXT("Warp window has an invalid time range"));
		}
		for (int32 RangeIndex = 0; RangeIndex < PlaybackRanges.Num(); ++RangeIndex)
		{
			const FVector2f& Range = PlaybackRanges[RangeIndex];
			const float EntryTime = RangeIndex == 0 ? Selection.StartTime : Range.X;
			if (Window.EndTime <= Range.X || Window.StartTime >= Range.Y)
			{
				continue;
			}
			if (Window.StartTime < Range.X || Window.EndTime > Range.Y)
			{
				if (Window.EndTime > EntryTime &&
				    PlaybackOffsets[RangeIndex] + FMath::Max(Window.StartTime - Range.X, 0.f) < PlacementTime)
				{
					return Reject(TEXT("Warp window crosses a discontinuous section boundary"));
				}
				continue;
			}
			WindowPlaybackTimes.Add(&Window,
			    FVector2f(PlaybackOffsets[RangeIndex] + (Window.StartTime - Range.X),
			        PlaybackOffsets[RangeIndex] + (Window.EndTime - Range.X)));
			ReachableWindows.Add(&Window);
			break;
		}
	}
	ReachableWindows.StableSort(
	    [&WindowPlaybackTimes](const FIMGTraversalWarpWindow& A, const FIMGTraversalWarpWindow& B) {
		    return WindowPlaybackTimes.FindChecked(&A).X < WindowPlaybackTimes.FindChecked(&B).X;
	    });
	TSet<FName> UsedTargets;
	TArray<const FIMGTraversalWarpWindow*, TInlineAllocator<4>> PlacementWindows;
	const FIMGTraversalWarpWindow* FinalTranslationWindow = nullptr;
	const FIMGTraversalWarpWindow* FinalHeightWindow = nullptr;
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
			if (Window.Modifier->bWarpTranslation && !Window.Modifier->bIgnoreZAxis)
			{
				FinalHeightWindow = &Window;
			}
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
		FinalHeightWindow = nullptr;
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
	auto SelectWindow = [&Windows, &WindowPlaybackTimes, MontageLength](FName Name, int32 Index, float MinEnd,
	                        float MaxEnd) -> const FIMGTraversalWarpWindow* {
		if (Index >= 0)
		{
			const FIMGTraversalWarpWindow* Window =
			    UIMGTraversalComponent::FindTranslationWindow(Windows, Name, Index, -1.f, MontageLength);
			const FVector2f* Times = WindowPlaybackTimes.Find(Window);
			return Times && Times->Y > MinEnd && Times->Y <= MaxEnd ? Window : nullptr;
		}
		if (Name.IsNone() || Index != -1)
		{
			return nullptr;
		}
		const FIMGTraversalWarpWindow* Result = nullptr;
		float LastEnd = MinEnd;
		for (const FIMGTraversalWarpWindow& Window : Windows)
		{
			const FVector2f* Times = WindowPlaybackTimes.Find(&Window);
			if (Times && Window.Modifier && Window.Modifier->bWarpTranslation &&
			    Window.Modifier->WarpTargetName == Name && Times->Y > MinEnd && Times->Y <= MaxEnd &&
			    Times->Y >= LastEnd)
			{
				Result = &Window;
				LastEnd = Times->Y;
			}
		}
		return Result;
	};
	auto AlignmentTime = [&Selection](const FIMGTraversalWarpWindow& Window) {
		return Window.Modifier->bSubtractRemainingRootMotion ? Selection.Montage->GetPlayLength()
		                                                     : Window.EndTime;
	};
	const FTransform InitialPose = Character->GetActorTransform();
	const USkeletalMeshComponent* Mesh = Character->GetMesh();
	const FQuat BaseRotation = Character->GetBaseRotationOffset();
	const FVector MeshScale = Mesh ? Mesh->GetComponentScale() : FVector::OneVector;
	const FTransform MeshRelative(BaseRotation, Character->GetBaseTranslationOffset(),
	    Mesh ? Mesh->GetRelativeScale3D() : FVector::OneVector);
	auto ExtractMotion = [&](float From, float To) {
		return UMotionWarpingUtilities::ExtractRootMotionFromAnimation(Selection.Montage, From, To);
	};
	auto AdvanceRootMotion = [&](float From, float To, FTransform& Pose) {
		for (int32 RangeIndex = 0; RangeIndex < PlaybackRanges.Num(); ++RangeIndex)
		{
			const FVector2f& Range = PlaybackRanges[RangeIndex];
			const float Offset = PlaybackOffsets[RangeIndex];
			const float Start = FMath::Max(From, Offset);
			const float End = FMath::Min(To, Offset + Range.Y - Range.X);
			if (End <= Start)
			{
				continue;
			}
			const FTransform MeshWorld = MeshRelative * Pose;
			Pose = Pose.GetRelativeTransform(MeshWorld) *
			    (ExtractMotion(FMath::Clamp(Range.X + Start - Offset, Range.X, Range.Y),
			         FMath::Clamp(Range.X + End - Offset, Range.X, Range.Y)) *
			        MeshWorld);
		}
	};
	TMap<const FIMGTraversalWarpWindow*, FTransform> RootOffsets;
	TMap<const FIMGTraversalWarpWindow*, FTransform> NativeTails;
	auto ResolveWindowPose = [&](const FIMGTraversalWarpWindow& Window, const FTransform& WarpTarget,
	                             const FTransform& EntryPose, FTransform& EndPose) {
		const URootMotionModifier_Warp* Modifier = Window.Modifier;
		const FTransform* CachedRootOffset = RootOffsets.Find(&Window);
		if (!CachedRootOffset)
		{
			FTransform RootOffset = FTransform::Identity;
			if (Modifier->WarpPointAnimProvider == EWarpPointAnimProvider::Bone)
			{
				if (!Mesh || !Mesh->GetAnimInstance() ||
				    !Mesh->GetAnimInstance()->GetRequiredBones().IsValid() ||
				    Mesh->GetAnimInstance()->GetRequiredBones().GetPoseBoneIndexForBoneName(
				        Modifier->WarpPointAnimBoneName) == INDEX_NONE)
				{
					PredictionUnavailableReason =
					    FString::Printf(TEXT("Warp-point bone %s has no available pose"),
					        *Modifier->WarpPointAnimBoneName.ToString());
					return false;
				}
				RootOffset = UMotionWarpingUtilities::CalculateRootTransformRelativeToWarpPointAtTime(
				    *Character, Selection.Montage, Window.EndTime, Modifier->WarpPointAnimBoneName);
			}
			else if (Modifier->WarpPointAnimProvider == EWarpPointAnimProvider::Static)
			{
				RootOffset = UMotionWarpingUtilities::CalculateRootTransformRelativeToWarpPointAtTime(
				    *Character, Selection.Montage, Window.EndTime, Modifier->WarpPointAnimTransform);
			}
			else if (Modifier->WarpPointAnimProvider != EWarpPointAnimProvider::None)
			{
				PredictionUnavailableReason = TEXT("Unsupported warp-point provider");
				return false;
			}
			static const IConsoleVariable* UseMeshScale =
			    IConsoleManager::Get().FindConsoleVariable(TEXT("a.MotionWarping.UseMeshScale"));
			if (UseMeshScale && UseMeshScale->GetInt() != 0)
			{
				RootOffset.ScaleTranslation(MeshScale);
			}
			CachedRootOffset = &RootOffsets.Add(&Window, RootOffset);
		}
		// FMotionWarpingTarget publishes only location and rotation.
		const FTransform RootTarget =
		    *CachedRootOffset * FTransform(WarpTarget.GetRotation(), WarpTarget.GetLocation());
		if (!RootTarget.IsValid())
		{
			PredictionUnavailableReason = TEXT("Warp-point root transform is invalid");
			return false;
		}
		const FTransform* CachedNativeTail = NativeTails.Find(&Window);
		if (!CachedNativeTail)
		{
			CachedNativeTail = &NativeTails.Add(&Window,
			    Modifier->bSubtractRemainingRootMotion ? ExtractMotion(Window.EndTime, MontageLength)
			                                           : FTransform::Identity);
			if (bVerboseDiagnostics)
			{
				const FVector2f& Times = WindowPlaybackTimes.FindChecked(&Window);
				const float EntryTime = Window.StartTime + FMath::Max(0.f, PlaybackStart - Times.X);
				LogTraversalEvent(TEXT("PlacementWindow"),
				    FString::Printf(
				        TEXT("Target=%s SelectionStart=%.6f AssetWindow=%.6f..%.6f AssetEntry=%.6f "
				             "RemainingTranslation=%s SubtractRemaining=%d NativeTail=%s "
				             "WarpPoint=%s Bone=%s RootOffset=%s Translation=%d IgnoreZ=%d Feet=%d"),
				        *Modifier->WarpTargetName.ToString(), Selection.StartTime, Window.StartTime,
				        Window.EndTime, EntryTime,
				        *ExtractMotion(EntryTime, Window.EndTime).GetTranslation().ToString(),
				        Modifier->bSubtractRemainingRootMotion,
				        *CachedNativeTail->GetTranslation().ToString(),
				        *UEnum::GetValueAsString(Modifier->WarpPointAnimProvider),
				        *Modifier->WarpPointAnimBoneName.ToString(), *CachedRootOffset->ToString(),
				        Modifier->bWarpTranslation, Modifier->bIgnoreZAxis, Modifier->bWarpToFeetLocation));
			}
		}
		const FTransform& NativeTail = *CachedNativeTail;
		FQuat EndRotation = EndPose.GetRotation();
		if (Modifier->bWarpRotation)
		{
			EndRotation = RootTarget.GetRotation();
			if (Modifier->RotationType == EMotionWarpRotationType::Facing)
			{
				const FVector Facing = (RootTarget.GetLocation() - EntryPose.GetLocation()).GetSafeNormal2D();
				EndRotation = FRotationMatrix::MakeFromXZ(Facing, FVector::UpVector).ToQuat();
			}
			EndRotation = Modifier->AdditionalRotationOffset.Quaternion() * EndRotation;
			if (Modifier->bSubtractRemainingRootMotion)
			{
				EndRotation *= BaseRotation * NativeTail.GetRotation().Inverse() * BaseRotation.Inverse();
			}
			EndPose.SetRotation(EndRotation);
		}
		if (!EndRotation.GetUpVector().Equals(FVector::UpVector, UprightTolerance))
		{
			PredictionUnavailableReason = TEXT("Projected window rotation is not upright");
			return false;
		}
		if (!Modifier->bWarpTranslation)
		{
			return EndPose.IsValid();
		}

		const bool bHasTranslation =
		    !ExtractMotion(Window.StartTime, Window.EndTime).GetTranslation().IsNearlyZero(2e-4);
		const FQuat MotionRotation = EndRotation * BaseRotation;
		FVector Center = RootTarget.GetLocation();
		if (bHasTranslation)
		{
			FVector VisualOffset = Character->GetBaseTranslationOffset();
			if (!Modifier->bWarpToFeetLocation)
			{
				VisualOffset.Z = 0.0;
			}
			Center -= EndRotation.RotateVector(VisualOffset * InitialPose.GetScale3D());
		}
		else
		{
			// Skew Warp's no-translation interpolation starts at the visual root (capsule feet).
			Center +=
			    EndRotation.GetUpVector() * Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		}

		if (bHasTranslation)
		{
			Center -= MotionRotation.RotateVector(NativeTail.GetTranslation() * MeshScale);
		}
		if (Modifier->bIgnoreZAxis)
		{
			// Skew Warp aligns to the current height on an ignored Z axis.
			Center.Z = EntryPose.GetLocation().Z;
		}
		EndPose.SetLocation(Center);
		return EndPose.IsValid();
	};
	TMap<FName, FTransform> WorldTargets;
	auto ProjectPlacement = [&](const FIMGTraversalWarpWindow& ReferenceWindow, bool bIncludeTail,
	                            FVector& Center) {
		FTransform Pose = InitialPose;
		if (!bProjectSequence || !PlacementWindows.Contains(&ReferenceWindow))
		{
			return false;
		}
		float Cursor = PlaybackStart;
		for (const FIMGTraversalWarpWindow* Window : PlacementWindows)
		{
			const FVector2f& Times = WindowPlaybackTimes.FindChecked(Window);
			const float WindowStart = FMath::Max(Cursor, Times.X);
			AdvanceRootMotion(Cursor, WindowStart, Pose);
			const FTransform EntryPose = Pose;
			AdvanceRootMotion(WindowStart, Times.Y, Pose);
			const FTransform* Target = WorldTargets.Find(Window->Modifier->WarpTargetName);
			if (!Target || !ResolveWindowPose(*Window, *Target, EntryPose, Pose))
			{
				if (PredictionUnavailableReason.IsEmpty())
				{
					PredictionUnavailableReason =
					    FString::Printf(TEXT("Could not resolve placement window %s %.3f..%.3f"),
					        *Window->Modifier->WarpTargetName.ToString(), Window->StartTime, Window->EndTime);
				}
				return false;
			}
			Cursor = Times.Y;
			if (!bIncludeTail && Window == &ReferenceWindow)
			{
				break;
			}
		}
		if (bIncludeTail)
		{
			AdvanceRootMotion(Cursor, PlacementTime, Pose);
		}
		Center = Pose.GetLocation();
		const bool bValidPose =
		    Pose.IsValid() && Pose.GetRotation().GetUpVector().Equals(FVector::UpVector, UprightTolerance);
		if (!bValidPose)
		{
			PredictionUnavailableReason = TEXT("Final projected pose is invalid or not upright");
		}
		return bValidPose;
	};
	const FIMGTraversalLandingSettings& Landing = Settings.Landing;
	const FIMGTraversalWarpBinding* TerminalBinding =
	    FinalTranslationWindow ? FindBinding(FinalTranslationWindow->Modifier->WarpTargetName) : nullptr;
	const FIMGTraversalWarpBinding* HeightBinding =
	    FinalHeightWindow ? FindBinding(FinalHeightWindow->Modifier->WarpTargetName) : nullptr;
	const bool bUsesBackFloor =
	    (TerminalBinding && TerminalBinding->Anchor == EIMGTraversalAnchor::BackFloor) ||
	    (HeightBinding && HeightBinding->Anchor == EIMGTraversalAnchor::BackFloor);
	const bool bDefaultMantle = Selection.Action == EIMGTraversalAction::Mantle &&
	    Landing.Mode == EIMGTraversalLandingMode::None && !bUsesBackFloor;
	const bool bCanCompensate = Selection.Action == EIMGTraversalAction::Mantle ||
	    Landing.Mode != EIMGTraversalLandingMode::None || bUsesBackFloor;
	TOptional<FVector> CompensatedPlacementCenter;
	const FIMGTraversalWarpWindow* LandingWindow = nullptr;
	UPrimitiveComponent* PlacementSupportToIgnore = nullptr;
	const TCHAR* CompensationSkipReason = TEXT("No supported height-adjusting window");
	auto CompensatePlacement = [&](const FIMGTraversalWarpWindow& Window, EIMGTraversalAnchor AnchorType,
	                               const FTransform& Anchor, const UPrimitiveComponent& Carrier) {
		// Explicit landing validates contact at the chosen window, before the montage tail.
		const FIMGTraversalWarpWindow& EvaluationWindow = LandingWindow ? *LandingWindow : Window;
		const bool bIncludeTail = LandingWindow == nullptr;
		FVector Center;
		if (!ProjectPlacement(EvaluationWindow, bIncludeTail, Center))
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
			            "EvaluationEnd=%.6f IncludeTail=%d Center=%s"),
			        LandingWindow ? TEXT("LandingWindow") : TEXT("PlaybackEnd"), Selection.StartTime,
			        *Window.Modifier->WarpTargetName.ToString(),
			        *EvaluationWindow.Modifier->WarpTargetName.ToString(), EvaluationWindow.EndTime,
			        bIncludeTail ? PlacementTime : WindowPlaybackTimes.FindChecked(&EvaluationWindow).Y,
			        bIncludeTail, *Center.ToString()));
		}
		const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		const UIMGCharacterMovementComponent* Movement = Character->GetIMGMovementComponent();
		const bool bRequireTopSupport = bDefaultMantle;
		const bool bUseBackFloor =
		    !bRequireTopSupport && Candidate.bHasBackFloor && IsValid(Candidate.BackFloorComponent);
		const double TopTolerance =
		    FMath::Max(static_cast<double>(Geometry.DepthHeightTolerance), UE_DOUBLE_KINDA_SMALL_NUMBER);
		const double TopMinZ = Candidate.TopSupportHeightRange.X - TopTolerance;
		const double TopMaxZ = Candidate.TopSupportHeightRange.Y + TopTolerance;
		auto IsPlacementSupport = [&](const FHitResult& Hit) {
			UPrimitiveComponent* Component = Hit.GetComponent();
			const bool bKnownSurface = Component &&
			    (Component == &Carrier || Candidate.ObstacleComponents.Contains(Component) ||
			        (bUseBackFloor && Component == Candidate.BackFloorComponent.Get()));
			// One collision component can contain both the obstacle top and the lower floor.
			const bool bInSupportLayer =
			    !bRequireTopSupport || (Hit.ImpactPoint.Z >= TopMinZ && Hit.ImpactPoint.Z <= TopMaxZ);
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
			if (AnchorType != EIMGTraversalAnchor::BackFloor && bUseBackFloor)
			{
				const FVector BackFloorLocation = Candidate.BackFloor.GetLocation();
				const double BackFloorHeightRange =
				    GetSupportHeightRange(BackFloorLocation, GetMinNormalZ(*Candidate.BackFloorComponent));
				SupportBottomZ = FMath::Min(SupportBottomZ, BackFloorLocation.Z - BackFloorHeightRange);
			}
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
			if (!ProjectPlacement(EvaluationWindow, bIncludeTail, ReprojectedCenter) ||
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
			    Character->GetIMGMovementComponent()->IsWalkable(Floor) &&
			        FIMGTraversalDetector::CanUseSurface(
			            Floor.GetComponent(), Geometry, Candidate.RequestSource),
			    TEXT("Animation-selected floor"), IMGTraversalDebug::EStage::Execution);
		}
#endif
		UPrimitiveComponent* FloorComponent = Floor.GetComponent();
		if (!bHit || !Character->GetIMGMovementComponent()->IsWalkable(Floor) ||
		    !FIMGTraversalDetector::CanUseSurface(FloorComponent, Geometry, Candidate.RequestSource))
		{
			return Reject(TEXT("Selected landing floor is unusable"));
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
	const FIMGTraversalWarpWindow* CompensationWindow = FinalHeightWindow;
	if (LandingWindow)
	{
		// A later height window cannot correct an earlier landing contact.
		CompensationWindow = nullptr;
		const float LandingEnd = WindowPlaybackTimes.FindChecked(LandingWindow).Y;
		for (const FIMGTraversalWarpWindow* Window : PlacementWindows)
		{
			if (Window->Modifier->bWarpTranslation && !Window->Modifier->bIgnoreZAxis &&
			    WindowPlaybackTimes.FindChecked(Window).Y <= LandingEnd)
			{
				CompensationWindow = Window;
			}
		}
	}
	if (bCanCompensate && CompensationWindow &&
	    (!LandingWindow || LandingWindow == CompensationWindow ||
	        CompensationWindow->Modifier->WarpTargetName != Landing.TargetName ||
	        WindowPlaybackTimes.FindChecked(LandingWindow).X >=
	            WindowPlaybackTimes.FindChecked(CompensationWindow).Y))
	{
		const FIMGTraversalWarpBinding* Binding = FindBinding(CompensationWindow->Modifier->WarpTargetName);
		FTransform Anchor;
		UPrimitiveComponent* Component = nullptr;
		if (Binding && GetAnchor(Binding->Anchor, Anchor, Component))
		{
			CompensatePlacement(*CompensationWindow, Binding->Anchor, Anchor, *Component);
		}
	}
	if (LandingWindow)
	{
		FVector PlacementCenter;
		if (CompensatedPlacementCenter.IsSet())
		{
			PlacementCenter = CompensatedPlacementCenter.GetValue();
		}
		else if (!ProjectPlacement(*LandingWindow, false, PlacementCenter))
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
			        FIMGTraversalDetector::CanUseSurface(RootSupport, Geometry, Candidate.RequestSource),
			    TEXT("Actual capsule support"), IMGTraversalDebug::EStage::Execution);
		}
#endif
		if (!RootFloor.IsWalkableFloor() || RootDrop < 0.f || RootDrop > Geometry.MaxDrop ||
		    !FIMGTraversalDetector::CanUseSurface(RootSupport, Geometry, Candidate.RequestSource))
		{
			return Reject(TEXT("Selected landing capsule has no usable floor support"));
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
	if (Selection.Action == EIMGTraversalAction::Mantle && !LandingWindow)
	{
		if (!bProjectSequence && bVerboseDiagnostics)
		{
			// The compensation model's limits do not invalidate an authored mantle trajectory.
			LogTraversalEvent(TEXT("PlacementPredictionFallback"),
			    FString::Printf(TEXT("Montage=%s Start=%.3f: retaining authored targets without placement "
			                         "compensation/final prediction; %s"),
			        *GetNameSafe(Selection.Montage.Get()), Selection.StartTime,
			        PredictionUnavailableReason.IsEmpty() ? TEXT("No resolvable final placement window")
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
