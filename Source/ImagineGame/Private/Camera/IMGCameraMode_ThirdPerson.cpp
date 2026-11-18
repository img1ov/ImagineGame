
#include "Camera/IMGCameraMode_ThirdPerson.h"
#include "Animation/AnimInstance.h"
#include "Camera/IMGCameraComponent.h"
#include "Camera/IMGCameraMode.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Curves/CurveVector.h"
#include "Engine/Canvas.h"
#include "Engine/SkeletalMesh.h"
#include "Camera/IMGCameraAssistInterface.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGCameraMode_ThirdPerson)

UIMGCameraMode_ThirdPerson::UIMGCameraMode_ThirdPerson()
{
	ViewPitchMin = -70.f;
	ViewPitchMax = 45.f;

	FRuntimeFloatCurve* Curves[] = { &TargetOffsetX, &TargetOffsetY, &TargetOffsetZ };
	const float Values[] = { -300.f, 30.f, 0.f };
	const float DefaultPitches[] = { ViewPitchMin, 0.f, ViewPitchMax };
	for (int32 Channel = 0; Channel < UE_ARRAY_COUNT(Curves); ++Channel)
	{
		FRuntimeFloatCurve* Curve = Curves[Channel];
		for (const float Pitch : DefaultPitches)
		{
			const FKeyHandle Key = Curve->EditorCurveData.AddKey(Pitch, Values[Channel]);
			Curve->EditorCurveData.SetKeyInterpMode(Key, RCIM_Cubic, false);
			Curve->EditorCurveData.SetKeyTangentMode(Key, RCTM_Auto, false);
		}
		Curve->EditorCurveData.AutoSetTangents();
	}
}

FIMGCameraFramingSettings UIMGCameraMode_ThirdPerson::EvaluateFramingSettings(float OrbitPitch) const
{
	FIMGCameraFramingSettings Settings;
	Settings.PostCollisionLookAtWeight = PostCollisionLookAtWeight;
	Settings.MinFocusDistance = MinFocusDistance;
	Settings.OffAxisTolerance = OffAxisTolerance;
	FVector Offset(-300.f, 30.f, 0.f);
	// Modes are cached; read current class defaults so edited curve keys remain visible.
	const UIMGCameraMode_ThirdPerson* Defaults = GetClass()->GetDefaultObject<UIMGCameraMode_ThirdPerson>();
	if (IsValid(Defaults) && FMath::IsFinite(OrbitPitch))
	{
		const bool bUseVectorAsset = !Defaults->bUseRuntimeFloatCurves && IsValid(Defaults->TargetOffsetCurve.Get());
		if (bUseVectorAsset)
		{
			const FVector Value = Defaults->TargetOffsetCurve->GetVectorValue(OrbitPitch);
			if (!Value.ContainsNaN()) { Offset = Value; }
		}
		else
		{
			const FRuntimeFloatCurve* Curves[] = { &Defaults->TargetOffsetX, &Defaults->TargetOffsetY, &Defaults->TargetOffsetZ };
			for (int32 Channel = 0; Channel < UE_ARRAY_COUNT(Curves); ++Channel)
			{
				const FRuntimeFloatCurve& Curve = *Curves[Channel];
				if (Curve.ExternalCurve && !IsValid(Curve.ExternalCurve.Get())) { continue; }
				const FRichCurve* RichCurve = Curve.GetRichCurveConst();
				if (!RichCurve->HasAnyData()) { continue; }
				const float Value = RichCurve->Eval(OrbitPitch);
				if (FMath::IsFinite(Value)) { Offset[Channel] = Value; }
			}
		}
	}
	Settings.OrbitDistance = -Offset.X;
	Settings.ShoulderOffset = Offset.Y;
	Settings.CameraVerticalOffset = Offset.Z;
	return Settings;
}

void UIMGCameraMode_ThirdPerson::UpdateView(float DeltaTime)
{
	UpdateForTarget();
	const TWeakObjectPtr<AActor> ViewTarget = GetTargetActor();
	const FRotator PreviousControlRotation = View.ControlRotation;

	const FVector DesiredPivotLocation = UpdateViewpoint(GetPivotLocation(), DeltaTime);
	const FVector PivotLocation = UpdateCameraLag(DesiredPivotLocation, DeltaTime)
		+ FVector::UpVector * UpdatePelvisOffset(DeltaTime);
	FRotator PivotRotation = GetPivotRotation();

	PivotRotation.Pitch = FMath::ClampAngle(PivotRotation.Pitch, ViewPitchMin, ViewPitchMax);
	const FRotator ControlRotation = PivotRotation;
	PivotRotation = UpdateRotationDamping(PivotRotation, DeltaTime);
	PivotRotation.Pitch = FMath::ClampAngle(PivotRotation.Pitch, ViewPitchMin, ViewPitchMax);

	View.ControlRotation = ControlRotation;
	View.FieldOfView = FieldOfView;
	FIMGCameraFramingInput FramingInput;
	FramingInput.Anchor = PivotLocation;
	FramingInput.OrbitRotation = PivotRotation;
	FramingInput.TargetRotation = ViewTarget.IsValid() ? ViewTarget->GetActorRotation() : FRotator::ZeroRotator;
	FramingInput.MinPitch = ViewPitchMin;
	FramingInput.MaxPitch = ViewPitchMax;
	bool bHasFocus = false;
	FVector FocusPoint = FVector::ZeroVector;
	GetFramingFocusPoint(ViewTarget.Get(), bHasFocus, FocusPoint);
	if (!ViewTarget.IsValid() || GetTargetActor() != ViewTarget.Get()) { ResetInterpolation(); return; }
	if (bHasFocus) { FramingInput.FocusPoint = FocusPoint; }
	View.Framing = IMGCameraFraming::Build(FramingInput, EvaluateFramingSettings(PivotRotation.Pitch));
	View.Location = View.Framing.bValid ? View.Framing.Location : PivotLocation;
	View.Rotation = View.Framing.bValid ? View.Framing.Rotation : PivotRotation;

	BuildCollisionRequest(View.Framing.bValid ? View.Framing.OrbitPivot : PivotLocation, PreviousControlRotation);
}

void UIMGCameraMode_ThirdPerson::GetFramingFocusPoint_Implementation(AActor* TargetActor, bool& bHasFocus, FVector& FocusPoint)
{
	bHasFocus = false;
	FocusPoint = FVector::ZeroVector;
}

FVector UIMGCameraMode_ThirdPerson::UpdateCameraLag(const FVector& DesiredPivotLocation, float DeltaTime)
{
	return LocationDampingState.Update(DesiredPivotLocation, DeltaTime, CameraLocationLagSpeed, CameraLagDampingRatio,
		bUseCameraLagSubstepping, CameraLagMaxTimeStep, CameraLagMaxDistance, bResetInterpolation || !bEnableCameraLag);
}

void UIMGCameraMode_ThirdPerson::ResetViewpoint()
{
	ViewpointDampingState.Reset();
	CachedViewRoot.Reset();
	PreviousViewReferenceOffset = FVector::ZeroVector;
	PreviousViewCapsuleHalfHeight = 0.f;
	bViewpointUsesCapsuleBase = false;
}

FVector UIMGCameraMode_ThirdPerson::UpdateViewpoint(const FVector& DesiredPivotLocation, float DeltaTime)
{
	const AActor* Target = GetTargetActor();
	const USceneComponent* Root = IsValid(Target) ? Target->GetRootComponent() : nullptr;
	if (CachedViewRoot.Get() != Root)
	{
		ResetViewpoint();
		CachedViewRoot = Root;
	}
	if (!IsValid(Root) || DesiredPivotLocation.ContainsNaN())
	{
		ResetViewpoint();
		return DesiredPivotLocation;
	}
	const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Root);
	const ACharacter* Character = Cast<ACharacter>(Target);
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	const bool bUseBase = Capsule && Movement && Movement->UpdatedComponent == Root && Movement->bCrouchMaintainsBaseLocation;
	const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 0.f;
	const FVector ReferenceAxis = bUseBase ? Movement->GetGravityDirection() : FVector::ZeroVector;
	const FVector ReferenceOffset = ReferenceAxis * HalfHeight;
	if (ViewpointDampingState.IsInitialized() && (bViewpointUsesCapsuleBase != bUseBase
		|| (bUseBase && !ReferenceAxis.Equals(PreviousViewReferenceOffset.GetSafeNormal(), UE_KINDA_SMALL_NUMBER))))
	{
		// Change reference rules using the previous size; this frame's resize still belongs to the new rule.
		ViewpointDampingState.Rebase(PreviousViewReferenceOffset - ReferenceAxis * PreviousViewCapsuleHalfHeight);
	}
	bViewpointUsesCapsuleBase = bUseBase;
	PreviousViewReferenceOffset = ReferenceOffset;
	PreviousViewCapsuleHalfHeight = HalfHeight;
	const FVector Reference = Root->GetComponentLocation() + ReferenceOffset;
	const float Strength = FMath::IsFinite(ViewpointSpringConstant)
		? FMath::Clamp(ViewpointSpringConstant, 0.f, 1000.f) : 20.f;
	const float Damping = FMath::IsFinite(ViewpointDampingRatio)
		? FMath::Clamp(ViewpointDampingRatio, 0.f, 10.f) : 1.f;
	const FVector Offset = ViewpointDampingState.Update(DesiredPivotLocation - Reference, DeltaTime,
		FMath::Sqrt(Strength), Damping, false, 1.f / 60.f, 0.f,
		bResetInterpolation || !FMath::IsFinite(DeltaTime) || DeltaTime > 32.f / 120.f);
	return Reference + Offset;
}

void UIMGCameraMode_ThirdPerson::ResetPelvisOffset()
{
	PelvisOffset = 0.0;
	PelvisOffsetVelocity = 0.0;
	bHasPelvisOffset = false;
	CachedPelvisMesh.Reset();
	CachedPelvisMeshAsset.Reset();
	CachedPelvisAnimInstance.Reset();
	CachedPelvisOffsetCurve = NAME_None;
	CachedPelvisBone = NAME_None;
}

double UIMGCameraMode_ThirdPerson::UpdatePelvisOffset(float DeltaTime)
{
	if (!bEnablePelvisOffset || PelvisOffsetCurveName.IsNone() || PelvisBoneName.IsNone()
		|| !FMath::IsFinite(DeltaTime) || DeltaTime < 0.f
		|| !FMath::IsFinite(PelvisOffsetSpringConstant) || PelvisOffsetSpringConstant < 0.f || PelvisOffsetSpringConstant > 1000.f
		|| !FMath::IsFinite(PelvisOffsetDampingRatio) || PelvisOffsetDampingRatio < 0.f || PelvisOffsetDampingRatio > 10.f)
	{
		ResetPelvisOffset();
		return 0.0;
	}

	const ACharacter* Character = Cast<ACharacter>(GetTargetActor());
	const USkeletalMeshComponent* Mesh = IsValid(Character) ? Character->GetMesh() : nullptr;
	const USkeletalMesh* MeshAsset = IsValid(Mesh) ? Mesh->GetSkeletalMeshAsset() : nullptr;
	const UAnimInstance* AnimInstance = IsValid(Mesh) ? Mesh->GetAnimInstance() : nullptr;
	if (!IsValid(Mesh) || !IsValid(MeshAsset) || !Mesh->IsRegistered()
		|| !IsValid(AnimInstance) || Mesh->GetBoneIndex(PelvisBoneName) == INDEX_NONE)
	{
		ResetPelvisOffset();
		return 0.0;
	}

	if (bResetInterpolation || CachedPelvisMesh.Get() != Mesh || CachedPelvisMeshAsset.Get() != MeshAsset
		|| CachedPelvisAnimInstance.Get() != AnimInstance
		|| CachedPelvisOffsetCurve != PelvisOffsetCurveName || CachedPelvisBone != PelvisBoneName)
	{
		ResetPelvisOffset();
	}
	CachedPelvisMesh = Mesh;
	CachedPelvisMeshAsset = MeshAsset;
	CachedPelvisAnimInstance = AnimInstance;
	CachedPelvisOffsetCurve = PelvisOffsetCurveName;
	CachedPelvisBone = PelvisBoneName;

	// Read the component's evaluated curves, including post-process animation and URO interpolation.
	float TargetOffset = 0.f;
	Mesh->GetCurveValue(PelvisOffsetCurveName, 0.f, TargetOffset);
	if (!FMath::IsFinite(TargetOffset))
	{
		ResetPelvisOffset();
		return 0.0;
	}

	if (!bHasPelvisOffset || PelvisOffsetSpringConstant <= UE_SMALL_NUMBER)
	{
		PelvisOffset = TargetOffset;
		PelvisOffsetVelocity = 0.0;
		bHasPelvisOffset = true;
	}
	else if (DeltaTime > UE_SMALL_NUMBER)
	{
		IMGCameraDamping::AdvanceSpring(double(TargetOffset), PelvisOffsetSpringConstant,
			PelvisOffsetDampingRatio, DeltaTime, PelvisOffset, PelvisOffsetVelocity);
	}

	if (!FMath::IsFinite(PelvisOffset) || !FMath::IsFinite(PelvisOffsetVelocity))
	{
		ResetPelvisOffset();
		return 0.0;
	}
	return PelvisOffset;
}

FRotator UIMGCameraMode_ThirdPerson::UpdateRotationDamping(const FRotator& TargetRotation, float DeltaTime)
{
	const UIMGCameraComponent* CameraComponent = GetIMGCameraComponent();
	if (!bEnableRotationDamping || !CameraComponent || CameraComponent->ProjectionMode != ECameraProjectionMode::Perspective
		|| !FMath::IsFinite(FieldOfView) || FieldOfView <= 0.f || FieldOfView >= 180.f
		|| !FMath::IsFinite(CameraComponent->AspectRatio) || CameraComponent->AspectRatio <= UE_SMALL_NUMBER)
	{
		RotationDampingState.Reset();
		return TargetRotation;
	}
	if (bResetInterpolation)
	{
		RotationDampingState.Reset();
	}

	FMinimalViewInfo ViewInfo;
	ViewInfo.FOV = FieldOfView;
	ViewInfo.AspectRatio = CameraComponent->AspectRatio;
	ViewInfo.bConstrainAspectRatio = CameraComponent->bConstrainAspectRatio;
	const FVector2D Tangents = CameraComponent->GetPerspectiveHalfFovTangents(ViewInfo);
	if (Tangents.X <= 0.0 || Tangents.Y <= 0.0)
	{
		RotationDampingState.Reset();
		return TargetRotation;
	}
	const FVector2D FieldOfViewDegrees(
		FMath::RadiansToDegrees(2.0 * FMath::Atan(Tangents.X)),
		FMath::RadiansToDegrees(2.0 * FMath::Atan(Tangents.Y)));
	return RotationDampingState.Update(TargetRotation, FieldOfViewDegrees, DeltaTime, RotationDampingSettings);
}

void UIMGCameraMode_ThirdPerson::OnActivation()
{
	Super::OnActivation();
	// Activation resets mode-local effects while the rendered collision history survives a blend.
	bResetInterpolation = true;
	++InterpolationResetRevision;
	LocationDampingState.Reset();
	ResetViewpoint();
	RotationDampingState.Reset();
	ResetPelvisOffset();
	CachedViewTarget.Reset();
	CachedViewController.Reset();
}

void UIMGCameraMode_ThirdPerson::OnDeactivation()
{
	RotationDampingState.Reset();
	ResetPelvisOffset();
	CachedViewTarget.Reset();
	CachedViewController.Reset();
	LocationDampingState.Reset();
	ResetViewpoint();
	Super::OnDeactivation();
}

void UIMGCameraMode_ThirdPerson::UpdateForTarget()
{
	const AActor* TargetActor = GetTargetActor();
	const APawn* TargetPawn = Cast<APawn>(TargetActor);
	const APlayerController* CandidateController = TargetPawn ? TargetPawn->GetController<APlayerController>() : nullptr;
	const APlayerController* Controller = IsValid(CandidateController) && !CandidateController->IsActorBeingDestroyed() ? CandidateController : nullptr;
	const TWeakObjectPtr<const AActor> CurrentTarget(TargetActor);
	const TWeakObjectPtr<const APlayerController> CurrentController(Controller);
	if (!CachedViewTarget.HasSameIndexAndSerialNumber(CurrentTarget) || !CachedViewController.HasSameIndexAndSerialNumber(CurrentController)
		|| (Controller && Controller->PlayerCameraManager && Controller->PlayerCameraManager->bGameCameraCutThisFrame))
	{
		bResetInterpolation = true;
	}
	CachedViewTarget = CurrentTarget;
	CachedViewController = CurrentController;
}

void UIMGCameraMode_ThirdPerson::DrawDebug(UCanvas* Canvas) const
{
	Super::DrawDebug(Canvas);

#if ENABLE_DRAW_DEBUG
	FDisplayDebugManager& DisplayDebugManager = Canvas->DisplayDebugManager;
	if (View.Framing.bValid)
	{
		DisplayDebugManager.DrawString(FString::Printf(TEXT("Framing: orbit %.1f cm, base %s, pivot %s, focus %.2f"),
			FVector::Distance(View.Framing.Location, View.Framing.OrbitPivot), *View.Framing.BasePivot.ToCompactString(),
			*View.Framing.OrbitPivot.ToCompactString(), View.Framing.FocusWeight));
	}
	if (bEnableCameraLag && LocationDampingState.IsInitialized())
	{
		DisplayDebugManager.DrawString(FString::Printf(TEXT("Lag: offset %.2f cm, velocity %.2f cm/s (sqrt(k) %.2f, damping %.2f)"),
			LocationDampingState.GetErrorDistance(), LocationDampingState.GetSpeed(),
			CameraLocationLagSpeed, CameraLagDampingRatio));
	}
	if (ViewpointDampingState.IsInitialized())
	{
		DisplayDebugManager.DrawString(FString::Printf(TEXT("Viewpoint: error %.2f cm (k %.2f, damping %.2f)"),
			ViewpointDampingState.GetErrorDistance(), ViewpointSpringConstant, ViewpointDampingRatio));
	}
	if (bEnablePelvisOffset)
	{
		DisplayDebugManager.DrawString(FString::Printf(TEXT("Pelvis offset [%s / %s]: %+.2f cm"),
			*PelvisOffsetCurveName.ToString(), *PelvisBoneName.ToString(), PelvisOffset));
	}

#endif
}

void UIMGCameraMode_ThirdPerson::BuildCollisionRequest(const FVector& PivotLocation, const FRotator& PreviousControlRotation)
{
	AActor* TargetActor = GetTargetActor();
	if (!bPreventPenetration || !IsValid(TargetActor)) { return; }

	APawn* Pawn = Cast<APawn>(TargetActor);
	AController* Controller = Pawn ? Pawn->GetController() : nullptr;
	AActor* Anchor = TargetActor;
	if (const IIMGCameraAssistInterface* Assist = Cast<IIMGCameraAssistInterface>(TargetActor))
	{
		const TOptional<AActor*> Alternative = Assist->GetCameraPreventPenetrationTarget();
		if (IsValid(Alternative.Get(nullptr))) { Anchor = Alternative.GetValue(); }
	}
	if (!IsValid(TargetActor) || !IsValid(Anchor)) { return; }

	FIMGCameraCollisionRequest Request;
	Request.Settings = CollisionSettings.Sanitized();
	Request.Target = TargetActor;
	Request.Anchor = Anchor;
	Request.Controller = Controller;
	Request.ReportPenetrationPercent = FMath::IsFinite(ReportPenetrationPercent)
		? FMath::Clamp(ReportPenetrationPercent, 0.f, 1.f) : 0.f;
	FIMGCameraCollisionInput& Input = Request.Input;
	Input.Origin = Anchor->GetActorLocation();
	Input.Fallback = Anchor == TargetActor ? GetPivotLocation() : Anchor->GetActorLocation();
	Input.Pivot = PivotLocation;
	Input.Location = View.Location;
	Input.Rotation = View.Rotation;
	Input.MinPitch = ViewPitchMin;
	Input.MaxPitch = ViewPitchMax;
	Input.MaxProbeRadius = Request.Settings.ProbeRadius;
	Input.bForcePushIn = bResetInterpolation
		|| !(View.ControlRotation - PreviousControlRotation).GetNormalized().IsNearlyZero(Request.Settings.LookInputAngleThreshold);

	if (const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Anchor->GetRootComponent()))
	{
		float Radius, HalfHeight;
		Capsule->GetScaledCapsuleSize(Radius, HalfHeight);
		Input.Origin = Capsule->GetComponentLocation();
		Input.MaxProbeRadius = FMath::Min(Request.Settings.ProbeRadius, FMath::Max(0.5f, Radius - Request.Settings.TargetRadiusMargin));
		const FVector Axis = Capsule->GetUpVector();
		const double SegmentHalfLength = FMath::Max(0.f, HalfHeight - Radius);
		const double AlongAxis = FVector::DotProduct(Input.Fallback - Input.Origin, Axis);
		const FVector AxisPoint = Input.Origin + Axis * FMath::Clamp(AlongAxis, -SegmentHalfLength, SegmentHalfLength);
		const float SafeRadius = FMath::Max(0.f, Radius - Input.MaxProbeRadius - Request.Settings.Clearance);
		Input.Fallback = AxisPoint + (Input.Fallback - AxisPoint).GetClampedToMaxSize(SafeRadius);
	}

	const TWeakObjectPtr<AActor> AssistActors[] = { Controller, TargetActor, Anchor };
	for (const TWeakObjectPtr<AActor>& WeakActor : AssistActors)
	{
		AActor* Actor = WeakActor.Get();
		if (!IsValid(Actor) || Request.AssistActors.Contains(Actor)) { continue; }
		Request.AssistActors.Add(Actor);
		Input.IgnoredActors.AddUnique(Actor);
		if (const IIMGCameraAssistInterface* Assist = Cast<IIMGCameraAssistInterface>(Actor))
		{
			TArray<const AActor*> Ignored;
			Assist->GetIgnoredActorsForCameraPentration(Ignored);
			for (const AActor* Other : Ignored) { if (IsValid(Other)) { Input.IgnoredActors.AddUnique(Other); } }
			if (!IsValid(Actor)) { continue; }
			TArray<const UPrimitiveComponent*> OcclusionComponents;
			Assist->GetIgnoredComponentsForCameraOcclusion(OcclusionComponents);
			for (const UPrimitiveComponent* Component : OcclusionComponents)
			{
				if (IsValid(Component)) { Input.IgnoredOcclusionComponents.AddUnique(Component); }
			}
			if (!IsValid(Actor)) { continue; }
			TArray<const AActor*> PushBeyond;
			Assist->GetActorsToPushCameraBeyond(PushBeyond);
			for (const AActor* Other : PushBeyond) { if (IsValid(Other)) { Input.PushBeyondActors.AddUnique(Other); } }
		}
	}
	if (bIgnoreTargetAttachments && IsValid(TargetActor) && IsValid(Anchor))
	{
		TArray<AActor*> Attached;
		TargetActor->GetAttachedActors(Attached, true, true);
		if (Anchor != TargetActor) { Anchor->GetAttachedActors(Attached, false, true); }
		for (const AActor* Actor : Attached) { if (IsValid(Actor)) { Input.IgnoredActors.AddUnique(Actor); } }
	}
	if (IsValid(TargetActor) && IsValid(Anchor) && GetTargetActor() == TargetActor
		&& TargetActor->GetWorld() == GetWorld() && Anchor->GetWorld() == GetWorld()
		&& GetIMGCameraComponent()->IsRegistered())
	{
		View.Collision = MoveTemp(Request);
	}
}
