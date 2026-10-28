#include "Character/Traversal/IMGTraversalTypes.h"

FIMGTraversalAnimationSettings::FIMGTraversalAnimationSettings()
{
	FIMGTraversalWarpBinding& Front = WarpBindings.AddDefaulted_GetRef();
	Front.TargetName = TEXT("FrontLedge");
	Front.Anchor = EIMGTraversalAnchor::FrontLedge;
	Front.Offset.SetTranslation(FVector(0.f, 0.f, 0.5f));
	FIMGTraversalWarpBinding& Back = WarpBindings.AddDefaulted_GetRef();
	Back.TargetName = TEXT("BackLedge");
	Back.Anchor = EIMGTraversalAnchor::BackLedge;
	FIMGTraversalWarpBinding& Floor = WarpBindings.AddDefaulted_GetRef();
	Floor.TargetName = TEXT("BackFloor");
	Floor.Anchor = EIMGTraversalAnchor::BackFloor;
	RequiredTargets.Add(TEXT("FrontLedge"));
}
