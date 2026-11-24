#include "Character/Traversal/Execution/IMGTraversalWarpPrediction.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "BoneContainer.h"
#include "Character/IMGCharacter.h"
#include "Character/Traversal/IMGTraversalTypes.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "HAL/IConsoleManager.h"
#include "Character/Traversal/Execution/IMGTraversalPlaybackPath.h"
#include "Math/RotationMatrix.h"
#include "MotionWarpingComponent.h"

FIMGTraversalWarpPrediction::FIMGTraversalWarpPrediction(AIMGCharacter& InCharacter,
    const FIMGTraversalAnimationSelection& InSelection, const FIMGTraversalPlaybackPath& InPlayback,
    TConstArrayView<const FIMGTraversalWarpWindow*> InWindows, const FTransform& InInitialPose,
    bool bInEnabled, bool bInVerboseDiagnostics, FString& InFailureReason,
    TFunctionRef<void(const TCHAR*, const FString&, bool)> InLogEvent)
    : Character(&InCharacter), Selection(InSelection), Playback(InPlayback), Windows(InWindows),
      InitialPose(InInitialPose), Mesh(InCharacter.GetMesh()), BaseRotation(InCharacter.GetBaseRotationOffset()),
      MeshScale(Mesh ? Mesh->GetComponentScale() : FVector::OneVector),
      MeshRelative(BaseRotation, InCharacter.GetBaseTranslationOffset(), Mesh ? Mesh->GetRelativeScale3D() : FVector::OneVector),
      bEnabled(bInEnabled), bVerboseDiagnostics(bInVerboseDiagnostics), PredictionUnavailableReason(InFailureReason), LogEvent(InLogEvent)
{
}

FTransform FIMGTraversalWarpPrediction::ExtractMotion(float From, float To) const
{
	return UMotionWarpingUtilities::ExtractRootMotionFromAnimation(Selection.Montage, From, To);
}

void FIMGTraversalWarpPrediction::AdvanceRootMotion(float From, float To, FTransform& Pose) const
{
	const auto& PlaybackRanges = Playback.GetRanges();
	const auto& PlaybackOffsets = Playback.GetOffsets();
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
}

bool FIMGTraversalWarpPrediction::ResolveWindowPose(const FIMGTraversalWarpWindow& Window,
    const FTransform& WarpTarget, const FTransform& EntryPose, FTransform& EndPose)
{
	constexpr double UprightTolerance = 1.e-3;
	const float PlaybackStart = Playback.GetStartTime();
	const auto& WindowPlaybackTimes = Playback.GetWindowTimes();
	const float MontageLength = Selection.Montage->GetPlayLength();
	auto LogTraversalEvent = [this](const TCHAR* Event, const FString& Detail) { LogEvent(Event, Detail, false); };
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
}

bool FIMGTraversalWarpPrediction::Project(const FIMGTraversalWarpWindow& ReferenceWindow,
    const TMap<FName, FTransform>& WorldTargets, FVector& Center)
{
	constexpr double UprightTolerance = 1.e-3;
	const float PlaybackStart = Playback.GetStartTime();
	const auto& WindowPlaybackTimes = Playback.GetWindowTimes();
	FTransform Pose = InitialPose;
	if (!bEnabled || !Windows.Contains(&ReferenceWindow))
	{
		return false;
	}
	float Cursor = PlaybackStart;
	for (const FIMGTraversalWarpWindow* Window : Windows)
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
		if (Window == &ReferenceWindow)
		{
			break;
		}
	}
	Center = Pose.GetLocation();
	const bool bValidPose =
	    Pose.IsValid() && Pose.GetRotation().GetUpVector().Equals(FVector::UpVector, UprightTolerance);
	if (!bValidPose)
	{
		PredictionUnavailableReason = TEXT("Final projected pose is invalid or not upright");
	}
	return bValidPose;
}
