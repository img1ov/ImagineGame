#include "Camera/IMGCameraCollision.h"

#include "Camera/IMGCameraDamping.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGCameraCollision)

namespace
{
	float Clean(float Value, float Default, float Min, float Max)
	{
		return FMath::Clamp(FMath::IsFinite(Value) ? Value : Default, Min, Max);
	}

	bool HasTag(const UPrimitiveComponent* Component, const AActor* Actor, FName Tag)
	{
		return !Tag.IsNone() && ((IsValid(Component) && Component->ComponentHasTag(Tag))
			|| (IsValid(Actor) && Actor->ActorHasTag(Tag)));
	}

	class FCameraQuery
	{
	public:
		FCameraQuery(const UWorld& InWorld, const FIMGCameraCollisionInput& InInput,
			const FIMGCameraCollisionSettings& InSettings, FIMGCameraCollisionResult& InResult)
			: World(InWorld), Input(InInput), Settings(InSettings), Result(InResult) {}

		void SetStage(EIMGCameraCollisionStage InStage) { Stage = InStage; }

		void Capture(const FVector& Start, const FVector& End, const FQuat& Rotation,
			const FCollisionShape& Shape, bool bBlocked, bool bSweep = false, const FHitResult* Hit = nullptr, bool bIgnored = false)
		{
#if ENABLE_DRAW_DEBUG
			if (!Input.bCollectDebug) { return; }
			if (Result.DebugQueries.Num() >= FMath::Clamp(Input.MaxDebugQueries, 1, 256)) { Result.bDebugTruncated = true; return; }
			FIMGCameraCollisionDebugQuery& Record = Result.DebugQueries.AddDefaulted_GetRef();
			Record.Start = Start; Record.End = End; Record.Rotation = Rotation;
			Record.Stage = Stage; Record.bBlocked = bBlocked; Record.bSweep = bSweep; Record.bIgnored = bIgnored;
			if (Shape.IsBox()) { Record.Shape = EIMGCameraCollisionDebugShape::Box; Record.Extent = Shape.GetBox(); }
			else if (Shape.IsCapsule())
			{
				Record.Shape = EIMGCameraCollisionDebugShape::Capsule;
				Record.Radius = Shape.GetCapsuleRadius(); Record.HalfHeight = Shape.GetCapsuleHalfHeight();
			}
			else { Record.Radius = Shape.GetSphereRadius(); }
			if (Hit && !Hit->ImpactPoint.ContainsNaN() && !Hit->ImpactNormal.ContainsNaN())
			{
				Record.HitLocation = Hit->ImpactPoint; Record.HitNormal = Hit->ImpactNormal;
			}
#endif
		}

		bool Sweep(const FVector& Start, const FVector& End, float Radius, bool bOcclusion, FHitResult& Hit)
		{
			FCollisionQueryParams Params = GetParams(bOcclusion);
			const FCollisionResponseParams Responses = MakeResponses(bOcclusion);
			for (int32 Retry = 0; Retry <= Settings.MaxIgnoredHitRetries; ++Retry)
			{
				++Result.QueryCount;
				const FCollisionShape Shape = FCollisionShape::MakeSphere(Radius);
				const bool bHit = World.SweepSingleByChannel(Hit, Start, End, FQuat::Identity, Settings.TraceChannel, Shape, Params, Responses);
				const bool bIgnore = bHit && ShouldIgnore(Hit.GetComponent(), Hit.GetActor(), bOcclusion);
				Capture(Start, End, FQuat::Identity, Shape, bHit, true, bHit ? &Hit : nullptr, bIgnore);
				if (!bHit)
				{
					return false;
				}
				if (Retry < Settings.MaxIgnoredHitRetries && bIgnore)
				{
					++Result.IgnoredHitCount;
					if (Hit.GetComponent()) { Params.AddIgnoredComponent(Hit.GetComponent()); }
					else if (Hit.GetActor()) { Params.AddIgnoredActor(Hit.GetActor()); }
					else { break; }
					continue;
				}
				Record(Hit.GetComponent());
				if (!FMath::IsFinite(Hit.Time) || Hit.Location.ContainsNaN())
				{
					Hit.Time = 0.f;
					Hit.Location = Start;
					Hit.bStartPenetrating = true;
				}
				Result.bInitialPenetration |= Hit.bStartPenetrating;
				return true;
			}
			// Exhausting the rejection budget must never turn an unknown blocker into clear space.
			return true;
		}

		bool Overlap(const FVector& Position, const FQuat& Rotation, const FCollisionShape& Shape,
			bool bOcclusion, TArray<FOverlapResult>* OutBlocked = nullptr, bool bIncludePushBeyond = false)
		{
			Overlaps.Reset();
			++Result.QueryCount;
			World.OverlapMultiByChannel(Overlaps, Position, Rotation, Settings.TraceChannel, Shape,
				GetParams(bOcclusion, bIncludePushBeyond), MakeResponses(bOcclusion));
			bool bBlocked = false;
			for (const FOverlapResult& Overlap : Overlaps)
			{
				if (!Overlap.bBlockingHit) { continue; }
				if (ShouldIgnore(Overlap.GetComponent(), Overlap.GetActor(), bOcclusion))
				{
					++Result.IgnoredHitCount;
					continue;
				}
				bBlocked = true;
				Record(Overlap.GetComponent());
				if (OutBlocked) { OutBlocked->Add(Overlap); }
			}
			Capture(Position, Position, Rotation, Shape, bBlocked);
			return bBlocked;
		}

		bool IsSphereClear(const FVector& Position, float Radius, bool bIncludePushBeyond = false)
		{
			return !Overlap(Position, FQuat::Identity, FCollisionShape::MakeSphere(Radius), false, nullptr, bIncludePushBeyond);
		}

		bool Recover(FVector& Position, float Radius, bool bIncludePushBeyond = false)
		{
			const FVector Original = Position;
			const FCollisionShape Shape = FCollisionShape::MakeSphere(Radius);
			TArray<FOverlapResult> Blocked;
			for (int32 Iteration = 0; Iteration < Settings.PenetrationRecoveryIterations; ++Iteration)
			{
				Blocked.Reset();
				if (!Overlap(Position, FQuat::Identity, Shape, false, &Blocked, bIncludePushBeyond)) { return true; }
				Result.bInitialPenetration = true;
				FVector Adjustment = FVector::ZeroVector;
				double Depth = 0.0;
				int32 NumMTDs = 0;
				for (const FOverlapResult& Overlap : Blocked)
				{
					UPrimitiveComponent* Component = Overlap.GetComponent();
					if (!IsValid(Component)) { continue; }
					++Result.QueryCount;
					FMTDResult MTD;
					if (Component->ComputePenetration(MTD, Shape, Position, FQuat::Identity)
						&& !MTD.Direction.ContainsNaN() && FMath::IsFinite(MTD.Distance) && MTD.Distance > Depth)
					{
						Depth = MTD.Distance;
						Adjustment = MTD.Direction.GetSafeNormal() * (Depth + Settings.Clearance);
					}
					if (++NumMTDs >= Settings.MaxPenetrationComponentTests) { break; }
				}
				if (Adjustment.IsNearlyZero() || (Position + Adjustment - Original).Size() > Settings.MaxPenetrationRecoveryDistance)
				{
					Position = Original;
					return false;
				}
				Position += Adjustment;
			}
			if (IsSphereClear(Position, Radius, bIncludePushBeyond)) { return true; }
			Position = Original;
			return false;
		}

		FVector Constrain(const FVector& Start, const FVector& End, float Radius, bool bOcclusion)
		{
			if (Start.Equals(End, UE_SMALL_NUMBER)) { return Start; }
			FHitResult Hit;
			if (!Sweep(Start, End, Radius, bOcclusion, Hit)) { return End; }
			if (Hit.bStartPenetrating) { return Start; }
			const double Length = FVector::Distance(Start, End);
			const double Distance = FMath::Max(0.0, double(Hit.Time) * Length - Settings.Clearance);
			return Start + (End - Start).GetSafeNormal() * Distance;
		}

		bool SegmentOverlaps(const FVector& Start, const FVector& End, float Radius, bool bOcclusion)
		{
			const FVector Delta = End - Start;
			const float HalfSegment = float(Delta.Size() * 0.5);
			const FQuat Rotation = Delta.IsNearlyZero() ? FQuat::Identity
				: FQuat::FindBetweenNormals(FVector::UpVector, Delta.GetSafeNormal());
			return Overlap((Start + End) * 0.5, Rotation,
				FCollisionShape::MakeCapsule(Radius, HalfSegment + Radius), bOcclusion);
		}

	private:
		const FCollisionQueryParams& GetParams(bool bOcclusion, bool bIncludePushBeyond = false)
		{
			TOptional<FCollisionQueryParams>& Cached = bOcclusion ? OcclusionParams
				: (bIncludePushBeyond ? FinalParams : ClippingParams);
			if (!Cached.IsSet()) { Cached.Emplace(MakeParams(bOcclusion, bIncludePushBeyond)); }
			return Cached.GetValue();
		}

		FCollisionQueryParams MakeParams(bool bOcclusion, bool bIncludePushBeyond = false) const
		{
			FCollisionQueryParams Params(SCENE_QUERY_STAT(IMGCameraCollision), Settings.bTraceComplex);
			for (const auto& Actor : Input.IgnoredActors)
			{
				if (Actor.IsValid() && !(bIncludePushBeyond && Settings.MaxPushBeyondDistance > 0.f
					&& Input.PushBeyondActors.Contains(Actor))) { Params.AddIgnoredActor(Actor.Get()); }
			}
			if (!bIncludePushBeyond && Settings.MaxPushBeyondDistance > 0.f)
			{
				int32 NumActors = 0;
				for (const auto& Actor : Input.PushBeyondActors)
				{
					if (!Actor.IsValid()) { continue; }
					if (++NumActors > Settings.MaxPushBeyondActors) { break; }
					Params.AddIgnoredActor(Actor.Get());
				}
			}
			if (bOcclusion)
			{
				for (const auto& Component : Input.IgnoredOcclusionComponents)
				{
					if (Component.IsValid()) { Params.AddIgnoredComponent(Component.Get()); }
				}
			}
			return Params;
		}

		FCollisionResponseParams MakeResponses(bool bOcclusion) const
		{
			FCollisionResponseParams Responses;
			if (bOcclusion && Settings.bIgnorePawnsForOcclusion) { Responses.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore); }
			return Responses;
		}

		bool ShouldIgnore(const UPrimitiveComponent* Component, const AActor* Actor, bool bOcclusion) const
		{
			if (HasTag(Component, Actor, Settings.IgnoreCollisionTag)) { return true; }
			if (!bOcclusion) { return false; }
			if (HasTag(Component, Actor, Settings.IgnoreOcclusionTag)) { return true; }
			return false;
		}

		void Record(const UPrimitiveComponent* Component)
		{
			if (IsValid(Component) && Result.HitComponents.Num() < 16) { Result.HitComponents.AddUnique(Component); }
		}

		const UWorld& World;
		const FIMGCameraCollisionInput& Input;
		const FIMGCameraCollisionSettings& Settings;
		FIMGCameraCollisionResult& Result;
		TOptional<FCollisionQueryParams> OcclusionParams;
		TOptional<FCollisionQueryParams> ClippingParams;
		TOptional<FCollisionQueryParams> FinalParams;
		TArray<FOverlapResult> Overlaps;
		EIMGCameraCollisionStage Stage = EIMGCameraCollisionStage::Origin;
	};

	void ConstrainedSpring(double Target, float Strength, float Damping, float DeltaTime, bool bSnap,
		double& Value, double& Velocity)
	{
		if (bSnap || Target < Value)
		{
			Value = Target;
			Velocity = 0.0;
		}
		else
		{
			IMGCameraDamping::AdvanceSpring(Target, Strength, Damping, DeltaTime, Value, Velocity);
			if (Value > Target) { Value = Target; Velocity = 0.0; }
		}
	}
}

FIMGCameraCollisionSettings FIMGCameraCollisionSettings::Sanitized() const
{
	FIMGCameraCollisionSettings S = *this;
	const FIMGCameraCollisionSettings D;
	if (int32(S.TraceChannel) < 0 || int32(S.TraceChannel) >= ECC_MAX) { S.TraceChannel = ECC_Camera; }
#define IMG_CAMERA_CLAMP(Field, Min, Max) S.Field = Clean(S.Field, D.Field, Min, Max)
	IMG_CAMERA_CLAMP(ProbeRadius, 1.f, 1000.f);
	IMG_CAMERA_CLAMP(TargetRadiusMargin, 0.f, 1000.f);
	IMG_CAMERA_CLAMP(Clearance, 0.01f, 100.f);
	IMG_CAMERA_CLAMP(ConsecutiveTestRadiusReduction, 0.01f, 1.f);
	IMG_CAMERA_CLAMP(RadiusRecoverySpring, 0.f, 10000.f);
	IMG_CAMERA_CLAMP(RadiusRecoveryDamping, 0.f, 10.f);
	IMG_CAMERA_CLAMP(RootToPivotBlend, 0.f, 1.f);
	IMG_CAMERA_CLAMP(RootRecoverySpring, 0.f, 10000.f);
	IMG_CAMERA_CLAMP(RootRecoveryDamping, 0.f, 10.f);
	IMG_CAMERA_CLAMP(MaxPenetrationRecoveryDistance, 0.f, 1000.f);
	IMG_CAMERA_CLAMP(DistanceRecoverySpring, 0.f, 10000.f);
	IMG_CAMERA_CLAMP(DistanceRecoveryDamping, 0.f, 10.f);
	IMG_CAMERA_CLAMP(OcclusionPushInBlendTime, 0.f, 2.f);
	IMG_CAMERA_CLAMP(DistanceRecoveryTolerance, 0.f, 100.f);
	IMG_CAMERA_CLAMP(StationaryRootSpeed, 0.f, 10000.f);
	IMG_CAMERA_CLAMP(StationaryCameraSpeed, 0.f, 10000.f);
	IMG_CAMERA_CLAMP(MaxReorientationSweepAngle, 0.f, 90.f);
	IMG_CAMERA_CLAMP(MaxTravelSweepAngle, 0.f, 120.f);
	IMG_CAMERA_CLAMP(MinCameraSpeedForTravelSweep, 0.f, 10000.f);
	IMG_CAMERA_CLAMP(MinReorientationSpeedToMaintainDirection, 0.f, 10000.f);
	IMG_CAMERA_CLAMP(ReorientationSpring, 0.f, 10000.f);
	IMG_CAMERA_CLAMP(ReorientationDamping, 0.f, 10.f);
	IMG_CAMERA_CLAMP(LookInputAngleThreshold, 0.f, 180.f);
	IMG_CAMERA_CLAMP(CameraBodyRadius, 0.5f, S.ProbeRadius);
	IMG_CAMERA_CLAMP(ClippingDetectionLength, 0.f, 1000.f);
	IMG_CAMERA_CLAMP(PullBackReleaseSpring, 0.f, 10000.f);
	IMG_CAMERA_CLAMP(PullBackGrowthFraction, 0.f, 1.f);
	IMG_CAMERA_CLAMP(PullBackDamping, 0.f, 10.f);
	IMG_CAMERA_CLAMP(MaxPushBeyondDistance, 0.f, 10000.f);
	IMG_CAMERA_CLAMP(PushBeyondDistanceScale, 1.f, 2.f);
	IMG_CAMERA_CLAMP(PushBeyondClearance, 0.f, 100.f);
	IMG_CAMERA_CLAMP(PushBeyondReleaseSpring, 0.f, 10000.f);
	IMG_CAMERA_CLAMP(PushBeyondDamping, 0.f, 10.f);
	IMG_CAMERA_CLAMP(MinNearClip, 0.1f, 1000.f);
	IMG_CAMERA_CLAMP(MaxNearClip, S.MinNearClip, 1000.f);
#undef IMG_CAMERA_CLAMP
	S.MaxIgnoredHitRetries = FMath::Clamp(S.MaxIgnoredHitRetries, 1, 32);
	S.PenetrationRecoveryIterations = FMath::Clamp(S.PenetrationRecoveryIterations, 1, 16);
	S.MaxPenetrationComponentTests = FMath::Clamp(S.MaxPenetrationComponentTests, 1, 64);
	S.SweepTests = FMath::Clamp(S.SweepTests, 2, 16);
	S.PathTests = FMath::Clamp(S.PathTests, 2, 16);
	S.ClippingSearchSteps = FMath::Clamp(S.ClippingSearchSteps, 1, 16);
	S.MaxPushBeyondActors = FMath::Clamp(S.MaxPushBeyondActors, 1, 16);
	S.MaxPushBeyondComponentTests = FMath::Clamp(S.MaxPushBeyondComponentTests, 1, 128);
	S.NearClipSlices = FMath::Clamp(S.NearClipSlices, 1, 16);
	return S;
}

FIMGCameraCollisionResult IMGCameraCollision::Solve(const UWorld& World, const FIMGCameraCollisionInput& Input,
	const FIMGCameraCollisionSettings& Settings, float DeltaTime, FIMGCameraCollisionState& State)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(IMGCameraCollisionSolve);
	FIMGCameraCollisionResult Result;
	Result.Location = Result.DesiredLocation = Input.Location;
	Result.Rotation = Input.Rotation;
	if (Input.Location.ContainsNaN() || Input.Origin.ContainsNaN() || Input.Fallback.ContainsNaN()
		|| Input.Pivot.ContainsNaN() || Input.Rotation.ContainsNaN() || !FMath::IsFinite(Input.MaxProbeRadius)
		|| Input.MaxProbeRadius <= 0.f)
	{
		Result.Location = State.bHasSafeLocation ? State.PreviousSafeLocation
			: (Input.Location.ContainsNaN() ? FVector::ZeroVector : Input.Location);
		if (Result.Rotation.ContainsNaN()) { Result.Rotation = FRotator::ZeroRotator; }
		State.Reset();
		return Result;
	}
	if (!FMath::IsFinite(DeltaTime) || DeltaTime < 0.f || DeltaTime > 32.f / 120.f)
	{
		State.Reset();
		DeltaTime = 0.f;
	}
	const FIMGCameraCollisionSettings S = Settings.Sanitized();
	const float MinPitch = Clean(Input.MinPitch, -89.f, -89.9f, 89.9f);
	const float MaxPitch = Clean(Input.MaxPitch, 89.f, MinPitch, 89.9f);
	FCameraQuery Query(World, Input, S, Result);
	const bool bFirstUpdate = !State.bInitialized;
	const double TargetRadius = FMath::Min(S.ProbeRadius, Input.MaxProbeRadius);
	ConstrainedSpring(TargetRadius, S.RadiusRecoverySpring, S.RadiusRecoveryDamping, DeltaTime,
		bFirstUpdate, State.Radius, State.RadiusVelocity);
	const float Radius = FMath::Max(0.5f, float(State.Radius));
	const float TestRadius = FMath::Max(0.5f, Radius - S.ConsecutiveTestRadiusReduction);
	const float BodyRadius = FMath::Min(S.CameraBodyRadius, TestRadius);
	const bool bSmoothOcclusion = S.OcclusionPushInBlendTime > UE_SMALL_NUMBER && BodyRadius < TestRadius - UE_SMALL_NUMBER;
	Result.ProbeRadius = Radius;

	FVector Origin = Input.Origin;
	bool bSafeOrigin = Query.Recover(Origin, Radius);
	if (!bSafeOrigin && !bFirstUpdate && FVector::Distance(State.PreviousRoot, Input.Origin) <= S.MaxPenetrationRecoveryDistance
		&& Query.IsSphereClear(State.PreviousRoot, Radius))
	{
		Origin = Query.Constrain(State.PreviousRoot, Input.Origin, TestRadius, false);
		bSafeOrigin = Query.IsSphereClear(Origin, TestRadius);
	}
	if (!bSafeOrigin && Query.IsSphereClear(Input.Location, Radius))
	{
		const FVector Candidate = Query.Constrain(Input.Location, Input.Origin, TestRadius, false);
		if (FVector::Distance(Candidate, Input.Origin) <= S.MaxPenetrationRecoveryDistance)
		{
			Origin = Candidate;
			bSafeOrigin = Query.IsSphereClear(Origin, TestRadius);
		}
	}
	Result.Origin = Origin;
	Result.Root = Origin;
	Result.bRecoveredOrigin = !Origin.Equals(Input.Origin, 0.01);
	if (!bSafeOrigin)
	{
		Query.SetStage(EIMGCameraCollisionStage::Final);
		FVector Candidate = Input.Location;
		Result.bHasSafeLocation = Query.Recover(Candidate, BodyRadius, true);
		if (!Result.bHasSafeLocation && State.bHasSafeLocation
			&& FVector::Distance(State.PreviousSafeLocation, Input.Location) <= S.MaxPenetrationRecoveryDistance
			&& Query.IsSphereClear(State.PreviousSafeLocation, BodyRadius, true))
		{
			Candidate = State.PreviousSafeLocation;
			Result.bHasSafeLocation = true;
		}
		Result.Location = Candidate;
		State.Reset();
		State.bHasSafeLocation = Result.bHasSafeLocation;
		if (Result.bHasSafeLocation) { State.PreviousSafeLocation = Candidate; }
		return Result;
	}

	auto ResolveRootSegment = [&](const FVector& Start, const FVector& End, double& Fraction, double& Velocity)
	{
		const FVector SafeEnd = Query.Constrain(Start, End, TestRadius, false);
		const double Length = FVector::Distance(Start, End);
		const double DesiredFraction = Length > UE_KINDA_SMALL_NUMBER
			? FMath::Clamp(FVector::Distance(Start, SafeEnd) / Length, 0.0, 1.0) : 1.0;
		ConstrainedSpring(DesiredFraction, S.RootRecoverySpring, S.RootRecoveryDamping, DeltaTime,
			bFirstUpdate, Fraction, Velocity);
		Fraction = FMath::Clamp(Fraction, 0.0, DesiredFraction);
		return FMath::Lerp(Start, End, Fraction);
	};
	Query.SetStage(EIMGCameraCollisionStage::Root);
	const FVector Fallback = ResolveRootSegment(Origin, Input.Fallback, State.FallbackFraction, State.FallbackVelocity);
	const FVector IdealRoot = FMath::Lerp(Fallback, Input.Pivot, S.RootToPivotBlend);
	const FVector Root = ResolveRootSegment(Fallback, IdealRoot, State.RootFraction, State.RootVelocity);
	Result.Root = Root;
	Result.bHasSafeRoot = true;

	if (!S.bMoveTowardsClearLineOfSight)
	{
		State.AutomaticOrbitOffset = FRotator::ZeroRotator;
	}
	if (Input.bForcePushIn || !S.bMoveTowardsClearLineOfSight)
	{
		State.ReorientationVelocity = FVector2D::ZeroVector;
	}
	const FQuat OrbitOffset = State.AutomaticOrbitOffset.Quaternion();
	const FVector DesiredLocation = Root + OrbitOffset.RotateVector(Input.Location - Root);
	Result.DesiredLocation = DesiredLocation;
	if (!State.AutomaticOrbitOffset.IsNearlyZero())
	{
		Result.Rotation = (OrbitOffset * Input.Rotation.Quaternion()).Rotator();
		Result.Rotation.Pitch = FMath::ClampAngle(Result.Rotation.Pitch, MinPitch, MaxPitch);
	}
	const FVector Ray = DesiredLocation - Root;
	const double DesiredDistance = Ray.Size();
	const FVector Outward = Ray.GetSafeNormal();
	const double RootSpeed = !bFirstUpdate && DeltaTime > UE_SMALL_NUMBER
		? FVector::Distance(Root, State.PreviousRoot) / DeltaTime : 0.0;
	const FVector CameraTravel = DesiredLocation - State.PreviousDesiredLocation;
	const double CameraSpeed = !bFirstUpdate && DeltaTime > UE_SMALL_NUMBER ? CameraTravel.Size() / DeltaTime : 0.0;
	const bool bStationary = RootSpeed <= S.StationaryRootSpeed && CameraSpeed <= S.StationaryCameraSpeed;
	FHitResult InitialHit;
	Query.SetStage(EIMGCameraCollisionStage::Occlusion);
	const bool bOccluded = DesiredDistance > UE_KINDA_SMALL_NUMBER && Query.Sweep(Root, DesiredLocation, TestRadius, true, InitialHit);
	Result.bOccluded = bOccluded;
	Result.DesiredDistance = DesiredDistance;
	double DesiredOrbitDistance = bOccluded
		? FMath::Max(0.0, InitialHit.Time * DesiredDistance - S.Clearance) : DesiredDistance;
	bool bPushedIn = bOccluded;
	const float PathRadius = FMath::Max(0.5f, TestRadius - S.ConsecutiveTestRadiusReduction);
	const float ClipRadius = bSmoothOcclusion ? BodyRadius
		: bOccluded ? FMath::Max(0.5f, PathRadius - S.ConsecutiveTestRadiusReduction) : PathRadius;
	double ProbePullBackRadius = bOccluded ? TestRadius : 0.0;
	FRotator RequestedOrbitDelta = FRotator::ZeroRotator;

	const bool bCanReorient = S.bMoveTowardsClearLineOfSight && !Input.bForcePushIn && !bStationary
		&& !(S.bPersistPushInBehaviour && State.bWasPushedIn);
	const bool bTravelSweep = S.bSweepToAvoidPopIn && !bFirstUpdate && CameraSpeed >= S.MinCameraSpeedForTravelSweep;
	if (bOccluded && !InitialHit.bStartPenetrating && (bCanReorient || bTravelSweep))
	{
		Query.SetStage(EIMGCameraCollisionStage::Search);
		FVector Axis = FVector::CrossProduct(Outward, bCanReorient ? FVector(InitialHit.Normal) : CameraTravel.GetSafeNormal());
		const FQuat RotationVelocity = FRotator(State.ReorientationVelocity.Y, State.ReorientationVelocity.X, 0.0).Quaternion();
		const double RotationSpeed = FMath::RadiansToDegrees(RotationVelocity.GetAngle());
		if (bCanReorient && RotationSpeed > UE_SMALL_NUMBER && RotationSpeed >= S.MinReorientationSpeedToMaintainDirection)
		{
			double Angle;
			RotationVelocity.ToAxisAndAngle(Axis, Angle);
		}
		if (!Axis.Normalize()) { Axis = FVector::UpVector; }
		const double PreferredDistance = bFirstUpdate ? DesiredDistance : FMath::Min(State.PreviousDistanceRatio, 1.0) * DesiredDistance;
		double BestError = FMath::Abs(DesiredOrbitDistance - PreferredDistance);
		FVector BestDirection = Outward;
		double BestDistance = DesiredOrbitDistance;
		FVector Direction = Outward;
		const float AngleStep = (bCanReorient ? S.MaxReorientationSweepAngle : S.MaxTravelSweepAngle) / S.SweepTests;
		for (int32 Index = 0; Index < S.SweepTests; ++Index)
		{
			const float Alpha = float(Index + 1) / S.SweepTests;
			const float Scale = bCanReorient ? 2.f * Alpha * Alpha : 1.f;
			Direction = Direction.RotateAngleAxis(AngleStep * Scale, Axis);
			FHitResult Hit;
			const bool bHit = Query.Sweep(Root, Root + Direction * DesiredDistance, TestRadius, true, Hit);
			const double Distance = bHit ? FMath::Max(0.0, Hit.Time * DesiredDistance - S.Clearance) : DesiredDistance;
			const double Error = FMath::Abs(Distance - PreferredDistance);
			if (Error < BestError) { BestDirection = Direction; BestDistance = Distance; BestError = Error; }
		}
		const double MinPathDistance = 2.0 * PathRadius;
		const double MaxPathDistance = BestDistance - S.ConsecutiveTestRadiusReduction;
		if (!BestDirection.Equals(Outward, 0.001) && MaxPathDistance > MinPathDistance)
		{
			for (int32 Index = 0; Index < S.PathTests; ++Index)
			{
				const double Distance = FMath::Lerp(MaxPathDistance, MinPathDistance, double(Index) / (S.PathTests - 1));
				const FVector OriginalPosition = Root + Outward * Distance;
				if (Query.SegmentOverlaps(OriginalPosition, Root + BestDirection * Distance,
					PathRadius, true)) { continue; }
				DesiredOrbitDistance = FVector::Distance(Root, Query.Constrain(OriginalPosition, DesiredLocation, PathRadius, true));
				ProbePullBackRadius = PathRadius;
				if (bCanReorient)
				{
					RequestedOrbitDelta = (BestDirection.Rotation() - Outward.Rotation()).GetNormalized();
					bPushedIn = false;
				}
				break;
			}
		}
	}
	if (S.bMoveTowardsClearLineOfSight && !Input.bForcePushIn)
	{
		double Yaw = 0.0;
		double Pitch = 0.0;
		IMGCameraDamping::AdvanceSpring(RequestedOrbitDelta.Yaw, S.ReorientationSpring, S.ReorientationDamping,
			DeltaTime, Yaw, State.ReorientationVelocity.X);
		IMGCameraDamping::AdvanceSpring(RequestedOrbitDelta.Pitch, S.ReorientationSpring, S.ReorientationDamping,
			DeltaTime, Pitch, State.ReorientationVelocity.Y);
		State.AutomaticOrbitOffset.Yaw = FMath::Clamp(State.AutomaticOrbitOffset.Yaw + Yaw,
			-double(S.MaxReorientationSweepAngle), double(S.MaxReorientationSweepAngle));
		State.AutomaticOrbitOffset.Pitch = FMath::Clamp(State.AutomaticOrbitOffset.Pitch + Pitch,
			FMath::Max(-double(S.MaxReorientationSweepAngle), MinPitch - Input.Rotation.Pitch),
			FMath::Min(double(S.MaxReorientationSweepAngle), MaxPitch - Input.Rotation.Pitch));
	}

	DesiredOrbitDistance = FMath::Clamp(DesiredOrbitDistance, 0.0, DesiredDistance);
	Result.OcclusionDistance = DesiredOrbitDistance;
	double HardLimit = DesiredOrbitDistance;
	// A clear broad sweep also proves clearance for the smaller body under the same filter.
	if (bSmoothOcclusion && bOccluded)
	{
		Query.SetStage(EIMGCameraCollisionStage::CameraLimit);
		HardLimit = FVector::Distance(Root, Query.Constrain(Root, DesiredLocation, BodyRadius, true));
		DesiredOrbitDistance = FMath::Min(DesiredOrbitDistance, HardLimit);
	}
	Result.HardLimitDistance = HardLimit;
	const double Ratio = DesiredDistance > UE_KINDA_SMALL_NUMBER ? DesiredOrbitDistance / DesiredDistance : 1.0;
	const double PreviousOrbitDistance = bFirstUpdate ? DesiredDistance : State.OrbitDistance;
	const bool bRecovering = !bFirstUpdate && DesiredOrbitDistance >= State.PreviousDampedOrbitDistance + S.DistanceRecoveryTolerance
		&& Ratio > State.PreviousDistanceRatio + UE_SMALL_NUMBER;
	Result.bRecovering = bRecovering;
	if (!bRecovering)
	{
		if (!bFirstUpdate && bSmoothOcclusion && DesiredOrbitDistance < State.OrbitDistance)
		{
			const double Alpha = FMath::Clamp(double(DeltaTime) / S.OcclusionPushInBlendTime, 0.0, 1.0);
			State.OrbitDistance = FMath::Lerp(State.OrbitDistance, DesiredOrbitDistance, Alpha);
		}
		else { State.OrbitDistance = DesiredOrbitDistance; }
		State.OrbitVelocity = 0.0;
		State.bPauseRecovery = true;
	}
	else
	{
		State.bPauseRecovery &= S.bPauseRecoveryWhenStationary && bStationary;
		if (!State.bPauseRecovery)
		{
			ConstrainedSpring(DesiredOrbitDistance, S.DistanceRecoverySpring, S.DistanceRecoveryDamping,
				DeltaTime, false, State.OrbitDistance, State.OrbitVelocity);
		}
	}
	// The broad probe controls framing; only the body limit may truncate inward blending.
	State.OrbitDistance = FMath::Clamp(State.OrbitDistance, 0.0, HardLimit);
	Result.bPausedRecovery = bRecovering && State.bPauseRecovery;
	Result.DampedDistance = State.OrbitDistance;

	FVector Camera = Root + Outward * State.OrbitDistance;
	Query.SetStage(EIMGCameraCollisionStage::Clipping);
	const FVector ClipEnd = Camera - Outward * FMath::Min(double(S.ClippingDetectionLength), State.OrbitDistance);
	Result.bClipping = Query.SegmentOverlaps(Camera, ClipEnd, ClipRadius, false);
	if (Result.bClipping)
	{
		const double DistanceBeforeClipping = State.OrbitDistance;
		for (int32 Step = 1; Step <= S.ClippingSearchSteps; ++Step)
		{
			const double Distance = DistanceBeforeClipping * (1.0 - double(Step) / S.ClippingSearchSteps);
			const FVector Start = Root + Outward * Distance;
			if (!Query.IsSphereClear(Start, ClipRadius)) { continue; }
			ProbePullBackRadius = FMath::Max(0.5f, ClipRadius - S.ConsecutiveTestRadiusReduction);
			Camera = Query.Constrain(Start, Camera, float(ProbePullBackRadius), false);
			State.OrbitDistance = FMath::Clamp(FVector::Distance(Root, Camera), 0.0, DistanceBeforeClipping);
			break;
		}
		State.OrbitVelocity = 0.0;
		State.bPauseRecovery = true;
	}

	const double DampedOrbitDistance = State.OrbitDistance;
	Result.ClippingDistance = DampedOrbitDistance;
	Camera = Root + Outward * State.OrbitDistance;

	// Entities explicitly exempted from radial occlusion may still require an exterior camera position.
	const double DistanceBeforePush = FVector::Distance(Root, Camera);
	double PushBeyondDistance = DistanceBeforePush;
	int32 PushActors = 0;
	int32 PushComponentTests = 0;
	Query.SetStage(EIMGCameraCollisionStage::PushBeyond);
	for (const auto& WeakActor : Input.PushBeyondActors)
	{
		const AActor* Actor = WeakActor.Get();
		if (!IsValid(Actor)) { continue; }
		if (++PushActors > S.MaxPushBeyondActors || S.MaxPushBeyondDistance <= 0.f) { break; }
		const FBox Bounds = Actor->GetComponentsBoundingBox();
		if (!Bounds.IsValid) { continue; }
		const double BoundDistance = FVector::Distance(Root, Bounds.GetCenter()) + Bounds.GetExtent().Size() + 2.0 * BodyRadius;
		const double TraceDistance = FMath::Min(double(S.MaxPushBeyondDistance), FMath::Max(PushBeyondDistance + 2.0 * BodyRadius, BoundDistance));
		if (TraceDistance <= UE_KINDA_SMALL_NUMBER) { continue; }
		TInlineComponentArray<UPrimitiveComponent*> Components;
		Actor->GetComponents(Components);
		for (UPrimitiveComponent* Component : Components)
		{
			if (!IsValid(Component) || !Component->IsRegistered() || !Component->IsQueryCollisionEnabled()
				|| HasTag(Component, Actor, S.IgnoreCollisionTag)
				|| Component->GetCollisionResponseToChannel(S.TraceChannel) != ECR_Block) { continue; }
			if (PushComponentTests >= S.MaxPushBeyondComponentTests)
			{
				Result.bPushBeyondBudgetExceeded = true;
				break;
			}
			++PushComponentTests;
			FHitResult Hit;
			++Result.QueryCount;
			const FCollisionShape Shape = FCollisionShape::MakeSphere(BodyRadius);
			const bool bHit = Component->SweepComponent(Hit, Root + Outward * TraceDistance, Root, FQuat::Identity, Shape, S.bTraceComplex);
			Query.Capture(Root + Outward * TraceDistance, Root, FQuat::Identity, Shape, bHit, true, bHit ? &Hit : nullptr);
			if (bHit && !Hit.bStartPenetrating && FMath::IsFinite(Hit.Time))
			{
				PushBeyondDistance = FMath::Max(PushBeyondDistance,
					FMath::Min(double(S.MaxPushBeyondDistance), (1.0 - Hit.Time) * TraceDistance + S.PushBeyondClearance));
			}
		}
		if (Result.bPushBeyondBudgetExceeded) { break; }
	}
	const bool bPushRequired = PushBeyondDistance > DistanceBeforePush + UE_SMALL_NUMBER;
	if (S.MaxPushBeyondDistance <= 0.f || (!bPushRequired && !State.bWasPushedBeyond))
	{
		// An idle exterior constraint must not oppose ordinary inward occlusion.
		State.PushBeyondDistance = DistanceBeforePush;
		State.PushBeyondVelocity = 0.0;
		State.bWasPushedBeyond = false;
	}
	else
	{
		if (bFirstUpdate || PushBeyondDistance > State.PushBeyondDistance)
		{
			State.PushBeyondDistance = PushBeyondDistance;
			State.PushBeyondVelocity = 0.0;
		}
		const double DesiredPushDistance = bPushRequired
			? FMath::Max(DistanceBeforePush, FMath::Min(double(S.MaxPushBeyondDistance), PushBeyondDistance * S.PushBeyondDistanceScale))
			: DistanceBeforePush;
		IMGCameraDamping::AdvanceSpring(DesiredPushDistance,
			DesiredPushDistance >= State.PushBeyondDistance ? 0.f : S.PushBeyondReleaseSpring,
			S.PushBeyondDamping, DeltaTime, State.PushBeyondDistance, State.PushBeyondVelocity);
		State.PushBeyondDistance = FMath::Max(State.PushBeyondDistance, PushBeyondDistance);
		if (State.PushBeyondDistance > DistanceBeforePush)
		{
			const FVector Constrained = Query.Constrain(Root, Root + Outward * State.PushBeyondDistance, ClipRadius, true);
			Camera = Root + Outward * FMath::Max(DistanceBeforePush, FVector::Distance(Root, Constrained));
			State.PushBeyondDistance = FVector::Distance(Root, Camera);
		}
		State.bWasPushedBeyond = State.PushBeyondDistance > DistanceBeforePush + UE_KINDA_SMALL_NUMBER;
	}
	State.OrbitDistance = FVector::Distance(Root, Camera);
	Result.PushBeyondDistance = State.OrbitDistance;
	double DesiredPullBack = !bSmoothOcclusion && S.bPullBackByProbeRadius && (bOccluded || Result.bClipping)
		? FMath::Min(ProbePullBackRadius, FMath::Max(0.0, DesiredDistance - State.OrbitDistance)) : 0.0;
	// Radius compensation cannot reverse the current inward movement.
	const double InwardDistance = FMath::Max(PreviousOrbitDistance - State.OrbitDistance, 0.0);
	DesiredPullBack = FMath::Min(DesiredPullBack, State.PullBackRadius + InwardDistance * S.PullBackGrowthFraction);
	if (bSmoothOcclusion || !S.bPullBackByProbeRadius || DesiredPullBack > UE_SMALL_NUMBER || DesiredPullBack >= State.PullBackRadius)
	{
		State.PullBackRadius = DesiredPullBack;
		State.PullBackVelocity = 0.0;
	}
	else
	{
		IMGCameraDamping::AdvanceSpring(0.0, S.PullBackReleaseSpring, S.PullBackDamping,
			DeltaTime, State.PullBackRadius, State.PullBackVelocity);
	}
	State.PullBackRadius = FMath::Clamp(State.PullBackRadius, 0.0, FMath::Max(0.0, DesiredDistance - State.OrbitDistance));
	if (State.PullBackRadius > UE_SMALL_NUMBER)
	{
		Query.SetStage(EIMGCameraCollisionStage::PullBack);
		Camera = Query.Constrain(Camera, Camera + Outward * State.PullBackRadius, BodyRadius, false);
	}
	Result.PullBackRadius = State.PullBackRadius;
	Query.SetStage(EIMGCameraCollisionStage::Final);
	Result.bHasSafeLocation = Query.Recover(Camera, BodyRadius, true);
	if (!Result.bHasSafeLocation && State.bHasSafeLocation
		&& FVector::Distance(State.PreviousSafeLocation, Camera) <= S.MaxPenetrationRecoveryDistance
		&& Query.IsSphereClear(State.PreviousSafeLocation, BodyRadius, true))
	{
		Camera = State.PreviousSafeLocation;
		Result.bHasSafeLocation = true;
	}
	if (!Result.bHasSafeLocation && Query.IsSphereClear(Root, BodyRadius, true))
	{
		Camera = Root;
		Result.bHasSafeLocation = true;
	}
	Result.Location = Camera;
	Result.FinalDistance = FVector::Distance(Root, Camera);
	Result.DistanceRatio = DesiredDistance > UE_KINDA_SMALL_NUMBER
		? float(FMath::Clamp(FVector::Distance(Root, Camera) / DesiredDistance, 0.0, 1.0)) : 1.f;
	State.PreviousRoot = Root;
	State.PreviousDesiredLocation = DesiredLocation;
	State.PreviousDampedOrbitDistance = DampedOrbitDistance;
	State.PreviousDistanceRatio = DesiredDistance > UE_KINDA_SMALL_NUMBER ? DampedOrbitDistance / DesiredDistance : 1.0;
	State.bWasPushedIn = bPushedIn;
	State.bInitialized = true;
	if (Result.bHasSafeLocation)
	{
		State.bHasSafeLocation = true;
		State.PreviousSafeLocation = Camera;
	}
	return Result;
}

float IMGCameraCollision::ScanNearClip(const UWorld& World, const FIMGCameraCollisionInput& Input,
	const FIMGCameraCollisionSettings& Settings, const FVector2D& HalfFovTangents, FIMGCameraCollisionResult& Diagnostics)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(IMGCameraNearClip);
	const FIMGCameraCollisionSettings S = Settings.Sanitized();
	if (!S.bScanNearClip || HalfFovTangents.ContainsNaN() || HalfFovTangents.X <= 0.0 || HalfFovTangents.Y <= 0.0
		|| Input.Location.ContainsNaN() || Input.Rotation.ContainsNaN()) { return -1.f; }
	FCameraQuery Query(World, Input, S, Diagnostics);
	Query.SetStage(EIMGCameraCollisionStage::NearClip);
	const FQuat Rotation = Input.Rotation.Quaternion();
	const FVector Forward = Rotation.GetForwardVector();
	const FVector BroadExtent(FMath::Max(0.05f, (S.MaxNearClip - S.MinNearClip) * 0.5f),
		HalfFovTangents.X * S.MaxNearClip, HalfFovTangents.Y * S.MaxNearClip);
	if (!Query.Overlap(Input.Location + Forward * ((S.MinNearClip + S.MaxNearClip) * 0.5f), Rotation,
		FCollisionShape::MakeBox(BroadExtent), false, nullptr, true)) { return S.MaxNearClip; }
	float SafeNearClip = S.MinNearClip;
	for (int32 Index = 0; Index < S.NearClipSlices; ++Index)
	{
		const float Start = FMath::Lerp(S.MinNearClip, S.MaxNearClip, float(Index) / S.NearClipSlices);
		const float End = FMath::Lerp(S.MinNearClip, S.MaxNearClip, float(Index + 1) / S.NearClipSlices);
		const FVector Extent(FMath::Max(0.05f, (End - Start) * 0.5f), HalfFovTangents.X * End, HalfFovTangents.Y * End);
		if (Query.Overlap(Input.Location + Forward * ((Start + End) * 0.5f), Rotation,
			FCollisionShape::MakeBox(Extent), false, nullptr, true)) { break; }
		SafeNearClip = End;
	}
	return SafeNearClip;
}
