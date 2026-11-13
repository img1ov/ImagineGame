#include "Character/Traversal/IMGTraversalDebugTypes.h"

FArchive& operator<<(FArchive& Ar, FIMGTraversalDebugShape& Value)
{
	uint8 Primitive = static_cast<uint8>(Value.Primitive);
	Ar << Primitive;
	if (Ar.IsLoading())
	{
		Value.Primitive = static_cast<EIMGTraversalDebugPrimitive>(Primitive);
	}
	uint8 Stage = static_cast<uint8>(Value.Stage);
	Ar << Stage;
	if (Ar.IsLoading())
	{
		Value.Stage = static_cast<EIMGTraversalDebugStage>(Stage);
	}
	Ar << Value.Start;
	Ar << Value.End;
	Ar << Value.Color;
	Ar << Value.Size;
	Ar << Value.Radius;
	Ar << Value.Thickness;
	Ar << Value.Attempt;
	Ar << Value.Purpose;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FIMGTraversalDebugEvent& Value)
{
	Ar << Value.Name;
	Ar << Value.Detail;
	uint8 Phase = static_cast<uint8>(Value.Phase);
	Ar << Phase;
	if (Ar.IsLoading())
	{
		Value.Phase = static_cast<EIMGTraversalDebugPhase>(Phase);
	}
	uint8 Stage = static_cast<uint8>(Value.Stage);
	Ar << Stage;
	if (Ar.IsLoading())
	{
		Value.Stage = static_cast<EIMGTraversalDebugStage>(Stage);
	}
	Ar << Value.Time;
	Ar << Value.Attempt;
	Ar << Value.AbilitySpec;
	Ar << Value.PredictionKey;
	Ar << Value.bWarning;
	Ar << Value.Sequence;
	Ar << Value.MontageInstance;
	Ar << Value.bActive;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FIMGTraversalDebugCandidate& Value)
{
	Ar << Value.Attempt;
	uint8 Phase = static_cast<uint8>(Value.Phase);
	Ar << Phase;
	if (Ar.IsLoading())
	{
		Value.Phase = static_cast<EIMGTraversalDebugPhase>(Phase);
	}
	Ar << Value.Label;
	Ar << Value.Source;
	Ar << Value.RecommendedAction;
	Ar << Value.bCanMantle;
	Ar << Value.bCanVault;
	Ar << Value.bCanHurdle;
	Ar << Value.ObstacleHeight;
	Ar << Value.ObstacleDepth;
	Ar << Value.DropHeight;
	Ar << Value.BackLedgeHeight;
	Ar << Value.HorizontalClearance;
	Ar << Value.VerticalClearance;
	Ar << Value.Distance3D;
	Ar << Value.DistanceXY;
	Ar << Value.EdgeSpan;
	Ar << Value.MidHeightClearance;
	Ar << Value.bHighOpenApproach;
	Ar << Value.ClimbAngle;
	Ar << Value.GroundSlopeAngle;
	Ar << Value.bHasBackLedge;
	Ar << Value.bHasBackFloor;
	Ar << Value.FrontLedge;
	Ar << Value.BackLedge;
	Ar << Value.BackFloor;
	Ar << Value.FrontNormal;
	Ar << Value.BackNormal;
	Ar << Value.GroundNormal;
	Ar << Value.GroundLocation;
	Ar << Value.Components;
	return Ar;
}

FArchive& IMGTraversalDebugSerialization::SerializeFrame(
    FArchive& Ar, FIMGTraversalDebugFrame& Value, uint8 Version)
{
	Ar << Value.Cycle;
	Ar << Value.ActorId;
	Ar << Value.Time;
	Ar << Value.DeltaSeconds;
	Ar << Value.Frame;
	Ar << Value.Actor;
	Ar << Value.World;
	Ar << Value.Role;
	Ar << Value.NetMode;
	Ar << Value.AbilitySpec;
	Ar << Value.PredictionKey;
	Ar << Value.Sequence;
	Ar << Value.MontageInstance;
	Ar << Value.Action;
	Ar << Value.Montage;
	Ar << Value.bActive;
	Ar << Value.Location;
	Ar << Value.Velocity;
	Ar << Value.MovementMode;
	Ar << Value.MontagePosition;
	Ar << Value.MontageLength;
	Ar << Value.MontagePhase;
	Ar << Value.Attempt;
	Ar << Value.Source;
	Ar << Value.Failure;
	Ar << Value.Direction;
	Ar << Value.ObstacleHeight;
	Ar << Value.ObstacleDepth;
	Ar << Value.DropHeight;
	Ar << Value.HorizontalClearance;
	Ar << Value.VerticalClearance;
	Ar << Value.DetectionMilliseconds;
	Ar << Value.Queries;
	Ar << Value.Dropped;
	Ar << Value.Events;
	Ar << Value.Candidates;
	Ar << Value.Geometry;
	if (Version >= 3)
	{
		Ar << Value.MontageAssetPath;
		Ar << Value.AbilityClassPath;
	}
	else if (Ar.IsLoading())
	{
		Value.MontageAssetPath.Reset();
		Value.AbilityClassPath.Reset();
	}
	if (Version >= 4)
	{
		Ar << Value.bHasHeightSample;
		Ar << Value.HeightCapture;
		Ar << Value.HeightReferenceZ;
		Ar << Value.MontageRootHeight;
		Ar << Value.WarpedCapsuleHeight;
	}
	else if (Ar.IsLoading())
	{
		Value.bHasHeightSample = false;
		Value.HeightCapture = 0;
		Value.HeightReferenceZ = Value.MontageRootHeight = Value.WarpedCapsuleHeight = 0;
	}
	return Ar;
}
FArchive& operator<<(FArchive& Ar, FIMGTraversalDebugFrame& Value)
{
	return IMGTraversalDebugSerialization::SerializeFrame(Ar, Value, 4);
}
