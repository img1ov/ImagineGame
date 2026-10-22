#include "IMGTraversalDebug.h"

#if ENABLE_DRAW_DEBUG

#include "Animation/AnimMontage.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Traversal/IMGTraversalTypes.h"

namespace
{
constexpr float DebugLineThicknessScale = 0.5f;
constexpr float DebugPointSizeScale = 0.7f;
constexpr float DebugSphereRadiusScale = 0.65f;

enum class ETraversalDebugPrimitive : uint8
{
	Line,
	Arrow,
	Point,
	Sphere,
	Capsule,
	String
};

struct FTraversalDebugCommand
{
	ETraversalDebugPrimitive Primitive = ETraversalDebugPrimitive::Line;
	FVector Start = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
	FColor Color = FColor::White;
	FString Text;
	int32 RequiredLevel = 1;
	float ValueA = 0.f;
	float ValueB = 0.f;
};

struct FTraversalDebugSnapshot
{
	TWeakObjectPtr<UWorld> World;
	TArray<FTraversalDebugCommand> Commands;
	int32 CandidateCommandStart = INDEX_NONE;
	bool bActive = false;
	bool bCaptureEnabled = true;
};

FTraversalDebugSnapshot TraversalSnapshot;

TAutoConsoleVariable<int32> CVarIMGTraversalDebugDraw(
	TEXT("IMG.Traversal.DebugDraw"),
	0,
	TEXT("Traversal debug draw level. 0=off, 1=success/active targets, 2=search/failures, 3=all probes."),
	ECVF_Cheat);

TAutoConsoleVariable<float> CVarIMGTraversalDebugDuration(
	TEXT("IMG.Traversal.DebugDuration"),
	2.f,
	TEXT("Lifetime in seconds for the final successful traversal snapshot. Process and active draws remain one-frame."),
	ECVF_Cheat);

float ResolveDuration(float Duration)
{
	return Duration >= 0.f ? Duration : FMath::Clamp(CVarIMGTraversalDebugDuration.GetValueOnGameThread(), 0.f, 30.f);
}

void RecordCommand(const UWorld* World, ETraversalDebugPrimitive Primitive,
	const FVector& Start, const FVector& End, const FColor& Color, int32 RequiredLevel,
	float ValueA = 0.f, float ValueB = 0.f, const FString& Text = FString())
{
	if (!TraversalSnapshot.bActive || !TraversalSnapshot.bCaptureEnabled
		|| TraversalSnapshot.World.Get() != World)
	{
		return;
	}
	FTraversalDebugCommand& Command = TraversalSnapshot.Commands.AddDefaulted_GetRef();
	Command.Primitive = Primitive;
	Command.Start = Start;
	Command.End = End;
	Command.Color = Color;
	Command.Text = Text;
	Command.RequiredLevel = RequiredLevel;
	Command.ValueA = ValueA;
	Command.ValueB = ValueB;
}

void ReplayTraversalSnapshot(float Duration)
{
	UWorld* World = TraversalSnapshot.World.Get();
	TArray<FTraversalDebugCommand> Commands = MoveTemp(TraversalSnapshot.Commands);
	TraversalSnapshot = FTraversalDebugSnapshot();
	if (!World)
	{
		return;
	}
	for (const FTraversalDebugCommand& Command : Commands)
	{
		switch (Command.Primitive)
		{
		case ETraversalDebugPrimitive::Line:
			IMGTraversalDebug::Line(World, Command.Start, Command.End, Command.Color,
				Command.RequiredLevel, Command.ValueA, Duration);
			break;
		case ETraversalDebugPrimitive::Arrow:
			IMGTraversalDebug::Arrow(World, Command.Start, Command.End, Command.Color,
				Command.RequiredLevel, Duration);
			break;
		case ETraversalDebugPrimitive::Point:
			IMGTraversalDebug::Point(World, Command.Start, Command.Color,
				Command.RequiredLevel, Command.ValueA, Duration);
			break;
		case ETraversalDebugPrimitive::Sphere:
			IMGTraversalDebug::Sphere(World, Command.Start, Command.ValueA, Command.Color,
				Command.RequiredLevel, Duration);
			break;
		case ETraversalDebugPrimitive::Capsule:
			IMGTraversalDebug::Capsule(World, Command.Start, Command.ValueA, Command.ValueB,
				Command.Color, Command.RequiredLevel, Duration);
			break;
		case ETraversalDebugPrimitive::String:
			IMGTraversalDebug::String(World, Command.Start, Command.Text, Command.Color,
				Command.RequiredLevel, Duration);
			break;
		default:
			break;
		}
	}
}
}

namespace IMGTraversalDebug
{
bool IsEnabled(int32 RequiredLevel)
{
	return CVarIMGTraversalDebugDraw.GetValueOnGameThread() >= RequiredLevel;
}

void BeginTraversalSnapshot(const UWorld* World)
{
	TraversalSnapshot = FTraversalDebugSnapshot();
	if (World && IsEnabled(1))
	{
		TraversalSnapshot.World = const_cast<UWorld*>(World);
		TraversalSnapshot.bActive = true;
	}
}

void SetTraversalSnapshotCaptureEnabled(bool bEnabled)
{
	if (TraversalSnapshot.bActive)
	{
		TraversalSnapshot.bCaptureEnabled = bEnabled;
	}
}

void BeginTraversalCandidateSnapshot()
{
	if (TraversalSnapshot.bActive)
	{
		TraversalSnapshot.CandidateCommandStart = TraversalSnapshot.Commands.Num();
	}
}

void RollbackTraversalCandidateSnapshot()
{
	if (TraversalSnapshot.bActive && TraversalSnapshot.CandidateCommandStart != INDEX_NONE)
	{
		TraversalSnapshot.Commands.SetNum(TraversalSnapshot.CandidateCommandStart);
		TraversalSnapshot.CandidateCommandStart = INDEX_NONE;
	}
}

void DiscardTraversalSnapshot()
{
	TraversalSnapshot = FTraversalDebugSnapshot();
}

void SuccessfulTraversal(const UWorld* World, const FIMGTraversalCandidate& Candidate,
	const FIMGTraversalAnimationSelection& Selection)
{
	if (!World || !IsEnabled(1))
	{
		return;
	}
	constexpr float FrameDuration = 0.f;
	const FVector Front = Candidate.FrontLedge.GetLocation();
	Point(World, Front, FColor::Yellow, 1, 12.f, FrameDuration);
	Arrow(World, Front, Front + Candidate.FrontLedgeNormal * 30.f, FColor::Magenta, 1, FrameDuration);
	Arrow(World, Front, Front + Candidate.FrontLedge.GetUnitAxis(EAxis::X) * 30.f,
		FColor::Yellow, 1, FrameDuration);
	if (Candidate.bHasBackLedge)
	{
		const FVector Back = Candidate.BackLedge.GetLocation();
		Point(World, Back, FColor::Orange, 1, 12.f, FrameDuration);
		Arrow(World, Back, Back + Candidate.BackLedgeNormal * 30.f, FColor::Orange, 1, FrameDuration);
		Line(World, Front, Back, FColor::Orange, 1, 1.f, FrameDuration);
	}
	if (Candidate.bHasBackFloor)
	{
		Point(World, Candidate.BackFloor.GetLocation(), FColor::Green, 1, 12.f, FrameDuration);
	}
	const FString Feasible = FString::Printf(TEXT("%s%s%s"),
		Candidate.bCanMantle ? TEXT("M") : TEXT("-"),
		Candidate.bCanVault ? TEXT("V") : TEXT("-"),
		Candidate.bCanHurdle ? TEXT("H") : TEXT("-"));
	String(World, Front + FVector::UpVector * 32.f,
		FString::Printf(TEXT("Traversal Success | %s | %s | Start %.2f Rate %.2f"),
			*StaticEnum<EIMGTraversalAction>()->GetNameStringByValue(static_cast<int64>(Selection.Action)),
			*GetNameSafe(Selection.Montage.Get()), Selection.StartTime, Selection.PlayRate),
		FColor::Green, 1, FrameDuration);
	String(World, Front + FVector::UpVector * 48.f,
		TEXT("Checks Geometry / Action / Animation / Warp / Montage PASS"),
		FColor::Green, 1, FrameDuration);
	String(World, Front + FVector::UpVector * 64.f,
		FString::Printf(TEXT("H %.0f D %.0f Clear %.0f/%.0f Drop %.0f Dist %.0f Ref %.0f [%s] Parts %d"),
			Candidate.ObstacleHeight, Candidate.ObstacleDepth, Candidate.HorizontalClearance,
			Candidate.VerticalClearance, Candidate.DropHeight, Candidate.DistanceToLedge,
			Candidate.ReferenceReachDistance, *Feasible, Candidate.ObstacleComponents.Num()),
		FColor::White, 1, FrameDuration);
	ReplayTraversalSnapshot(-1.f);
}

void Line(const UWorld* World, const FVector& Start, const FVector& End, const FColor& Color,
	int32 RequiredLevel, float Thickness, float Duration)
{
	if (World && IsEnabled(RequiredLevel))
	{
		DrawDebugLine(World, Start, End, Color, false, ResolveDuration(Duration), 0,
			FMath::Max(Thickness * DebugLineThicknessScale, 0.25f));
		RecordCommand(World, ETraversalDebugPrimitive::Line, Start, End, Color, RequiredLevel, Thickness);
	}
}

void Arrow(const UWorld* World, const FVector& Start, const FVector& End, const FColor& Color,
	int32 RequiredLevel, float Duration)
{
	if (World && IsEnabled(RequiredLevel))
	{
		DrawDebugDirectionalArrow(World, Start, End, 7.f, Color, false, ResolveDuration(Duration), 0, 0.6f);
		RecordCommand(World, ETraversalDebugPrimitive::Arrow, Start, End, Color, RequiredLevel);
	}
}

void Point(const UWorld* World, const FVector& Location, const FColor& Color,
	int32 RequiredLevel, float Size, float Duration)
{
	if (World && IsEnabled(RequiredLevel))
	{
		DrawDebugPoint(World, Location, FMath::Max(Size * DebugPointSizeScale, 3.f),
			Color, false, ResolveDuration(Duration));
		RecordCommand(World, ETraversalDebugPrimitive::Point, Location, FVector::ZeroVector,
			Color, RequiredLevel, Size);
	}
}

void Sphere(const UWorld* World, const FVector& Center, float Radius, const FColor& Color,
	int32 RequiredLevel, float Duration)
{
	if (World && IsEnabled(RequiredLevel))
	{
		DrawDebugSphere(World, Center, Radius * DebugSphereRadiusScale, 8, Color,
			false, ResolveDuration(Duration), 0, 0.5f);
		RecordCommand(World, ETraversalDebugPrimitive::Sphere, Center, FVector::ZeroVector,
			Color, RequiredLevel, Radius);
	}
}

void Capsule(const UWorld* World, const FVector& Center, float HalfHeight, float Radius, const FColor& Color,
	int32 RequiredLevel, float Duration)
{
	if (World && IsEnabled(RequiredLevel))
	{
		DrawDebugCapsule(World, Center, HalfHeight, Radius, FQuat::Identity, Color,
			false, ResolveDuration(Duration), 0, 0.5f);
		RecordCommand(World, ETraversalDebugPrimitive::Capsule, Center, FVector::ZeroVector,
			Color, RequiredLevel, HalfHeight, Radius);
	}
}

void String(const UWorld* World, const FVector& Location, const FString& Text, const FColor& Color,
	int32 RequiredLevel, float Duration)
{
	if (World && IsEnabled(RequiredLevel))
	{
		DrawDebugString(World, Location, Text, nullptr, Color, ResolveDuration(Duration), true, 0.8f);
		RecordCommand(World, ETraversalDebugPrimitive::String, Location, FVector::ZeroVector,
			Color, RequiredLevel, 0.f, 0.f, Text);
	}
}
}

#endif
