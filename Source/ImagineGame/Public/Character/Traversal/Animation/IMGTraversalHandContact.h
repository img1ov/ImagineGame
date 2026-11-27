#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

class UPrimitiveComponent;
class UWorld;
struct FCollisionQueryParams;

/** Geometry thresholds governing palm contact sweeps and drift limits. */
struct IMAGINEGAME_API FIMGTraversalHandContactSettings
{
	float VerticalTraceHalfHeight = 30.f;
	float VerticalTraceRadius = 7.5f;
	float FrontEdgeTraceDistance = 50.f;
	float FrontEdgeTraceRadius = 5.f;
	float MaxHandCorrectionDistance = 25.f;
	float ContactBreakDistance = 50.f;

	bool IsValid() const
	{
		return FMath::IsFinite(VerticalTraceHalfHeight) && VerticalTraceHalfHeight > 0.f &&
		    FMath::IsFinite(VerticalTraceRadius) && VerticalTraceRadius >= 0.f &&
		    FMath::IsFinite(FrontEdgeTraceDistance) && FrontEdgeTraceDistance > 0.f &&
		    FMath::IsFinite(FrontEdgeTraceRadius) && FrontEdgeTraceRadius >= 0.f &&
		    FMath::IsFinite(MaxHandCorrectionDistance) && MaxHandCorrectionDistance > 0.f &&
		    FMath::IsFinite(ContactBreakDistance) && ContactBreakDistance > 0.f;
	}

	bool operator==(const FIMGTraversalHandContactSettings& Other) const
	{
		return VerticalTraceHalfHeight == Other.VerticalTraceHalfHeight &&
		    VerticalTraceRadius == Other.VerticalTraceRadius &&
		    FrontEdgeTraceDistance == Other.FrontEdgeTraceDistance &&
		    FrontEdgeTraceRadius == Other.FrontEdgeTraceRadius &&
		    MaxHandCorrectionDistance == Other.MaxHandCorrectionDistance &&
		    ContactBreakDistance == Other.ContactBreakDistance;
	}

	bool operator!=(const FIMGTraversalHandContactSettings& Other) const { return !(*this == Other); }
};

/** Resolves world-space palm contacts and caches them in the contacted surface's local space. */
class IMAGINEGAME_API FIMGTraversalHandContact
{
public:
	void Reset();

	/** Game-thread access for copying the accepted contact's surface transform. */
	const UPrimitiveComponent* GetContactSurface() const;

	bool UpdateContact(const UWorld& World, const FCollisionQueryParams& QueryParams, ECollisionChannel Channel,
	    TConstArrayView<TObjectPtr<UPrimitiveComponent>> Surfaces, const FTransform& FrontEdge, const FVector& Palm,
	    float MinNormalZ, FIMGTraversalHandContactSettings InSettings, bool bRefineFrontEdge, FVector& OutTarget,
	    bool& bOutNewContact);

private:
	TWeakObjectPtr<UPrimitiveComponent> ContactSurface;
	FVector ContactPointLocal = FVector::ZeroVector;
	double ContactStartTime = -1.0;
	double LastValidHitTime = -1.0;
	double LastContactUpdateTime = -1.0;
	bool bContactBroken = false;
	// Changed geometry settings invalidate both the accepted contact and its break state.
	FIMGTraversalHandContactSettings CachedSettings;
};
