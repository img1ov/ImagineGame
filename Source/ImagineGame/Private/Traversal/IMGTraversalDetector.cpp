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

#if ENABLE_DRAW_DEBUG
FString GetFailureName(EIMGTraversalFailure Failure)
{
	return StaticEnum<EIMGTraversalFailure>()->GetNameStringByValue(static_cast<int64>(Failure));
}

FString GetActionName(EIMGTraversalAction Action)
{
	return StaticEnum<EIMGTraversalAction>()->GetNameStringByValue(static_cast<int64>(Action));
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

void DrawCandidateResult(const AIMGCharacter& Character, const FIMGTraversalCandidate& Candidate)
{
	if (!IMGTraversalDebug::IsEnabled(1))
	{
		return;
	}
	const UWorld* World = Character.GetWorld();
	const FVector Front = Candidate.FrontLedge.GetLocation();
	IMGTraversalDebug::Point(World, Front, FColor::Yellow, 1, 18.f);
	IMGTraversalDebug::Arrow(World, Front, Front + Candidate.FrontLedgeNormal * 45.f, FColor::Magenta, 1);
	IMGTraversalDebug::Arrow(World, Front, Front + Candidate.FrontLedge.GetUnitAxis(EAxis::X) * 45.f, FColor::Yellow, 1);
	if (Candidate.bHasBackLedge)
	{
		const FVector Back = Candidate.BackLedge.GetLocation();
		IMGTraversalDebug::Point(World, Back, FColor::Orange, 1, 18.f);
		IMGTraversalDebug::Arrow(World, Back, Back + Candidate.BackLedgeNormal * 45.f, FColor::Orange, 1);
		IMGTraversalDebug::Line(World, Front, Back, FColor::Orange, 1, 2.f);
	}
	if (Candidate.bHasBackFloor)
	{
		IMGTraversalDebug::Point(World, Candidate.BackFloor.GetLocation(), FColor::Green, 1, 18.f);
	}
	const FString Feasible = FString::Printf(TEXT("%s%s%s"),
		Candidate.bCanMantle ? TEXT("M") : TEXT("-"),
		Candidate.bCanVault ? TEXT("V") : TEXT("-"),
		Candidate.bCanHurdle ? TEXT("H") : TEXT("-"));
	IMGTraversalDebug::String(World, Front + FVector::UpVector * 28.f,
		FString::Printf(TEXT("Traversal: %s  H %.0f  D %.0f  HClear %.0f  VClear %.0f  Drop %.0f  Dist %.0f  GTAReach %.0f  [%s]  Parts %d"),
			*GetActionName(Candidate.RecommendedAction), Candidate.ObstacleHeight,
			Candidate.ObstacleDepth, Candidate.HorizontalClearance, Candidate.VerticalClearance, Candidate.DropHeight,
			Candidate.DistanceToLedge, Candidate.ReferenceReachDistance, *Feasible,
			Candidate.ObstacleComponents.Num()), FColor::Green, 1);
}
#endif

bool IsComponentUsable(const UPrimitiveComponent* Component, const FIMGTraversalQuery& Query)
{
	return IsValid(Component)
		&& (!Component->IsSimulatingPhysics() || Query.bAllowSimulatingPhysics)
		&& Component->GetComponentVelocity().Size() <= Query.MaxMovingSpeed
		&& Component->GetPhysicsAngularVelocityInDegrees().Size() <= Query.MaxAngularSpeedDegrees;
}

bool AddObstacleComponent(FIMGTraversalCandidate& Candidate, UPrimitiveComponent* Component,
	const FIMGTraversalQuery& Query)
{
	if (!IsComponentUsable(Component, Query))
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

float GetActionReach(const FIMGTraversalQuery& Query, float ForwardSpeed, float ObstacleHeight)
{
	if (ObstacleHeight > 80.f)
	{
		return Query.ForwardDistance;
	}
	if (ForwardSpeed < Query.RunningSpeedThreshold)
	{
		return Query.StepUpForwardDistance;
	}
	const float Alpha = FMath::GetRangePct(
		Query.RunningSpeedThreshold, Query.ForwardDistanceAtMaxSpeed, ForwardSpeed);
	return FMath::Lerp(Query.RunningForwardDistance, Query.MaxForwardDistance,
		FMath::Clamp(Alpha, 0.f, 1.f));
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

bool HasCapsuleRoom(const UWorld& World, const UCapsuleComponent& Capsule, const AActor& Character,
	const FVector& Center, float Radius, float HalfHeight)
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IMGTraversalRoom), false, &Character);
	const FCollisionResponseParams Responses(Capsule.GetCollisionResponseToChannels());
	const bool bHasRoom = !World.OverlapBlockingTestByChannel(Center, FQuat::Identity, Capsule.GetCollisionObjectType(),
		FCollisionShape::MakeCapsule(Radius, HalfHeight), Params, Responses);
#if ENABLE_DRAW_DEBUG
	IMGTraversalDebug::Capsule(&World, Center, HalfHeight, Radius,
		bHasRoom ? FColor::Green : FColor::Red, 3);
#endif
	return bHasRoom;
}

bool MakeCandidate(const FIMGTraversalQuery& Query, const FHitResult& Face, const FVector& Forward,
	FIMGTraversalCandidate& Out, EIMGTraversalFailure& Failure)
{
	AIMGCharacter* Character = Query.Character.Get();
	UPrimitiveComponent* FaceComponent = Face.GetComponent();
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	const UIMGCharacterMovementComponent* Movement = Character->GetIMGMovementComponent();
	UWorld* World = Character->GetWorld();
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const FVector Up = FVector::UpVector;
	const float FootZ = Character->GetActorLocation().Z - HalfHeight;
	const FVector FlatFaceNormal = Face.ImpactNormal.GetSafeNormal2D();
	const FVector SurfaceForward = -FlatFaceNormal;
	const FVector Right = FVector::CrossProduct(Up, SurfaceForward).GetSafeNormal();
	const float FacingDot = FVector::DotProduct(SurfaceForward, Forward);
	if (!IsValid(FaceComponent) || !World || FacingDot < Query.MinFacingDot
		|| Face.ImpactNormal.Z > 0.9f
		|| SurfaceForward.IsNearlyZero() || Right.IsNearlyZero())
	{
		Failure = EIMGTraversalFailure::InvalidSurface;
		return false;
	}
	if (!AddObstacleComponent(Out, FaceComponent, Query))
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
		IMGTraversalDebug::String(World, ProbeOrigin + Up * Query.MaxHeight,
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
	if (!AddObstacleComponent(Out, TopComponent, Query))
	{
		Failure = EIMGTraversalFailure::MovingTarget;
		return false;
	}
	const float Height = CenterTop.ImpactPoint.Z - FootZ;
	if (Height < Query.MinHeight || Height > Query.MaxHeight)
	{
		Failure = EIMGTraversalFailure::NoFrontLedge;
#if ENABLE_DRAW_DEBUG
		IMGTraversalDebug::String(World, CenterTop.ImpactPoint + Up * 12.f,
			FString::Printf(TEXT("FrontLedge: height %.0f outside %.0f..%.0f"),
				Height, Query.MinHeight, Query.MaxHeight), FColor::Red, 2);
#endif
		return false;
	}

	const float SideOffset = FMath::Max(TopProbeRadius * 2.f,
		Query.MinWidth * 0.5f - TopProbeRadius);
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
		if (!AddObstacleComponent(Out, SideTop.GetComponent(), Query))
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
		FMath::CeilToInt(Query.MaxDepth / DepthProbeSpacing), 1, MaxDepthSamples);
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
		FString::Printf(TEXT("Depth 1/%d  0cm"), DepthSamples), FColor::Cyan, 3);
#endif
	for (int32 Index = 1; Index < DepthSamples; ++Index)
	{
		const FVector Probe = FrontPoint + SurfaceForward * (DepthProbeSpacing * Index);
		const FVector ProbeStart = Probe + Up * DepthStartOffset;
		const FVector ProbeEnd = ProbeStart - Up * Query.DepthProbeLength;
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
			FString::Printf(TEXT("Depth %d/%d  %.0fcm"), Index + 1, DepthSamples,
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
				Out.DropHeight = Query.DepthProbeLength;
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
		if (!AddObstacleComponent(Out, DepthHit.GetComponent(), Query))
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
				if (!AddObstacleComponent(Out, RefineHit.GetComponent(), Query))
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
		? FVector::Dist2D(FrontPoint, LastTopPoint) : Query.MaxDepth;
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
			if (!AddObstacleComponent(Out, BackFace.GetComponent(), Query))
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
		: Query.MaxHeight;
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
	const float MaxClearance = FMath::Max(Query.MaxHorizontalClearance, HorizontalClearanceRadius * 2.f);
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
		HorizontalClearance <= Query.MinHorizontalClearance ? FColor::Red : FColor::Green, 3);
#endif
	if (HorizontalClearance <= Query.MinHorizontalClearance)
	{
		Failure = EIMGTraversalFailure::BlockedPath;
#if ENABLE_DRAW_DEBUG
		IMGTraversalDebug::String(World, FrontPoint + Up * (HalfHeight + 38.f),
			FString::Printf(TEXT("BlockedPath: clearance <= %.0fcm"), Query.MinHorizontalClearance),
			FColor::Red, 2);
#endif
		return false;
	}

	if (Out.bHasBackLedge && bHasDropHit)
	{
		Out.BackLedgeHeight = LastTopPoint.Z - FirstDropHit.ImpactPoint.Z;
		const FVector FloorCenter = FirstDropHit.ImpactPoint + Up * (HalfHeight + 3.f);
		if (Out.DropHeight >= 0.f && Out.DropHeight <= Query.MaxDrop
			&& IsComponentUsable(FirstDropHit.GetComponent(), Query)
			&& HasCapsuleRoom(*World, *Capsule, *Character, FloorCenter, Radius, HalfHeight))
		{
			Out.bHasBackFloor = true;
			Out.BackFloorComponent = FirstDropHit.GetComponent();
			Out.BackFloor = FTransform(Facing, FirstDropHit.ImpactPoint);
		}
	}

	Out.bCanMantle = Out.VerticalClearance >= 180.f
		&& (Out.ObstacleDepth >= 32.5f
			|| (Out.HorizontalClearance > 40.f && Out.DropHeight <= 32.5f));
	const float ForwardSpeed = FMath::Max(0.f, FVector::DotProduct(Movement->Velocity, Forward));
	const bool bRunning = ForwardSpeed >= Query.RunningSpeedThreshold;
	Out.bCanVault = Out.bHasBackLedge && Height <= Query.MaxVaultHeight
		&& IsVaultExitSupported(Out, bRunning);
	Out.bCanHurdle = Out.bCanVault && Out.bHasBackFloor && Height <= Query.MaxHurdleHeight;
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
	OutCandidate = FIMGTraversalCandidate();
	OutFailure = EIMGTraversalFailure::None;
	AIMGCharacter* Character = Query.Character.Get();
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
	if (Forward.IsNearlyZero() || Query.ForwardDistance <= 0.f
		|| Query.StepUpForwardDistance <= 0.f || Query.StepUpForwardDistance > Query.ForwardDistance
		|| Query.RunningForwardDistance < Query.ForwardDistance
		|| Query.MaxForwardDistance < Query.RunningForwardDistance
		|| Query.ForwardDistanceAtMaxSpeed <= Query.RunningSpeedThreshold
		|| Query.MaxAnimationReachDistance <= 0.f || Query.MaxAnimationReachDistance > Query.SearchDistance
		|| Query.MinHeight < 0.f || Query.MaxDepth <= 0.f
		|| Query.MaxHorizontalClearance <= 0.f || Query.MinHorizontalClearance < 0.f
		|| Query.MaxHorizontalClearance <= Query.MinHorizontalClearance
		|| Query.DepthProbeLength <= 0.f || Query.MaxDrop < 0.f
		|| Query.MaxHeight <= Query.MinHeight || Query.MinWidth <= 0.f
		|| Query.MinFacingDot < -1.f || Query.MinFacingDot > 1.f
		|| Query.MaxMovingSpeed < 0.f || Query.MaxAngularSpeedDegrees < 0.f || Query.MaxCandidateAge <= 0.f)
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
	const float ForwardSpeed = FMath::Max(0.f, FVector::DotProduct(Movement->Velocity, Forward));
#if ENABLE_DRAW_DEBUG
	if (IMGTraversalDebug::IsEnabled(2))
	{
		IMGTraversalDebug::String(Character->GetWorld(), Origin + FVector::UpVector * (HalfHeight + 35.f),
			FString::Printf(TEXT("Traversal Search: %.0fcm  MMReach %.0fcm  GTA %.0f/%.0f..%.0fcm  Height %.0f..%.0f  Speed %.0f  %s"),
				Query.SearchDistance, Query.MaxAnimationReachDistance, Query.StepUpForwardDistance, Query.ForwardDistance,
				Query.MaxForwardDistance, Query.MinHeight, Query.MaxHeight, Movement->Velocity.Size2D(),
				Movement->IsFalling() ? TEXT("Air") : TEXT("Ground")), FColor::White, 2);
	}
#endif
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IMGTraversalCandidates), false, Character);
	TArray<FHitResult> Hits;
	const float DuplicateDistanceSquared = FMath::Square(9.f);
	const float FootZ = Origin.Z - HalfHeight;
	const float SearchBaseZ = Origin.Z + SearchStartHeightOffset;
	const float HighestProbeZ = FootZ + Query.MaxHeight + 25.f;
	const int32 ProbeLayerCount = FMath::Clamp(FMath::RoundToInt(
		(HighestProbeZ - SearchBaseZ) / SearchProbeSeparation), 1, MaxForwardProbeLayers);
	for (int32 LayerIndex = 0; LayerIndex < ProbeLayerCount; ++LayerIndex)
	{
		TArray<FHitResult> ProbeHits;
		const FVector Start(Origin.X, Origin.Y,
			SearchBaseZ + SearchProbeRadius + SearchProbeSeparation * LayerIndex);
		const FVector End = Start + Forward * Query.SearchDistance;
		Character->GetWorld()->SweepMultiByChannel(ProbeHits, Start, End,
			FQuat::Identity, IMG_TraceChannel_Traversal,
			FCollisionShape::MakeSphere(SearchProbeRadius), Params);
#if ENABLE_DRAW_DEBUG
		const FColor SearchColor = ProbeHits.IsEmpty() ? FColor::Silver : FColor::Cyan;
		IMGTraversalDebug::Line(Character->GetWorld(), Start, End, SearchColor, 2, 1.5f);
		IMGTraversalDebug::Sphere(Character->GetWorld(), Start, SearchProbeRadius, SearchColor, 2);
		IMGTraversalDebug::Sphere(Character->GetWorld(), End, SearchProbeRadius, SearchColor, 2);
		IMGTraversalDebug::String(Character->GetWorld(), End + FVector::UpVector * 5.f,
			FString::Printf(TEXT("Probe %d/%d  Z %.0f  Hits %d"), LayerIndex + 1, ProbeLayerCount,
				Start.Z - FootZ, ProbeHits.Num()), SearchColor, 2);
#endif
		for (const FHitResult& Hit : ProbeHits)
		{
			if (!Hit.bBlockingHit || !Hit.GetComponent())
			{
				continue;
			}
			const int32 ExistingIndex = Hits.IndexOfByPredicate([&Hit, DuplicateDistanceSquared](const FHitResult& Existing)
			{
				return Existing.GetComponent() == Hit.GetComponent()
					&& FVector::DistSquared2D(Existing.ImpactPoint, Hit.ImpactPoint) <= DuplicateDistanceSquared
					&& FVector::DotProduct(Existing.ImpactNormal, Hit.ImpactNormal) >= 0.9f;
			});
			if (ExistingIndex == INDEX_NONE)
			{
				Hits.Add(Hit);
			}
			else if (Hit.ImpactPoint.Z > Hits[ExistingIndex].ImpactPoint.Z)
			{
				// A wall creates a vertical stack of hits; the upper sample is the useful ledge seed.
				Hits[ExistingIndex] = Hit;
			}
		}
	}
	Hits.Sort([&Origin, &Forward](const FHitResult& A, const FHitResult& B)
	{
		return GetCandidateScore(Origin, Forward, A) < GetCandidateScore(Origin, Forward, B);
	});
	OutFailure = EIMGTraversalFailure::NoSurface;
	int32 Tested = 0;
	for (const FHitResult& Hit : Hits)
	{
		if (++Tested > MaxCandidateHits)
		{
			break;
		}
		FIMGTraversalCandidate Candidate;
		EIMGTraversalFailure Failure = EIMGTraversalFailure::InvalidSurface;
#if ENABLE_DRAW_DEBUG
		IMGTraversalDebug::String(Character->GetWorld(), Hit.ImpactPoint + FVector::UpVector * 20.f,
			FString::Printf(TEXT("Candidate %d/%d  Score %.2f  D %.0f  Facing %.2f"), Tested,
				FMath::Min(Hits.Num(), MaxCandidateHits), GetCandidateScore(Origin, Forward, Hit),
				FVector::Dist2D(Origin, Hit.ImpactPoint),
				-FVector::DotProduct(Hit.ImpactNormal.GetSafeNormal2D(), Forward)), FColor::Yellow, 2);
#endif
		if (MakeCandidate(Query, Hit, Forward, Candidate, Failure))
		{
			Candidate.ReferenceReachDistance = GetActionReach(Query, ForwardSpeed, Candidate.ObstacleHeight);
			const float CandidateDistance = FVector::Dist2D(Origin, Candidate.FrontLedge.GetLocation());
			if (CandidateDistance > Query.MaxAnimationReachDistance)
			{
				OutFailure = EIMGTraversalFailure::OutOfReach;
#if ENABLE_DRAW_DEBUG
				IMGTraversalDebug::String(Character->GetWorld(), Candidate.FrontLedge.GetLocation() + FVector::UpVector * 45.f,
					FString::Printf(TEXT("OutOfReach: %.0f > MM %.0fcm (GTA %.0f)"), CandidateDistance,
						Query.MaxAnimationReachDistance, Candidate.ReferenceReachDistance),
					FColor::Red, 2);
#endif
				continue;
			}
#if ENABLE_DRAW_DEBUG
			if (CandidateDistance > Candidate.ReferenceReachDistance && IMGTraversalDebug::IsEnabled(2))
			{
				IMGTraversalDebug::String(Character->GetWorld(), Candidate.FrontLedge.GetLocation() + FVector::UpVector * 45.f,
					FString::Printf(TEXT("ReachLedge approach: %.0fcm (GTA initiation %.0fcm)"),
						CandidateDistance, Candidate.ReferenceReachDistance), FColor::Yellow, 2);
			}
#endif
			OutCandidate = Candidate;
			OutFailure = EIMGTraversalFailure::None;
#if ENABLE_DRAW_DEBUG
			DrawCandidateResult(*Character, OutCandidate);
#endif
			return true;
		}
		OutFailure = Failure;
#if ENABLE_DRAW_DEBUG
		DrawFailure(*Character, Failure, Hit.ImpactPoint, 2);
#endif
	}
#if ENABLE_DRAW_DEBUG
	DrawFailure(*Character, OutFailure, Origin + Forward * Query.SearchDistance + FVector::UpVector * 30.f);
#endif
	return false;
}
