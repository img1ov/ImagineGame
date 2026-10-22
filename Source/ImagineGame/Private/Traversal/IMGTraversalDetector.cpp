#include "Traversal/IMGTraversalDetector.h"

#include "Character/IMGCharacter.h"
#include "Character/IMGCharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Math/RotationMatrix.h"
#include "Physics/IMGCollisionChannels.h"

#if ENABLE_DRAW_DEBUG
#include "IMGTraversalDebug.h"
#endif

namespace
{
constexpr int32 MaxCandidateHits = 12;
constexpr int32 MaxDepthSamples = 5;
constexpr int32 MaxForwardProbeLayers = 6;
constexpr float SearchProbeRadius = 21.5f;
constexpr float SearchProbeSeparation = SearchProbeRadius * 2.f;
constexpr float SearchStartHeightOffset = -75.f;
constexpr float TopProbeRadius = 10.f;
constexpr float TopProbeStartOffset = SearchProbeSeparation + SearchProbeRadius;
constexpr float TopProbeLength = SearchProbeSeparation * 2.f;
constexpr float TopHeightTolerance = 20.f;
constexpr float DepthProbeRadius = 27.5f;
constexpr float DepthProbeSpacing = DepthProbeRadius * 2.f;
constexpr float DepthHeightTolerance = 20.f;
constexpr float HorizontalClearanceRadius = 22.5f;
constexpr float HorizontalClearanceVerticalOffset = 15.f;
constexpr float HorizontalClearanceBackOffset = 25.f;
constexpr float VerticalClearanceRadius = 17.5f;
constexpr float HandholdPathVerticalOffset = 20.f;
constexpr float HandholdPathNormalOffset = 33.5f;
constexpr float EdgePlanarTolerance = 9.f;
constexpr float EdgeHeadingToleranceDot = 0.70710678f;
constexpr float EdgeVerticalContinuity = SearchProbeSeparation * 2.f;
constexpr float WalkableSlopeMinNormalZ = 0.55f;
constexpr float WalkableSlopeBaseOffset = 25.f;
constexpr float WalkableSlopeDistanceTolerance = 13.5f;
constexpr float WalkableSlopeProjectionLength = 1000.f;
constexpr float HighOpenApproachMinHeight = 180.f;
constexpr float HighOpenApproachMinClearance = 125.f;

#if ENABLE_DRAW_DEBUG
FString GetFailureName(EIMGTraversalFailure Failure)
{
	return StaticEnum<EIMGTraversalFailure>()->GetNameStringByValue(static_cast<int64>(Failure));
}

void DrawFailure(const AIMGCharacter& Character, EIMGTraversalFailure Failure, const FVector& Location, int32 Level = 1)
{
	if (!IMGTraversalDebug::IsEnabled(Level))
	{
		return;
	}
	IMGTraversalDebug::Point(Character.GetWorld(), Location, FColor::Red, Level, 14.f);
	IMGTraversalDebug::String(Character.GetWorld(), Location + FVector::UpVector * 16.f,
		FString::Printf(TEXT("Traversal: %s"), *GetFailureName(Failure)), FColor::Red, Level);
}

#endif

bool IsComponentUsable(const UPrimitiveComponent* Component, const FIMGTraversalDetectorSettings& Settings)
{
	return IsValid(Component)
		&& (!Component->IsSimulatingPhysics() || Settings.bAllowSimulatingPhysics)
		&& Component->GetComponentVelocity().Size() <= Settings.MaxMovingSpeed
		&& Component->GetPhysicsAngularVelocityInDegrees().Size() <= Settings.MaxAngularSpeedDegrees;
}

bool AddObstacleComponent(FIMGTraversalCandidate& Candidate, UPrimitiveComponent* Component,
	const FIMGTraversalDetectorSettings& Settings)
{
	if (!IsComponentUsable(Component, Settings))
	{
		return false;
	}
	Candidate.ObstacleComponents.AddUnique(Component);
	return true;
}

float GetCandidateScore(const FVector& Origin, const FVector& Forward, const FHitResult& Hit)
{
	const float DistanceScore = FVector::DistSquared2D(Origin, Hit.ImpactPoint)
		/ FMath::Square(500.f) * 5.f;
	const float Facing = FMath::Clamp(
		-FVector::DotProduct(Hit.ImpactNormal.GetSafeNormal2D(), Forward), -1.f, 1.f);
	return DistanceScore + (1.f - Facing) * 0.5f;
}

bool IsReachableAlongGroundSlope(UIMGCharacterMovementComponent& Movement, const FVector& FootLocation,
	const FVector& LedgeLocation, const FVector& SurfaceForward)
{
	if (!Movement.IsMovingOnGround())
	{
		return false;
	}
	const FHitResult& GroundHit = Movement.GetGroundInfo().GroundHitResult;
	const FVector GroundNormal = GroundHit.ImpactNormal.GetSafeNormal();
	if (!GroundHit.bBlockingHit || GroundNormal.Z <= WalkableSlopeMinNormalZ
		|| GroundNormal.Equals(FVector::UpVector, KINDA_SMALL_NUMBER))
	{
		return false;
	}
	const FVector SlopeDirection = FVector::VectorPlaneProject(SurfaceForward, GroundNormal).GetSafeNormal();
	if (SlopeDirection.Z <= 0.f)
	{
		return false;
	}
	const FVector ProjectionStart = FootLocation + FVector::UpVector * WalkableSlopeBaseOffset;
	const FVector ProjectionEnd = ProjectionStart + SlopeDirection * WalkableSlopeProjectionLength;
	const FVector ClosestPoint = FMath::ClosestPointOnSegment(LedgeLocation, ProjectionStart, ProjectionEnd);
	const FVector Separation = LedgeLocation - ClosestPoint;
#if ENABLE_DRAW_DEBUG
	IMGTraversalDebug::Line(Movement.GetWorld(), ProjectionStart, ProjectionEnd, FColor::Silver, 3, 1.f);
	IMGTraversalDebug::Line(Movement.GetWorld(), ClosestPoint, LedgeLocation,
		Separation.Z < 0.f || Separation.Size() < WalkableSlopeDistanceTolerance
			? FColor::Red : FColor::Green, 3, 1.f);
#endif
	return Separation.Z < 0.f || Separation.Size() < WalkableSlopeDistanceTolerance;
}

bool IsVaultExitSupported(const FIMGTraversalCandidate& Candidate, bool bRunning)
{
	struct FExitCondition
	{
		float MinHeight;
		float MaxHeight;
		float MinDepth;
		float MaxDepth;
		float MinDrop;
		float MaxDrop;
		float MinVerticalClearance;
		float ExitClearance;
		bool bRequiresRunning;
	};

	static constexpr FExitCondition Conditions[] =
	{
		{0.f, 80.f, 0.f, 32.5f, 32.5f, TNumericLimits<float>::Max(), 120.f, 50.f, false},
		{0.f, 80.f, 0.f, 57.5f, 32.5f, TNumericLimits<float>::Max(), 150.f, 75.f, true},
		{80.f, TNumericLimits<float>::Max(), 0.f, 20.f, 50.f, TNumericLimits<float>::Max(), 0.f, 65.f, false},
		{80.f, TNumericLimits<float>::Max(), 0.f, 35.f, 32.5f, 50.f, 0.f, 65.f, false},
		{80.f, TNumericLimits<float>::Max(), 0.f, 50.f, 50.f, TNumericLimits<float>::Max(), 0.f, 75.f, false},
		{80.f, TNumericLimits<float>::Max(), 0.f, 70.f, 50.f, TNumericLimits<float>::Max(), 0.f, 75.f, false},
		{60.f, 120.f, 100.f, 200.f, 60.f, 250.f, 0.f, 100.f, false},
	};

	for (const FExitCondition& Condition : Conditions)
	{
		if ((!Condition.bRequiresRunning || bRunning)
			&& Candidate.ObstacleHeight > Condition.MinHeight
			&& Candidate.ObstacleHeight <= Condition.MaxHeight
			&& Candidate.ObstacleDepth >= Condition.MinDepth
			&& Candidate.ObstacleDepth <= Condition.MaxDepth
			&& Candidate.DropHeight > Condition.MinDrop
			&& Candidate.DropHeight < Condition.MaxDrop
			&& Candidate.VerticalClearance >= Condition.MinVerticalClearance
			&& Candidate.HorizontalClearance >= Candidate.ObstacleDepth + Condition.ExitClearance)
		{
			return true;
		}
	}
	return false;
}

bool TraceTop(UWorld& World, const FVector& Point, float StartZ, float EndZ, float ProbeRadius,
	const FCollisionQueryParams& Params, FHitResult& OutHit)
{
	const FVector Start(Point.X, Point.Y, StartZ);
	const FVector End(Point.X, Point.Y, EndZ);
	const bool bHit = World.SweepSingleByChannel(OutHit, Start, End, FQuat::Identity,
		IMG_TraceChannel_Traversal, FCollisionShape::MakeSphere(ProbeRadius), Params)
		&& OutHit.bBlockingHit && !OutHit.bStartPenetrating;
#if ENABLE_DRAW_DEBUG
	IMGTraversalDebug::Line(&World, Start, bHit ? OutHit.ImpactPoint : End,
		bHit ? FColor::Cyan : FColor::Red, 3);
	IMGTraversalDebug::Sphere(&World, Start, ProbeRadius, bHit ? FColor::Cyan : FColor::Red, 3);
	if (bHit)
	{
		IMGTraversalDebug::Point(&World, OutHit.ImpactPoint, FColor::Cyan, 3, 7.f);
	}
#endif
	return bHit;
}

bool MakeCandidate(const FIMGTraversalQuery& Query, const FHitResult& Face, const FVector& Forward,
	float FrontEdgeVerticalSpan, float MidHeightApproachClearance,
	FIMGTraversalCandidate& Out, EIMGTraversalFailure& Failure)
{
	AIMGCharacter* Character = Query.Character.Get();
	const FIMGTraversalDetectorSettings& Settings = Query.Settings;
	UPrimitiveComponent* FaceComponent = Face.GetComponent();
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	UIMGCharacterMovementComponent* Movement = Character->GetIMGMovementComponent();
	UWorld* World = Character->GetWorld();
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const FVector Up = FVector::UpVector;
	const float FootZ = Character->GetActorLocation().Z - HalfHeight;
	const FVector FlatFaceNormal = Face.ImpactNormal.GetSafeNormal2D();
	const FVector SurfaceForward = -FlatFaceNormal;
	const FVector Right = FVector::CrossProduct(Up, SurfaceForward).GetSafeNormal();
	const float FacingDot = FVector::DotProduct(SurfaceForward, Forward);
	if (!IsValid(FaceComponent) || !World || FacingDot < Settings.MinFacingDot
		|| Face.ImpactNormal.Z > 0.9f
		|| SurfaceForward.IsNearlyZero() || Right.IsNearlyZero())
	{
		Failure = EIMGTraversalFailure::InvalidSurface;
		return false;
	}
	if (!AddObstacleComponent(Out, FaceComponent, Settings))
	{
		Failure = EIMGTraversalFailure::MovingTarget;
#if ENABLE_DRAW_DEBUG
		IMGTraversalDebug::String(World, Face.ImpactPoint + Up * 15.f,
			FString::Printf(TEXT("MovingTarget: %s"), *GetNameSafe(FaceComponent)), FColor::Red, 2);
#endif
		return false;
	}
#if ENABLE_DRAW_DEBUG
	IMGTraversalDebug::Point(World, Face.ImpactPoint, FColor::Yellow, 2, 11.f);
	IMGTraversalDebug::Arrow(World, Face.ImpactPoint, Face.ImpactPoint + Face.ImpactNormal * 40.f,
		FColor::Yellow, 2);
#endif
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IMGTraversalTop), false, Character);
	// UE channel sweeps stop at the front blocking face; move the vertical capsule just past it
	// to reproduce the reference detector's multi-hit top-surface selection.
	const FVector ProbeOrigin = Face.ImpactPoint + SurfaceForward * (TopProbeRadius + 2.f);
	FHitResult CenterTop;
	if (!TraceTop(*World, ProbeOrigin, Face.ImpactPoint.Z + TopProbeStartOffset,
		Face.ImpactPoint.Z + TopProbeStartOffset - TopProbeLength, TopProbeRadius, Params, CenterTop))
	{
		Failure = EIMGTraversalFailure::NoFrontLedge;
#if ENABLE_DRAW_DEBUG
		IMGTraversalDebug::String(World, ProbeOrigin + Up * Settings.MaxHeight,
			TEXT("FrontLedge: top sweep missed"), FColor::Red, 2);
#endif
		return false;
	}
	if (!Movement->IsWalkable(CenterTop))
	{
		Failure = EIMGTraversalFailure::NoFrontLedge;
#if ENABLE_DRAW_DEBUG
		IMGTraversalDebug::String(World, CenterTop.ImpactPoint + Up * 12.f,
			TEXT("FrontLedge: top is not walkable"), FColor::Red, 2);
#endif
		return false;
	}
	UPrimitiveComponent* TopComponent = CenterTop.GetComponent();
	if (!AddObstacleComponent(Out, TopComponent, Settings))
	{
		Failure = EIMGTraversalFailure::MovingTarget;
		return false;
	}
	const float Height = CenterTop.ImpactPoint.Z - FootZ;
	if (Height < Settings.MinHeight || Height > Settings.MaxHeight)
	{
		Failure = EIMGTraversalFailure::NoFrontLedge;
#if ENABLE_DRAW_DEBUG
		IMGTraversalDebug::String(World, CenterTop.ImpactPoint + Up * 12.f,
			FString::Printf(TEXT("FrontLedge: height %.0f outside %.0f..%.0f"),
				Height, Settings.MinHeight, Settings.MaxHeight), FColor::Red, 2);
#endif
		return false;
	}

	const float SideOffset = FMath::Max(TopProbeRadius * 2.f,
		Settings.MinWidth * 0.5f - TopProbeRadius);
	FVector SideTopPoints[2];
	int32 SidePointIndex = 0;
	for (const float Side : {-1.f, 1.f})
	{
		FHitResult SideTop;
		if (!TraceTop(*World, ProbeOrigin + Right * SideOffset * Side,
			Face.ImpactPoint.Z + TopProbeStartOffset,
			Face.ImpactPoint.Z + TopProbeStartOffset - TopProbeLength,
			TopProbeRadius, Params, SideTop)
			|| !Movement->IsWalkable(SideTop)
			|| FMath::Abs(SideTop.ImpactPoint.Z - CenterTop.ImpactPoint.Z) > TopHeightTolerance)
		{
			Failure = EIMGTraversalFailure::InsufficientWidth;
#if ENABLE_DRAW_DEBUG
			IMGTraversalDebug::String(World, ProbeOrigin + Right * SideOffset * Side + Up * 15.f,
				Side < 0.f ? TEXT("Width: left support failed") : TEXT("Width: right support failed"),
				FColor::Red, 2);
#endif
			return false;
		}
		if (!AddObstacleComponent(Out, SideTop.GetComponent(), Settings))
		{
			Failure = EIMGTraversalFailure::MovingTarget;
			return false;
		}
		SideTopPoints[SidePointIndex++] = SideTop.ImpactPoint;
	}
#if ENABLE_DRAW_DEBUG
	IMGTraversalDebug::Line(World, SideTopPoints[0], SideTopPoints[1], FColor::Blue, 3, 2.f);
#endif

	FVector FrontPoint = CenterTop.ImpactPoint;
	FrontPoint -= FlatFaceNormal
		* FVector::DotProduct(FrontPoint - Face.ImpactPoint, FlatFaceNormal);
	const FVector FootLocation(Character->GetActorLocation().X, Character->GetActorLocation().Y, FootZ);
	if (IsReachableAlongGroundSlope(*Movement, FootLocation, FrontPoint, SurfaceForward))
	{
		Failure = EIMGTraversalFailure::InvalidSurface;
#if ENABLE_DRAW_DEBUG
		IMGTraversalDebug::String(World, FrontPoint + Up * 20.f,
			TEXT("Surface: reachable along ground slope"), FColor::Red, 2);
#endif
		return false;
	}
	const FRotator Facing = FRotationMatrix::MakeFromXZ(SurfaceForward, Up).Rotator();
	Out.FrontLedge = FTransform(Facing, FrontPoint);
	Out.HitComponent = FaceComponent;
	Out.FrontLedgeComponent = TopComponent;
	Out.FrontLedgeNormal = Face.ImpactNormal;
	Out.ApproachDirection = Forward;
	Out.ObstacleHeight = Height;
	const USkeletalMeshComponent* Mesh = Character->GetMesh();
	Out.DistanceToLedge = FVector::Distance(FrontPoint,
		Mesh ? Mesh->GetComponentLocation() : Character->GetActorLocation());
	Out.FrontEdgeVerticalSpan = FrontEdgeVerticalSpan;
	Out.MidHeightApproachClearance = MidHeightApproachClearance;
	Out.bHasHighOpenApproach = Height > HighOpenApproachMinHeight
		&& MidHeightApproachClearance >= HighOpenApproachMinClearance;
	Out.DetectionTime = World->GetTimeSeconds();
	Out.Speed = Movement->Velocity.Size2D();
	Out.MovementMode = Movement->MovementMode;
	Out.bHasFrontLedge = true;

	FVector LastTopPoint = FrontPoint;
	UPrimitiveComponent* LastTopComponent = TopComponent;
	FVector FirstInvalidPoint = FrontPoint;
	FHitResult FirstDropHit;
	bool bHasDropHit = false;
	bool bHitRisingObstacle = false;
	const int32 DepthSamples = FMath::Clamp(
		FMath::CeilToInt(Settings.MaxDepth / DepthProbeSpacing), 1, MaxDepthSamples);
	const float DepthStartOffset = HorizontalClearanceVerticalOffset
		+ 2.f * HorizontalClearanceRadius * 2.f
		- (DepthProbeRadius - HorizontalClearanceRadius) - HorizontalClearanceRadius;
	bool bFoundEdge = false;
#if ENABLE_DRAW_DEBUG
	const FVector FirstDepthProbeStart = FrontPoint + Up * DepthStartOffset;
	IMGTraversalDebug::Line(World, FirstDepthProbeStart, FrontPoint,
		FColor::Cyan, 3, 2.f);
	IMGTraversalDebug::Sphere(World, FirstDepthProbeStart, DepthProbeRadius,
		FColor::Cyan, 3);
	IMGTraversalDebug::String(World, FirstDepthProbeStart,
		FString::Printf(TEXT("D1/%d 0"), DepthSamples), FColor::Cyan, 3);
#endif
	for (int32 Index = 1; Index < DepthSamples; ++Index)
	{
		const FVector Probe = FrontPoint + SurfaceForward * (DepthProbeSpacing * Index);
		const FVector ProbeStart = Probe + Up * DepthStartOffset;
		const FVector ProbeEnd = ProbeStart - Up * Settings.DepthProbeLength;
		FHitResult DepthHit;
		const bool bDepthHit = World->SweepSingleByChannel(DepthHit, ProbeStart, ProbeEnd,
			FQuat::Identity, IMG_TraceChannel_Traversal,
			FCollisionShape::MakeSphere(DepthProbeRadius), Params)
			&& DepthHit.bBlockingHit && !DepthHit.bStartPenetrating && Movement->IsWalkable(DepthHit);
#if ENABLE_DRAW_DEBUG
		IMGTraversalDebug::Line(World, ProbeStart, bDepthHit ? DepthHit.ImpactPoint : ProbeEnd,
			bDepthHit ? FColor::Cyan : FColor::Red, 3, 2.f);
		IMGTraversalDebug::Sphere(World, ProbeStart, DepthProbeRadius,
			bDepthHit ? FColor::Cyan : FColor::Red, 3);
		IMGTraversalDebug::String(World, ProbeStart,
			FString::Printf(TEXT("D%d/%d %.0f"), Index + 1, DepthSamples,
				DepthProbeSpacing * Index), bDepthHit ? FColor::Cyan : FColor::Red, 3);
#endif
		if (!bDepthHit || FMath::Abs(DepthHit.ImpactPoint.Z - LastTopPoint.Z) > DepthHeightTolerance)
		{
			FirstInvalidPoint = Probe;
			bHitRisingObstacle = bDepthHit && DepthHit.ImpactPoint.Z > LastTopPoint.Z + DepthHeightTolerance;
			bFoundEdge = !bHitRisingObstacle;
			if (bDepthHit && !bHitRisingObstacle)
			{
				FirstDropHit = DepthHit;
				bHasDropHit = true;
				Out.DropHeight = FMath::Max(FrontPoint.Z - DepthHit.ImpactPoint.Z, 0.f);
			}
			else if (!bDepthHit)
			{
				Out.DropHeight = Settings.DepthProbeLength;
			}
#if ENABLE_DRAW_DEBUG
			if (bHitRisingObstacle)
			{
				IMGTraversalDebug::String(World, DepthHit.ImpactPoint + Up * 12.f,
					TEXT("Depth stopped by rising obstacle"), FColor::Orange, 2);
			}
#endif
			break;
		}
		LastTopPoint = DepthHit.ImpactPoint;
		if (!AddObstacleComponent(Out, DepthHit.GetComponent(), Settings))
		{
			Failure = EIMGTraversalFailure::MovingTarget;
			return false;
		}
		LastTopComponent = DepthHit.GetComponent();
	}
	if (bFoundEdge)
	{
		FVector ValidPoint = LastTopPoint;
		FVector InvalidPoint = FirstInvalidPoint;
		for (int32 Iteration = 0; Iteration < 4; ++Iteration)
		{
			const FVector Probe = FMath::Lerp(ValidPoint, InvalidPoint, 0.5f);
			FHitResult RefineHit;
			if (TraceTop(*World, Probe, FrontPoint.Z + 30.f, FrontPoint.Z - 30.f,
				TopProbeRadius, Params, RefineHit)
				&& Movement->IsWalkable(RefineHit)
				&& FMath::Abs(RefineHit.ImpactPoint.Z - FrontPoint.Z) <= DepthHeightTolerance)
			{
				ValidPoint = RefineHit.ImpactPoint;
				if (!AddObstacleComponent(Out, RefineHit.GetComponent(), Settings))
				{
					Failure = EIMGTraversalFailure::MovingTarget;
					return false;
				}
				LastTopComponent = RefineHit.GetComponent();
			}
			else
			{
				InvalidPoint = Probe;
			}
		}
		LastTopPoint = ValidPoint;
	}
	Out.ObstacleDepth = (bFoundEdge || bHitRisingObstacle)
		? FVector::Dist2D(FrontPoint, LastTopPoint) : Settings.MaxDepth;
	Out.bHasBackLedge = bFoundEdge;
	Out.BackLedge = FTransform(Facing, LastTopPoint);
	Out.BackLedgeComponent = LastTopComponent;
	Out.BackLedgeNormal = SurfaceForward;
#if ENABLE_DRAW_DEBUG
	IMGTraversalDebug::Line(World, FrontPoint, LastTopPoint, FColor::Orange, 3, 2.f);
	IMGTraversalDebug::Point(World, LastTopPoint, bFoundEdge ? FColor::Orange : FColor::Silver, 3, 10.f);
#endif
	if (Out.bHasBackLedge)
	{
		FHitResult BackFace;
		const FVector BackFaceHeight = LastTopPoint - Up * FMath::Min(25.f, Height * 0.25f);
		const FVector BackFaceStart = BackFaceHeight + SurfaceForward * (DepthProbeSpacing + 5.f);
		const FVector BackFaceEnd = BackFaceHeight - SurfaceForward * (DepthProbeSpacing + 5.f);
		const bool bBackFaceHit = World->LineTraceSingleByChannel(BackFace, BackFaceStart,
			BackFaceEnd, IMG_TraceChannel_Traversal, Params) && BackFace.bBlockingHit;
#if ENABLE_DRAW_DEBUG
		IMGTraversalDebug::Line(World, BackFaceStart, bBackFaceHit ? BackFace.ImpactPoint : BackFaceEnd,
			bBackFaceHit ? FColor::Orange : FColor::Red, 3, 1.5f);
#endif
		if (bBackFaceHit)
		{
			if (!AddObstacleComponent(Out, BackFace.GetComponent(), Settings))
			{
				Failure = EIMGTraversalFailure::MovingTarget;
				return false;
			}
			Out.BackLedgeNormal = BackFace.ImpactNormal;
		}
	}

	FCollisionQueryParams ApproachParams(SCENE_QUERY_STAT(IMGTraversalApproachPath), false, Character);
	FHitResult ApproachPathHit;
	const float ApproachRadius = FMath::Max(Radius * 0.5f, 5.f);
	const FVector ApproachStart = Character->GetActorLocation();
	const FVector ApproachEnd = FrontPoint + Up * HandholdPathVerticalOffset
		+ FlatFaceNormal * HandholdPathNormalOffset;
	const bool bApproachBlocked = World->SweepSingleByChannel(ApproachPathHit, ApproachStart, ApproachEnd,
		FQuat::Identity, IMG_TraceChannel_Traversal,
		FCollisionShape::MakeSphere(ApproachRadius), ApproachParams)
		&& ApproachPathHit.bBlockingHit;
#if ENABLE_DRAW_DEBUG
	const FVector ApproachDebugEnd = bApproachBlocked
		? (ApproachPathHit.bStartPenetrating ? ApproachStart : ApproachPathHit.ImpactPoint) : ApproachEnd;
	IMGTraversalDebug::Line(World, ApproachStart, ApproachDebugEnd,
		bApproachBlocked ? FColor::Red : FColor::Green, 3, 2.f);
	IMGTraversalDebug::Sphere(World, ApproachStart, ApproachRadius,
		bApproachBlocked ? FColor::Red : FColor::Green, 3);
	IMGTraversalDebug::Sphere(World, ApproachDebugEnd, ApproachRadius,
		bApproachBlocked ? FColor::Red : FColor::Green, 3);
#endif
	if (bApproachBlocked)
	{
		Failure = EIMGTraversalFailure::BlockedPath;
#if ENABLE_DRAW_DEBUG
		IMGTraversalDebug::String(World, ApproachDebugEnd + Up * 12.f,
			TEXT("BlockedPath: handhold capsule"), FColor::Red, 2);
#endif
		return false;
	}

	const FVector VerticalClearanceStart = FrontPoint
		+ SurfaceForward * VerticalClearanceRadius
		+ Up * (HorizontalClearanceVerticalOffset + HorizontalClearanceRadius);
	const FVector VerticalClearanceEnd(VerticalClearanceStart.X, VerticalClearanceStart.Y,
		FrontPoint.Z + FMath::Max(VerticalClearanceRadius * 2.f, HalfHeight * 2.f - 5.f));
	FHitResult VerticalClearanceHit;
	FCollisionQueryParams VerticalClearanceParams(SCENE_QUERY_STAT(IMGTraversalVerticalClearance), false, Character);
	const bool bVerticalClearanceHit = World->SweepSingleByChannel(VerticalClearanceHit,
		VerticalClearanceStart, VerticalClearanceEnd, FQuat::Identity,
		IMG_TraceChannel_Traversal, FCollisionShape::MakeSphere(VerticalClearanceRadius),
		VerticalClearanceParams) && VerticalClearanceHit.bBlockingHit;
	Out.VerticalClearance = bVerticalClearanceHit
		? (VerticalClearanceHit.bStartPenetrating ? 0.f
			: FMath::Max(VerticalClearanceHit.Location.Z - FrontPoint.Z, 0.f))
		: Settings.MaxHeight;
#if ENABLE_DRAW_DEBUG
	const FVector VerticalDebugEnd = bVerticalClearanceHit
		? (VerticalClearanceHit.bStartPenetrating ? VerticalClearanceStart : VerticalClearanceHit.Location)
		: VerticalClearanceEnd;
	IMGTraversalDebug::Line(World, VerticalClearanceStart, VerticalDebugEnd,
		bVerticalClearanceHit ? FColor::Yellow : FColor::Green, 3, 2.f);
	IMGTraversalDebug::Sphere(World, VerticalClearanceStart, VerticalClearanceRadius,
		bVerticalClearanceHit ? FColor::Yellow : FColor::Green, 3);
	IMGTraversalDebug::String(World, VerticalDebugEnd + Right * 10.f,
		FString::Printf(TEXT("Vertical clearance %.0fcm"), Out.VerticalClearance),
		bVerticalClearanceHit ? FColor::Yellow : FColor::Green, 3);
#endif

	FVector ClearanceDirection = SurfaceForward;
	const FVector TopSlope = LastTopPoint - FrontPoint;
	if (TopSlope.Z > 0.f && !TopSlope.IsNearlyZero())
	{
		const FVector NormalizedSlope = TopSlope.GetSafeNormal();
		const float SlopeAngle = FMath::RadiansToDegrees(FMath::Acos(
			FMath::Clamp(FVector::DotProduct(NormalizedSlope, SurfaceForward), -1.f, 1.f)));
		if (SlopeAngle < 42.5f)
		{
			ClearanceDirection = NormalizedSlope;
		}
	}
	const float MaxClearance = FMath::Max(Settings.MaxHorizontalClearance, HorizontalClearanceRadius * 2.f);
	float HorizontalClearance = MaxClearance;
	for (int32 ClearanceIndex = 0; ClearanceIndex < 2; ++ClearanceIndex)
	{
		FVector ClearanceStart = FrontPoint
			+ Up * (HorizontalClearanceVerticalOffset
				+ HorizontalClearanceRadius * (1.f + ClearanceIndex * 2.f));
		if (ClearanceIndex == 0)
		{
			ClearanceStart -= ClearanceDirection * HorizontalClearanceBackOffset;
		}
		const FVector ClearanceEnd = ClearanceStart + ClearanceDirection * MaxClearance;
		FHitResult ClearanceHit;
		FCollisionQueryParams ClearanceParams(SCENE_QUERY_STAT(IMGTraversalHorizontalClearance), false, Character);
		const bool bClearanceHit = World->SweepSingleByChannel(ClearanceHit, ClearanceStart, ClearanceEnd,
			FQuat::Identity, IMG_TraceChannel_Traversal,
			FCollisionShape::MakeSphere(HorizontalClearanceRadius), ClearanceParams);
		const bool bClearanceBlocked = bClearanceHit && ClearanceHit.bBlockingHit;
		if (bClearanceBlocked)
		{
			const float HitClearance = ClearanceHit.bStartPenetrating ? 0.f
				: FVector::Dist2D(ClearanceStart, ClearanceHit.Location)
					- FVector::Dist2D(ClearanceStart, FrontPoint);
			HorizontalClearance = FMath::Min(HorizontalClearance, FMath::Clamp(HitClearance, 0.f, MaxClearance));
		}
#if ENABLE_DRAW_DEBUG
		const FVector ClearanceDebugEnd = bClearanceBlocked
			? (ClearanceHit.bStartPenetrating ? ClearanceStart : ClearanceHit.Location) : ClearanceEnd;
		IMGTraversalDebug::Line(World, ClearanceStart,
			ClearanceDebugEnd,
			bClearanceBlocked ? FColor::Orange : FColor::Green, 3, 2.f);
		IMGTraversalDebug::Sphere(World, ClearanceStart, HorizontalClearanceRadius,
			bClearanceBlocked ? FColor::Orange : FColor::Green, 3);
#endif
	}
	Out.HorizontalClearance = HorizontalClearance;
#if ENABLE_DRAW_DEBUG
	IMGTraversalDebug::String(World, FrontPoint + Up * (HalfHeight + 20.f),
		FString::Printf(TEXT("Horizontal clearance %.0fcm"), HorizontalClearance),
		HorizontalClearance <= Settings.MinHorizontalClearance ? FColor::Red : FColor::Green, 3);
#endif
	if (HorizontalClearance <= Settings.MinHorizontalClearance)
	{
		Failure = EIMGTraversalFailure::BlockedPath;
#if ENABLE_DRAW_DEBUG
		IMGTraversalDebug::String(World, FrontPoint + Up * (HalfHeight + 38.f),
			FString::Printf(TEXT("BlockedPath: clearance <= %.0fcm"), Settings.MinHorizontalClearance),
			FColor::Red, 2);
#endif
		return false;
	}

	if (Out.bHasBackLedge && bHasDropHit)
	{
		Out.BackLedgeHeight = LastTopPoint.Z - FirstDropHit.ImpactPoint.Z;
		if (Out.DropHeight >= 0.f && Out.DropHeight <= Settings.MaxDrop
			&& IsComponentUsable(FirstDropHit.GetComponent(), Settings))
		{
			// This probe establishes rear support geometry, not the animation landing pose.
			// The selected montage curve determines that pose and the GA validates its full capsule and path.
			Out.bHasBackFloor = true;
			Out.BackFloorComponent = FirstDropHit.GetComponent();
			Out.BackFloor = FTransform(Facing, FirstDropHit.ImpactPoint);
		}
	}

	Out.bCanMantle = Out.VerticalClearance >= 180.f
		&& (Out.ObstacleDepth >= 32.5f
			|| (Out.HorizontalClearance > 40.f && Out.DropHeight <= 32.5f));
	const float ForwardSpeed = FMath::Max(0.f, FVector::DotProduct(Movement->Velocity, Forward));
	const bool bRunning = ForwardSpeed >= Settings.RunningSpeedThreshold;
	Out.bCanVault = Out.bHasBackLedge && Height <= Settings.MaxVaultHeight
		&& IsVaultExitSupported(Out, bRunning);
	Out.bCanHurdle = Out.bCanVault && Out.bHasBackFloor && Height <= Settings.MaxHurdleHeight;
	Out.RecommendedAction = Out.bCanHurdle ? EIMGTraversalAction::Hurdle
		: Out.bCanVault ? EIMGTraversalAction::Vault
		: Out.bCanMantle ? EIMGTraversalAction::Mantle
		: EIMGTraversalAction::None;
	if (!Out.bCanMantle && !Out.bCanVault && !Out.bCanHurdle)
	{
		Failure = EIMGTraversalFailure::NoSupportedAction;
		return false;
	}
	Out.LocalFrontLedge = Out.FrontLedge.GetRelativeTransform(TopComponent->GetComponentTransform());
	if (const UPrimitiveComponent* BackLedgeComponent = Out.BackLedgeComponent.Get())
	{
		Out.LocalBackLedge = Out.BackLedge.GetRelativeTransform(BackLedgeComponent->GetComponentTransform());
	}
	if (const UPrimitiveComponent* BackFloorComponent = Out.BackFloorComponent.Get())
	{
		Out.LocalBackFloor = Out.BackFloor.GetRelativeTransform(BackFloorComponent->GetComponentTransform());
	}
	return true;
}
}

bool FIMGTraversalDetector::Detect(const FIMGTraversalQuery& Query, FIMGTraversalCandidate& OutCandidate,
	EIMGTraversalFailure& OutFailure)
{
#if ENABLE_DRAW_DEBUG
	IMGTraversalDebug::DiscardTraversalSnapshot();
#endif
	OutCandidate = FIMGTraversalCandidate();
	OutFailure = EIMGTraversalFailure::None;
	AIMGCharacter* Character = Query.Character.Get();
	const FIMGTraversalDetectorSettings& Settings = Query.Settings;
	if (!IsValid(Character) || !Character->GetWorld() || !Character->GetCapsuleComponent())
	{
		OutFailure = EIMGTraversalFailure::InvalidCharacter;
		return false;
	}
	const UIMGCharacterMovementComponent* Movement = Character->GetIMGMovementComponent();
	if (!Movement || Character->GetStance() != EIMGStance::Stand
		|| !(Movement->IsMovingOnGround() || Movement->IsFalling()))
	{
		OutFailure = EIMGTraversalFailure::InvalidMovement;
#if ENABLE_DRAW_DEBUG
		DrawFailure(*Character, OutFailure, Character->GetActorLocation() + FVector::UpVector * 50.f);
#endif
		return false;
	}
	const FVector Forward = FVector(Query.Direction.X, Query.Direction.Y, 0.f).GetSafeNormal();
	if (Forward.IsNearlyZero() || Settings.RunningSpeedThreshold < 0.f
		|| Settings.SearchDistance <= 0.f
		|| Settings.MaxAnimationReachDistance <= 0.f
		|| Settings.MaxAnimationReachDistance > Settings.SearchDistance
		|| Settings.MinHeight < 0.f || Settings.MaxDepth <= 0.f
		|| Settings.MaxHorizontalClearance <= 0.f || Settings.MinHorizontalClearance < 0.f
		|| Settings.MaxHorizontalClearance <= Settings.MinHorizontalClearance
		|| Settings.DepthProbeLength <= 0.f || Settings.MaxDrop < 0.f
		|| Settings.MaxHeight <= Settings.MinHeight || Settings.MinWidth <= 0.f
		|| Settings.MinFacingDot < -1.f || Settings.MinFacingDot > 1.f
		|| Settings.MaxMovingSpeed < 0.f || Settings.MaxAngularSpeedDegrees < 0.f
		|| Settings.MaxCandidateAge <= 0.f)
	{
		OutFailure = EIMGTraversalFailure::InvalidMovement;
#if ENABLE_DRAW_DEBUG
		DrawFailure(*Character, OutFailure, Character->GetActorLocation() + FVector::UpVector * 50.f);
#endif
		return false;
	}

	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const FVector Origin = Character->GetActorLocation();
#if ENABLE_DRAW_DEBUG
	IMGTraversalDebug::BeginTraversalSnapshot(Character->GetWorld());
	if (IMGTraversalDebug::IsEnabled(2))
	{
		IMGTraversalDebug::String(Character->GetWorld(), Origin + FVector::UpVector * (HalfHeight + 35.f),
			FString::Printf(TEXT("Search %.0f | Anim %.0f | H %.0f..%.0f | Speed %.0f | %s"),
				Settings.SearchDistance, Settings.MaxAnimationReachDistance,
				Settings.MinHeight, Settings.MaxHeight, Movement->Velocity.Size2D(),
				Movement->IsFalling() ? TEXT("Air") : TEXT("Ground")), FColor::White, 2);
	}
#endif
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IMGTraversalCandidates), false, Character);
	TArray<FHitResult> EdgeHits;
	TArray<FVector> EdgeMidPoints;
	TArray<FVector> EdgeNormalSums;
	TArray<FVector2D> EdgeHeightRanges;
	TArray<float> EdgeScores;
	const float FootZ = Origin.Z - HalfHeight;
	const float SearchBaseZ = Origin.Z + SearchStartHeightOffset;
	const float HighestProbeZ = FootZ + Settings.MaxHeight + 25.f;
	const int32 ProbeLayerCount = FMath::Clamp(FMath::RoundToInt(
		(HighestProbeZ - SearchBaseZ) / SearchProbeSeparation), 1, MaxForwardProbeLayers);
	TArray<float> SearchLayerDistances;
	SearchLayerDistances.Init(Settings.SearchDistance, ProbeLayerCount);
#if ENABLE_DRAW_DEBUG
	// The broad search layers stay live-only; the successful snapshot starts with the search summary.
	IMGTraversalDebug::SetTraversalSnapshotCaptureEnabled(false);
#endif
	for (int32 LayerIndex = 0; LayerIndex < ProbeLayerCount; ++LayerIndex)
	{
		TArray<FHitResult> ProbeHits;
		const FVector Start(Origin.X, Origin.Y,
			SearchBaseZ + SearchProbeRadius + SearchProbeSeparation * LayerIndex);
		const FVector End = Start + Forward * Settings.SearchDistance;
		Character->GetWorld()->SweepMultiByChannel(ProbeHits, Start, End,
			FQuat::Identity, IMG_TraceChannel_Traversal,
			FCollisionShape::MakeSphere(SearchProbeRadius), Params);
		for (const FHitResult& Hit : ProbeHits)
		{
			if (Hit.bBlockingHit)
			{
				SearchLayerDistances[LayerIndex] = FMath::Min(SearchLayerDistances[LayerIndex],
					FVector::Dist2D(Start, Hit.Location));
			}
		}
#if ENABLE_DRAW_DEBUG
		const FColor SearchColor = ProbeHits.IsEmpty() ? FColor::Silver : FColor::Cyan;
		IMGTraversalDebug::Line(Character->GetWorld(), Start, End, SearchColor, 2, 1.5f);
		IMGTraversalDebug::Sphere(Character->GetWorld(), Start, SearchProbeRadius, SearchColor, 2);
		IMGTraversalDebug::Sphere(Character->GetWorld(), End, SearchProbeRadius, SearchColor, 2);
		IMGTraversalDebug::String(Character->GetWorld(), End + FVector::UpVector * 5.f,
			FString::Printf(TEXT("P%d/%d Z%.0f H%d"), LayerIndex + 1, ProbeLayerCount,
				Start.Z - FootZ, ProbeHits.Num()), SearchColor, 2);
#endif
		for (const FHitResult& Hit : ProbeHits)
		{
			if (!Hit.bBlockingHit || !Hit.GetComponent())
			{
				continue;
			}
			int32 EdgeIndex = INDEX_NONE;
			for (int32 ExistingIndex = 0; ExistingIndex < EdgeHits.Num(); ++ExistingIndex)
			{
				const FVector AverageNormal = EdgeNormalSums[ExistingIndex].GetSafeNormal();
				const FVector2D HeightRange = EdgeHeightRanges[ExistingIndex];
				if (FVector::DistSquared2D(EdgeMidPoints[ExistingIndex], Hit.ImpactPoint)
						<= FMath::Square(EdgePlanarTolerance)
					&& FVector::DotProduct(AverageNormal.GetSafeNormal2D(), Hit.ImpactNormal.GetSafeNormal2D())
						>= EdgeHeadingToleranceDot
					&& Hit.ImpactPoint.Z >= HeightRange.X - EdgeVerticalContinuity
					&& Hit.ImpactPoint.Z <= HeightRange.Y + EdgeVerticalContinuity)
				{
					EdgeIndex = ExistingIndex;
					break;
				}
			}
			if (EdgeIndex == INDEX_NONE)
			{
				EdgeHits.Add(Hit);
				EdgeMidPoints.Add(Hit.ImpactPoint);
				EdgeNormalSums.Add(Hit.ImpactNormal);
				EdgeHeightRanges.Add(FVector2D(Hit.ImpactPoint.Z, Hit.ImpactPoint.Z));
				EdgeScores.Add(GetCandidateScore(Origin, Forward, Hit));
			}
			else
			{
				EdgeMidPoints[EdgeIndex] = (EdgeMidPoints[EdgeIndex] + Hit.ImpactPoint) * 0.5f;
				EdgeNormalSums[EdgeIndex] += Hit.ImpactNormal;
				EdgeHeightRanges[EdgeIndex].X = FMath::Min(EdgeHeightRanges[EdgeIndex].X, Hit.ImpactPoint.Z);
				EdgeHeightRanges[EdgeIndex].Y = FMath::Max(EdgeHeightRanges[EdgeIndex].Y, Hit.ImpactPoint.Z);
				EdgeScores[EdgeIndex] = FMath::Min(EdgeScores[EdgeIndex], GetCandidateScore(Origin, Forward, Hit));
				if (Hit.ImpactPoint.Z > EdgeHits[EdgeIndex].ImpactPoint.Z)
				{
					EdgeHits[EdgeIndex] = Hit;
				}
			}
		}
	}
#if ENABLE_DRAW_DEBUG
	IMGTraversalDebug::SetTraversalSnapshotCaptureEnabled(true);
#endif
	TArray<int32> EdgeOrder;
	EdgeOrder.Reserve(EdgeHits.Num());
	for (int32 EdgeIndex = 0; EdgeIndex < EdgeHits.Num(); ++EdgeIndex)
	{
		const FVector AverageNormal = EdgeNormalSums[EdgeIndex].GetSafeNormal();
		if (!AverageNormal.IsNearlyZero())
		{
			EdgeHits[EdgeIndex].ImpactNormal = AverageNormal;
		}
		EdgeOrder.Add(EdgeIndex);
#if ENABLE_DRAW_DEBUG
		const FVector EdgeBottom(EdgeHits[EdgeIndex].ImpactPoint.X, EdgeHits[EdgeIndex].ImpactPoint.Y,
			EdgeHeightRanges[EdgeIndex].X);
		const FVector EdgeTop(EdgeBottom.X, EdgeBottom.Y, EdgeHeightRanges[EdgeIndex].Y);
		IMGTraversalDebug::Line(Character->GetWorld(), EdgeBottom, EdgeTop, FColor::Purple, 2, 1.5f);
		IMGTraversalDebug::Arrow(Character->GetWorld(), EdgeTop,
			EdgeTop + EdgeHits[EdgeIndex].ImpactNormal * 30.f, FColor::Purple, 2);
#endif
	}
	EdgeOrder.Sort([&EdgeScores](int32 A, int32 B)
	{
		return EdgeScores[A] < EdgeScores[B];
	});
	float MidHeightApproachClearance = Settings.SearchDistance;
	for (int32 LayerIndex = 1; LayerIndex < FMath::Min(ProbeLayerCount, 4); ++LayerIndex)
	{
		MidHeightApproachClearance = FMath::Min(MidHeightApproachClearance, SearchLayerDistances[LayerIndex]);
	}
	OutFailure = EIMGTraversalFailure::NoSurface;
	int32 Tested = 0;
	for (const int32 EdgeIndex : EdgeOrder)
	{
		if (++Tested > MaxCandidateHits)
		{
			break;
		}
		const FHitResult& Hit = EdgeHits[EdgeIndex];
		const float EdgeVerticalSpan = EdgeHeightRanges[EdgeIndex].Y - EdgeHeightRanges[EdgeIndex].X;
		FIMGTraversalCandidate Candidate;
		EIMGTraversalFailure Failure = EIMGTraversalFailure::InvalidSurface;
#if ENABLE_DRAW_DEBUG
		IMGTraversalDebug::BeginTraversalCandidateSnapshot();
		IMGTraversalDebug::String(Character->GetWorld(), Hit.ImpactPoint + FVector::UpVector * 20.f,
			FString::Printf(TEXT("C%d/%d Score %.2f D%.0f Face %.2f Span %.0f Mid %.0f"), Tested,
				FMath::Min(EdgeOrder.Num(), MaxCandidateHits), EdgeScores[EdgeIndex],
				FVector::Dist2D(Origin, Hit.ImpactPoint),
				-FVector::DotProduct(Hit.ImpactNormal.GetSafeNormal2D(), Forward),
				EdgeVerticalSpan, MidHeightApproachClearance), FColor::Yellow, 2);
#endif
		if (MakeCandidate(Query, Hit, Forward, EdgeVerticalSpan, MidHeightApproachClearance,
			Candidate, Failure))
		{
			const float CandidateDistance = FVector::Dist2D(Origin, Candidate.FrontLedge.GetLocation());
			if (CandidateDistance > Settings.MaxAnimationReachDistance)
			{
				OutFailure = EIMGTraversalFailure::OutOfReach;
#if ENABLE_DRAW_DEBUG
				IMGTraversalDebug::String(Character->GetWorld(), Candidate.FrontLedge.GetLocation() + FVector::UpVector * 45.f,
					FString::Printf(TEXT("OutOfReach: %.0f > Anim %.0f"), CandidateDistance,
						Settings.MaxAnimationReachDistance),
					FColor::Red, 2);
				IMGTraversalDebug::RollbackTraversalCandidateSnapshot();
#endif
				continue;
			}
			OutCandidate = Candidate;
			OutFailure = EIMGTraversalFailure::None;
			return true;
		}
		OutFailure = Failure;
#if ENABLE_DRAW_DEBUG
		DrawFailure(*Character, Failure, Hit.ImpactPoint, 2);
		IMGTraversalDebug::RollbackTraversalCandidateSnapshot();
#endif
	}
#if ENABLE_DRAW_DEBUG
	IMGTraversalDebug::DiscardTraversalSnapshot();
	DrawFailure(*Character, OutFailure, Origin + Forward * Settings.SearchDistance + FVector::UpVector * 30.f);
#endif
	return false;
}
