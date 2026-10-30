
#include "Camera/IMGCameraMode_ThirdPerson.h"
#include "Animation/AnimInstance.h"
#include "Camera/IMGCameraComponent.h"
#include "Camera/IMGCameraMode.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/IMGPenetrationAvoidanceFeeler.h"
#include "Curves/CurveVector.h"
#include "Engine/Canvas.h"
#include "Engine/LocalPlayer.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CameraBlockingVolume.h"
#include "Camera/IMGCameraAssistInterface.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Math/RotationMatrix.h"
#include "SceneView.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGCameraMode_ThirdPerson)

namespace IMGCameraMode_ThirdPerson_Statics
{
	static const FName NAME_IgnoreCameraCollision = TEXT("IgnoreCameraCollision");

	void AdvancePelvisOffsetSpring(double Target, double SpringConstant, double DampingRatio, double DeltaTime,
		double& Offset, double& Velocity)
	{
		const double Displacement = Offset - Target;
		const double HalfDamping = DampingRatio * FMath::Sqrt(SpringConstant);
		const double OmegaSquared = SpringConstant - FMath::Square(HalfDamping);
		double NextDisplacement;
		if (OmegaSquared >= UE_SMALL_NUMBER)
		{
			const double Omega = FMath::Sqrt(OmegaSquared);
			const double C = (Velocity + HalfDamping * Displacement) / Omega;
			const double Sin = FMath::Sin(Omega * DeltaTime);
			const double Cos = FMath::Cos(Omega * DeltaTime);
			const double Decay = FMath::Exp(-HalfDamping * DeltaTime);
			const double Wave = Displacement * Cos + C * Sin;
			NextDisplacement = Decay * Wave;
			Velocity = Decay * (-HalfDamping * Wave + Omega * (C * Cos - Displacement * Sin));
		}
		else
		{
			// GTA uses only the slow exponential mode here, including at critical damping.
			const double Rate = -HalfDamping + FMath::Sqrt(FMath::Max(-OmegaSquared, 0.0));
			NextDisplacement = Displacement * FMath::Exp(Rate * DeltaTime);
			Velocity = Rate * NextDisplacement;
		}
		Offset = Target + NextDisplacement;
	}
}

UIMGCameraMode_ThirdPerson::UIMGCameraMode_ThirdPerson()
{
	TargetOffsetCurve = nullptr;

	PenetrationAvoidanceFeelers.Add(FIMGPenetrationAvoidanceFeeler(FRotator(+00.0f, +00.0f, 0.0f), 1.00f, 1.00f, 14.f, 0));
	PenetrationAvoidanceFeelers.Add(FIMGPenetrationAvoidanceFeeler(FRotator(+00.0f, +16.0f, 0.0f), 0.75f, 0.75f, 00.f, 3));
	PenetrationAvoidanceFeelers.Add(FIMGPenetrationAvoidanceFeeler(FRotator(+00.0f, -16.0f, 0.0f), 0.75f, 0.75f, 00.f, 3));
	PenetrationAvoidanceFeelers.Add(FIMGPenetrationAvoidanceFeeler(FRotator(+00.0f, +32.0f, 0.0f), 0.50f, 0.50f, 00.f, 5));
	PenetrationAvoidanceFeelers.Add(FIMGPenetrationAvoidanceFeeler(FRotator(+00.0f, -32.0f, 0.0f), 0.50f, 0.50f, 00.f, 5));
	PenetrationAvoidanceFeelers.Add(FIMGPenetrationAvoidanceFeeler(FRotator(+20.0f, +00.0f, 0.0f), 1.00f, 1.00f, 00.f, 4));
	PenetrationAvoidanceFeelers.Add(FIMGPenetrationAvoidanceFeeler(FRotator(-20.0f, +00.0f, 0.0f), 0.50f, 0.50f, 00.f, 4));
}

void UIMGCameraMode_ThirdPerson::UpdateView(float DeltaTime)
{
	UpdateForTarget();
	UpdateStanceOffset(DeltaTime);

	const FVector DesiredPivotLocation = GetPivotLocation();
	const FVector PivotLocation = UpdateCameraLag(DesiredPivotLocation, DeltaTime) + CurrentStanceOffset
		+ FVector::UpVector * UpdatePelvisOffset(DeltaTime);
	FRotator PivotRotation = GetPivotRotation();

	PivotRotation.Pitch = FMath::ClampAngle(PivotRotation.Pitch, ViewPitchMin, ViewPitchMax);
	const FRotator ControlRotation = PivotRotation;
	PivotRotation = UpdateRotationDamping(PivotRotation, DeltaTime);
	PivotRotation.Pitch = FMath::ClampAngle(PivotRotation.Pitch, ViewPitchMin, ViewPitchMax);

	View.Location = PivotLocation;
	View.Rotation = PivotRotation;
	View.ControlRotation = ControlRotation;
	View.FieldOfView = FieldOfView;

	// Apply third person offset using pitch.
	if (!bUseRuntimeFloatCurves)
	{
		if (TargetOffsetCurve)
		{
			const FVector TargetOffset = TargetOffsetCurve->GetVectorValue(PivotRotation.Pitch);
			View.Location = PivotLocation + PivotRotation.RotateVector(TargetOffset);
		}
	}
	else
	{
		FVector TargetOffset(0.0f);

		TargetOffset.X = TargetOffsetX.GetRichCurveConst()->Eval(PivotRotation.Pitch);
		TargetOffset.Y = TargetOffsetY.GetRichCurveConst()->Eval(PivotRotation.Pitch);
		TargetOffset.Z = TargetOffsetZ.GetRichCurveConst()->Eval(PivotRotation.Pitch);

		View.Location = PivotLocation + PivotRotation.RotateVector(TargetOffset);
	}

	UpdateShake(DeltaTime);

	// Include shake displacement in the final penetration check.
	UpdatePreventPenetration(DeltaTime);
}

void UIMGCameraMode_ThirdPerson::GetShakeInput_Implementation(AActor* TargetActor, float DeltaTime,
	bool& bActive, float& AmplitudeScale, float& FrequencyScale)
{
	bActive = false;
	AmplitudeScale = 1.f;
	FrequencyScale = 1.f;
}

void UIMGCameraMode_ThirdPerson::ResetShake()
{
	ShakeState.Reset();
	ShakeAmplitudeScale = 1.f;
	ShakeFrequencyScale = 1.f;
}

void UIMGCameraMode_ThirdPerson::UpdateShake(float DeltaTime)
{
	AActor* TargetActor = GetTargetActor();
	if (!bEnableShake || !IsValid(TargetActor))
	{
		ResetShake();
		return;
	}
	if (bResetInterpolation)
	{
		ResetShake();
	}
	bool bActive = false;
	float AmplitudeScale = 1.f;
	float FrequencyScale = 1.f;
	GetShakeInput(TargetActor, DeltaTime, bActive, AmplitudeScale, FrequencyScale);
	if (!bEnableShake || !IsValid(TargetActor) || GetTargetActor() != TargetActor)
	{
		ResetShake();
		return;
	}
	if (bActive)
	{
		if (!FMath::IsFinite(AmplitudeScale) || AmplitudeScale < 0.f
			|| !FMath::IsFinite(FrequencyScale) || FrequencyScale < 0.f)
		{
			ResetShake();
			return;
		}
		ShakeAmplitudeScale = AmplitudeScale;
		ShakeFrequencyScale = FrequencyScale;
		ShakeState.Start(ShakeSettings);
	}
	else
	{
		// Keep the last requested scales during release, even when a gait curve has already returned to zero.
		ShakeState.Release();
	}

	FVector LocationOffset;
	FRotator RotationOffset;
	float FOVOffset;
	ShakeState.Update(DeltaTime, ShakeAmplitudeScale, ShakeFrequencyScale, ShakeSettings,
		LocationOffset, RotationOffset, FOVOffset);
	View.Location += View.Rotation.RotateVector(LocationOffset);
	if (!RotationOffset.IsNearlyZero())
	{
		View.Rotation = (View.Rotation.Quaternion() * RotationOffset.GetNormalized().Quaternion()).Rotator();
	}
	View.FieldOfView = FMath::Clamp(View.FieldOfView + FOVOffset, 5.f, 170.f);
}

FVector UIMGCameraMode_ThirdPerson::UpdateCameraLag(const FVector& DesiredPivotLocation, float DeltaTime)
{
	if (!bHasLaggedPivotLocation || bResetInterpolation || !bEnableCameraLag || CameraLocationLagSpeed <= 0.0f)
	{
		LaggedPivotLocation = DesiredPivotLocation;
		bHasLaggedPivotLocation = true;
		return LaggedPivotLocation;
	}
	if (!FMath::IsFinite(DeltaTime) || DeltaTime < 0.f || !FMath::IsFinite(CameraLocationLagSpeed))
	{
		LaggedPivotLocation = DesiredPivotLocation;
		return LaggedPivotLocation;
	}

	if (bUseCameraLagSubstepping && DeltaTime > CameraLagMaxTimeStep)
	{
		constexpr int32 MaxSubsteps = 32;
		const float MaxTimeStep = FMath::Max(CameraLagMaxTimeStep, 1.0f / 200.0f);
		if (!FMath::IsFinite(MaxTimeStep) || DeltaTime > MaxTimeStep * MaxSubsteps)
		{
			LaggedPivotLocation = DesiredPivotLocation;
			return LaggedPivotLocation;
		}
		const FVector PivotMovementStep = (DesiredPivotLocation - LaggedPivotLocation) / DeltaTime;
		FVector LerpTarget = LaggedPivotLocation;
		float RemainingTime = DeltaTime;

		for (int32 StepIndex = 0; StepIndex < MaxSubsteps && RemainingTime > UE_KINDA_SMALL_NUMBER; ++StepIndex)
		{
			const float Step = FMath::Min(MaxTimeStep, RemainingTime);
			LerpTarget += PivotMovementStep * Step;
			RemainingTime -= Step;
			LaggedPivotLocation = FMath::VInterpTo(LaggedPivotLocation, LerpTarget, Step, CameraLocationLagSpeed);
		}
	}
	else
	{
		LaggedPivotLocation = FMath::VInterpTo(LaggedPivotLocation, DesiredPivotLocation, DeltaTime, CameraLocationLagSpeed);
	}

	if (CameraLagMaxDistance > 0.0f)
	{
		LaggedPivotLocation = DesiredPivotLocation
			+ (LaggedPivotLocation - DesiredPivotLocation).GetClampedToMaxSize(CameraLagMaxDistance);
	}

	return LaggedPivotLocation;
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
		IMGCameraMode_ThirdPerson_Statics::AdvancePelvisOffsetSpring(TargetOffset, PelvisOffsetSpringConstant,
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

	// Use this mode's FOV and UE's projection rules, including the local player's split-screen rectangle.
	FMinimalViewInfo ViewInfo;
	ViewInfo.FOV = FieldOfView;
	ViewInfo.AspectRatio = CameraComponent->AspectRatio;
	ViewInfo.bConstrainAspectRatio = CameraComponent->bConstrainAspectRatio;
	FMatrix Projection = ViewInfo.CalculateProjectionMatrix();
	if (const APlayerController* Controller = CachedViewController.Get())
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

	if (ViewInfo.ProjectionMode != ECameraProjectionMode::Perspective
		|| !FMath::IsFinite(Projection.M[0][0]) || Projection.M[0][0] <= UE_SMALL_NUMBER
		|| !FMath::IsFinite(Projection.M[1][1]) || Projection.M[1][1] <= UE_SMALL_NUMBER)
	{
		RotationDampingState.Reset();
		return TargetRotation;
	}

	const FVector2D FieldOfViewDegrees(
		FMath::RadiansToDegrees(2.0 * FMath::Atan(1.0 / Projection.M[0][0])),
		FMath::RadiansToDegrees(2.0 * FMath::Atan(1.0 / Projection.M[1][1])));
	return RotationDampingState.Update(TargetRotation, FieldOfViewDegrees, DeltaTime, RotationDampingSettings);
}

void UIMGCameraMode_ThirdPerson::OnActivation()
{
	Super::OnActivation();
	ResetInterpolation();
	bHasLaggedPivotLocation = false;
	RotationDampingState.Reset();
	ResetShake();
	ResetPelvisOffset();
	CachedViewTarget.Reset();
	CachedViewController.Reset();
}

void UIMGCameraMode_ThirdPerson::OnDeactivation()
{
	RotationDampingState.Reset();
	ResetShake();
	ResetPelvisOffset();
	CachedViewTarget.Reset();
	CachedViewController.Reset();
	bHasLaggedPivotLocation = false;
	Super::OnDeactivation();
}

void UIMGCameraMode_ThirdPerson::UpdateForTarget()
{
	const AActor* TargetActor = GetTargetActor();
	const APawn* TargetPawn = Cast<APawn>(TargetActor);
	const APlayerController* Controller = TargetPawn ? TargetPawn->GetController<APlayerController>() : nullptr;
	if (CachedViewTarget.Get() != TargetActor || CachedViewController.Get() != Controller
		|| (Controller && Controller->PlayerCameraManager && Controller->PlayerCameraManager->bGameCameraCutThisFrame))
	{
		bResetInterpolation = true;
	}
	CachedViewTarget = TargetActor;
	CachedViewController = Controller;

	if (const ACharacter* TargetCharacter = Cast<ACharacter>(TargetActor))
	{
		// ACharacter owns the active eye height. Native crouch updates it through
		// RecalculateBaseEyeHeight, while custom stances can update it in their
		// lifecycle callbacks without introducing a camera dependency.
		const ACharacter* TargetCharacterCDO = TargetCharacter->GetClass()->GetDefaultObject<ACharacter>();
		const float EyeHeightAdjustment = TargetCharacter->BaseEyeHeight - TargetCharacterCDO->BaseEyeHeight;
		SetTargetStanceOffset(FVector(0.0f, 0.0f, EyeHeightAdjustment));
		return;
	}

	SetTargetStanceOffset(FVector::ZeroVector);
}

void UIMGCameraMode_ThirdPerson::DrawDebug(UCanvas* Canvas) const
{
	Super::DrawDebug(Canvas);

#if ENABLE_DRAW_DEBUG
	FDisplayDebugManager& DisplayDebugManager = Canvas->DisplayDebugManager;
	if (bEnablePelvisOffset)
	{
		DisplayDebugManager.DrawString(FString::Printf(TEXT("Pelvis offset [%s / %s]: %+.2f cm"),
			*PelvisOffsetCurveName.ToString(), *PelvisBoneName.ToString(), PelvisOffset));
	}
	if (bEnableShake)
	{
		DisplayDebugManager.DrawString(FString::Printf(TEXT("Shake: envelope %.2f, amplitude %.2f, frequency %.2f (%s)"),
			ShakeState.GetEnvelopeLevel(), ShakeAmplitudeScale, ShakeFrequencyScale,
			ShakeState.IsReleasing() ? TEXT("releasing") : ShakeState.IsActive() ? TEXT("active") : TEXT("inactive")));
	}
	for (int i = 0; i < DebugActorsHitDuringCameraPenetration.Num(); i++)
	{
		DisplayDebugManager.DrawString(
			FString::Printf(TEXT("HitActorDuringPenetration[%d]: %s")
				, i
				, *DebugActorsHitDuringCameraPenetration[i]->GetName()));
	}

	LastDrawDebugTime = GetWorld()->GetTimeSeconds();
#endif
}

void UIMGCameraMode_ThirdPerson::UpdatePreventPenetration(float DeltaTime)
{
	if (!bPreventPenetration || PenetrationAvoidanceFeelers.IsEmpty())
	{
		return;
	}

	AActor* TargetActor = GetTargetActor();
	if (!IsValid(TargetActor))
	{
		return;
	}

	APawn* TargetPawn = Cast<APawn>(TargetActor);
	AController* TargetController = TargetPawn ? TargetPawn->GetController() : nullptr;
	IIMGCameraAssistInterface* TargetActorAssist = Cast<IIMGCameraAssistInterface>(TargetActor);

	TOptional<AActor*> OptionalPPTarget = TargetActorAssist ? TargetActorAssist->GetCameraPreventPenetrationTarget() : TOptional<AActor*>();
	AActor* PPActor = OptionalPPTarget.Get(nullptr);
	if (!IsValid(PPActor))
	{
		PPActor = TargetActor;
	}
	if (!IsValid(PPActor))
	{
		return;
	}

	const UPrimitiveComponent* PPActorRootComponent = Cast<UPrimitiveComponent>(PPActor->GetRootComponent());
	if (PPActorRootComponent)
	{
		// Attempt at picking SafeLocation automatically, so we reduce camera translation when aiming.
		// Our camera is our reticle, so we want to preserve our aim and keep that as steady and smooth as possible.
		// Pick closest point on capsule to our aim line.
		FVector ClosestPointOnLineToCapsuleCenter;
		FVector SafeLocation = PPActor->GetActorLocation();
		FMath::PointDistToLine(SafeLocation, View.Rotation.Vector(), View.Location, ClosestPointOnLineToCapsuleCenter);

		const float PushInDistance = FMath::Max(0.0f, PenetrationAvoidanceFeelers[0].Extent + CollisionPushOutDistance);
		if (const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(PPActorRootComponent))
		{
			float Radius, HalfHeight;
			Capsule->GetScaledCapsuleSize(Radius, HalfHeight);
			const FVector CapsuleCenter = Capsule->GetComponentLocation();
			const FVector CapsuleAxis = Capsule->GetUpVector();
			const float SegmentHalfLength = FMath::Max(0.0f, HalfHeight - Radius);
			const float SafeRadius = FMath::Max(0.0f, Radius - PushInDistance);
			const double DistanceAlongAxis = FVector::DotProduct(ClosestPointOnLineToCapsuleCenter - CapsuleCenter, CapsuleAxis);
			const FVector AxisPoint = CapsuleCenter + CapsuleAxis * FMath::Clamp(DistanceAlongAxis, -double(SegmentHalfLength), double(SegmentHalfLength));

			// Project into the inset capsule continuously. Pushing only exterior points
			// inward produces a jump when the aim line crosses the original surface.
			SafeLocation = SafeRadius > 0.0f
				? AxisPoint + (ClosestPointOnLineToCapsuleCenter - AxisPoint).GetClampedToMaxSize(SafeRadius)
				: CapsuleCenter;
		}
		else
		{
			const float MaxHalfHeight = FMath::Max(0.0f, PPActor->GetSimpleCollisionHalfHeight() - PushInDistance);
			SafeLocation.Z = FMath::Clamp(ClosestPointOnLineToCapsuleCenter.Z, SafeLocation.Z - MaxHalfHeight, SafeLocation.Z + MaxHalfHeight);
			float DistanceSqr;
			if (PPActorRootComponent->GetSquaredDistanceToCollision(ClosestPointOnLineToCapsuleCenter, DistanceSqr, SafeLocation))
			{
				SafeLocation += (SafeLocation - ClosestPointOnLineToCapsuleCenter).GetSafeNormal() * PushInDistance;
			}
		}

		// Then aim line to desired camera position
		bool const bSingleRayPenetrationCheck = !bDoPredictiveAvoidance;
		PreventCameraPenetration(*PPActor, SafeLocation, View.Location, DeltaTime, AimLineToDesiredPosBlockedPct, bSingleRayPenetrationCheck);

		if (AimLineToDesiredPosBlockedPct < ReportPenetrationPercent)
		{
			const TWeakObjectPtr<AActor> AssistActors[] = { TargetController, TargetActor, PPActor };
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(AssistActors); ++Index)
			{
				AActor* AssistActor = AssistActors[Index].Get();
				bool bAlreadyNotified = false;
				for (int32 Previous = 0; Previous < Index; ++Previous)
				{
					bAlreadyNotified |= AssistActor == AssistActors[Previous].Get();
				}
				if (IsValid(AssistActor) && !bAlreadyNotified)
				{
					if (IIMGCameraAssistInterface* Assist = Cast<IIMGCameraAssistInterface>(AssistActor))
					{
						Assist->OnCameraPenetratingTarget();
					}
				}
			}
		}
	}
}

void UIMGCameraMode_ThirdPerson::PreventCameraPenetration(class AActor const& ViewTarget, FVector const& SafeLoc, FVector& CameraLoc, float const& DeltaTime, float& DistBlockedPct, bool bSingleRayOnly)
{
#if ENABLE_DRAW_DEBUG
	DebugActorsHitDuringCameraPenetration.Reset();
#endif

	float HardBlockedPct = DistBlockedPct;
	float SoftBlockedPct = DistBlockedPct;

	FVector BaseRay = CameraLoc - SafeLoc;
	FRotationMatrix BaseRayMatrix(BaseRay.Rotation());
	FVector BaseRayLocalUp, BaseRayLocalFwd, BaseRayLocalRight;

	BaseRayMatrix.GetScaledAxes(BaseRayLocalFwd, BaseRayLocalRight, BaseRayLocalUp);

	float DistBlockedPctThisFrame = 1.f;

	int32 const NumRaysToShoot = bSingleRayOnly ? FMath::Min(1, PenetrationAvoidanceFeelers.Num()) : PenetrationAvoidanceFeelers.Num();
	FCollisionQueryParams SphereParams(SCENE_QUERY_STAT(CameraPen), false, nullptr/*PlayerCamera*/);
	FCollisionResponseParams ResponseParams;
	ResponseParams.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);

	SphereParams.AddIgnoredActor(&ViewTarget);

	//TODO IIMGCameraTarget.GetIgnoredActorsForCameraPentration();
	//if (IgnoreActorForCameraPenetration)
	//{
	//	SphereParams.AddIgnoredActor(IgnoreActorForCameraPenetration);
	//}

	FCollisionShape SphereShape = FCollisionShape::MakeSphere(0.f);
	UWorld* World = GetWorld();

	for (int32 RayIdx = 0; RayIdx < NumRaysToShoot; ++RayIdx)
	{
		FIMGPenetrationAvoidanceFeeler& Feeler = PenetrationAvoidanceFeelers[RayIdx];
		if (bResetInterpolation || Feeler.FramesUntilNextTrace <= 0)
		{
			// calc ray target
			FVector RayTarget;
			{
				FVector RotatedRay = BaseRay.RotateAngleAxis(Feeler.AdjustmentRot.Yaw, BaseRayLocalUp);
				RotatedRay = RotatedRay.RotateAngleAxis(Feeler.AdjustmentRot.Pitch, BaseRayLocalRight);
				RayTarget = SafeLoc + RotatedRay;
			}

			// Pawn objects do not push the camera.
			SphereShape.Sphere.Radius = Feeler.Extent;
			ECollisionChannel TraceChannel = ECC_Camera;

			// do multi-line check to make sure the hits we throw out aren't
			// masking real hits behind (these are important rays).

			// MT-> passing camera as actor so that camerablockingvolumes know when it's the camera doing traces
			FHitResult Hit;
			const bool bHit = World->SweepSingleByChannel(Hit, SafeLoc, RayTarget, FQuat::Identity, TraceChannel, SphereShape, SphereParams, ResponseParams);
#if ENABLE_DRAW_DEBUG
			if (World->TimeSince(LastDrawDebugTime) < 1.f)
			{
				DrawDebugSphere(World, SafeLoc, SphereShape.Sphere.Radius, 8, FColor::Red);
				DrawDebugSphere(World, bHit ? Hit.Location : RayTarget, SphereShape.Sphere.Radius, 8, FColor::Red);
				DrawDebugLine(World, SafeLoc, bHit ? Hit.Location : RayTarget, FColor::Red);
			}
#endif // ENABLE_DRAW_DEBUG

			Feeler.FramesUntilNextTrace = Feeler.TraceInterval;

			const AActor* HitActor = Hit.GetActor();

			if (bHit && HitActor)
			{
				bool bIgnoreHit = false;

				if (HitActor->ActorHasTag(IMGCameraMode_ThirdPerson_Statics::NAME_IgnoreCameraCollision))
				{
					bIgnoreHit = true;
					SphereParams.AddIgnoredActor(HitActor);
				}

				// Ignore CameraBlockingVolume hits that occur in front of the ViewTarget.
				if (!bIgnoreHit && HitActor->IsA<ACameraBlockingVolume>())
				{
					const FVector ViewTargetForwardXY = ViewTarget.GetActorForwardVector().GetSafeNormal2D();
					const FVector ViewTargetLocation = ViewTarget.GetActorLocation();
					const FVector HitOffset = Hit.Location - ViewTargetLocation;
					const FVector HitDirectionXY = HitOffset.GetSafeNormal2D();
					const float DotHitDirection = FVector::DotProduct(ViewTargetForwardXY, HitDirectionXY);
					if (DotHitDirection > 0.0f)
					{
						bIgnoreHit = true;
						// Ignore this CameraBlockingVolume on the remaining sweeps.
						SphereParams.AddIgnoredActor(HitActor);
					}
					else
					{
#if ENABLE_DRAW_DEBUG
						DebugActorsHitDuringCameraPenetration.AddUnique(TObjectPtr<const AActor>(HitActor));
#endif
					}
				}
				
				if (!bIgnoreHit)
				{
					// Recompute blocked pct taking into account pushout distance.
					const float RayLength = (RayTarget - SafeLoc).Size();
					const float PushedBlockPct = FMath::Clamp(
						((Hit.Location - SafeLoc).Size() - CollisionPushOutDistance) / FMath::Max(RayLength, UE_KINDA_SMALL_NUMBER),
						0.0f,
						1.0f);
					const float WorldWeight = FMath::Clamp(Feeler.WorldWeight, 0.0f, 1.0f);
					const float NewBlockPct = PushedBlockPct + (1.0f - PushedBlockPct) * (1.0f - WorldWeight);
					DistBlockedPctThisFrame = FMath::Min(NewBlockPct, DistBlockedPctThisFrame);

					// This feeler got a hit, so do another trace next frame
					Feeler.FramesUntilNextTrace = 0;

#if ENABLE_DRAW_DEBUG
					DebugActorsHitDuringCameraPenetration.AddUnique(TObjectPtr<const AActor>(HitActor));
#endif
				}
			}

			if (RayIdx == 0)
			{
				// don't interpolate toward this one, snap to it
				// assumes ray 0 is the center/main ray 
				HardBlockedPct = DistBlockedPctThisFrame;
			}
			else
			{
				SoftBlockedPct = DistBlockedPctThisFrame;
			}
		}
		else
		{
			--Feeler.FramesUntilNextTrace;
		}
	}

	if (bResetInterpolation)
	{
		DistBlockedPct = DistBlockedPctThisFrame;
	}
	else if (DistBlockedPct < DistBlockedPctThisFrame)
	{
		// interpolate smoothly out
		if (PenetrationBlendOutTime > DeltaTime)
		{
			DistBlockedPct = DistBlockedPct + DeltaTime / PenetrationBlendOutTime * (DistBlockedPctThisFrame - DistBlockedPct);
		}
		else
		{
			DistBlockedPct = DistBlockedPctThisFrame;
		}
	}
	else
	{
		if (DistBlockedPct > HardBlockedPct)
		{
			DistBlockedPct = HardBlockedPct;
		}
		else if (DistBlockedPct > SoftBlockedPct)
		{
			// interpolate smoothly in
			if (PenetrationBlendInTime > DeltaTime)
			{
				DistBlockedPct = DistBlockedPct - DeltaTime / PenetrationBlendInTime * (DistBlockedPct - SoftBlockedPct);
			}
			else
			{
				DistBlockedPct = SoftBlockedPct;
			}
		}
	}

	DistBlockedPct = FMath::Clamp<float>(DistBlockedPct, 0.f, 1.f);
	if (DistBlockedPct < (1.f - ZERO_ANIMWEIGHT_THRESH))
	{
		CameraLoc = SafeLoc + (CameraLoc - SafeLoc) * DistBlockedPct;
	}
}

void UIMGCameraMode_ThirdPerson::SetTargetStanceOffset(FVector NewTargetOffset)
{
	if (TargetStanceOffset.Equals(NewTargetOffset))
	{
		return;
	}

	StanceOffsetBlendPct = 0.0f;
	InitialStanceOffset = CurrentStanceOffset;
	TargetStanceOffset = NewTargetOffset;
}

void UIMGCameraMode_ThirdPerson::UpdateStanceOffset(float DeltaTime)
{
	if (bResetInterpolation)
	{
		InitialStanceOffset = TargetStanceOffset;
		CurrentStanceOffset = TargetStanceOffset;
		StanceOffsetBlendPct = 1.0f;
		return;
	}

	if (StanceOffsetBlendPct < 1.0f)
	{
		StanceOffsetBlendPct = FMath::Min(StanceOffsetBlendPct + DeltaTime * CrouchOffsetBlendMultiplier, 1.0f);
		CurrentStanceOffset = FMath::InterpEaseInOut(InitialStanceOffset, TargetStanceOffset, StanceOffsetBlendPct, 1.0f);
	}
	else
	{
		CurrentStanceOffset = TargetStanceOffset;
		StanceOffsetBlendPct = 1.0f;
	}
}
