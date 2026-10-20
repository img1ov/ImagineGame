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
constexpr int32 MaxDepthSamples = 12;
constexpr int32 MaxForwardProbeLayers = 12;

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
		FString::Printf(TEXT("Traversal: %s  H %.0f  D %.0f  Clear %.0f  Drop %.0f  Dist %.0f  [%s]  Parts %d"),
			*GetActionName(Candidate.RecommendedAction), Candidate.ObstacleHeight,
			Candidate.ObstacleDepth, Candidate.HorizontalClearance, Candidate.DropHeight,
			Candidate.DistanceToLedge, *Feasible,
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
	if (!IsValid(FaceComponent) || !World || FVector::DotProduct(Face.ImpactNormal, Forward) > -0.35f
		|| FMath::Abs(Face.ImpactNormal.Z) > 0.55f || SurfaceForward.IsNearlyZero() || Right.IsNearlyZero())
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
	const float TopProbeRadius = FMath::Clamp(Radius * 0.2f, 5.f, 10.f);
	const FVector ProbeOrigin = Face.ImpactPoint
		+ SurfaceForward * (TopProbeRadius + 2.f);
	FHitResult CenterTop;
	if (!TraceTop(*World, ProbeOrigin, FootZ + Query.MaxHeight + 20.f,
		FootZ + Query.MinHeight - 10.f, TopProbeRadius, Params, CenterTop))
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

	const float SideOffset = FMath::Max(Query.MinWidth * 0.5f - TopProbeRadius,
		Radius * 0.7f - TopProbeRadius);
	FVector SideTopPoints[2];
	int32 SidePointIndex = 0;
	for (const float Side : {-1.f, 1.f})
	{
		FHitResult SideTop;
		if (!TraceTop(*World, ProbeOrigin + Right * SideOffset * Side,
			FootZ + Query.MaxHeight + 20.f, FootZ + Query.MinHeight - 10.f,
			TopProbeRadius, Params, SideTop)
			|| !Movement->IsWalkable(SideTop)
			|| FMath::Abs(SideTop.ImpactPoint.Z - CenterTop.ImpactPoint.Z) > 20.f)
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
	Out.FrontLedgeComponent = FaceComponent;
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
	FVector FirstMissingPoint = FrontPoint;
	int32 ValidDepthSamples = 0;
	const float Step = FMath::Max(15.f, FMath::Min(Radius, Query.MaxDepth / MaxDepthSamples));
	const int32 DepthSamples = FMath::Min(MaxDepthSamples, FMath::CeilToInt(Query.MaxDepth / Step));
	bool bFoundEdge = false;
	for (int32 Index = 1; Index <= DepthSamples; ++Index)
	{
		const FVector Probe = FrontPoint + SurfaceForward * (Step * Index);
		FHitResult DepthHit;
		const bool bDepthHit = TraceTop(*World, Probe, FrontPoint.Z + 30.f, FrontPoint.Z - 30.f,
			TopProbeRadius, Params, DepthHit)
			&& Movement->IsWalkable(DepthHit)
			&& FMath::Abs(DepthHit.ImpactPoint.Z - FrontPoint.Z) <= 25.f;
		if (!bDepthHit)
		{
			FirstMissingPoint = Probe;
			FHitResult RisingObstacleHit;
			const FVector RisingStart = LastTopPoint + Up * (TopProbeRadius + 2.f);
			const FVector RisingEnd = Probe + Up * (TopProbeRadius + 2.f);
			const bool bHitRisingObstacle = World->SweepSingleByChannel(RisingObstacleHit,
				RisingStart, RisingEnd, FQuat::Identity, IMG_TraceChannel_Traversal,
				FCollisionShape::MakeSphere(TopProbeRadius), Params)
				&& RisingObstacleHit.bBlockingHit
				&& FMath::Abs(RisingObstacleHit.ImpactNormal.Z) < 0.55f;
			bFoundEdge = !bHitRisingObstacle;
#if ENABLE_DRAW_DEBUG
			const FVector RisingDebugEnd = bHitRisingObstacle
				? (RisingObstacleHit.bStartPenetrating ? RisingStart : RisingObstacleHit.ImpactPoint)
				: RisingEnd;
			IMGTraversalDebug::Line(World, RisingStart,
				RisingDebugEnd,
				bHitRisingObstacle ? FColor::Orange : FColor::Green, 3, 2.f);
			if (bHitRisingObstacle)
			{
				IMGTraversalDebug::String(World, RisingDebugEnd + Up * 12.f,
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
		++ValidDepthSamples;
#if ENABLE_DRAW_DEBUG
		IMGTraversalDebug::String(World, DepthHit.ImpactPoint + Up * 8.f,
			FString::Printf(TEXT("Depth %d  %.0fcm"), Index, Step * Index), FColor::Cyan, 3);
#endif
	}
	if (bFoundEdge)
	{
		FVector ValidPoint = LastTopPoint;
		FVector InvalidPoint = FirstMissingPoint;
		for (int32 Iteration = 0; Iteration < 4; ++Iteration)
		{
			const FVector Probe = FMath::Lerp(ValidPoint, InvalidPoint, 0.5f);
			FHitResult RefineHit;
			if (TraceTop(*World, Probe, FrontPoint.Z + 30.f, FrontPoint.Z - 30.f,
				TopProbeRadius, Params, RefineHit)
				&& Movement->IsWalkable(RefineHit)
				&& FMath::Abs(RefineHit.ImpactPoint.Z - FrontPoint.Z) <= 25.f)
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
		const FVector BackFaceStart = BackFaceHeight + SurfaceForward * (Step + 5.f);
		const FVector BackFaceEnd = BackFaceHeight - SurfaceForward * (Step + 5.f);
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

	const FVector TopCenter = FrontPoint + Up * (HalfHeight + 3.f);
	if (!HasCapsuleRoom(*World, *Capsule, *Character, TopCenter, Radius, HalfHeight))
	{
		Failure = EIMGTraversalFailure::UnsafeTop;
#if ENABLE_DRAW_DEBUG
		IMGTraversalDebug::String(World, TopCenter + Up * HalfHeight,
			TEXT("UnsafeTop: capsule blocked"), FColor::Red, 2);
#endif
		return false;
	}
	FCollisionQueryParams ApproachParams(SCENE_QUERY_STAT(IMGTraversalApproachPath), false, Character);
	for (const UPrimitiveComponent* ObstacleComponent : Out.ObstacleComponents)
	{
		if (IsValid(ObstacleComponent))
		{
			ApproachParams.AddIgnoredComponent(ObstacleComponent);
		}
	}
	const FCollisionResponseParams CapsuleResponses(Capsule->GetCollisionResponseToChannels());
	FHitResult ApproachPathHit;
	const FVector ApproachStart = Character->GetActorLocation() + Up * 2.f;
	const bool bApproachBlocked = World->SweepSingleByChannel(ApproachPathHit, ApproachStart, TopCenter,
		FQuat::Identity, Capsule->GetCollisionObjectType(),
		FCollisionShape::MakeCapsule(FMath::Max(1.f, Radius - 2.f), FMath::Max(Radius, HalfHeight - 2.f)),
		ApproachParams, CapsuleResponses);
#if ENABLE_DRAW_DEBUG
	IMGTraversalDebug::Line(World, ApproachStart, bApproachBlocked ? ApproachPathHit.Location : TopCenter,
		bApproachBlocked ? FColor::Red : FColor::Green, 3, 2.f);
	IMGTraversalDebug::Capsule(World, ApproachStart, FMath::Max(Radius, HalfHeight - 2.f),
		FMath::Max(1.f, Radius - 2.f), bApproachBlocked ? FColor::Red : FColor::Green, 3);
#endif
	if (bApproachBlocked)
	{
		Failure = EIMGTraversalFailure::BlockedPath;
#if ENABLE_DRAW_DEBUG
		IMGTraversalDebug::String(World, ApproachPathHit.Location + Up * HalfHeight,
			TEXT("BlockedPath: approach capsule"), FColor::Red, 2);
#endif
		return false;
	}
	const float ClearanceProbeRadius = FMath::Clamp(Radius * 0.65f, 15.f, 22.5f);
	const float ClearanceBackOffset = 25.f;
	const float MaxClearance = FMath::Max(Query.MaxHorizontalClearance, ClearanceProbeRadius * 2.f);
	float HorizontalClearance = MaxClearance;
	for (int32 ClearanceIndex = 0; ClearanceIndex < 2; ++ClearanceIndex)
	{
		const FVector ClearanceStart = FrontPoint - SurfaceForward * ClearanceBackOffset
			+ Up * (15.f + ClearanceProbeRadius * (1.f + ClearanceIndex * 2.f));
		const FVector ClearanceEnd = ClearanceStart + SurfaceForward * (MaxClearance + ClearanceBackOffset);
		FHitResult ClearanceHit;
		FCollisionQueryParams ClearanceParams(SCENE_QUERY_STAT(IMGTraversalHorizontalClearance), false, Character);
		const bool bClearanceHit = World->SweepSingleByChannel(ClearanceHit, ClearanceStart, ClearanceEnd,
			FQuat::Identity, IMG_TraceChannel_Traversal,
			FCollisionShape::MakeSphere(ClearanceProbeRadius), ClearanceParams);
		const bool bClearanceBlocked = bClearanceHit && ClearanceHit.bBlockingHit;
		if (bClearanceBlocked)
		{
			const float HitClearance = ClearanceHit.bStartPenetrating ? 0.f
				: FVector::DotProduct(ClearanceHit.ImpactPoint - FrontPoint, SurfaceForward);
			HorizontalClearance = FMath::Min(HorizontalClearance, FMath::Clamp(HitClearance, 0.f, MaxClearance));
		}
#if ENABLE_DRAW_DEBUG
		const FVector ClearanceDebugEnd = bClearanceBlocked
			? (ClearanceHit.bStartPenetrating ? ClearanceStart : ClearanceHit.ImpactPoint) : ClearanceEnd;
		IMGTraversalDebug::Line(World, ClearanceStart,
			ClearanceDebugEnd,
			bClearanceBlocked ? FColor::Orange : FColor::Green, 3, 2.f);
		IMGTraversalDebug::Sphere(World, ClearanceStart, ClearanceProbeRadius,
			bClearanceBlocked ? FColor::Orange : FColor::Green, 3);
#endif
	}
	Out.HorizontalClearance = HorizontalClearance;
#if ENABLE_DRAW_DEBUG
	IMGTraversalDebug::String(World, FrontPoint + Up * (HalfHeight + 20.f),
		FString::Printf(TEXT("Horizontal clearance %.0fcm"), HorizontalClearance),
		HorizontalClearance <= 35.f ? FColor::Red : FColor::Green, 3);
#endif
	if (HorizontalClearance <= 35.f)
	{
		Failure = EIMGTraversalFailure::BlockedPath;
#if ENABLE_DRAW_DEBUG
		IMGTraversalDebug::String(World, FrontPoint + Up * (HalfHeight + 38.f),
			TEXT("BlockedPath: less than 35cm ledge clearance"), FColor::Red, 2);
#endif
		return false;
	}

	if (Out.bHasBackLedge)
	{
		const FVector FloorProbe = LastTopPoint + SurfaceForward * (Step + Radius);
		FHitResult FloorHit;
		FCollisionQueryParams FloorParams(SCENE_QUERY_STAT(IMGTraversalFloor), false, Character);
		const FVector FloorTraceStart = FloorProbe + Up * 10.f;
		const FVector FloorTraceEnd = FloorProbe - Up * Query.MaxDrop;
		const bool bFloorHit = World->LineTraceSingleByChannel(FloorHit, FloorTraceStart,
			FloorTraceEnd, IMG_TraceChannel_Traversal, FloorParams);
		const bool bWalkableFloor = bFloorHit && Movement->IsWalkable(FloorHit);
#if ENABLE_DRAW_DEBUG
		IMGTraversalDebug::Line(World, FloorTraceStart, bFloorHit ? FloorHit.ImpactPoint : FloorTraceEnd,
			bWalkableFloor ? FColor::Green : FColor::Red, 3, 2.f);
		if (bFloorHit)
		{
			IMGTraversalDebug::Point(World, FloorHit.ImpactPoint,
				bWalkableFloor ? FColor::Green : FColor::Red, 3, 9.f);
		}
#endif
		if (bWalkableFloor)
		{
			Out.DropHeight = FrontPoint.Z - FloorHit.ImpactPoint.Z;
			Out.BackLedgeHeight = LastTopPoint.Z - FloorHit.ImpactPoint.Z;
			const FVector FloorCenter = FloorHit.ImpactPoint + Up * (HalfHeight + 3.f);
			if (Out.DropHeight >= -20.f && Out.DropHeight <= Query.MaxDrop
				&& IsComponentUsable(FloorHit.GetComponent(), Query)
				&& HasCapsuleRoom(*World, *Capsule, *Character, FloorCenter, Radius, HalfHeight))
			{
				Out.bHasBackFloor = true;
				Out.BackFloorComponent = FloorHit.GetComponent();
				Out.BackFloor = FTransform(Facing, FloorHit.ImpactPoint);
			}
		}
	}

	Out.bCanMantle = true;
	const float VaultExitClearance = FMath::Max(50.f, Radius * 1.5f);
	Out.bCanVault = Out.bHasBackLedge && Height <= Query.MaxVaultHeight
		&& HorizontalClearance >= Out.ObstacleDepth + VaultExitClearance;
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
	Out.LocalFrontLedge = Out.FrontLedge.GetRelativeTransform(FaceComponent->GetComponentTransform());
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
		|| Query.MaxForwardDistance < Query.ForwardDistance || Query.ForwardDistanceAtMaxSpeed <= 0.f
		|| Query.MinHeight < 0.f || Query.MaxDepth <= 0.f || Query.MaxHorizontalClearance <= 0.f || Query.MaxDrop < 0.f
		|| Query.MaxHeight <= Query.MinHeight || Query.MinWidth <= 0.f
		|| Query.MaxMovingSpeed < 0.f || Query.MaxAngularSpeedDegrees < 0.f || Query.MaxCandidateAge <= 0.f)
	{
		OutFailure = EIMGTraversalFailure::InvalidMovement;
#if ENABLE_DRAW_DEBUG
		DrawFailure(*Character, OutFailure, Character->GetActorLocation() + FVector::UpVector * 50.f);
#endif
		return false;
	}

	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const FVector Origin = Character->GetActorLocation();
	const float ForwardSpeed = FMath::Max(0.f, FVector::DotProduct(Movement->Velocity, Forward));
	const float TraceForwardDistance = Movement->IsMovingOnGround()
		? FMath::GetMappedRangeValueClamped(FVector2D(0.f, Query.ForwardDistanceAtMaxSpeed),
			FVector2D(Query.ForwardDistance, Query.MaxForwardDistance), ForwardSpeed)
		: Query.ForwardDistance;
#if ENABLE_DRAW_DEBUG
	if (IMGTraversalDebug::IsEnabled(2))
	{
		IMGTraversalDebug::String(Character->GetWorld(), Origin + FVector::UpVector * (HalfHeight + 35.f),
			FString::Printf(TEXT("Traversal Search: %.0fcm  Height %.0f..%.0f  Speed %.0f  %s"),
				TraceForwardDistance, Query.MinHeight, Query.MaxHeight, Movement->Velocity.Size2D(),
				Movement->IsFalling() ? TEXT("Air") : TEXT("Ground")), FColor::White, 2);
	}
#endif
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IMGTraversalCandidates), false, Character);
	TArray<FHitResult> Hits;
	const float DuplicateDistanceSquared = FMath::Square(FMath::Max(2.f, Radius * 0.25f));
	const float FootZ = Origin.Z - HalfHeight;
	const float SearchRadius = FMath::Clamp(Radius * 0.45f, 12.f, 22.f);
	const float LowestProbeZ = FootZ + Query.MinHeight;
	const float HighestProbeZ = FootZ + Query.MaxHeight;
	const int32 ProbeLayerCount = FMath::Clamp(
		FMath::CeilToInt((HighestProbeZ - LowestProbeZ) / (SearchRadius * 2.f)) + 1,
		1, MaxForwardProbeLayers);
	for (int32 LayerIndex = 0; LayerIndex < ProbeLayerCount; ++LayerIndex)
	{
		TArray<FHitResult> ProbeHits;
		const float Alpha = ProbeLayerCount > 1
			? static_cast<float>(LayerIndex) / static_cast<float>(ProbeLayerCount - 1) : 0.f;
		const FVector Start(Origin.X, Origin.Y, FMath::Lerp(LowestProbeZ, HighestProbeZ, Alpha));
		const FVector End = Start + Forward * TraceForwardDistance;
		Character->GetWorld()->SweepMultiByChannel(ProbeHits, Start, End,
			FQuat::Identity, IMG_TraceChannel_Traversal, FCollisionShape::MakeSphere(SearchRadius), Params);
#if ENABLE_DRAW_DEBUG
		const FColor SearchColor = ProbeHits.IsEmpty() ? FColor::Silver : FColor::Cyan;
		IMGTraversalDebug::Line(Character->GetWorld(), Start, End, SearchColor, 2, 1.5f);
		IMGTraversalDebug::Sphere(Character->GetWorld(), Start, SearchRadius, SearchColor, 2);
		IMGTraversalDebug::Sphere(Character->GetWorld(), End, SearchRadius, SearchColor, 2);
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
		auto Score = [&Origin, &Forward](const FHitResult& Hit)
		{
			const float Distance = FVector::Dist2D(Origin, Hit.ImpactPoint);
			const float Facing = FMath::Clamp(-FVector::DotProduct(Hit.ImpactNormal.GetSafeNormal2D(), Forward), 0.f, 1.f);
			return Distance + (1.f - Facing) * 100.f;
		};
		return Score(A) < Score(B);
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
			FString::Printf(TEXT("Candidate %d/%d  D %.0f  Facing %.2f"), Tested,
				FMath::Min(Hits.Num(), MaxCandidateHits), FVector::Dist2D(Origin, Hit.ImpactPoint),
				-FVector::DotProduct(Hit.ImpactNormal.GetSafeNormal2D(), Forward)), FColor::Yellow, 2);
#endif
		if (MakeCandidate(Query, Hit, Forward, Candidate, Failure))
		{
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
	DrawFailure(*Character, OutFailure, Origin + Forward * TraceForwardDistance + FVector::UpVector * 30.f);
#endif
	return false;
}
