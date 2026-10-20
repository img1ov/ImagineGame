#include "Traversal/IMGTraversalDetector.h"

#include "Character/IMGCharacter.h"
#include "Character/IMGCharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
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
	IMGTraversalDebug::Arrow(World, Front, Front + Candidate.FrontLedgeNormal * 45.f, FColor::Yellow, 1);
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
		FString::Printf(TEXT("Traversal: %s  H %.0f  D %.0f  Drop %.0f  [%s]"),
			*GetActionName(Candidate.RecommendedAction), Candidate.ObstacleHeight,
			Candidate.ObstacleDepth, Candidate.DropHeight, *Feasible), FColor::Green, 1);
}
#endif

bool TraceTop(UPrimitiveComponent& Component, const FVector& Point, float StartZ, float EndZ,
	const FCollisionQueryParams& Params, FHitResult& OutHit)
{
	const FVector Start(Point.X, Point.Y, StartZ);
	const FVector End(Point.X, Point.Y, EndZ);
	const bool bHit = Component.LineTraceComponent(OutHit, Start, End, IMG_TraceChannel_Traversal, Params,
		FCollisionResponseParams::DefaultResponseParam, FCollisionObjectQueryParams::DefaultObjectQueryParam)
		&& OutHit.bBlockingHit;
#if ENABLE_DRAW_DEBUG
	IMGTraversalDebug::Line(Component.GetWorld(), Start, bHit ? OutHit.ImpactPoint : End,
		bHit ? FColor::Cyan : FColor::Red, 3);
	if (bHit)
	{
		IMGTraversalDebug::Point(Component.GetWorld(), OutHit.ImpactPoint, FColor::Cyan, 3, 7.f);
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
	UPrimitiveComponent* Component = Face.GetComponent();
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	const UIMGCharacterMovementComponent* Movement = Character->GetIMGMovementComponent();
	UWorld* World = Character->GetWorld();
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const FVector Up = FVector::UpVector;
	const FVector Right = FVector::CrossProduct(Up, Forward).GetSafeNormal();
	const float FootZ = Character->GetActorLocation().Z - HalfHeight;
	if (!IsValid(Component) || !World || FVector::DotProduct(Face.ImpactNormal, Forward) > -0.35f
		|| FMath::Abs(Face.ImpactNormal.Z) > 0.55f || Right.IsNearlyZero())
	{
		Failure = EIMGTraversalFailure::InvalidSurface;
		return false;
	}
#if ENABLE_DRAW_DEBUG
	IMGTraversalDebug::Point(World, Face.ImpactPoint, FColor::Yellow, 2, 11.f);
	IMGTraversalDebug::Arrow(World, Face.ImpactPoint, Face.ImpactPoint + Face.ImpactNormal * 40.f,
		FColor::Yellow, 2);
#endif
	if ((Component->IsSimulatingPhysics() && !Query.bAllowSimulatingPhysics)
		|| Component->GetComponentVelocity().Size() > Query.MaxMovingSpeed
		|| Component->GetPhysicsAngularVelocityInDegrees().Size() > Query.MaxAngularSpeedDegrees)
	{
		Failure = EIMGTraversalFailure::MovingTarget;
		return false;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(IMGTraversalTop), false, Character);
	const FVector ProbeOrigin = Face.ImpactPoint + Forward * FMath::Max(8.f, Radius * 0.45f);
	FHitResult CenterTop;
	if (!TraceTop(*Component, ProbeOrigin, FootZ + Query.MaxHeight + 20.f,
		FootZ + Query.MinHeight - 10.f, Params, CenterTop)
		|| !Movement->IsWalkable(CenterTop))
	{
		Failure = EIMGTraversalFailure::NoFrontLedge;
		return false;
	}
	const float Height = CenterTop.ImpactPoint.Z - FootZ;
	if (Height < Query.MinHeight || Height > Query.MaxHeight)
	{
		Failure = EIMGTraversalFailure::NoFrontLedge;
		return false;
	}

	const float SideOffset = FMath::Max(Query.MinWidth * 0.5f, Radius * 0.7f);
	FVector SideTopPoints[2];
	int32 SidePointIndex = 0;
	for (const float Side : {-1.f, 1.f})
	{
		FHitResult SideTop;
		if (!TraceTop(*Component, ProbeOrigin + Right * SideOffset * Side,
			FootZ + Query.MaxHeight + 20.f, FootZ + Query.MinHeight - 10.f, Params, SideTop)
			|| !Movement->IsWalkable(SideTop)
			|| FMath::Abs(SideTop.ImpactPoint.Z - CenterTop.ImpactPoint.Z) > 20.f)
		{
			Failure = EIMGTraversalFailure::InsufficientWidth;
			return false;
		}
		SideTopPoints[SidePointIndex++] = SideTop.ImpactPoint;
	}
#if ENABLE_DRAW_DEBUG
	IMGTraversalDebug::Line(World, SideTopPoints[0], SideTopPoints[1], FColor::Blue, 3, 2.f);
#endif

	const FVector FrontPoint = CenterTop.ImpactPoint;
	const FRotator Facing = FRotationMatrix::MakeFromXZ(Forward, Up).Rotator();
	Out.FrontLedge = FTransform(Facing, FrontPoint);
	Out.HitComponent = Component;
	Out.FrontLedgeNormal = Face.ImpactNormal;
	Out.ApproachDirection = Forward;
	Out.ObstacleHeight = Height;
	Out.DistanceToLedge = FVector::Dist2D(Character->GetActorLocation(), Face.ImpactPoint);
	Out.DetectionTime = World->GetTimeSeconds();
	Out.Speed = Movement->Velocity.Size2D();
	Out.MovementMode = Movement->MovementMode;
	Out.bHasFrontLedge = true;

	FVector LastTopPoint = FrontPoint;
	FVector FirstMissingPoint = FrontPoint;
	int32 ValidDepthSamples = 0;
	const float Step = FMath::Max(15.f, FMath::Min(Radius, Query.MaxDepth / MaxDepthSamples));
	const int32 DepthSamples = FMath::Min(MaxDepthSamples, FMath::CeilToInt(Query.MaxDepth / Step));
	bool bFoundEdge = false;
	for (int32 Index = 1; Index <= DepthSamples; ++Index)
	{
		const FVector Probe = FrontPoint + Forward * (Step * Index);
		FHitResult DepthHit;
		if (!TraceTop(*Component, Probe, FrontPoint.Z + 30.f, FrontPoint.Z - 30.f, Params, DepthHit)
			|| !Movement->IsWalkable(DepthHit)
			|| FMath::Abs(DepthHit.ImpactPoint.Z - FrontPoint.Z) > 25.f)
		{
			bFoundEdge = true;
			FirstMissingPoint = Probe;
			break;
		}
		LastTopPoint = DepthHit.ImpactPoint;
		++ValidDepthSamples;
	}
	if (bFoundEdge)
	{
		FVector ValidPoint = LastTopPoint;
		FVector InvalidPoint = FirstMissingPoint;
		for (int32 Iteration = 0; Iteration < 4; ++Iteration)
		{
			const FVector Probe = FMath::Lerp(ValidPoint, InvalidPoint, 0.5f);
			FHitResult RefineHit;
			if (TraceTop(*Component, Probe, FrontPoint.Z + 30.f, FrontPoint.Z - 30.f, Params, RefineHit)
				&& Movement->IsWalkable(RefineHit)
				&& FMath::Abs(RefineHit.ImpactPoint.Z - FrontPoint.Z) <= 25.f)
			{
				ValidPoint = RefineHit.ImpactPoint;
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
	Out.BackLedgeNormal = Forward;
#if ENABLE_DRAW_DEBUG
	IMGTraversalDebug::Line(World, FrontPoint, LastTopPoint, FColor::Orange, 3, 2.f);
	IMGTraversalDebug::Point(World, LastTopPoint, bFoundEdge ? FColor::Orange : FColor::Silver, 3, 10.f);
#endif
	if (Out.bHasBackLedge)
	{
		FHitResult BackFace;
		const FVector BackFaceHeight = LastTopPoint - Up * FMath::Min(25.f, Height * 0.25f);
		const FVector BackFaceStart = BackFaceHeight + Forward * (Step + 5.f);
		const FVector BackFaceEnd = BackFaceHeight - Forward * (Step + 5.f);
		const bool bBackFaceHit = Component->LineTraceComponent(BackFace, BackFaceStart,
			BackFaceEnd, IMG_TraceChannel_Traversal, Params,
			FCollisionResponseParams::DefaultResponseParam, FCollisionObjectQueryParams::DefaultObjectQueryParam)
			&& BackFace.bBlockingHit;
#if ENABLE_DRAW_DEBUG
		IMGTraversalDebug::Line(World, BackFaceStart, bBackFaceHit ? BackFace.ImpactPoint : BackFaceEnd,
			bBackFaceHit ? FColor::Orange : FColor::Red, 3, 1.5f);
#endif
		if (bBackFaceHit)
		{
			Out.BackLedgeNormal = BackFace.ImpactNormal;
		}
	}

	const FVector TopCenter = FrontPoint + Up * (HalfHeight + 3.f);
	if (!HasCapsuleRoom(*World, *Capsule, *Character, TopCenter, Radius, HalfHeight))
	{
		Failure = EIMGTraversalFailure::UnsafeTop;
		return false;
	}
	if (ValidDepthSamples > 0)
	{
		FHitResult PathHit;
		FCollisionQueryParams PathParams(SCENE_QUERY_STAT(IMGTraversalTopPath), false, Character);
		const FCollisionResponseParams Responses(Capsule->GetCollisionResponseToChannels());
		const FVector PathEnd = LastTopPoint + Up * (HalfHeight + 3.f);
		const bool bBlocked = World->SweepSingleByChannel(PathHit, TopCenter, PathEnd,
			FQuat::Identity, Capsule->GetCollisionObjectType(), FCollisionShape::MakeCapsule(Radius, HalfHeight),
			PathParams, Responses);
#if ENABLE_DRAW_DEBUG
		IMGTraversalDebug::Line(World, TopCenter, bBlocked ? PathHit.Location : PathEnd,
			bBlocked ? FColor::Red : FColor::Green, 3, 2.f);
		IMGTraversalDebug::Capsule(World, PathEnd, HalfHeight, Radius,
			bBlocked ? FColor::Red : FColor::Green, 3);
#endif
		if (bBlocked)
		{
			Failure = EIMGTraversalFailure::BlockedPath;
			return false;
		}
	}

	if (Out.bHasBackLedge)
	{
		const FVector FloorProbe = LastTopPoint + Forward * (Step + Radius);
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
				&& HasCapsuleRoom(*World, *Capsule, *Character, FloorCenter, Radius, HalfHeight))
			{
				Out.bHasBackFloor = true;
				Out.BackFloorComponent = FloorHit.GetComponent();
				Out.BackFloor = FTransform(Facing, FloorHit.ImpactPoint);
			}
		}
	}

	Out.bCanMantle = ValidDepthSamples > 0;
	Out.bCanVault = Out.bHasBackLedge && Height <= Query.MaxVaultHeight;
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
	const FTransform ComponentWorld = Component->GetComponentTransform();
	Out.LocalFrontLedge = Out.FrontLedge.GetRelativeTransform(ComponentWorld);
	Out.LocalBackLedge = Out.BackLedge.GetRelativeTransform(ComponentWorld);
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
		|| Query.MaxDepth <= 0.f
		|| Query.MaxHeight <= Query.MinHeight || Query.MinWidth <= 0.f)
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
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IMGTraversalCandidates), false, Character);
	TArray<FHitResult> Hits;
	const float DuplicateDistanceSquared = FMath::Square(FMath::Max(2.f, Radius * 0.25f));
	for (const float HeightOffset : {-HalfHeight + Radius, 0.f, HalfHeight - Radius})
	{
		TArray<FHitResult> ProbeHits;
		const FVector Start = Origin + FVector::UpVector * HeightOffset;
		const FVector End = Start + Forward * TraceForwardDistance;
		Character->GetWorld()->SweepMultiByChannel(ProbeHits, Start, End,
			FQuat::Identity, IMG_TraceChannel_Traversal, FCollisionShape::MakeSphere(Radius * 0.65f), Params);
#if ENABLE_DRAW_DEBUG
		const FColor SearchColor = ProbeHits.IsEmpty() ? FColor::Silver : FColor::Cyan;
		IMGTraversalDebug::Line(Character->GetWorld(), Start, End, SearchColor, 2, 1.5f);
		IMGTraversalDebug::Sphere(Character->GetWorld(), Start, Radius * 0.65f, SearchColor, 2);
		IMGTraversalDebug::Sphere(Character->GetWorld(), End, Radius * 0.65f, SearchColor, 2);
#endif
		for (const FHitResult& Hit : ProbeHits)
		{
			if (!Hit.bBlockingHit || !Hit.GetComponent())
			{
				continue;
			}
			const bool bDuplicate = Hits.ContainsByPredicate([&Hit, DuplicateDistanceSquared](const FHitResult& Existing)
			{
				return Existing.GetComponent() == Hit.GetComponent()
					&& FVector::DistSquared2D(Existing.ImpactPoint, Hit.ImpactPoint) <= DuplicateDistanceSquared
					&& FVector::DotProduct(Existing.ImpactNormal, Hit.ImpactNormal) >= 0.9f;
			});
			if (!bDuplicate)
			{
				Hits.Add(Hit);
			}
		}
	}
	Hits.Sort([&Origin](const FHitResult& A, const FHitResult& B)
	{
		return FVector::DistSquared(Origin, A.ImpactPoint) < FVector::DistSquared(Origin, B.ImpactPoint);
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
