
#include "Camera/IMGCameraComponent.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Camera/IMGCameraMode.h"
#include "Camera/IMGCameraMode_Default.h"
#include "Camera/IMGCameraShakeInstance.h"
#include "Camera/IMGCameraShakePattern.h"
#include "Camera/IMGCameraShakeDefinition.h"
#include "Camera/IMGCameraAssistInterface.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PrimitiveComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/LocalPlayer.h"
#include "SceneView.h"
#include "CoreGlobals.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "HAL/IConsoleManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGCameraComponent)

namespace
{
	TAutoConsoleVariable<int32> CVarCameraDebug(TEXT("IMG.Camera.Debug"), 0,
		TEXT("0: off, 1: framing and collision anchors, 2: bounded query shapes and stage labels."), ECVF_Cheat);
	TAutoConsoleVariable<float> CVarCameraDebugDuration(TEXT("IMG.Camera.DebugDuration"), 0.f,
		TEXT("Lifetime of camera debug geometry in seconds (0 is one frame, maximum 2)."), ECVF_Cheat);
	TAutoConsoleVariable<int32> CVarCameraDebugMaxQueries(TEXT("IMG.Camera.DebugMaxQueries"), 128,
		TEXT("Maximum query shapes captured per camera update (1 to 256)."), ECVF_Cheat);

#if ENABLE_DRAW_DEBUG
	const TCHAR* StageName(EIMGCameraCollisionStage Stage)
	{
		switch (Stage)
		{
		case EIMGCameraCollisionStage::Origin: return TEXT("Origin");
		case EIMGCameraCollisionStage::Root: return TEXT("Root");
		case EIMGCameraCollisionStage::Occlusion: return TEXT("Occlusion");
		case EIMGCameraCollisionStage::Search: return TEXT("Search");
		case EIMGCameraCollisionStage::CameraLimit: return TEXT("CameraLimit");
		case EIMGCameraCollisionStage::Clipping: return TEXT("Clipping");
		case EIMGCameraCollisionStage::PushBeyond: return TEXT("PushBeyond");
		case EIMGCameraCollisionStage::PullBack: return TEXT("Radius");
		case EIMGCameraCollisionStage::Final: return TEXT("Final");
		case EIMGCameraCollisionStage::NearClip: return TEXT("NearClip");
		default: return TEXT("Query");
		}
	}

	void DrawQuery(const UWorld* World, const FIMGCameraCollisionDebugQuery& Query, float Duration)
	{
		if (Query.Start.ContainsNaN() || Query.End.ContainsNaN() || Query.Rotation.ContainsNaN()) { return; }
		const FColor Color = Query.bIgnored ? FColor::Silver : Query.bBlocked ? FColor::Red : FColor::Cyan;
		if (Query.bSweep)
		{
			const FVector Delta = Query.End - Query.Start;
			const FQuat Rotation = Delta.IsNearlyZero() ? FQuat::Identity : FQuat::FindBetweenNormals(FVector::UpVector, Delta.GetSafeNormal());
			DrawDebugCapsule(World, (Query.Start + Query.End) * 0.5, Delta.Size() * 0.5 + Query.Radius,
				Query.Radius, Rotation, Color, false, Duration);
			if (Query.bBlocked)
			{
				DrawDebugDirectionalArrow(World, Query.HitLocation, Query.HitLocation + Query.HitNormal * 25.f,
					6.f, Color, false, Duration, 0, 1.f);
			}
		}
		else if (Query.Shape == EIMGCameraCollisionDebugShape::Box)
		{
			DrawDebugBox(World, Query.Start, Query.Extent, Query.Rotation, Color, false, Duration);
		}
		else if (Query.Shape == EIMGCameraCollisionDebugShape::Capsule)
		{
			DrawDebugCapsule(World, Query.Start, Query.HalfHeight, Query.Radius, Query.Rotation, Color, false, Duration);
		}
		else { DrawDebugSphere(World, Query.Start, Query.Radius, 12, Color, false, Duration); }
	}
#endif
}


UIMGCameraComponent::UIMGCameraComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	CameraModeStack = nullptr;
	FieldOfViewOffset = 0.0f;
}

void UIMGCameraComponent::OnRegister()
{
	bEndingPlay = false;
	Super::OnRegister();
	if (!IsRegistered() || bEndingPlay) { return; }

	if (!CameraModeStack)
	{
		CameraModeStack = NewObject<UIMGCameraModeStack>(this);
		check(CameraModeStack);
	}
	if (IsActive()) { CameraModeStack->ActivateStack(); }
	else { CameraModeStack->DeactivateStack(); }
}

void UIMGCameraComponent::OnUnregister()
{
	TGuardValue<bool> UnregisterGuard(bUnregistering, true);
	StopAllCameraShakes(true);
	ResetCollision();
	if (CameraModeStack)
	{
		CameraModeStack->DeactivateStack();
	}
	FieldOfViewOffset = 0.f;
	LastCameraUpdateFrame = MAX_uint64;
	Super::OnUnregister();
}

void UIMGCameraComponent::Activate(bool bReset)
{
	if (bEndingPlay || bUnregistering) { return; }
	const bool bWasActive = IsActive();
	if (bReset) { StopAllCameraShakes(true); }
	Super::Activate(bReset);
	if (IsRegistered() && !bEndingPlay && IsActive() && CameraModeStack && (!bWasActive || bReset))
	{
		CameraModeStack->ActivateStack();
		ResetCollision();
		CameraModeStack->ResetInterpolation();
		LastCameraUpdateFrame = MAX_uint64;
	}
}

void UIMGCameraComponent::Deactivate()
{
	Super::Deactivate();
	if (!IsActive())
	{
		StopAllCameraShakes(true);
		if (CameraModeStack) { CameraModeStack->DeactivateStack(); }
		ResetCollision();
		FieldOfViewOffset = 0.f;
		LastCameraUpdateFrame = MAX_uint64;
	}
}

void UIMGCameraComponent::BeginPlay()
{
	bEndingPlay = false;
	Super::BeginPlay();
	if (bEndingPlay || !IsRegistered()) { return; }
	if (IsActive() && CameraModeStack) { CameraModeStack->ActivateStack(); }
	ResetCollision();
	if (CameraModeStack) { CameraModeStack->ResetInterpolation(); }
}

void UIMGCameraComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndingPlay = true;
	StopAllCameraShakes(true);
	if (CameraModeStack) { CameraModeStack->DeactivateStack(); }
	DetermineCameraModeDelegate.Unbind();
	ResetCollision();
	FieldOfViewOffset = 0.f;
	LastCameraUpdateFrame = MAX_uint64;
	Super::EndPlay(EndPlayReason);
}

void UIMGCameraComponent::GetCameraView(float DeltaTime, FMinimalViewInfo& DesiredView)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(IMGCameraView);

	// Viewport and Sequencer previews share the runtime view entry point.
	const UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		Super::GetCameraView(DeltaTime, DesiredView);
		return;
	}

	const TWeakObjectPtr<AActor> Target = GetTargetActor();
	if (bEndingPlay || bUnregistering || !IsRegistered() || !IsActive() || !CameraModeStack || !Target.IsValid()
		|| Target->IsActorBeingDestroyed() || IsXRHeadTrackedCamera())
	{
		StopAllCameraShakes(true);
		ResetCollision();
		FieldOfViewOffset = 0.f;
		LastCameraUpdateFrame = MAX_uint64;
		Super::GetCameraView(DeltaTime, DesiredView);
		return;
	}
	if (LastCameraUpdateFrame == MAX_uint64 || (LastCameraUpdateFrame != GFrameCounter && LastCameraUpdateFrame + 1 != GFrameCounter))
	{
		ResetCollision();
		CameraModeStack->ResetInterpolation();
	}

	UpdateCameraModes();

	FIMGCameraModeView CameraModeView;
	if (!IsRegistered() || !IsActive() || !Target.IsValid() || GetTargetActor() != Target.Get()
		|| !CameraModeStack->EvaluateStack(DeltaTime, CameraModeView) || !IsRegistered() || !IsActive()
		|| !Target.IsValid() || GetTargetActor() != Target.Get()
		|| CameraModeView.Location.ContainsNaN() || CameraModeView.Rotation.ContainsNaN()
		|| CameraModeView.ControlRotation.ContainsNaN() || !FMath::IsFinite(CameraModeView.FieldOfView))
	{
		StopAllCameraShakes(true);
		ResetCollision();
		FieldOfViewOffset = 0.f;
		LastCameraUpdateFrame = MAX_uint64;
		Super::GetCameraView(DeltaTime, DesiredView);
		return;
	}

	// Keep player controller in sync with the latest view.
	if (APawn* TargetPawn = Cast<APawn>(GetTargetActor()))
	{
		if (APlayerController* PC = TargetPawn->GetController<APlayerController>())
		{
			PC->SetControlRotation(CameraModeView.ControlRotation);
		}
	}
	if (!IsRegistered() || !IsActive() || bEndingPlay || bUnregistering || !Target.IsValid()
		|| Target->IsActorBeingDestroyed() || GetTargetActor() != Target.Get())
	{
		StopAllCameraShakes(true);
		ResetCollision();
		FieldOfViewOffset = 0.f;
		LastCameraUpdateFrame = MAX_uint64;
		Super::GetCameraView(DeltaTime, DesiredView);
		return;
	}

	// Apply any offset that was added to the field of view.
	if (FieldOfViewOffsetFrame == GFrameCounter) { CameraModeView.FieldOfView += FieldOfViewOffset; }
	FieldOfViewOffset = 0.0f;
	FieldOfViewOffsetFrame = MAX_uint64;
	CameraModeView.FieldOfView = FMath::IsFinite(CameraModeView.FieldOfView)
		? FMath::Clamp(CameraModeView.FieldOfView, 5.f, 170.f) : FIMGCameraModeView().FieldOfView;

	// Keep camera component in sync with the latest view.
	FieldOfView = CameraModeView.FieldOfView;

	// Fill in desired view.
	DesiredView.Location = CameraModeView.Location;
	DesiredView.Rotation = CameraModeView.Rotation;
	DesiredView.FOV = CameraModeView.FieldOfView;
	DesiredView.OrthoWidth = OrthoWidth;
	DesiredView.OrthoNearClipPlane = OrthoNearClipPlane;
	DesiredView.OrthoFarClipPlane = OrthoFarClipPlane;
	DesiredView.AspectRatio = AspectRatio;
	DesiredView.bConstrainAspectRatio = bConstrainAspectRatio;
	DesiredView.bUseFieldOfViewForLOD = bUseFieldOfViewForLOD;
	DesiredView.ProjectionMode = ProjectionMode;
	DesiredView.PerspectiveNearClipPlane = -1.f;
	UpdateCameraShakes(DeltaTime, CameraModeView, DesiredView);
	FieldOfView = DesiredView.FOV;
	ResolveCollision(DeltaTime, CameraModeView, DesiredView);
#if ENABLE_DRAW_DEBUG
	if (GetWorld() && (bDrawCollision || CVarCameraDebug.GetValueOnGameThread() > 0
		|| GetWorld()->TimeSince(LastCollisionDebugTime) < 1.f) && CameraModeView.Framing.bValid)
	{
		const float RequestedDuration = CVarCameraDebugDuration.GetValueOnGameThread();
		const float Duration = FMath::IsFinite(RequestedDuration) ? FMath::Clamp(RequestedDuration, 0.f, 2.f) : 0.f;
		const FIMGCameraFramingResult& Frame = CameraModeView.Framing;
		DrawDebugSphere(GetWorld(), Frame.BasePivot, 5.f, 12, FColor::Blue, false, Duration);
		DrawDebugSphere(GetWorld(), Frame.OrbitPivot, 5.f, 12, FColor::Yellow, false, Duration);
		DrawDebugLine(GetWorld(), Frame.BasePivot, Frame.OrbitPivot, FColor::Yellow, false, Duration);
		DrawDebugLine(GetWorld(), Frame.OrbitPivot, CameraModeView.Location, FColor::White, false, Duration);
		const FVector LookAt = Frame.FocusPoint.IsSet() && Frame.FocusWeight > UE_SMALL_NUMBER ? *Frame.FocusPoint
			: DesiredView.Location + DesiredView.Rotation.Vector() * 200.f;
		DrawDebugDirectionalArrow(GetWorld(), DesiredView.Location, LookAt, 10.f, FColor::Green, false, Duration);
	}
#endif
	SetWorldLocationAndRotation(DesiredView.Location, DesiredView.Rotation);
	if (!IsRegistered() || !IsActive() || bEndingPlay || !Target.IsValid() || GetTargetActor() != Target.Get())
	{
		StopAllCameraShakes(true);
		ResetCollision();
		LastCameraUpdateFrame = MAX_uint64;
		return;
	}
	LastCameraUpdateFrame = GFrameCounter;

	// See if the CameraActor wants to override the PostProcess settings used.
	DesiredView.PostProcessBlendWeight = PostProcessBlendWeight;
	if (PostProcessBlendWeight > 0.0f)
	{
		DesiredView.PostProcessSettings = PostProcessSettings;
	}


	if (CameraModeView.Collision.IsSet() && CollisionDiagnostics.DistanceRatio < CameraModeView.Collision->ReportPenetrationPercent)
	{
		const auto AssistActors = CameraModeView.Collision->AssistActors;
		for (const auto& Actor : AssistActors)
		{
			if (!IsRegistered() || !IsActive() || !Target.IsValid() || GetTargetActor() != Target.Get()) { break; }
			if (Actor.IsValid())
			{
				if (IIMGCameraAssistInterface* Assist = Cast<IIMGCameraAssistInterface>(Actor.Get()))
				{
					Assist->OnCameraPenetratingTarget();
				}
			}
		}
	}
}

void UIMGCameraComponent::AddFieldOfViewOffset(float FovOffset)
{
	if (!FMath::IsFinite(FovOffset) || bEndingPlay || bUnregistering) { return; }
	if (FieldOfViewOffsetFrame != GFrameCounter)
	{
		FieldOfViewOffset = 0.f;
		FieldOfViewOffsetFrame = GFrameCounter;
	}
	FieldOfViewOffset += FovOffset;
}

void UIMGCameraComponent::ResetCollision()
{
	CollisionState.Reset();
	CollisionDiagnostics = FIMGCameraCollisionResult();
	CollisionTarget.Reset();
	CollisionAnchor.Reset();
	CollisionController.Reset();
}

void UIMGCameraComponent::ResolveCollision(float DeltaTime, const FIMGCameraModeView& CameraModeView, FMinimalViewInfo& DesiredView)
{
	if (!CameraModeView.Collision.IsSet() || !GetWorld() || DesiredView.ProjectionMode != ECameraProjectionMode::Perspective)
	{
		ResetCollision();
		return;
	}
	const FIMGCameraCollisionRequest& Request = CameraModeView.Collision.GetValue();
	if (!Request.Target.IsValid() || !Request.Anchor.IsValid()
		|| Request.Target->GetWorld() != GetWorld() || Request.Anchor->GetWorld() != GetWorld())
	{
		ResetCollision();
		return;
	}
	const AActor* ControllerActor = Request.Controller.Get();
	if (ControllerActor && ControllerActor->IsActorBeingDestroyed()) { ControllerActor = nullptr; }
	const TWeakObjectPtr<const AActor> CurrentController(ControllerActor);
	const APlayerController* Controller = Cast<APlayerController>(ControllerActor);
	if (!CollisionTarget.HasSameIndexAndSerialNumber(Request.Target) || !CollisionAnchor.HasSameIndexAndSerialNumber(Request.Anchor)
		|| !CollisionController.HasSameIndexAndSerialNumber(CurrentController)
		|| CollisionTraceChannel != Request.Settings.TraceChannel || bCollisionTraceComplex != Request.Settings.bTraceComplex
		|| (Controller && Controller->PlayerCameraManager && Controller->PlayerCameraManager->bGameCameraCutThisFrame))
	{
		ResetCollision();
	}
	CollisionTarget = Request.Target;
	CollisionAnchor = Request.Anchor;
	CollisionController = CurrentController;
	CollisionTraceChannel = Request.Settings.TraceChannel;
	bCollisionTraceComplex = Request.Settings.bTraceComplex;
	FIMGCameraCollisionInput Input = Request.Input;
	Input.Location = DesiredView.Location;
	Input.Rotation = DesiredView.Rotation;
	const FVector RawLocation = DesiredView.Location;
	const FRotator RawRotation = DesiredView.Rotation;
#if ENABLE_DRAW_DEBUG
	const int32 DebugLevel = FMath::Max(CVarCameraDebug.GetValueOnGameThread(),
		(bDrawCollision || GetWorld()->TimeSince(LastCollisionDebugTime) < 1.f) ? 1 : 0);
	Input.bCollectDebug = DebugLevel >= 2;
	Input.MaxDebugQueries = FMath::Clamp(CVarCameraDebugMaxQueries.GetValueOnGameThread(), 1, 256);
#endif
	CollisionDiagnostics = IMGCameraCollision::Solve(*GetWorld(), Input, Request.Settings, DeltaTime, CollisionState);
	DesiredView.Location = CollisionDiagnostics.Location;
	DesiredView.Rotation = CollisionDiagnostics.Rotation;
	if (CollisionDiagnostics.bHasSafeRoot && CameraModeView.Framing.bValid)
	{
		FIMGCameraFramingResult Frame = CameraModeView.Framing;
		FVector ReferenceLocation = RawLocation;
		FRotator ReferenceRotation = RawRotation;
		if (!Frame.FocusPoint.IsSet() || Frame.FocusWeight <= UE_SMALL_NUMBER)
		{
			const FQuat OrbitDelta = CollisionDiagnostics.Rotation.Quaternion() * RawRotation.Quaternion().Inverse();
			Frame.OrbitPivot = CollisionDiagnostics.Root + OrbitDelta.RotateVector(Frame.OrbitPivot - CollisionDiagnostics.Root);
			Frame.Rotation = (OrbitDelta * Frame.Rotation.Quaternion()).Rotator();
			ReferenceLocation = CollisionDiagnostics.DesiredLocation;
			ReferenceRotation = CollisionDiagnostics.Rotation;
		}
		DesiredView.Rotation = IMGCameraFraming::ResolveLookAt(Frame, ReferenceLocation, ReferenceRotation,
			CollisionDiagnostics.Root, DesiredView.Location, CollisionDiagnostics.Rotation);
	}
	Input.Location = DesiredView.Location;
	Input.Rotation = DesiredView.Rotation;
	if (Request.Settings.bScanNearClip)
	{
		DesiredView.PerspectiveNearClipPlane = IMGCameraCollision::ScanNearClip(*GetWorld(), Input, Request.Settings,
			GetPerspectiveHalfFovTangents(DesiredView), CollisionDiagnostics);
	}
	CollisionDiagnostics.NearClipPlane = DesiredView.PerspectiveNearClipPlane;
#if ENABLE_DRAW_DEBUG
	if (DebugLevel > 0)
	{
		const float RequestedDuration = CVarCameraDebugDuration.GetValueOnGameThread();
		const float Duration = FMath::IsFinite(RequestedDuration) ? FMath::Clamp(RequestedDuration, 0.f, 2.f) : 0.f;
		if (!Input.Origin.ContainsNaN())
		{
			DrawDebugSphere(GetWorld(), Input.Origin, 5.f, 12, FColor::Orange, false, Duration);
			DrawDebugLine(GetWorld(), Input.Origin, CollisionDiagnostics.Origin, FColor::Orange, false, Duration);
		}
		DrawDebugSphere(GetWorld(), CollisionDiagnostics.Origin, 5.f, 12, FColor::Orange, false, Duration);
		DrawDebugSphere(GetWorld(), CollisionDiagnostics.Root, CollisionDiagnostics.ProbeRadius, 16,
			CollisionDiagnostics.bHasSafeRoot ? FColor::Cyan : FColor::Red, false, Duration);
		DrawDebugLine(GetWorld(), CollisionDiagnostics.Root, CollisionDiagnostics.DesiredLocation, FColor::Yellow, false, Duration);
		DrawDebugSphere(GetWorld(), CollisionDiagnostics.DesiredLocation, 5.f, 12, FColor::White, false, Duration);
		DrawDebugSphere(GetWorld(), CollisionDiagnostics.Location,
			FMath::Min(Request.Settings.CameraBodyRadius, FMath::Max(0.5f, CollisionDiagnostics.ProbeRadius)), 16,
			CollisionDiagnostics.bHasSafeLocation ? FColor::Green : FColor::Red, false, Duration);
		if (DebugLevel >= 2)
		{
			EIMGCameraCollisionStage PreviousStage = EIMGCameraCollisionStage::NearClip;
			bool bHasPreviousStage = false;
			for (const auto& Query : CollisionDiagnostics.DebugQueries)
			{
				DrawQuery(GetWorld(), Query, Duration);
				if (!bHasPreviousStage || Query.Stage != PreviousStage)
				{
					DrawDebugString(GetWorld(), Query.Start, StageName(Query.Stage), nullptr, FColor::White, Duration, false, 0.7f);
					PreviousStage = Query.Stage;
					bHasPreviousStage = true;
				}
			}
			DrawDebugString(GetWorld(), CollisionDiagnostics.Root + FVector(0, 0, 30), FString::Printf(
				TEXT("desired %.1f > occlusion %.1f (hard %.1f) > spring %.1f > clip %.1f > push %.1f + radius %.1f > final %.1f\nblocked %d clipping %d recovery %d paused %d, queries %d%s"),
				CollisionDiagnostics.DesiredDistance, CollisionDiagnostics.OcclusionDistance, CollisionDiagnostics.HardLimitDistance, CollisionDiagnostics.DampedDistance,
				CollisionDiagnostics.ClippingDistance, CollisionDiagnostics.PushBeyondDistance, CollisionDiagnostics.PullBackRadius,
				CollisionDiagnostics.FinalDistance, CollisionDiagnostics.bOccluded, CollisionDiagnostics.bClipping,
				CollisionDiagnostics.bRecovering, CollisionDiagnostics.bPausedRecovery, CollisionDiagnostics.QueryCount,
				CollisionDiagnostics.bDebugTruncated ? TEXT(" (draw capped)") : TEXT("")), nullptr, FColor::White, Duration);
		}
	}
#endif
}

FVector2D UIMGCameraComponent::GetPerspectiveHalfFovTangents(const FMinimalViewInfo& View) const
{
	if (View.ProjectionMode != ECameraProjectionMode::Perspective || !FMath::IsFinite(View.FOV)
		|| View.FOV <= 0.f || View.FOV >= 180.f || !FMath::IsFinite(View.AspectRatio) || View.AspectRatio <= UE_SMALL_NUMBER)
	{
		return FVector2D::ZeroVector;
	}
	FMinimalViewInfo ViewInfo = View;
	FMatrix Projection = ViewInfo.CalculateProjectionMatrix();
	const APawn* Pawn = Cast<APawn>(GetTargetActor());
	const APlayerController* Controller = Pawn ? Pawn->GetController<APlayerController>() : nullptr;
	if (Controller)
	{
		if (const ULocalPlayer* LocalPlayer = Controller->GetLocalPlayer())
		{
			int32 Width = 0;
			int32 Height = 0;
			Controller->GetViewportSize(Width, Height);
			Width = FMath::TruncToInt(Width * LocalPlayer->Size.X);
			Height = FMath::TruncToInt(Height * LocalPlayer->Size.Y);
			if (Width > 0 && Height > 0)
			{
				FSceneViewProjectionData ProjectionData;
				const FIntRect ViewRect(0, 0, Width, Height);
				ProjectionData.SetViewRectangle(ViewRect);
				FMinimalViewInfo::CalculateProjectionMatrixGivenViewRectangle(
					ViewInfo, LocalPlayer->AspectRatioAxisConstraint, ViewRect, ProjectionData);
				Projection = ProjectionData.ProjectionMatrix;
			}
		}
	}
	if (!FMath::IsFinite(Projection.M[0][0]) || Projection.M[0][0] <= UE_SMALL_NUMBER
		|| !FMath::IsFinite(Projection.M[1][1]) || Projection.M[1][1] <= UE_SMALL_NUMBER)
	{
		return FVector2D::ZeroVector;
	}
	return FVector2D(1.0 / Projection.M[0][0], 1.0 / Projection.M[1][1]);
}

void UIMGCameraComponent::UpdateCameraModes()
{
	check(CameraModeStack);

	if (CameraModeStack->IsStackActivate())
	{
		TSubclassOf<UIMGCameraMode> CameraMode;
		if (DetermineCameraModeDelegate.IsBound())
		{
			CameraMode = DetermineCameraModeDelegate.Execute();
		}
		if (bEndingPlay || !IsRegistered() || !IsActive() || !CameraModeStack->IsStackActivate()) { return; }

		if (!CameraMode || CameraMode->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			CameraMode = UIMGCameraMode_Default::StaticClass();
		}
		CameraModeStack->PushCameraMode(CameraMode);
	}
}

void UIMGCameraComponent::ResetInterpolation()
{
	StopAllCameraShakes(true);
	ResetCollision();
	if (CameraModeStack)
	{
		CameraModeStack->ResetInterpolation();
	}
}

void UIMGCameraComponent::NotifyCameraCut()
{
	Super::NotifyCameraCut();
	ResetInterpolation();
}

void UIMGCameraComponent::DrawDebug(UCanvas* Canvas) const
{
	check(Canvas);

	FDisplayDebugManager& DisplayDebugManager = Canvas->DisplayDebugManager;

	DisplayDebugManager.SetFont(GEngine->GetSmallFont());
	DisplayDebugManager.SetDrawColor(FColor::Yellow);
	DisplayDebugManager.DrawString(FString::Printf(TEXT("IMGCameraComponent: %s"), *GetNameSafe(GetTargetActor())));

	DisplayDebugManager.SetDrawColor(FColor::White);
	DisplayDebugManager.DrawString(FString::Printf(TEXT("   Location: %s"), *GetComponentLocation().ToCompactString()));
	DisplayDebugManager.DrawString(FString::Printf(TEXT("   Rotation: %s"), *GetComponentRotation().ToCompactString()));
	DisplayDebugManager.DrawString(FString::Printf(TEXT("   FOV: %f"), FieldOfView));
	DisplayDebugManager.DrawString(FString::Printf(TEXT("   Shake instances: %d"), CameraShakes.Num()));
	for (const UIMGCameraShakeInstance* Shake : CameraShakes)
	{
		if (IsValid(Shake) && Shake->GetIMGPattern() && IsValid(Shake->GetDefinition()))
		{
			DisplayDebugManager.DrawString(FString::Printf(TEXT("      %s [%s]: envelope %.3f, scale %.2f, frequency %.2f, %s"),
				*GetNameSafe(Shake->GetDefinition()), *Shake->GetDefinition()->ShakeCategory.ToString(), Shake->GetIMGPattern()->GetEnvelopeLevel(),
				Shake->ShakeScale, Shake->GetIMGPattern()->GetFrequencyScale(),
				Shake->IsReleasing() ? TEXT("releasing") : Shake->IsContinuous() ? TEXT("continuous") : TEXT("one-shot")));
		}
	}
	if (CollisionTarget.IsValid())
	{
		DisplayDebugManager.DrawString(FString::Printf(TEXT("   Collision: radius %.1f cm, distance %.3f, near %.2f cm, queries %d, filtered %d, root %s, camera %s, initial-overlap %s, recovered-origin %s, paused %s, push-budget %s"),
			CollisionDiagnostics.ProbeRadius, CollisionDiagnostics.DistanceRatio, CollisionDiagnostics.NearClipPlane, CollisionDiagnostics.QueryCount,
			CollisionDiagnostics.IgnoredHitCount, CollisionDiagnostics.bHasSafeRoot ? TEXT("safe") : TEXT("invalid"),
			CollisionDiagnostics.bHasSafeLocation ? TEXT("safe") : TEXT("invalid"),
			CollisionDiagnostics.bInitialPenetration ? TEXT("yes") : TEXT("no"), CollisionDiagnostics.bRecoveredOrigin ? TEXT("yes") : TEXT("no"),
			CollisionDiagnostics.bPausedRecovery ? TEXT("yes") : TEXT("no"),
			CollisionDiagnostics.bPushBeyondBudgetExceeded ? TEXT("exhausted") : TEXT("ok")));
		DisplayDebugManager.DrawString(FString::Printf(
			TEXT("   Distances cm: desired %.1f > occlusion %.1f (hard %.1f) > spring %.1f > clip %.1f > push %.1f + radius %.1f > final %.1f; blocked %d, clipping %d, recovering %d"),
			CollisionDiagnostics.DesiredDistance, CollisionDiagnostics.OcclusionDistance, CollisionDiagnostics.HardLimitDistance, CollisionDiagnostics.DampedDistance,
			CollisionDiagnostics.ClippingDistance, CollisionDiagnostics.PushBeyondDistance, CollisionDiagnostics.PullBackRadius,
			CollisionDiagnostics.FinalDistance, CollisionDiagnostics.bOccluded, CollisionDiagnostics.bClipping, CollisionDiagnostics.bRecovering));
		for (const auto& Component : CollisionDiagnostics.HitComponents)
		{
			if (Component.IsValid()) { DisplayDebugManager.DrawString(FString::Printf(TEXT("      Hit: %s"), *Component->GetPathName())); }
		}
		if (const UWorld* World = GetWorld())
		{
			LastCollisionDebugTime = World->GetTimeSeconds();
		}
	}

	if (CameraModeStack) { CameraModeStack->DrawDebug(Canvas); }
}

void UIMGCameraComponent::GetBlendInfo(float& OutWeightOfTopLayer, FGameplayTag& OutTagOfTopLayer) const
{
	OutWeightOfTopLayer = 0.f;
	OutTagOfTopLayer = FGameplayTag();
	if (IsRegistered() && IsActive() && !bEndingPlay && CameraModeStack)
	{
		CameraModeStack->GetBlendInfo(/*out*/ OutWeightOfTopLayer, /*out*/ OutTagOfTopLayer);
	}
}
