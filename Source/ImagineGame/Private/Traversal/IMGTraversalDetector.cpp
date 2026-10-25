#include "Traversal/IMGTraversalDetector.h"

#include "Character/IMGCharacter.h"
#include "Character/IMGCharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Math/RotationMatrix.h"

#if ENABLE_DRAW_DEBUG
#include "IMGTraversalDebug.h"
#endif

namespace
{
constexpr int32 MaxCandidateHits = 12;
constexpr float WalkableSlopeBaseOffset = 25.f;
constexpr float WalkableSlopeProjectionLength = 1000.f;
constexpr float HighOpenApproachMinHeight = 180.f;
constexpr float HighOpenApproachMinClearance = 125.f;

bool HasSurfaceTag(const UPrimitiveComponent* Component, FName Tag)
{
	return Component && !Tag.IsNone() &&
	    (Component->ComponentHasTag(Tag) || (Component->GetOwner() && Component->GetOwner()->ActorHasTag(Tag)));
}

bool IsSurfaceAllowed(const UPrimitiveComponent* Component, const FIMGTraversalDetectorSettings& Settings,
    EIMGTraversalRequestSource Source)
{
	if (!IsValid(Component))
	{
		return false;
	}
	for (FName Tag : Settings.BlockedSurfaceTags)
	{
		if (HasSurfaceTag(Component, Tag))
		{
			return false;
		}
	}
	if (Source == EIMGTraversalRequestSource::Automatic)
	{
		for (FName Tag : Settings.AutomaticBlockedSurfaceTags)
		{
			if (HasSurfaceTag(Component, Tag))
			{
				return false;
			}
		}
	}
	return true;
}

void GatherSurfaceHits(UWorld& World, const FVector& Start, const FVector& End, float Radius, ECollisionChannel Channel,
    const FCollisionQueryParams& Params, TArray<FHitResult>& Hits)
{
	// Object queries retain successive surfaces; filter by the configured traversal response afterwards.
	World.SweepMultiByObjectType(Hits, Start, End, FQuat::Identity,
	    FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects), FCollisionShape::MakeSphere(Radius),
	    Params);
	Hits.RemoveAll(
	    [Channel](const FHitResult& Hit)
	    {
		    return !IsValid(Hit.GetComponent()) ||
		        Hit.GetComponent()->GetCollisionResponseToChannel(Channel) != ECR_Block;
	    });
	Hits.StableSort([](const FHitResult& A, const FHitResult& B) { return A.Time < B.Time; });
}

FVector GetGroundReference(AIMGCharacter& Character, UIMGCharacterMovementComponent& Movement)
{
	const FHitResult& Ground = Movement.GetGroundInfo().GroundHitResult;
	if (Ground.bBlockingHit && !Ground.bStartPenetrating && !Ground.ImpactPoint.ContainsNaN())
	{
		return Ground.ImpactPoint;
	}
	return Character.GetActorLocation() -
	    FVector::UpVector * Character.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
}

bool AreSettingsValid(const FIMGTraversalDetectorSettings& Settings)
{
	if (Settings.bScaleSearchDistanceBySpeed &&
	    (!FMath::IsFinite(Settings.MinSearchDistanceScale) || Settings.MinSearchDistanceScale <= 0.f ||
	        Settings.MinSearchDistanceScale > 1.f || !FMath::IsFinite(Settings.SpeedForMaxSearchDistance) ||
	        Settings.SpeedForMaxSearchDistance <= 0.f))
	{
		return false;
	}
	for (float Distance : Settings.AutomaticSearchLayerDistances)
	{
		if (!FMath::IsFinite(Distance) || Distance <= 0.f)
		{
			return false;
		}
	}
	const float Positive[] = {Settings.SearchDistance, Settings.MaxAnimationReachDistance, Settings.MaxDepth,
	    Settings.MaxHorizontalClearance, Settings.DepthProbeLength, Settings.MinWidth, Settings.MaxCandidateAge,
	    Settings.SearchProbeRadius, Settings.TopProbeRadius, Settings.DepthProbeRadius,
	    Settings.HorizontalClearanceRadius, Settings.VerticalClearanceRadius, Settings.ScoreDistanceScale};
	for (float Value : Positive)
	{
		if (!FMath::IsFinite(Value) || Value <= 0.f)
		{
			return false;
		}
	}
	const float NonNegative[] = {Settings.MinHeight, Settings.MaxHeight, Settings.MinHorizontalClearance,
	    Settings.MaxDrop, Settings.RunningSpeedThreshold, Settings.MaxMovingSpeed, Settings.MaxAngularSpeedDegrees,
	    Settings.MaxHurdleHeight, Settings.MaxVaultHeight, Settings.TopHeightTolerance, Settings.HandholdMinNormalZ,
	    Settings.DepthHeightTolerance, Settings.HorizontalClearanceVerticalOffset,
	    Settings.HorizontalClearanceBackOffset, Settings.HandholdPathNormalOffset,
	    Settings.UseRequestDirectionBelowHeight, Settings.EdgePlanarTolerance, Settings.EdgeHeadingToleranceDegrees,
	    Settings.WalkableSlopeMinNormalZ, Settings.WalkableSlopeDistanceTolerance, Settings.MaxClearanceSlopeAngle,
	    Settings.AutomaticMaxGroundDrop, Settings.DistanceScoreWeight, Settings.HeadingScoreWeight,
	    Settings.SecondaryHitPenalty, Settings.StairMinEdgeSpan, Settings.TaggedStairMinEdgeSpan,
	    Settings.StairAmbiguousEdgeSpan, Settings.StairMaxPlanarDrift, Settings.LargeApproachAngle,
	    Settings.MaxApproachAngle};
	for (float Value : NonNegative)
	{
		if (!FMath::IsFinite(Value) || Value < 0.f)
		{
			return false;
		}
	}
	return FMath::IsFinite(Settings.SearchStartHeightOffset) && FMath::IsFinite(Settings.HandholdPathVerticalOffset) &&
	    FMath::IsFinite(Settings.MinFacingDot) && Settings.MinFacingDot >= -1.f && Settings.MinFacingDot <= 1.f &&
	    Settings.HandholdMinNormalZ <= 1.f && Settings.WalkableSlopeMinNormalZ <= 1.f &&
	    Settings.LargeApproachAngle <= Settings.MaxApproachAngle && Settings.MaxApproachAngle <= 90.f &&
	    Settings.SecondaryHitPenalty >= 1.f && Settings.EdgeHeadingToleranceDegrees <= 90.f &&
	    Settings.MaxClearanceSlopeAngle < 90.f && Settings.MaxHeight > Settings.MinHeight &&
	    Settings.MaxHorizontalClearance > Settings.MinHorizontalClearance && Settings.MaxSearchLayers >= 1 &&
	    Settings.MaxSearchLayers <= 16 && Settings.MaxDepthSamples >= 1 && Settings.MaxDepthSamples <= 16 &&
	    Settings.AutomaticMinDepthSamples >= 1 && Settings.AutomaticMinDepthSamples <= 16 &&
	    Settings.BackLedgeRefinementSteps >= 0 && Settings.BackLedgeRefinementSteps <= 8;
}

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
	return IsValid(Component) && (!Component->IsSimulatingPhysics() || Settings.bAllowSimulatingPhysics) &&
	    Component->GetComponentVelocity().Size() <= Settings.MaxMovingSpeed &&
	    Component->GetPhysicsAngularVelocityInDegrees().Size() <= Settings.MaxAngularSpeedDegrees;
}

bool AddObstacleComponent(
    FIMGTraversalCandidate& Candidate, UPrimitiveComponent* Component, const FIMGTraversalDetectorSettings& Settings)
{
	if (!IsComponentUsable(Component, Settings))
	{
		return false;
	}
	Candidate.ObstacleComponents.AddUnique(Component);
	return true;
}

float GetCandidateScore(const FVector& Origin, const FVector& Forward, const FHitResult& Hit,
    const FIMGTraversalDetectorSettings& Settings, bool bSecondary)
{
	const float DistanceScore =
	    FMath::Clamp(
	        FVector::DistSquared2D(Origin, Hit.ImpactPoint) / FMath::Square(Settings.ScoreDistanceScale), 0.f, 1.f) *
	    Settings.DistanceScoreWeight;
	const float Facing = FMath::Clamp(-FVector::DotProduct(Hit.ImpactNormal.GetSafeNormal2D(), Forward), -1.f, 1.f);
	return (DistanceScore + (1.f - Facing) * 0.5f * Settings.HeadingScoreWeight) *
	    (bSecondary ? Settings.SecondaryHitPenalty : 1.f);
}

bool IsReachableAlongGroundSlope(UIMGCharacterMovementComponent& Movement, const FVector& FootLocation,
    const FVector& LedgeLocation, const FVector& SurfaceForward, const FIMGTraversalDetectorSettings& Settings)
{
	if (!Movement.IsMovingOnGround())
	{
		return false;
	}
	const FHitResult& GroundHit = Movement.GetGroundInfo().GroundHitResult;
	const FVector GroundNormal = GroundHit.ImpactNormal.GetSafeNormal();
	if (!GroundHit.bBlockingHit || GroundNormal.Z <= Settings.WalkableSlopeMinNormalZ ||
	    GroundNormal.Equals(FVector::UpVector, KINDA_SMALL_NUMBER))
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
	    Separation.Z < 0.f || Separation.Size() < Settings.WalkableSlopeDistanceTolerance ? FColor::Red : FColor::Green,
	    3, 1.f);
#endif
	return Separation.Z < 0.f || Separation.Size() < Settings.WalkableSlopeDistanceTolerance;
}

bool MatchesVaultRecommendation(const FIMGTraversalCandidate& Candidate, bool bRunning)
{
	struct FExitCondition
	{
		float MinHeight;
		float MaxHeight;
		float MinDepth;
		float MaxStandDepth;
		float MaxClamberDepth;
		float MinDrop;
		float MaxDrop;
		float MinVerticalClearance;
		float ExitClearance;
		bool bRequiresRunning;
	};

	static constexpr FExitCondition Conditions[] = {
	    {0.f, 80.f, 0.f, 32.5f, 35.f, 32.5f, TNumericLimits<float>::Max(), 120.f, 50.f, false},
	    {0.f, 80.f, 0.f, 57.5f, 57.5f, 32.5f, TNumericLimits<float>::Max(), 150.f, 75.f, true},
	    {80.f, TNumericLimits<float>::Max(), 0.f, 20.f, 20.f, 50.f, TNumericLimits<float>::Max(), 0.f, 65.f, false},
	    {80.f, TNumericLimits<float>::Max(), 0.f, 20.f, 35.f, 32.5f, 50.f, 0.f, 65.f, false},
	    {80.f, TNumericLimits<float>::Max(), 0.f, 50.f, 50.f, 50.f, TNumericLimits<float>::Max(), 0.f, 75.f, false},
	    {80.f, TNumericLimits<float>::Max(), 0.f, 70.f, 70.f, 50.f, TNumericLimits<float>::Max(), 0.f, 75.f, false},
	    {60.f, 120.f, 100.f, 200.f, 200.f, 60.f, 250.f, 0.f, 100.f, false},
	};

	for (const FExitCondition& Condition : Conditions)
	{
		if ((!Condition.bRequiresRunning || bRunning) && Candidate.ObstacleHeight > Condition.MinHeight &&
		    Candidate.ObstacleHeight <= Condition.MaxHeight && Candidate.ObstacleDepth >= Condition.MinDepth &&
		    Candidate.ObstacleDepth <=
		        (Candidate.ObstacleHeight < 80.f ? Condition.MaxStandDepth : Condition.MaxClamberDepth) &&
		    Candidate.DropHeight > Condition.MinDrop && Candidate.DropHeight < Condition.MaxDrop &&
		    Candidate.VerticalClearance >= Condition.MinVerticalClearance &&
		    Candidate.HorizontalClearance >= Candidate.ObstacleDepth + Condition.ExitClearance)
		{
			return true;
		}
	}
	return false;
}

bool TraceTop(UWorld& World, const FVector& Point, float StartZ, float EndZ, float ProbeRadius,
    ECollisionChannel TraceChannel, const FCollisionQueryParams& Params, FHitResult& OutHit, float MinNormalZ = 0.1f)
{
	const FVector Start(Point.X, Point.Y, StartZ);
	const FVector End(Point.X, Point.Y, EndZ);
	TArray<FHitResult> Hits;
	GatherSurfaceHits(World, Start, End, ProbeRadius, TraceChannel, Params, Hits);
	bool bHit = false;
	for (const FHitResult& Hit : Hits)
	{
		if (!Hit.bStartPenetrating && (Hit.ImpactNormal.Z + Hit.Normal.Z) * 0.5f > MinNormalZ)
		{
			OutHit = Hit;
			bHit = true;
			break;
		}
	}
#if ENABLE_DRAW_DEBUG
	IMGTraversalDebug::Line(&World, Start, bHit ? OutHit.ImpactPoint : End, bHit ? FColor::Cyan : FColor::Red, 3);
	IMGTraversalDebug::Sphere(&World, Start, ProbeRadius, bHit ? FColor::Cyan : FColor::Red, 3);
#endif
	return bHit;
}

bool MakeCandidate(const FIMGTraversalQuery& Query, const FHitResult& Face, const FVector& Forward,
    float FrontEdgeVerticalSpan, float FrontEdgePlanarDrift, float MidHeightApproachClearance,
    TConstArrayView<FHitResult> ApproachHits, bool bForceFaceDirection, FIMGTraversalCandidate& Out,
    EIMGTraversalFailure& Failure)
{
	AIMGCharacter* Character = Query.Character.Get();
	const FIMGTraversalDetectorSettings& Settings = Query.Settings;
	const float SearchProbeSeparation = Settings.SearchProbeRadius * 2.f;
	const float TopProbeRadius = Settings.TopProbeRadius;
	const float TopProbeStartOffset = SearchProbeSeparation + Settings.SearchProbeRadius;
	const float TopProbeLength = SearchProbeSeparation * 2.f;
	const float TopHeightTolerance = Settings.TopHeightTolerance;
	const float DepthProbeRadius = Settings.DepthProbeRadius;
	const float DepthProbeSpacing = DepthProbeRadius * 2.f;
	const float DepthHeightTolerance = Settings.DepthHeightTolerance;
	const float HorizontalClearanceRadius = Settings.HorizontalClearanceRadius;
	const float HorizontalClearanceVerticalOffset = Settings.HorizontalClearanceVerticalOffset;
	const float HorizontalClearanceBackOffset = Settings.HorizontalClearanceBackOffset;
	const float VerticalClearanceRadius = Settings.VerticalClearanceRadius;
	const float HandholdPathVerticalOffset = Settings.HandholdPathVerticalOffset;
	const float HandholdPathNormalOffset = Settings.HandholdPathNormalOffset;
	UPrimitiveComponent* FaceComponent = Face.GetComponent();
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	UIMGCharacterMovementComponent* Movement = Character->GetIMGMovementComponent();
	UWorld* World = Character->GetWorld();
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const FVector Up = FVector::UpVector;
	const float FootZ = Character->GetActorLocation().Z - HalfHeight;
	const FVector FlatFaceNormal = Face.ImpactNormal.GetSafeNormal2D();
	FVector SurfaceForward = -FlatFaceNormal;
	const FVector Right = FVector::CrossProduct(Up, SurfaceForward).GetSafeNormal();
	const float FacingDot = FVector::DotProduct(SurfaceForward, Forward);
	if (!IsSurfaceAllowed(FaceComponent, Query.Settings, Query.Source) || !World || FacingDot < Settings.MinFacingDot ||
	    Face.ImpactNormal.Z > 0.9f || SurfaceForward.IsNearlyZero() || Right.IsNearlyZero())
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
	IMGTraversalDebug::Arrow(World, Face.ImpactPoint, Face.ImpactPoint + Face.ImpactNormal * 40.f, FColor::Yellow, 2);
#endif
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IMGTraversalTop), false, Character);
	// UE channel sweeps stop at the front blocking face; move the vertical capsule just past it
	// to reproduce the reference detector's multi-hit top-surface selection.
	const FVector ProbeOrigin = Face.ImpactPoint + SurfaceForward * (TopProbeRadius + 2.f);
	FHitResult CenterTop;
	if (!TraceTop(*World, ProbeOrigin, Face.ImpactPoint.Z + TopProbeStartOffset,
	        Face.ImpactPoint.Z + TopProbeStartOffset - TopProbeLength, TopProbeRadius, Settings.TraceChannel, Params,
	        CenterTop, Settings.HandholdMinNormalZ))
	{
		Failure = EIMGTraversalFailure::NoFrontLedge;
#if ENABLE_DRAW_DEBUG
		IMGTraversalDebug::String(
		    World, ProbeOrigin + Up * Settings.MaxHeight, TEXT("FrontLedge: top sweep missed"), FColor::Red, 2);
#endif
		return false;
	}
	UPrimitiveComponent* TopComponent = CenterTop.GetComponent();
	if (!IsSurfaceAllowed(TopComponent, Query.Settings, Query.Source) ||
	    !AddObstacleComponent(Out, TopComponent, Settings))
	{
		Failure = EIMGTraversalFailure::MovingTarget;
		return false;
	}
	const FVector GroundLocation = GetGroundReference(*Character, *Movement);
	float Height = CenterTop.ImpactPoint.Z - (Settings.bUseGroundPositionForHeight ? GroundLocation.Z : FootZ);
	if (Height < Settings.MinHeight || Height > Settings.MaxHeight)
	{
		Failure = EIMGTraversalFailure::NoFrontLedge;
#if ENABLE_DRAW_DEBUG
		IMGTraversalDebug::String(World, CenterTop.ImpactPoint + Up * 12.f,
		    FString::Printf(
		        TEXT("FrontLedge: height %.0f outside %.0f..%.0f"), Height, Settings.MinHeight, Settings.MaxHeight),
		    FColor::Red, 2);
#endif
		return false;
	}

	const float SideOffset = FMath::Max(TopProbeRadius * 2.f, Settings.MinWidth * 0.5f - TopProbeRadius);
	FHitResult SideTop[2];
	bool bSideValid[2];
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const float Offset = Index == 0 ? -SideOffset : SideOffset;
		bSideValid[Index] = TraceTop(*World, ProbeOrigin + Right * Offset, Face.ImpactPoint.Z + TopProbeStartOffset,
		                        Face.ImpactPoint.Z + TopProbeStartOffset - TopProbeLength, TopProbeRadius,
		                        Settings.TraceChannel, Params, SideTop[Index], Settings.HandholdMinNormalZ) &&
		    FMath::Abs(SideTop[Index].ImpactPoint.Z - CenterTop.ImpactPoint.Z) <= TopHeightTolerance &&
		    IsSurfaceAllowed(SideTop[Index].GetComponent(), Query.Settings, Query.Source);
	}
	if (!(bSideValid[0] && bSideValid[1]))
	{
		// Manual/AI requests may recenter the handhold on the supported side; automatic requests stay strict.
		if (Query.Source == EIMGTraversalRequestSource::Automatic || bSideValid[0] == bSideValid[1])
		{
			Failure = EIMGTraversalFailure::InsufficientWidth;
			return false;
		}
		const int32 ValidSide = bSideValid[0] ? 0 : 1;
		const float Sign = ValidSide == 0 ? -1.f : 1.f;
		FHitResult Outer;
		if (!TraceTop(*World, ProbeOrigin + Right * (SideOffset * 2.f * Sign), Face.ImpactPoint.Z + TopProbeStartOffset,
		        Face.ImpactPoint.Z + TopProbeStartOffset - TopProbeLength, TopProbeRadius, Settings.TraceChannel,
		        Params, Outer, Settings.HandholdMinNormalZ) ||
		    FMath::Abs(Outer.ImpactPoint.Z - CenterTop.ImpactPoint.Z) > TopHeightTolerance ||
		    !IsSurfaceAllowed(Outer.GetComponent(), Query.Settings, Query.Source) ||
		    !AddObstacleComponent(Out, Outer.GetComponent(), Settings))
		{
			Failure = EIMGTraversalFailure::InsufficientWidth;
			return false;
		}
		SideTop[1 - ValidSide] = CenterTop;
		CenterTop = SideTop[ValidSide];
		TopComponent = CenterTop.GetComponent();
		SideTop[ValidSide] = Outer;
	}
	if (bSideValid[0] && bSideValid[1] && Height < Settings.UseRequestDirectionBelowHeight &&
	    FVector::DistSquared2D(SideTop[0].ImpactPoint, SideTop[1].ImpactPoint) <
	        FMath::Square(TopProbeRadius * 6.f * 0.6f))
	{
		const float Sign = FVector::DistSquared2D(CenterTop.ImpactPoint, SideTop[0].ImpactPoint) <
		        FVector::DistSquared2D(CenterTop.ImpactPoint, SideTop[1].ImpactPoint)
		    ? -1.f
		    : 1.f;
		FHitResult Additional;
		if (!TraceTop(*World, ProbeOrigin + Right * (TopProbeRadius * 3.f * Sign),
		        Face.ImpactPoint.Z + TopProbeStartOffset, Face.ImpactPoint.Z + TopProbeStartOffset - TopProbeLength,
		        TopProbeRadius, Settings.TraceChannel, Params, Additional, Settings.HandholdMinNormalZ) ||
		    FMath::Abs(Additional.ImpactPoint.Z - CenterTop.ImpactPoint.Z) > TopHeightTolerance ||
		    !IsSurfaceAllowed(Additional.GetComponent(), Query.Settings, Query.Source) ||
		    !AddObstacleComponent(Out, Additional.GetComponent(), Settings))
		{
			Failure = EIMGTraversalFailure::InsufficientWidth;
			return false;
		}
	}
	Height = CenterTop.ImpactPoint.Z - (Settings.bUseGroundPositionForHeight ? GroundLocation.Z : FootZ);
	if (Height < Settings.MinHeight || Height > Settings.MaxHeight)
	{
		Failure = EIMGTraversalFailure::NoFrontLedge;
		return false;
	}
	const bool bTaggedStairs =
	    HasSurfaceTag(FaceComponent, Settings.StairSurfaceTag) || HasSurfaceTag(TopComponent, Settings.StairSurfaceTag);
	const bool bOnStairs = Movement->IsMovingOnGround() &&
	    HasSurfaceTag(Movement->GetGroundInfo().GroundHitResult.GetComponent(), Settings.StairSurfaceTag);
	if ((bTaggedStairs || bOnStairs) &&
	    (FrontEdgeVerticalSpan < (bTaggedStairs ? Settings.TaggedStairMinEdgeSpan : Settings.StairMinEdgeSpan) ||
	        (FrontEdgeVerticalSpan < Settings.StairAmbiguousEdgeSpan &&
	            FrontEdgePlanarDrift > Settings.StairMaxPlanarDrift)))
	{
		Failure = EIMGTraversalFailure::InvalidSurface;
		return false;
	}
	for (const FHitResult& Side : SideTop)
	{
		if (!AddObstacleComponent(Out, Side.GetComponent(), Settings))
		{
			Failure = EIMGTraversalFailure::MovingTarget;
			return false;
		}
	}
#if ENABLE_DRAW_DEBUG
	IMGTraversalDebug::Line(World, SideTop[0].ImpactPoint, SideTop[1].ImpactPoint, FColor::Blue, 3, 2.f);
#endif

	FVector FrontPoint = CenterTop.ImpactPoint;
	FrontPoint -= FlatFaceNormal * FVector::DotProduct(FrontPoint - Face.ImpactPoint, FlatFaceNormal);
	if (!bForceFaceDirection && Height < Settings.UseRequestDirectionBelowHeight)
	{
		SurfaceForward = Forward;
	}
	const FVector FootLocation = Movement->IsMovingOnGround()
	    ? GroundLocation
	    : FVector(Character->GetActorLocation().X, Character->GetActorLocation().Y, FootZ);
	if (IsReachableAlongGroundSlope(*Movement, FootLocation, FrontPoint, SurfaceForward, Settings))
	{
		Failure = EIMGTraversalFailure::InvalidSurface;
#if ENABLE_DRAW_DEBUG
		IMGTraversalDebug::String(
		    World, FrontPoint + Up * 20.f, TEXT("Surface: reachable along ground slope"), FColor::Red, 2);
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
	Out.DistanceToLedge =
	    FVector::Distance(FrontPoint, Mesh ? Mesh->GetComponentLocation() : Character->GetActorLocation());
	Out.PlanarDistanceToLedge = FVector::Dist2D(FrontPoint, Character->GetActorLocation());
	Out.FrontEdgeVerticalSpan = FrontEdgeVerticalSpan;
	Out.FrontEdgePlanarDrift = FrontEdgePlanarDrift;
	Out.MidHeightApproachClearance = MidHeightApproachClearance;
	Out.bHasHighOpenApproach =
	    Height > HighOpenApproachMinHeight && MidHeightApproachClearance >= HighOpenApproachMinClearance;
	Out.DetectionTime = World->GetTimeSeconds();
	Out.Speed = Movement->Velocity.Size2D();
	Out.GroundLocation = GroundLocation;
	Out.RequestSource = Query.Source;
	Out.MovementMode = Movement->MovementMode;
	Out.bHasFrontLedge = true;
	const float HandholdDistance = FVector::Dist2D(Character->GetActorLocation(), FrontPoint);
	int32 LargeApproachAngles = 0;
	if (HandholdDistance > 45.f)
	{
		for (int32 Index = 0; Index < ApproachHits.Num(); ++Index)
		{
			const FHitResult& Hit = ApproachHits[Index];
			if (!Hit.bBlockingHit)
			{
				continue;
			}
			const float Distance = FVector::Dist2D(Character->GetActorLocation(), Hit.ImpactPoint);
			FVector Point = Character->GetActorLocation() +
			    (FrontPoint - Character->GetActorLocation()).GetSafeNormal2D() * Distance;
			Point.Z = Hit.ImpactPoint.Z;
			const FVector Difference = FrontPoint - Point;
			if (Distance < HandholdDistance && Difference.Size2D() > 10.5f &&
			    Difference.Z > (Index == 0 ? 20.f : 30.f) && Point.Z - GroundLocation.Z > 40.f)
			{
				const float Angle =
				    FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Difference.GetSafeNormal().Z, -1.f, 1.f)));
				Out.ClimbAngle = FMath::Max(Out.ClimbAngle, Angle);
				if (Angle > Settings.LargeApproachAngle)
				{
					++LargeApproachAngles;
				}
			}
		}
	}
	if (LargeApproachAngles > 1 && Out.ClimbAngle > Settings.MaxApproachAngle)
	{
		Failure = EIMGTraversalFailure::BlockedPath;
		return false;
	}

	FVector LastTopPoint = FrontPoint;
	UPrimitiveComponent* LastTopComponent = TopComponent;
	FVector FirstInvalidPoint = FrontPoint;
	FHitResult FirstDropHit;
	bool bHasDropHit = false;
	bool bHitRisingObstacle = false;
	const int32 DepthSamples =
	    FMath::Clamp(FMath::CeilToInt(Settings.MaxDepth / DepthProbeSpacing), 1, Settings.MaxDepthSamples);
	const float DepthStartOffset = HorizontalClearanceVerticalOffset + 2.f * HorizontalClearanceRadius * 2.f -
	    (DepthProbeRadius - HorizontalClearanceRadius) - HorizontalClearanceRadius;
	bool bFoundEdge = false;
	FVector NormalSum = FVector::ZeroVector;
	FVector HighestTop = FrontPoint;
	int32 NormalCount = 0;
	int32 FirstChangedIndex = INDEX_NONE;
	FVector PreviousPoint = FrontPoint;
	float MaximumDrop = 0.f;
	bool bMissingDepth = false;
	int32 MissingDepthIndex = INDEX_NONE;
	const int32 MinimumSamples = Query.Source == EIMGTraversalRequestSource::Automatic
	    ? FMath::Min(Settings.AutomaticMinDepthSamples, DepthSamples)
	    : 1;
	for (int32 Index = 0; Index < DepthSamples; ++Index)
	{
		const FVector Probe = FrontPoint + SurfaceForward * (DepthProbeSpacing * Index);
		const FVector Start = Probe + Up * DepthStartOffset;
		const FVector End = Start - Up * Settings.DepthProbeLength;
		FHitResult Hit;
		// Depth measures geometry even if the hit cannot be used as a walkable landing.
		const bool bHit = World->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, Settings.TraceChannel,
		                      FCollisionShape::MakeSphere(DepthProbeRadius), Params) &&
		    Hit.bBlockingHit && !Hit.bStartPenetrating;
#if ENABLE_DRAW_DEBUG
		IMGTraversalDebug::Line(World, Start, bHit ? Hit.ImpactPoint : End, bHit ? FColor::Cyan : FColor::Red, 3);
		IMGTraversalDebug::Sphere(World, Start, DepthProbeRadius, FColor::Cyan, 3);
#endif
		if (bHit)
		{
			if (Hit.ImpactNormal.Z >= 0.f)
			{
				NormalSum += Hit.ImpactNormal;
				++NormalCount;
			}
			if (Hit.ImpactPoint.Z > HighestTop.Z)
			{
				HighestTop = Hit.ImpactPoint;
			}
			MaximumDrop = FMath::Max(MaximumDrop, FrontPoint.Z - Hit.ImpactPoint.Z);
		}
		const bool bChanged = !bHit || FMath::Abs(Hit.ImpactPoint.Z - PreviousPoint.Z) > DepthHeightTolerance;
		if (bChanged && FirstChangedIndex == INDEX_NONE)
		{
			FirstChangedIndex = Index;
			FirstInvalidPoint = Probe;
			bHitRisingObstacle = bHit && Hit.ImpactPoint.Z > PreviousPoint.Z + DepthHeightTolerance;
			bFoundEdge = !bHitRisingObstacle;
			if (bHit && !bHitRisingObstacle)
			{
				FirstDropHit = Hit;
				bHasDropHit = true;
				Out.DropHeight = FMath::Max(FrontPoint.Z - Hit.ImpactPoint.Z, 0.f);
			}
			else if (!bHit)
			{
				Out.DropHeight = Settings.DepthProbeLength;
			}
		}
		if (!bHit)
		{
			bMissingDepth = true;
			MissingDepthIndex = Index;
			break;
		}
		if (FirstChangedIndex == INDEX_NONE)
		{
			LastTopPoint = Hit.ImpactPoint;
			LastTopComponent = Hit.GetComponent();
			if (!IsSurfaceAllowed(LastTopComponent, Query.Settings, Query.Source) ||
			    !AddObstacleComponent(Out, LastTopComponent, Settings))
			{
				Failure = EIMGTraversalFailure::InvalidSurface;
				return false;
			}
		}
		PreviousPoint = Hit.ImpactPoint;
		if (FirstChangedIndex != INDEX_NONE && Index + 1 >= MinimumSamples)
		{
			break;
		}
	}
	if (Query.Source == EIMGTraversalRequestSource::Automatic &&
	    ((bMissingDepth && MissingDepthIndex + 1 <= MinimumSamples) ||
	        GroundLocation.Z - (FrontPoint.Z - MaximumDrop) > Settings.AutomaticMaxGroundDrop))
	{
		Failure = EIMGTraversalFailure::UnsafeTop;
		return false;
	}
	if (bFoundEdge)
	{
		FVector ValidPoint = LastTopPoint;
		FVector InvalidPoint = FirstInvalidPoint;
		for (int32 Iteration = 0; Iteration < Settings.BackLedgeRefinementSteps; ++Iteration)
		{
			const FVector Probe = FMath::Lerp(ValidPoint, InvalidPoint, 0.5f);
			FHitResult RefineHit;
			if (TraceTop(*World, Probe, FrontPoint.Z + 30.f, FrontPoint.Z - 30.f, TopProbeRadius, Settings.TraceChannel,
			        Params, RefineHit) &&
			    RefineHit.ImpactNormal.Z > Settings.HandholdMinNormalZ &&
			    FMath::Abs(RefineHit.ImpactPoint.Z - LastTopPoint.Z) <= DepthHeightTolerance)
			{
				ValidPoint = RefineHit.ImpactPoint;
				if (!IsSurfaceAllowed(RefineHit.GetComponent(), Query.Settings, Query.Source) ||
				    !AddObstacleComponent(Out, RefineHit.GetComponent(), Settings))
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
	Out.ObstacleDepth = FVector::Dist2D(FrontPoint, LastTopPoint);
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
		const bool bBackFaceHit =
		    World->LineTraceSingleByChannel(BackFace, BackFaceStart, BackFaceEnd, Settings.TraceChannel, Params) &&
		    BackFace.bBlockingHit;
#if ENABLE_DRAW_DEBUG
		IMGTraversalDebug::Line(World, BackFaceStart, bBackFaceHit ? BackFace.ImpactPoint : BackFaceEnd,
		    bBackFaceHit ? FColor::Orange : FColor::Red, 3, 1.5f);
#endif
		if (bBackFaceHit)
		{
			if (!IsSurfaceAllowed(BackFace.GetComponent(), Query.Settings, Query.Source) ||
			    !AddObstacleComponent(Out, BackFace.GetComponent(), Settings))
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
	const FVector ApproachEnd =
	    FrontPoint + Up * HandholdPathVerticalOffset - SurfaceForward * HandholdPathNormalOffset;
	const bool bApproachBlocked =
	    World->SweepSingleByChannel(ApproachPathHit, ApproachStart, ApproachEnd, FQuat::Identity, Settings.TraceChannel,
	        FCollisionShape::MakeSphere(ApproachRadius), ApproachParams) &&
	    ApproachPathHit.bBlockingHit;
#if ENABLE_DRAW_DEBUG
	const FVector ApproachDebugEnd = bApproachBlocked
	    ? (ApproachPathHit.bStartPenetrating ? ApproachStart : ApproachPathHit.ImpactPoint)
	    : ApproachEnd;
	IMGTraversalDebug::Line(
	    World, ApproachStart, ApproachDebugEnd, bApproachBlocked ? FColor::Red : FColor::Green, 3, 2.f);
	IMGTraversalDebug::Sphere(World, ApproachStart, ApproachRadius, bApproachBlocked ? FColor::Red : FColor::Green, 3);
	IMGTraversalDebug::Sphere(
	    World, ApproachDebugEnd, ApproachRadius, bApproachBlocked ? FColor::Red : FColor::Green, 3);
#endif
	if (bApproachBlocked)
	{
		Failure = EIMGTraversalFailure::BlockedPath;
#if ENABLE_DRAW_DEBUG
		IMGTraversalDebug::String(
		    World, ApproachDebugEnd + Up * 12.f, TEXT("BlockedPath: handhold capsule"), FColor::Red, 2);
#endif
		return false;
	}

	const FVector VerticalClearanceStart = FrontPoint + SurfaceForward * VerticalClearanceRadius +
	    Up * (HorizontalClearanceVerticalOffset + HorizontalClearanceRadius);
	const FVector VerticalClearanceEnd(VerticalClearanceStart.X, VerticalClearanceStart.Y,
	    FrontPoint.Z + FMath::Max(VerticalClearanceRadius * 2.f, HalfHeight * 2.f - 5.f));
	FHitResult VerticalClearanceHit;
	FCollisionQueryParams VerticalClearanceParams(SCENE_QUERY_STAT(IMGTraversalVerticalClearance), false, Character);
	const bool bVerticalClearanceHit =
	    World->SweepSingleByChannel(VerticalClearanceHit, VerticalClearanceStart, VerticalClearanceEnd, FQuat::Identity,
	        Settings.TraceChannel, FCollisionShape::MakeSphere(VerticalClearanceRadius), VerticalClearanceParams) &&
	    VerticalClearanceHit.bBlockingHit;
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
	Out.GroundNormal = NormalCount > 0 ? NormalSum.GetSafeNormal() : Up;
	FVector TopSlope = HighestTop - FrontPoint;
	if (TopSlope.IsNearlyZero())
	{
		TopSlope = FVector::VectorPlaneProject(SurfaceForward, Out.GroundNormal);
	}
	if (TopSlope.Z > 0.f && !TopSlope.IsNearlyZero())
	{
		const FVector NormalizedSlope = TopSlope.GetSafeNormal();
		const float SlopeAngle = FMath::RadiansToDegrees(
		    FMath::Acos(FMath::Clamp(FVector::DotProduct(NormalizedSlope, SurfaceForward), -1.f, 1.f)));
		if (SlopeAngle < Settings.MaxClearanceSlopeAngle)
		{
			ClearanceDirection = NormalizedSlope;
			Out.GroundSlopeAngle = SlopeAngle;
		}
	}
	const float MaxClearance = FMath::Max(Settings.MaxHorizontalClearance, HorizontalClearanceRadius * 2.f);
	float HorizontalClearance = MaxClearance;
	for (int32 ClearanceIndex = 0; ClearanceIndex < 2; ++ClearanceIndex)
	{
		FVector ClearanceStart = FrontPoint +
		    Up * (HorizontalClearanceVerticalOffset + HorizontalClearanceRadius * (1.f + ClearanceIndex * 2.f));
		if (ClearanceIndex == 0)
		{
			ClearanceStart -= ClearanceDirection * HorizontalClearanceBackOffset;
		}
		const FVector ClearanceEnd = ClearanceStart + ClearanceDirection * MaxClearance;
		FHitResult ClearanceHit;
		FCollisionQueryParams ClearanceParams(SCENE_QUERY_STAT(IMGTraversalHorizontalClearance), false, Character);
		const bool bClearanceHit =
		    World->SweepSingleByChannel(ClearanceHit, ClearanceStart, ClearanceEnd, FQuat::Identity,
		        Settings.TraceChannel, FCollisionShape::MakeSphere(HorizontalClearanceRadius), ClearanceParams);
		const bool bClearanceBlocked = bClearanceHit && ClearanceHit.bBlockingHit;
		if (bClearanceBlocked)
		{
			const float HitClearance = ClearanceHit.bStartPenetrating
			    ? 0.f
			    : FVector::Dist2D(ClearanceStart, ClearanceHit.Location) - FVector::Dist2D(ClearanceStart, FrontPoint);
			HorizontalClearance = FMath::Min(HorizontalClearance, FMath::Clamp(HitClearance, 0.f, MaxClearance));
		}
#if ENABLE_DRAW_DEBUG
		const FVector ClearanceDebugEnd = bClearanceBlocked
		    ? (ClearanceHit.bStartPenetrating ? ClearanceStart : ClearanceHit.Location)
		    : ClearanceEnd;
		IMGTraversalDebug::Line(
		    World, ClearanceStart, ClearanceDebugEnd, bClearanceBlocked ? FColor::Orange : FColor::Green, 3, 2.f);
		IMGTraversalDebug::Sphere(
		    World, ClearanceStart, HorizontalClearanceRadius, bClearanceBlocked ? FColor::Orange : FColor::Green, 3);
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
		    FString::Printf(TEXT("BlockedPath: clearance <= %.0fcm"), Settings.MinHorizontalClearance), FColor::Red, 2);
#endif
		return false;
	}

	if (Out.bHasBackLedge && bHasDropHit)
	{
		Out.BackLedgeHeight = LastTopPoint.Z - FirstDropHit.ImpactPoint.Z;
		if (Out.DropHeight >= 0.f && Out.DropHeight <= Settings.MaxDrop && Movement->IsWalkable(FirstDropHit) &&
		    IsSurfaceAllowed(FirstDropHit.GetComponent(), Query.Settings, Query.Source) &&
		    IsComponentUsable(FirstDropHit.GetComponent(), Settings))
		{
			// The selected animation resolves the final landing; this is only sampled rear support.
			Out.bHasBackFloor = true;
			Out.BackFloorComponent = FirstDropHit.GetComponent();
			Out.BackFloor = FTransform(Facing, FirstDropHit.ImpactPoint);
		}
	}

	// These describe available geometry. The selected animation owns its support/clearance contract.
	Out.bCanMantle = Out.bHasFrontLedge;
	Out.bCanVault = Out.bHasFrontLedge && Out.bHasBackLedge;
	Out.bCanHurdle = Out.bCanVault;
	const bool bRunning = FVector::DotProduct(Movement->Velocity, Forward) >= Settings.RunningSpeedThreshold;
	const bool bRecommendVault =
	    Out.bCanVault && Height <= Settings.MaxVaultHeight && MatchesVaultRecommendation(Out, bRunning);
	Out.RecommendedAction = bRecommendVault && Out.bHasBackFloor && Height <= Settings.MaxHurdleHeight
	    ? EIMGTraversalAction::Hurdle
	    : bRecommendVault ? EIMGTraversalAction::Vault
	                      : EIMGTraversalAction::Mantle;
	if (Query.Source == EIMGTraversalRequestSource::Automatic && Settings.bRejectAutomaticHighOpenApproach &&
	    Out.bHasHighOpenApproach)
	{
		Failure = EIMGTraversalFailure::InvalidSurface;
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
} // namespace

bool FIMGTraversalDetector::CanUseSurface(const UPrimitiveComponent* Component,
    const FIMGTraversalDetectorSettings& Settings, EIMGTraversalRequestSource Source)
{
	return IsSurfaceAllowed(Component, Settings, Source) && IsComponentUsable(Component, Settings);
}

bool FIMGTraversalDetector::Detect(
    const FIMGTraversalQuery& Query, FIMGTraversalCandidate& OutCandidate, EIMGTraversalFailure& OutFailure)
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
	if (!Movement || Character->GetStance() != EIMGStance::Stand ||
	    !(Movement->IsMovingOnGround() || Movement->IsFalling()))
	{
		OutFailure = EIMGTraversalFailure::InvalidMovement;
#if ENABLE_DRAW_DEBUG
		DrawFailure(*Character, OutFailure, Character->GetActorLocation() + FVector::UpVector * 50.f);
#endif
		return false;
	}
	const FVector Forward = FVector(Query.Direction.X, Query.Direction.Y, 0.f).GetSafeNormal();
	if (Forward.IsNearlyZero() || Query.Direction.ContainsNaN() || Movement->Velocity.ContainsNaN() ||
	    !AreSettingsValid(Settings) ||
	    static_cast<uint8>(Query.Source) > static_cast<uint8>(EIMGTraversalRequestSource::AI))
	{
		OutFailure = EIMGTraversalFailure::InvalidMovement;
#if ENABLE_DRAW_DEBUG
		DrawFailure(*Character, OutFailure, Character->GetActorLocation() + FVector::UpVector * 50.f);
#endif
		return false;
	}

	const float SearchProbeRadius = Settings.SearchProbeRadius;
	const float SearchProbeSeparation = SearchProbeRadius * 2.f;
	const float SearchStartHeightOffset = Settings.SearchStartHeightOffset;
	const float EdgePlanarTolerance = Settings.EdgePlanarTolerance;
	const float EdgeHeadingToleranceDot = FMath::Cos(FMath::DegreesToRadians(Settings.EdgeHeadingToleranceDegrees));
	const float EdgeVerticalContinuity = SearchProbeSeparation * 2.f;
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const FVector Origin = Character->GetActorLocation();
	const float SearchSpeed = Movement->Velocity.Size2D();
	const float SearchScale = Settings.bScaleSearchDistanceBySpeed
	    ? FMath::Lerp(Settings.MinSearchDistanceScale, 1.f,
	          FMath::Clamp(SearchSpeed / Settings.SpeedForMaxSearchDistance, 0.f, 1.f))
	    : 1.f;
	const float SearchDistance = Settings.SearchDistance * SearchScale;
#if ENABLE_DRAW_DEBUG
	IMGTraversalDebug::BeginTraversalSnapshot(Character->GetWorld());
	if (IMGTraversalDebug::IsEnabled(2))
	{
		IMGTraversalDebug::String(Character->GetWorld(), Origin + FVector::UpVector * (HalfHeight + 35.f),
		    FString::Printf(TEXT("Search %.0f/%.0f | Scale %.2f | Anim %.0f | H %.0f..%.0f | Speed %.0f | %s"),
		        SearchDistance, Settings.SearchDistance, SearchScale, Settings.MaxAnimationReachDistance,
		        Settings.MinHeight, Settings.MaxHeight, SearchSpeed,
		        Movement->IsFalling() ? TEXT("Air") : TEXT("Ground")),
		    FColor::White, 2);
	}
#endif
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IMGTraversalCandidates), false, Character);
	TArray<FHitResult> EdgeHits;
	TArray<FVector> EdgeMidPoints;
	TArray<FVector> EdgeNormalSums;
	TArray<FVector2D> EdgeHeightRanges;
	TArray<FVector> EdgeLowestPoints;
	TArray<FVector> EdgeHighestPoints;
	TArray<float> EdgeScores;
	const float FootZ = Origin.Z - HalfHeight;
	const float SearchBaseZ = Origin.Z + SearchStartHeightOffset;
	const float HighestProbeZ = FootZ + Settings.MaxHeight + 25.f;
	const int32 ProbeLayerCount = FMath::Clamp(
	    FMath::RoundToInt((HighestProbeZ - SearchBaseZ) / SearchProbeSeparation), 1, Settings.MaxSearchLayers);
	TArray<FHitResult> FirstLayerHits;
	FirstLayerHits.SetNum(ProbeLayerCount);
	TArray<float> SearchLayerDistances;
	SearchLayerDistances.SetNumUninitialized(ProbeLayerCount);
#if ENABLE_DRAW_DEBUG
	// The broad search layers stay live-only; the successful snapshot starts with the search summary.
	IMGTraversalDebug::SetTraversalSnapshotCaptureEnabled(false);
#endif
	for (int32 LayerIndex = 0; LayerIndex < ProbeLayerCount; ++LayerIndex)
	{
		float LayerMaxDistance = Settings.SearchDistance;
		if (Query.Source == EIMGTraversalRequestSource::Automatic && !Settings.AutomaticSearchLayerDistances.IsEmpty())
		{
			const int32 DistanceIndex = FMath::Min(LayerIndex, Settings.AutomaticSearchLayerDistances.Num() - 1);
			LayerMaxDistance = FMath::Min(LayerMaxDistance, Settings.AutomaticSearchLayerDistances[DistanceIndex]);
		}
		const float LayerDistance = LayerMaxDistance * SearchScale;
		// A miss proves clearance only as far as this layer was actually swept.
		SearchLayerDistances[LayerIndex] = LayerDistance;
		TArray<FHitResult> ProbeHits;
		const FVector Start(Origin.X, Origin.Y, SearchBaseZ + SearchProbeRadius + SearchProbeSeparation * LayerIndex);
		const FVector End = Start + Forward * LayerDistance;
		GatherSurfaceHits(
		    *Character->GetWorld(), Start, End, SearchProbeRadius, Settings.TraceChannel, Params, ProbeHits);
		for (int32 Index = 0; Index < ProbeHits.Num(); ++Index)
		{
			if (!IsSurfaceAllowed(ProbeHits[Index].GetComponent(), Query.Settings, Query.Source))
			{
				ProbeHits.SetNum(Index + 1);
				break;
			}
		}
		if (ProbeHits.Num() > 4)
		{
			ProbeHits.SetNum(4);
		}
		if (!ProbeHits.IsEmpty())
		{
			FirstLayerHits[LayerIndex] = ProbeHits[0];
		}
		for (const FHitResult& Hit : ProbeHits)
		{
			if (Hit.bBlockingHit)
			{
				SearchLayerDistances[LayerIndex] =
				    FMath::Min(SearchLayerDistances[LayerIndex], FVector::Dist2D(Start, Hit.Location));
			}
		}
#if ENABLE_DRAW_DEBUG
		const FColor SearchColor = ProbeHits.IsEmpty() ? FColor::Silver : FColor::Cyan;
		IMGTraversalDebug::Line(Character->GetWorld(), Start, End, SearchColor, 2, 1.5f);
		IMGTraversalDebug::Sphere(Character->GetWorld(), Start, SearchProbeRadius, SearchColor, 2);
		IMGTraversalDebug::Sphere(Character->GetWorld(), End, SearchProbeRadius, SearchColor, 2);
		IMGTraversalDebug::String(Character->GetWorld(), End + FVector::UpVector * 5.f,
		    FString::Printf(TEXT("P%d/%d L%.0f Z%.0f H%d"), LayerIndex + 1, ProbeLayerCount, LayerDistance,
		        Start.Z - FootZ, ProbeHits.Num()),
		    SearchColor, 2);
#endif
		for (int32 HitIndex = 0; HitIndex < ProbeHits.Num(); ++HitIndex)
		{
			const FHitResult& Hit = ProbeHits[HitIndex];
			if (!Hit.bBlockingHit || !IsSurfaceAllowed(Hit.GetComponent(), Query.Settings, Query.Source) ||
			    Hit.bStartPenetrating)
			{
				continue;
			}
			int32 EdgeIndex = INDEX_NONE;
			for (int32 ExistingIndex = 0; ExistingIndex < EdgeHits.Num(); ++ExistingIndex)
			{
				const FVector AverageNormal = EdgeNormalSums[ExistingIndex].GetSafeNormal();
				const FVector2D HeightRange = EdgeHeightRanges[ExistingIndex];
				if (FVector::DistSquared2D(EdgeMidPoints[ExistingIndex], Hit.ImpactPoint) <=
				        FMath::Square(EdgePlanarTolerance) &&
				    FVector::DotProduct(AverageNormal.GetSafeNormal2D(), Hit.ImpactNormal.GetSafeNormal2D()) >=
				        EdgeHeadingToleranceDot &&
				    Hit.ImpactPoint.Z >= HeightRange.X - EdgeVerticalContinuity &&
				    Hit.ImpactPoint.Z <= HeightRange.Y + EdgeVerticalContinuity)
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
				EdgeLowestPoints.Add(Hit.ImpactPoint);
				EdgeHighestPoints.Add(Hit.ImpactPoint);
				EdgeScores.Add(GetCandidateScore(Origin, Forward, Hit, Settings, HitIndex > 0));
			}
			else
			{
				EdgeMidPoints[EdgeIndex] = (EdgeMidPoints[EdgeIndex] + Hit.ImpactPoint) * 0.5f;
				EdgeNormalSums[EdgeIndex] += Hit.ImpactNormal;
				if (Hit.ImpactPoint.Z < EdgeHeightRanges[EdgeIndex].X)
				{
					EdgeLowestPoints[EdgeIndex] = Hit.ImpactPoint;
				}
				if (Hit.ImpactPoint.Z > EdgeHeightRanges[EdgeIndex].Y)
				{
					EdgeHighestPoints[EdgeIndex] = Hit.ImpactPoint;
				}
				EdgeHeightRanges[EdgeIndex].X = FMath::Min(EdgeHeightRanges[EdgeIndex].X, Hit.ImpactPoint.Z);
				EdgeHeightRanges[EdgeIndex].Y = FMath::Max(EdgeHeightRanges[EdgeIndex].Y, Hit.ImpactPoint.Z);
				EdgeScores[EdgeIndex] =
				    FMath::Min(EdgeScores[EdgeIndex], GetCandidateScore(Origin, Forward, Hit, Settings, HitIndex > 0));
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
		const FVector EdgeBottom(
		    EdgeHits[EdgeIndex].ImpactPoint.X, EdgeHits[EdgeIndex].ImpactPoint.Y, EdgeHeightRanges[EdgeIndex].X);
		const FVector EdgeTop(EdgeBottom.X, EdgeBottom.Y, EdgeHeightRanges[EdgeIndex].Y);
		IMGTraversalDebug::Line(Character->GetWorld(), EdgeBottom, EdgeTop, FColor::Purple, 2, 1.5f);
		IMGTraversalDebug::Arrow(
		    Character->GetWorld(), EdgeTop, EdgeTop + EdgeHits[EdgeIndex].ImpactNormal * 30.f, FColor::Purple, 2);
#endif
	}
	EdgeOrder.Sort([&EdgeScores](int32 A, int32 B) { return EdgeScores[A] < EdgeScores[B]; });
	float MidHeightApproachClearance = SearchDistance;
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
		        -FVector::DotProduct(Hit.ImpactNormal.GetSafeNormal2D(), Forward), EdgeVerticalSpan,
		        MidHeightApproachClearance),
		    FColor::Yellow, 2);
#endif
		const float EdgePlanarDrift = FVector::Dist2D(EdgeLowestPoints[EdgeIndex], EdgeHighestPoints[EdgeIndex]);
		bool bValid = MakeCandidate(Query, Hit, Forward, EdgeVerticalSpan, EdgePlanarDrift, MidHeightApproachClearance,
		    FirstLayerHits, false, Candidate, Failure);
		if (!bValid && Candidate.bHasFrontLedge && Candidate.ObstacleHeight < Settings.UseRequestDirectionBelowHeight &&
		    !Hit.ImpactNormal.GetSafeNormal2D().Equals(-Forward, KINDA_SMALL_NUMBER))
		{
#if ENABLE_DRAW_DEBUG
			IMGTraversalDebug::RollbackTraversalCandidateSnapshot();
			IMGTraversalDebug::BeginTraversalCandidateSnapshot();
#endif
			Candidate = FIMGTraversalCandidate();
			bValid = MakeCandidate(Query, Hit, Forward, EdgeVerticalSpan, EdgePlanarDrift, MidHeightApproachClearance,
			    FirstLayerHits, true, Candidate, Failure);
		}
		if (bValid)
		{
			const float CandidateDistance = FVector::Dist2D(Origin, Candidate.FrontLedge.GetLocation());
			if (CandidateDistance > Settings.MaxAnimationReachDistance)
			{
				OutFailure = EIMGTraversalFailure::OutOfReach;
#if ENABLE_DRAW_DEBUG
				IMGTraversalDebug::String(Character->GetWorld(),
				    Candidate.FrontLedge.GetLocation() + FVector::UpVector * 45.f,
				    FString::Printf(
				        TEXT("OutOfReach: %.0f > Anim %.0f"), CandidateDistance, Settings.MaxAnimationReachDistance),
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
	DrawFailure(*Character, OutFailure, Origin + Forward * SearchDistance + FVector::UpVector * 30.f);
#endif
	return false;
}
