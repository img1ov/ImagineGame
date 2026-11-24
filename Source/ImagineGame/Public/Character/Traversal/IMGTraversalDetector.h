#pragma once

#include "Character/Traversal/IMGTraversalTypes.h"

/** Synchronous geometry query shared by player, AI and server validation. */
class IMAGINEGAME_API FIMGTraversalDetector
{
public:
	static bool Detect(
	    const FIMGTraversalQuery& Query, FIMGTraversalCandidate& OutCandidate, EIMGTraversalFailure& OutFailure);
	static bool CanUseSurface(const UPrimitiveComponent* Component, const FIMGTraversalDetectorSettings& Settings,
	    EIMGTraversalRequestSource Source);
	/** Floor eligibility does not require climbable-channel responses or climbing tags. */
	static bool CanUseFloor(const UPrimitiveComponent* Component, const FIMGTraversalDetectorSettings& Settings);
};
