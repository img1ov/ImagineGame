
#include "Camera/IMGCameraMode_ThirdPerson.h"
#include "Camera/IMGCameraMode.h"
#include "Components/PrimitiveComponent.h"
#include "Camera/IMGPenetrationAvoidanceFeeler.h"
#include "Curves/CurveVector.h"
#include "Engine/Canvas.h"
#include "GameFramework/CameraBlockingVolume.h"
#include "Camera/IMGCameraAssistInterface.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Character.h"
#include "Character/IMGCharacter.h"
#include "Math/RotationMatrix.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGCameraMode_ThirdPerson)

namespace IMGCameraMode_ThirdPerson_Statics
{
	static const FName NAME_IgnoreCameraCollision = TEXT("IgnoreCameraCollision");
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
	UpdateForTarget(DeltaTime);
	UpdateStanceOffset(DeltaTime);

	const FVector PivotLocation = UpdateCameraLag(DeltaTime) + CurrentStanceOffset;
	FRotator PivotRotation = GetPivotRotation();

	PivotRotation.Pitch = FMath::ClampAngle(PivotRotation.Pitch, ViewPitchMin, ViewPitchMax);

	View.Location = PivotLocation;
	View.Rotation = PivotRotation;
	View.ControlRotation = View.Rotation;
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

	// Adjust final desired camera location to prevent any penetration
	UpdatePreventPenetration(DeltaTime);
}

FVector UIMGCameraMode_ThirdPerson::UpdateCameraLag(float DeltaTime)
{
	const FVector DesiredPivotLocation = GetPivotLocation();
	if (!bHasLaggedPivotLocation || bResetInterpolation || !bEnableCameraLag || CameraLocationLagSpeed <= 0.0f)
	{
		LaggedPivotLocation = DesiredPivotLocation;
		bHasLaggedPivotLocation = true;
		return LaggedPivotLocation;
	}

	if (bUseCameraLagSubstepping && DeltaTime > CameraLagMaxTimeStep)
	{
		const FVector PivotMovementStep = (DesiredPivotLocation - LaggedPivotLocation) / DeltaTime;
		FVector LerpTarget = LaggedPivotLocation;
		float RemainingTime = DeltaTime;
		const float MaxTimeStep = FMath::Max(CameraLagMaxTimeStep, 1.0f / 200.0f);

		while (RemainingTime > UE_KINDA_SMALL_NUMBER)
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

void UIMGCameraMode_ThirdPerson::OnActivation()
{
	Super::OnActivation();
	bHasLaggedPivotLocation = false;
}

void UIMGCameraMode_ThirdPerson::UpdateForTarget(float DeltaTime)
{

	if (const ACharacter* TargetCharacter = Cast<ACharacter>(GetTargetActor()))
	{
		if (const AIMGCharacter* IMGCharacter = Cast<AIMGCharacter>(TargetCharacter); IMGCharacter && IMGCharacter->IsCrawling())
		{
			SetTargetStanceOffset(FVector(0.0f, 0.0f, IMGCharacter->GetCrawledEyeHeight() - IMGCharacter->GetStandingEyeHeight()));
			return;
		}
		if (TargetCharacter->IsCrouched())
		{
			const ACharacter* TargetCharacterCDO = TargetCharacter->GetClass()->GetDefaultObject<ACharacter>();
			const float CrouchedHeightAdjustment = TargetCharacterCDO->CrouchedEyeHeight - TargetCharacterCDO->BaseEyeHeight;

			SetTargetStanceOffset(FVector(0.0f, 0.0f, CrouchedHeightAdjustment));

			return;
		}
	}

	SetTargetStanceOffset(FVector::ZeroVector);
}

void UIMGCameraMode_ThirdPerson::DrawDebug(UCanvas* Canvas) const
{
	Super::DrawDebug(Canvas);

#if ENABLE_DRAW_DEBUG
	FDisplayDebugManager& DisplayDebugManager = Canvas->DisplayDebugManager;
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

	APawn* TargetPawn = Cast<APawn>(TargetActor);
	AController* TargetController = TargetPawn ? TargetPawn->GetController() : nullptr;
	IIMGCameraAssistInterface* TargetControllerAssist = Cast<IIMGCameraAssistInterface>(TargetController);

	IIMGCameraAssistInterface* TargetActorAssist = Cast<IIMGCameraAssistInterface>(TargetActor);

	TOptional<AActor*> OptionalPPTarget = TargetActorAssist ? TargetActorAssist->GetCameraPreventPenetrationTarget() : TOptional<AActor*>();
	AActor* PPActor = OptionalPPTarget.IsSet() ? OptionalPPTarget.GetValue() : TargetActor;
	IIMGCameraAssistInterface* PPActorAssist = OptionalPPTarget.IsSet() ? Cast<IIMGCameraAssistInterface>(PPActor) : nullptr;

	const UPrimitiveComponent* PPActorRootComponent = Cast<UPrimitiveComponent>(PPActor->GetRootComponent());
	if (PPActorRootComponent)
	{
		// Attempt at picking SafeLocation automatically, so we reduce camera translation when aiming.
		// Our camera is our reticle, so we want to preserve our aim and keep that as steady and smooth as possible.
		// Pick closest point on capsule to our aim line.
		FVector ClosestPointOnLineToCapsuleCenter;
		FVector SafeLocation = PPActor->GetActorLocation();
		FMath::PointDistToLine(SafeLocation, View.Rotation.Vector(), View.Location, ClosestPointOnLineToCapsuleCenter);

		// Adjust Safe distance height to be same as aim line, but within capsule.
		float const PushInDistance = PenetrationAvoidanceFeelers[0].Extent + CollisionPushOutDistance;
		float const MaxHalfHeight = PPActor->GetSimpleCollisionHalfHeight() - PushInDistance;
		SafeLocation.Z = FMath::Clamp(ClosestPointOnLineToCapsuleCenter.Z, SafeLocation.Z - MaxHalfHeight, SafeLocation.Z + MaxHalfHeight);

		float DistanceSqr;
		PPActorRootComponent->GetSquaredDistanceToCollision(ClosestPointOnLineToCapsuleCenter, DistanceSqr, SafeLocation);
		// Push back inside capsule to avoid initial penetration when doing line checks.
		if (PenetrationAvoidanceFeelers.Num() > 0)
		{
			SafeLocation += (SafeLocation - ClosestPointOnLineToCapsuleCenter).GetSafeNormal() * PushInDistance;
		}

		// Then aim line to desired camera position
		bool const bSingleRayPenetrationCheck = !bDoPredictiveAvoidance;
		PreventCameraPenetration(*PPActor, SafeLocation, View.Location, DeltaTime, AimLineToDesiredPosBlockedPct, bSingleRayPenetrationCheck);

		IIMGCameraAssistInterface* AssistArray[] = { TargetControllerAssist, TargetActorAssist, PPActorAssist };

		if (AimLineToDesiredPosBlockedPct < ReportPenetrationPercent)
		{
			for (IIMGCameraAssistInterface* Assist : AssistArray)
			{
				if (Assist)
				{
					// camera is too close, tell the assists
					Assist->OnCameraPenetratingTarget();
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
		if (Feeler.FramesUntilNextTrace <= 0)
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
