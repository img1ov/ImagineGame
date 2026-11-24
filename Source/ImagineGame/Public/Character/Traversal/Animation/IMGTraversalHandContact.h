#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

class UPrimitiveComponent;
class UWorld;
struct FCollisionQueryParams;

/** Resolves world-space palm contacts and caches them in the contacted surface's local space. */
class IMAGINEGAME_API FIMGTraversalHandContact
{
public:
	void Reset();

	bool UpdateContact(const UWorld& World, const FCollisionQueryParams& QueryParams, ECollisionChannel Channel,
	    TConstArrayView<TObjectPtr<UPrimitiveComponent>> Surfaces, const FTransform& FrontEdge, const FVector& Palm,
	    float MinNormalZ, float InVerticalTraceHalfHeight, float InVerticalTraceRadius, float InFrontEdgeTraceDistance,
	    float InFrontEdgeTraceRadius, float InMaxHandCorrectionDistance, float InContactBreakDistance,
	    bool bRefineFrontEdge, FVector& OutTarget);

private:
	TWeakObjectPtr<UPrimitiveComponent> ContactSurface;
	FVector ContactPointLocal = FVector::ZeroVector;
	double ContactStartTime = -1.0;
	double LastValidHitTime = -1.0;
	double LastContactUpdateTime = -1.0;
	bool bContactBroken = false;
	// Changed geometry settings invalidate both the accepted contact and its break state.
	float VerticalTraceHalfHeight = 0.f;
	float VerticalTraceRadius = 0.f;
	float FrontEdgeTraceDistance = 0.f;
	float FrontEdgeTraceRadius = 0.f;
	float MaxHandCorrectionDistance = 0.f;
	float ContactBreakDistance = 0.f;
};
