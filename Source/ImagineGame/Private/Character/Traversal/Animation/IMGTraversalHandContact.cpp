#include "Character/Traversal/Animation/IMGTraversalHandContact.h"

#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"

namespace
{
constexpr double UnrestrictedHeightUpdateDuration = 0.10;
constexpr double FrontEdgeRefinementDuration = 0.50;
constexpr double ContactMissGracePeriod = 0.10;
constexpr double MaxContactUpdateInterval = 0.15;
constexpr double MaxContactHeightSpeed = 60.0;
} // namespace

void FIMGTraversalHandContact::Reset()
{
	*this = FIMGTraversalHandContact();
}

const UPrimitiveComponent* FIMGTraversalHandContact::GetContactSurface() const
{
	check(IsInGameThread());
	return ContactSurface.Get();
}

bool FIMGTraversalHandContact::UpdateContact(const UWorld& World, const FCollisionQueryParams& QueryParams,
    ECollisionChannel Channel, TConstArrayView<TObjectPtr<UPrimitiveComponent>> Surfaces,
    const FTransform& FrontEdge, const FVector& Palm, float MinNormalZ, FIMGTraversalHandContactSettings InSettings,
    bool bRefineFrontEdge, FVector& OutTarget, bool& bOutNewContact)
{
	OutTarget = FVector::ZeroVector;
	bOutNewContact = false;
	if (!IsInGameThread())
	{
		return false;
	}
	const FVector Forward = FrontEdge.GetUnitAxis(EAxis::X).GetSafeNormal2D();
	const FVector Edge = FrontEdge.GetLocation();
	const double Now = World.GetTimeSeconds();
	if (!FrontEdge.IsValid() || Forward.IsNearlyZero() || Palm.ContainsNaN() || Surfaces.IsEmpty() ||
	    !FMath::IsFinite(Now) || !FMath::IsFinite(MinNormalZ) || !InSettings.IsValid())
	{
		Reset();
		return false;
	}
	InSettings.ContactBreakDistance = FMath::Max(InSettings.ContactBreakDistance, InSettings.MaxHandCorrectionDistance);
	if (CachedSettings != InSettings ||
	    (LastContactUpdateTime >= 0.0 &&
	        (Now < LastContactUpdateTime || Now - LastContactUpdateTime > MaxContactUpdateInterval)))
	{
		Reset();
	}
	CachedSettings = InSettings;
	const double DeltaTime = LastContactUpdateTime >= 0.0 ? Now - LastContactUpdateTime : 0.0;
	LastContactUpdateTime = Now;
	if (bContactBroken)
	{
		return false;
	}

	const auto IsAllowedSurface = [&](const UPrimitiveComponent* Component)
	{
		return IsValid(Component) && Component->IsRegistered() && Component->GetWorld() == &World &&
		    Surfaces.ContainsByPredicate([Component](const auto& Entry) { return Entry.Get() == Component; }) &&
		    Component->GetComponentTransform().IsValid();
	};
	const auto IsNearFrontEdge = [&](const FVector& Point)
	{
		const double Depth = FVector::DotProduct(Point - Edge, Forward);
		return Depth >= -InSettings.VerticalTraceRadius && Depth <= InSettings.FrontEdgeTraceDistance &&
		    FMath::Abs(Point.Z - Edge.Z) <= InSettings.VerticalTraceHalfHeight + InSettings.VerticalTraceRadius;
	};

	const UPrimitiveComponent* PreviousSurface = ContactSurface.Get();
	bool bHasTarget = IsAllowedSurface(PreviousSurface);
	const FVector PreviousTarget = bHasTarget
	    ? PreviousSurface->GetComponentTransform().TransformPosition(ContactPointLocal)
	    : FVector::ZeroVector;
	bHasTarget = bHasTarget && IsNearFrontEdge(PreviousTarget);
	if (bHasTarget && FVector::DistSquared(PreviousTarget, Palm) > FMath::Square(InSettings.ContactBreakDistance))
	{
		bContactBroken = true;
		return false;
	}

	const FVector ProbeCenter(Palm.X, Palm.Y, Edge.Z);
	const FVector ProbeOffset = FVector::UpVector * InSettings.VerticalTraceHalfHeight;
	FHitResult Hit;
	// The directed GTA capsule probe corresponds to a sphere sweep.
	const bool bHit = World.SweepSingleByChannel(Hit, ProbeCenter + ProbeOffset, ProbeCenter - ProbeOffset,
	    FQuat::Identity, Channel, FCollisionShape::MakeSphere(InSettings.VerticalTraceRadius), QueryParams);
	const UPrimitiveComponent* HitSurface = Hit.GetComponent();
	if (bHit && Hit.IsValidBlockingHit() && Hit.ImpactNormal.Z >= MinNormalZ && IsAllowedSurface(HitSurface) &&
	    IsNearFrontEdge(Hit.ImpactPoint))
	{
		FVector Candidate = Hit.ImpactPoint;
		const bool bSameSurface = bHasTarget && HitSurface == PreviousSurface;
		const double ContactTime = bSameSurface ? Now - ContactStartTime : 0.0;
		if (bRefineFrontEdge && ContactTime < FrontEdgeRefinementDuration)
		{
			FHitResult FrontHit;
			const FVector FrontStart = Candidate - Forward * InSettings.FrontEdgeTraceDistance;
			if (World.SweepSingleByChannel(FrontHit, FrontStart, Candidate, FQuat::Identity, Channel,
			        FCollisionShape::MakeSphere(InSettings.FrontEdgeTraceRadius), QueryParams) &&
			    FrontHit.IsValidBlockingHit() && FrontHit.GetComponent() == HitSurface)
			{
				FVector Refined = FMath::Lerp(FrontHit.ImpactPoint, Candidate, ContactTime / FrontEdgeRefinementDuration);
				Refined.Z = FMath::Max(Candidate.Z, FrontHit.ImpactPoint.Z);
				if (IsNearFrontEdge(Refined) &&
				    FVector::DistSquared(Refined, Palm) < FMath::Square(InSettings.MaxHandCorrectionDistance))
				{
					Candidate = Refined;
				}
			}
		}
		if (bSameSurface && ContactTime > UnrestrictedHeightUpdateDuration)
		{
			// Limit height changes per second so contact does not depend on frame rate.
			const double MaxStep = MaxContactHeightSpeed * DeltaTime;
			Candidate.Z = PreviousTarget.Z + FMath::Clamp(Candidate.Z - PreviousTarget.Z, -MaxStep, MaxStep);
		}
		if (FVector::DistSquared(Candidate, Palm) < FMath::Square(InSettings.MaxHandCorrectionDistance))
		{
			const FVector LocalCandidate = HitSurface->GetComponentTransform().InverseTransformPosition(Candidate);
			if (!LocalCandidate.ContainsNaN())
			{
				ContactSurface = Hit.GetComponent();
				ContactPointLocal = LocalCandidate;
				if (!bSameSurface)
				{
					ContactStartTime = Now;
				}
				LastValidHitTime = Now;
				OutTarget = Candidate;
				bOutNewContact = !bSameSurface;
				return true;
			}
		}
	}

	// Bridge a short miss on a thin ledge, while the last verified surface and target are still usable.
	if (bHasTarget && Now - LastValidHitTime <= ContactMissGracePeriod &&
	    FVector::DistSquared(PreviousTarget, Palm) < FMath::Square(InSettings.MaxHandCorrectionDistance))
	{
		OutTarget = PreviousTarget;
		return true;
	}
	ContactSurface.Reset();
	ContactStartTime = LastValidHitTime = -1.0;
	return false;
}
