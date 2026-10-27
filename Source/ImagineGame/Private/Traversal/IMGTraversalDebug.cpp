#include "IMGTraversalDebug.h"

#if ENABLE_DRAW_DEBUG

#include "Animation/AnimMontage.h"
#include "Components/PrimitiveComponent.h"
#include "Debug/DebugDrawService.h"
#include "DrawDebugHelpers.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "IMGLogChannels.h"
#include "Traversal/IMGTraversalTypes.h"

namespace
{
constexpr float DebugLineThicknessScale = 0.5f;
constexpr float DebugPointSizeScale = 0.7f;

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
	FString DisplayText;
	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<AActor> Actor;
	FName TextKey;
	double ExpireTime = 0.0;
	uint64 FrameNumber = 0;
	int32 RequiredLevel = 1;
	IMGTraversalDebug::EStage Stage = IMGTraversalDebug::EStage::Execution;
	float ValueA = 0.f;
	float ValueB = 0.f;
};

struct FTraversalDebugSnapshot
{
	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<AActor> Actor;
	TArray<FTraversalDebugCommand> Commands;
	int32 CandidateCommandStart = INDEX_NONE;
	bool bActive = false;
	bool bCaptureEnabled = true;
};

FTraversalDebugSnapshot TraversalSnapshot;
TArray<FTraversalDebugCommand> TextLabels;
FDelegateHandle TextDrawHandle;

TAutoConsoleVariable<int32> CVarIMGTraversalDebugDraw(
	TEXT("IMG.Traversal.DebugDraw"),
	0,
	TEXT("Traversal debug draw level. 0=off, 1=candidate outputs/active targets, 2=search/failures, 3=all probes."),
	ECVF_Cheat);

TAutoConsoleVariable<float> CVarIMGTraversalDebugDuration(
	TEXT("IMG.Traversal.DebugDuration"),
	2.f,
	TEXT("Lifetime in seconds for the final successful traversal snapshot. Active targets remain one-frame."),
	ECVF_Cheat);

TAutoConsoleVariable<int32> CVarIMGTraversalDebugStages(
	TEXT("IMG.Traversal.DebugStages"), 63,
	TEXT("Stage mask: 1=search, 2=intersections, 4=handhold compute, 8=volume validation, 16=output, 32=execution."),
	ECVF_Cheat);

TAutoConsoleVariable<float> CVarIMGTraversalDebugProcessDuration(
	TEXT("IMG.Traversal.DebugProcessDuration"), 0.f,
	TEXT("Process/failure draw lifetime, 0=one frame. Set 3 for GTA-style probe history; active targets stay one-frame."),
	ECVF_Cheat);

TAutoConsoleVariable<int32> CVarIMGTraversalDebugLog(
	TEXT("IMG.Traversal.DebugLog"), 0,
	TEXT("Requires DebugDraw. 0=off, 1=candidate outputs, 2=all enabled stage labels including rejections."),
	ECVF_Cheat);

TAutoConsoleVariable<float> CVarIMGTraversalDebugLineScale(
	TEXT("IMG.Traversal.DebugLineScale"), 1.f,
	TEXT("Visual scale for traversal debug stroke widths and arrowheads. 1 restores the original size."),
	ECVF_Cheat);

TAutoConsoleVariable<float> CVarIMGTraversalDebugSphereScale(
	TEXT("IMG.Traversal.DebugSphereScale"), 1.f / 3.f,
	TEXT("Visual scale for traversal debug sphere radii and points. 1 shows actual probe radii; queries and capsule bounds are unchanged."),
	ECVF_Cheat);

TAutoConsoleVariable<float> CVarIMGTraversalDebugTextScale(
	TEXT("IMG.Traversal.DebugTextScale"), 0.65f,
	TEXT("Traversal label font scale. Labels are wrapped, deduplicated and placed without screen-space overlap."),
	ECVF_Cheat);

float GetLineScale()
{
	return FMath::Clamp(CVarIMGTraversalDebugLineScale.GetValueOnGameThread(), 0.01f, 4.f);
}

float GetSphereScale()
{
	return FMath::Clamp(CVarIMGTraversalDebugSphereScale.GetValueOnGameThread(), 0.01f, 4.f);
}

float ResolveDuration(float Duration)
{
	if (Duration >= 0.f)
	{
		return Duration;
	}
	return FMath::Clamp(Duration == -1.f ? CVarIMGTraversalDebugDuration.GetValueOnGameThread()
		: CVarIMGTraversalDebugProcessDuration.GetValueOnGameThread(), 0.f, 30.f);
}

bool IsTextExpired(const FTraversalDebugCommand& Label)
{
	const UWorld* World = Label.World.Get();
	return !World || Label.Actor.IsStale() || (Label.ValueA <= 0.f
		? Label.FrameNumber != GFrameCounter : Label.ExpireTime <= World->GetTimeSeconds());
}

FString WrapText(const FString& Text)
{
	constexpr int32 MaxColumns = 76;
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, false);
	FString Result;
	for (FString& TextLine : Lines)
	{
		while (TextLine.Len() > MaxColumns)
		{
			int32 Break = INDEX_NONE;
			TextLine.Left(MaxColumns).FindLastChar(TEXT(' '), Break);
			const bool bBreakAtSpace = Break >= MaxColumns / 2;
			Break = bBreakAtSpace ? Break : MaxColumns;
			Result += TextLine.Left(Break) + TEXT("\n");
			TextLine.RightChopInline(Break + (bBreakAtSpace ? 1 : 0));
		}
		Result += TextLine + TEXT("\n");
	}
	Result.RemoveFromEnd(TEXT("\n"));
	return Result;
}

void DrawTextLabels(UCanvas* Canvas, APlayerController* PlayerController)
{
	TextLabels.RemoveAll([](const FTraversalDebugCommand& Label) { return IsTextExpired(Label); });
	if (!IMGTraversalDebug::IsEnabled(1))
	{
		TextLabels.Reset();
		return;
	}
	if (!Canvas || !Canvas->Canvas || !PlayerController || !GEngine || !GEngine->GetSmallFont())
	{
		return;
	}
	UWorld* World = PlayerController->GetWorld();
	TArray<int32> Order;
	for (int32 Index = 0; Index < TextLabels.Num(); ++Index)
	{
		const FTraversalDebugCommand& Label = TextLabels[Index];
		if (Label.World.Get() == World && IMGTraversalDebug::IsEnabled(Label.RequiredLevel) &&
			IMGTraversalDebug::IsStageEnabled(Label.Stage))
		{
			Order.Add(Index);
		}
	}
	Order.StableSort([](int32 A, int32 B)
	{
		if (TextLabels[A].RequiredLevel != TextLabels[B].RequiredLevel)
		{
			return TextLabels[A].RequiredLevel < TextLabels[B].RequiredLevel;
		}
		return !TextLabels[A].TextKey.IsNone() && TextLabels[B].TextKey.IsNone();
	});
	constexpr float Margin = 8.f;
	constexpr float Gap = 4.f;
	const float FontScale = FMath::Clamp(CVarIMGTraversalDebugTextScale.GetValueOnGameThread(), 0.25f, 1.5f);
	const FVector2D ViewSize(Canvas->SizeX, Canvas->SizeY);
	const FVector2D LayoutSize = ViewSize - FVector2D(0.f, 24.f);
	FVector CameraLocation;
	FRotator CameraRotation;
	PlayerController->GetPlayerViewPoint(CameraLocation, CameraRotation);
	TArray<FBox2D> Occupied;
	int32 HiddenLabels = 0;
	for (int32 Index : Order)
	{
		const FTraversalDebugCommand& Label = TextLabels[Index];
		if (FVector::DotProduct(Label.Start - CameraLocation, CameraRotation.Vector()) <= 0.f)
		{
			continue;
		}
		const FVector Projected = Canvas->Project(Label.Start);
		const FVector2D Anchor(Projected.X, Projected.Y);
		float Width = 0.f;
		float Height = 0.f;
		Canvas->StrLen(GEngine->GetSmallFont(), Label.DisplayText, Width, Height);
		const FVector2D Size(FMath::Max(Width * FontScale, 1.f), FMath::Max(Height * FontScale, 1.f));
		if (Size.X + Margin * 2.f > LayoutSize.X || Size.Y + Margin * 2.f > LayoutSize.Y ||
			Anchor.X < 0.f || Anchor.X > ViewSize.X || Anchor.Y < 0.f || Anchor.Y > ViewSize.Y)
		{
			++HiddenLabels;
			continue;
		}
		const FVector2D Desired(
			FMath::Clamp(Anchor.X + Gap * 2.f, static_cast<double>(Margin), ViewSize.X - Size.X - Margin),
			FMath::Clamp(Anchor.Y - Size.Y - Gap, static_cast<double>(Margin), LayoutSize.Y - Size.Y - Margin));
		FVector2D Position = Desired;
		bool bPlaced = false;
		for (int32 Column = 0; Column < 5 && !bPlaced; ++Column)
		{
			const int32 Direction = Column % 2 == 0 ? 1 : -1;
			Position.X = FMath::Clamp(Desired.X + Direction * ((Column + 1) / 2) * (Size.X + Gap),
				static_cast<double>(Margin), ViewSize.X - Size.X - Margin);
			Position.Y = Desired.Y;
			for (int32 Attempt = 0; Attempt <= Occupied.Num(); ++Attempt)
			{
				const FBox2D Bounds(Position, Position + Size + FVector2D(Gap, Gap));
				const FBox2D* Collision = Occupied.FindByPredicate(
					[&Bounds](const FBox2D& Existing) { return Existing.Intersect(Bounds); });
				if (!Collision)
				{
					Occupied.Add(Bounds);
					bPlaced = true;
					break;
				}
				Position.Y = Collision->Max.Y + Gap;
				if (Position.Y + Size.Y > LayoutSize.Y - Margin)
				{
					Position.Y = Margin;
				}
			}
		}
		if (!bPlaced)
		{
			++HiddenLabels;
			continue;
		}
		if (FVector2D::Distance(Position, Anchor) > 20.f)
		{
			FLinearColor LeaderColor(Label.Color);
			LeaderColor.A = 0.4f;
			Canvas->K2_DrawLine(Anchor, Position, 0.5f * GetLineScale(), LeaderColor);
		}
		Canvas->K2_DrawText(GEngine->GetSmallFont(), Label.DisplayText, Position,
			FVector2D(FontScale, FontScale), FLinearColor(Label.Color));
	}
	if (HiddenLabels > 0)
	{
		Canvas->K2_DrawText(GEngine->GetSmallFont(), FString::Printf(
			TEXT("Traversal: %d labels outside view/full. Filter DebugStages or use DebugLog 2."), HiddenLabels),
			FVector2D(Margin, ViewSize.Y - 20.f), FVector2D(FontScale, FontScale), FLinearColor::White);
	}
}

void QueueText(const UWorld* World, const FVector& Location, const FString& Text, const FColor& Color,
	int32 RequiredLevel, float Duration, IMGTraversalDebug::EStage Stage, FName Key, const AActor* Actor)
{
	TextLabels.RemoveAll([](const FTraversalDebugCommand& Label) { return IsTextExpired(Label); });
	FTraversalDebugCommand* Entry = TextLabels.FindByPredicate([&](const FTraversalDebugCommand& Label)
	{
		return Label.World.Get() == World && Label.Actor.Get() == Actor && Label.Stage == Stage &&
			(!Key.IsNone() ? Label.TextKey == Key : Label.TextKey.IsNone() && Label.Text == Text && Label.Start.Equals(Location, 1.f));
	});
	if (!Entry)
	{
		constexpr int32 MaxLabels = 1024;
		if (TextLabels.Num() >= MaxLabels)
		{
			int32 RemoveIndex = TextLabels.IndexOfByPredicate([](const FTraversalDebugCommand& Label)
				{ return Label.RequiredLevel >= 3 && Label.TextKey.IsNone(); });
			TextLabels.RemoveAt(RemoveIndex != INDEX_NONE ? RemoveIndex : 0);
		}
		Entry = &TextLabels.AddDefaulted_GetRef();
	}
	Entry->Primitive = ETraversalDebugPrimitive::String;
	Entry->World = const_cast<UWorld*>(World);
	Entry->Actor = const_cast<AActor*>(Actor);
	Entry->TextKey = Key;
	Entry->Start = Location;
	Entry->Text = Text;
	Entry->DisplayText = WrapText(Text);
	Entry->Color = Color;
	Entry->RequiredLevel = RequiredLevel;
	Entry->Stage = Stage;
	Entry->ValueA = FMath::Max(Entry->ValueA, ResolveDuration(Duration));
	Entry->ExpireTime = World->GetTimeSeconds() + Entry->ValueA;
	Entry->FrameNumber = GFrameCounter;
	if (!TextDrawHandle.IsValid())
	{
		TextDrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateStatic(&DrawTextLabels));
	}
}

void RecordCommand(const UWorld* World, ETraversalDebugPrimitive Primitive,
	const FVector& Start, const FVector& End, const FColor& Color, int32 RequiredLevel,
	float ValueA = 0.f, float ValueB = 0.f, const FString& Text = FString(),
	IMGTraversalDebug::EStage Stage = IMGTraversalDebug::EStage::Execution, FName Key = NAME_None, const AActor* Actor = nullptr)
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
	Command.Actor = const_cast<AActor*>(Actor ? Actor : TraversalSnapshot.Actor.Get());
	Command.TextKey = Key;
	Command.RequiredLevel = RequiredLevel;
	Command.Stage = Stage;
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
				Command.RequiredLevel, Command.ValueA, Duration, Command.Stage);
			break;
		case ETraversalDebugPrimitive::Arrow:
			IMGTraversalDebug::Arrow(World, Command.Start, Command.End, Command.Color,
				Command.RequiredLevel, Duration, Command.Stage);
			break;
		case ETraversalDebugPrimitive::Point:
			IMGTraversalDebug::Point(World, Command.Start, Command.Color,
				Command.RequiredLevel, Command.ValueA, Duration, Command.Stage);
			break;
		case ETraversalDebugPrimitive::Sphere:
			IMGTraversalDebug::Sphere(World, Command.Start, Command.ValueA, Command.Color,
				Command.RequiredLevel, Duration, Command.Stage);
			break;
		case ETraversalDebugPrimitive::Capsule:
			IMGTraversalDebug::Capsule(World, Command.Start, Command.ValueA, Command.ValueB,
				Command.Color, Command.RequiredLevel, Duration, Command.Stage);
			break;
		case ETraversalDebugPrimitive::String:
			IMGTraversalDebug::String(World, Command.Start, Command.Text, Command.Color,
				Command.RequiredLevel, Duration, Command.Stage, Command.TextKey, Command.Actor.Get());
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

bool IsStageEnabled(EStage Stage)
{
	return (CVarIMGTraversalDebugStages.GetValueOnGameThread() & static_cast<int32>(Stage)) != 0;
}

void Shutdown()
{
	if (TextDrawHandle.IsValid())
	{
		UDebugDrawService::Unregister(TextDrawHandle);
		TextDrawHandle.Reset();
	}
	TextLabels.Reset();
	TraversalSnapshot = FTraversalDebugSnapshot();
}

void BeginTraversalSnapshot(const AActor* Actor, bool bReset)
{
	if (!bReset && Actor && TraversalSnapshot.Actor.Get() == Actor)
	{
		return;
	}
	TraversalSnapshot = FTraversalDebugSnapshot();
	if (Actor && Actor->GetWorld() && IsEnabled(1))
	{
		TraversalSnapshot.World = Actor->GetWorld();
		TraversalSnapshot.Actor = const_cast<AActor*>(Actor);
		TraversalSnapshot.bActive = true;
		String(Actor->GetWorld(), Actor->GetActorLocation() + FVector::UpVector * 120.f,
			FString::Printf(TEXT("%s | %s | NetMode %d | t %.3f"), *GetNameSafe(Actor),
				*UEnum::GetValueAsString(Actor->GetLocalRole()), static_cast<int32>(Actor->GetNetMode()),
				Actor->GetWorld()->GetTimeSeconds()), FColor::White, 2, -2.f, EStage::Output, FName(TEXT("Actor")), Actor);
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

void DiscardTraversalSnapshot(const AActor* Actor)
{
	if (!Actor || TraversalSnapshot.Actor.Get() == Actor)
	{
		TraversalSnapshot = FTraversalDebugSnapshot();
	}
}

void CandidateOutput(const UWorld* World, const FIMGTraversalCandidate& Candidate, const FString& Label)
{
	if (!World || !IsEnabled(1) || !IsStageEnabled(EStage::Output))
	{
		return;
	}
	const FVector Front = Candidate.FrontLedge.GetLocation();
	Point(World, Front, FColor::Yellow, 1, 12.f, -2.f, EStage::Output);
	Arrow(World, Front, Front + Candidate.FrontLedgeNormal * 30.f, FColor::Magenta, 1, -2.f, EStage::Output);
	Arrow(World, Front, Front + Candidate.FrontLedge.GetUnitAxis(EAxis::X) * 30.f,
		FColor::Yellow, 1, -2.f, EStage::Output);
	const FVector HeightStart = Front + Candidate.FrontLedgeNormal.GetSafeNormal2D() * 5.f;
	Measure(World, HeightStart, HeightStart - FVector::UpVector * Candidate.ObstacleHeight,
		TEXT("Height"), Candidate.ObstacleHeight, FColor::Yellow);
	if (Candidate.bHasBackLedge)
	{
		const FVector Back = Candidate.BackLedge.GetLocation();
		Point(World, Back, FColor::Orange, 1, 12.f, -2.f, EStage::Output);
		Arrow(World, Back, Back + Candidate.BackLedgeNormal * 30.f, FColor::Orange, 1, -2.f, EStage::Output);
		Measure(World, Front + FVector::UpVector * 5.f, Back + FVector::UpVector * 5.f,
			TEXT("Depth XY"), Candidate.ObstacleDepth, FColor::Orange);
	}
	if (Candidate.bHasBackFloor)
	{
		const FVector Floor = Candidate.BackFloor.GetLocation();
		Point(World, Floor, FColor::Green, 1, 12.f, -2.f, EStage::Output);
		Measure(World, FVector(Floor.X, Floor.Y, Front.Z), Floor,
			TEXT("Drop (front)"), Candidate.DropHeight, FColor::Green);
		if (Candidate.bHasBackLedge)
		{
			const FVector Back = Candidate.BackLedge.GetLocation();
			Measure(World, Back, FVector(Back.X, Back.Y, Floor.Z), TEXT("Rear height"),
				Candidate.BackLedgeHeight, FColor::Orange);
		}
	}
	Measure(World, Front, Front + FVector::UpVector * Candidate.VerticalClearance,
		TEXT("VClear probe/cap"), Candidate.VerticalClearance, FColor::Yellow);
	Measure(World, Front, Front + Candidate.FrontLedge.GetUnitAxis(EAxis::X).GetSafeNormal2D() * Candidate.HorizontalClearance,
		TEXT("HClear probe/cap"), Candidate.HorizontalClearance, FColor::Magenta);
	const FString Feasible = FString::Printf(TEXT("%s%s%s"),
		Candidate.bCanMantle ? TEXT("M") : TEXT("-"),
		Candidate.bCanVault ? TEXT("V") : TEXT("-"),
		Candidate.bCanHurdle ? TEXT("H") : TEXT("-"));
	FString Summary = Label;
	Summary += FString::Printf(TEXT("\nHeight %.1f  Depth XY %.1f  Drop %.1f  Rear height %.1f")
		TEXT("\nClearance H/V %.1f / %.1f  Dist3D/XY %.1f / %.1f [%s] Parts %d"),
		Candidate.ObstacleHeight, Candidate.ObstacleDepth, Candidate.DropHeight, Candidate.BackLedgeHeight,
		Candidate.HorizontalClearance, Candidate.VerticalClearance, Candidate.DistanceToLedge,
		Candidate.PlanarDistanceToLedge, *Feasible, Candidate.ObstacleComponents.Num());
	if (IsEnabled(2))
	{
		Summary += FString::Printf(TEXT("\nRecommended %s | Source %s | Ground %s"),
			*StaticEnum<EIMGTraversalAction>()->GetNameStringByValue(static_cast<int64>(Candidate.RecommendedAction)),
			*StaticEnum<EIMGTraversalRequestSource>()->GetNameStringByValue(static_cast<int64>(Candidate.RequestSource)),
			*Candidate.GroundLocation.ToCompactString());
		Summary += FString::Printf(TEXT("\nEdge span %.1f  MidClear %.1f  HighOpen %d")
			TEXT("\nClimb/Slope %.1f / %.1f  Back/Floor %d / %d"),
			Candidate.FrontEdgeVerticalSpan, Candidate.MidHeightApproachClearance,
			Candidate.bHasHighOpenApproach, Candidate.ClimbAngle, Candidate.GroundSlopeAngle,
			Candidate.bHasBackLedge, Candidate.bHasBackFloor);
	}
	const FVector SummaryAnchor = Front + Candidate.FrontLedgeNormal.GetSafeNormal2D() * 40.f + FVector::UpVector * 100.f;
	String(World, SummaryAnchor, Summary, FColor::Green, 1, -2.f, EStage::Output, FName(TEXT("CandidateOutput")));
	if (CVarIMGTraversalDebugLog.GetValueOnGameThread() >= 1)
	{
		UE_LOG(LogIMG, Log, TEXT("[Traversal][%s] %s | H=%.2f Depth=%.2f Drop=%.2f HClear=%.2f VClear=%.2f | Front=%s Back=%s Floor=%s [Back=%d Floor=%d]"),
			*GetNameSafe(TraversalSnapshot.Actor.Get()), *Label, Candidate.ObstacleHeight, Candidate.ObstacleDepth,
			Candidate.DropHeight, Candidate.HorizontalClearance, Candidate.VerticalClearance,
			*Front.ToString(), *Candidate.BackLedge.GetLocation().ToString(), *Candidate.BackFloor.GetLocation().ToString(),
			Candidate.bHasBackLedge, Candidate.bHasBackFloor);
		const AActor* Actor = TraversalSnapshot.Actor.Get();
		UE_LOG(LogIMG, Log, TEXT("[Traversal][%s] Role=%d NetMode=%d | FrontNormal=%s Into=%s BackNormal=%s GroundNormal=%s | Dist3D=%.2f XY=%.2f Climb=%.2f Slope=%.2f"),
			*GetNameSafe(Actor), Actor ? static_cast<int32>(Actor->GetLocalRole()) : -1,
			Actor ? static_cast<int32>(Actor->GetNetMode()) : -1, *Candidate.FrontLedgeNormal.ToString(),
			*Candidate.FrontLedge.GetUnitAxis(EAxis::X).ToString(), *Candidate.BackLedgeNormal.ToString(),
			*Candidate.GroundNormal.ToString(), Candidate.DistanceToLedge, Candidate.PlanarDistanceToLedge,
			Candidate.ClimbAngle, Candidate.GroundSlopeAngle);
	}
}

void SuccessfulTraversal(const AActor* Actor, const FIMGTraversalCandidate& Candidate,
	const FIMGTraversalAnimationSelection& Selection)
{
	if (!Actor || !IsEnabled(1))
	{
		return;
	}
	if (TraversalSnapshot.Actor.Get() != Actor)
	{
		BeginTraversalSnapshot(Actor);
	}
	CandidateOutput(Actor->GetWorld(), Candidate,
		FString::Printf(TEXT("Execution | %s | %s | Start %.2f Rate %.2f"),
			*StaticEnum<EIMGTraversalAction>()->GetNameStringByValue(static_cast<int64>(Selection.Action)),
			*GetNameSafe(Selection.Montage.Get()), Selection.StartTime, Selection.PlayRate));
	ReplayTraversalSnapshot(-1.f);
}

void Intersection(const UWorld* World, const FHitResult& Hit, bool bAccepted, const FString& Label,
	EStage Stage, int32 RequiredLevel)
{
	if (!World || !IsEnabled(RequiredLevel) || !IsStageEnabled(Stage))
	{
		return;
	}
	const FColor Color = bAccepted ? FColor::Cyan : FColor::Red;
	Sphere(World, Hit.ImpactPoint, bAccepted ? 1.25f : 2.5f, Color, RequiredLevel, -2.f, Stage);
	Arrow(World, Hit.ImpactPoint, Hit.ImpactPoint + Hit.ImpactNormal * 10.f, Color, RequiredLevel, -2.f, Stage);
	String(World, Hit.ImpactPoint + FVector::UpVector * 4.f,
		FString::Printf(TEXT("%s | %s | %s%s"), *Label, *GetNameSafe(Hit.GetComponent()),
			bAccepted ? TEXT("accepted") : TEXT("rejected"), Hit.bStartPenetrating ? TEXT(" PENETRATING") : TEXT("")),
		Color, RequiredLevel, -2.f, Stage);
}

void SweepSphere(const UWorld* World, const FVector& Start, const FVector& End, float Radius,
	const FHitResult* Hit, const FString& Label, const FColor& Color, EStage Stage, int32 RequiredLevel)
{
	if (!World || !IsEnabled(RequiredLevel) || !IsStageEnabled(Stage))
	{
		return;
	}
	// Endpoints describe the requested sweep; Location is the stopped sphere center, ImpactPoint the surface.
	Line(World, Start, End, Color, RequiredLevel, 1.f, -2.f, Stage);
	Sphere(World, Start, Radius, Color, RequiredLevel, -2.f, Stage);
	Sphere(World, End, Radius, Color, RequiredLevel, -2.f, Stage);
	String(World, Start + FVector::UpVector * (Radius + 4.f),
		FString::Printf(TEXT("%s | R %.1f L %.1f | %s"), *Label, Radius, FVector::Distance(Start, End),
			Hit ? TEXT("hit") : TEXT("clear")), Color, RequiredLevel, -2.f, Stage);
	if (Hit)
	{
		const FVector Center = Hit->bStartPenetrating ? Start : Hit->Location;
		Sphere(World, Center, Radius, FColor::Red, RequiredLevel, -2.f, Stage);
		Line(World, Center, Hit->ImpactPoint, FColor::White, RequiredLevel, 1.f, -2.f, Stage);
		Point(World, Hit->ImpactPoint, Color, RequiredLevel, 7.f, -2.f, Stage);
		Arrow(World, Hit->ImpactPoint, Hit->ImpactPoint + Hit->ImpactNormal * 10.f, Color, RequiredLevel, -2.f, Stage);
		String(World, Hit->ImpactPoint, FString::Printf(TEXT("%s contact | %s%s"), *Label,
			*GetNameSafe(Hit->GetComponent()), Hit->bStartPenetrating ? TEXT(" PENETRATING") : TEXT("")),
			Color, RequiredLevel, -2.f, Stage);
	}
}

void Measure(const UWorld* World, const FVector& Start, const FVector& End, const FString& Label,
	float Value, const FColor& Color)
{
	Line(World, Start, End, Color, 1, 1.f, -2.f, EStage::Output);
	Sphere(World, Start, 2.f, Color, 1, -2.f, EStage::Output);
	Sphere(World, End, 2.f, Color, 1, -2.f, EStage::Output);
	String(World, (Start + End) * 0.5f, FString::Printf(TEXT("%s %.1f cm"), *Label, Value),
		Color, 1, -2.f, EStage::Output);
}

void Line(const UWorld* World, const FVector& Start, const FVector& End, const FColor& Color,
	int32 RequiredLevel, float Thickness, float Duration, EStage Stage)
{
	if (World && IsEnabled(RequiredLevel) && IsStageEnabled(Stage))
	{
		DrawDebugLine(World, Start, End, Color, false, ResolveDuration(Duration), 0,
			FMath::Max(Thickness * DebugLineThicknessScale, 0.25f) * GetLineScale());
		if (Duration == -2.f)
		{
			RecordCommand(World, ETraversalDebugPrimitive::Line, Start, End, Color, RequiredLevel, Thickness, 0.f, FString(), Stage);
		}
	}
}

void Arrow(const UWorld* World, const FVector& Start, const FVector& End, const FColor& Color,
	int32 RequiredLevel, float Duration, EStage Stage)
{
	if (World && IsEnabled(RequiredLevel) && IsStageEnabled(Stage))
	{
		const float Scale = GetLineScale();
		DrawDebugDirectionalArrow(World, Start, End, 7.f * Scale, Color, false, ResolveDuration(Duration), 0, 0.6f * Scale);
		if (Duration == -2.f)
		{
			RecordCommand(World, ETraversalDebugPrimitive::Arrow, Start, End, Color, RequiredLevel, 0.f, 0.f, FString(), Stage);
		}
	}
}

void Point(const UWorld* World, const FVector& Location, const FColor& Color,
	int32 RequiredLevel, float Size, float Duration, EStage Stage)
{
	if (World && IsEnabled(RequiredLevel) && IsStageEnabled(Stage))
	{
		DrawDebugPoint(World, Location, FMath::Max(Size * DebugPointSizeScale, 3.f) * GetSphereScale(),
			Color, false, ResolveDuration(Duration));
		if (Duration == -2.f)
		{
			RecordCommand(World, ETraversalDebugPrimitive::Point, Location, FVector::ZeroVector,
				Color, RequiredLevel, Size, 0.f, FString(), Stage);
		}
	}
}

void Sphere(const UWorld* World, const FVector& Center, float Radius, const FColor& Color,
	int32 RequiredLevel, float Duration, EStage Stage)
{
	if (World && IsEnabled(RequiredLevel) && IsStageEnabled(Stage))
	{
		DrawDebugSphere(World, Center, Radius * GetSphereScale(), 12, Color,
			false, ResolveDuration(Duration), 0, 0.5f * GetLineScale());
		if (Duration == -2.f)
		{
			RecordCommand(World, ETraversalDebugPrimitive::Sphere, Center, FVector::ZeroVector,
				Color, RequiredLevel, Radius, 0.f, FString(), Stage);
		}
	}
}

void Capsule(const UWorld* World, const FVector& Center, float HalfHeight, float Radius, const FColor& Color,
	int32 RequiredLevel, float Duration, EStage Stage)
{
	if (World && IsEnabled(RequiredLevel) && IsStageEnabled(Stage))
	{
		DrawDebugCapsule(World, Center, HalfHeight, Radius, FQuat::Identity, Color,
			false, ResolveDuration(Duration), 0, 0.5f * GetLineScale());
		if (Duration == -2.f)
		{
			RecordCommand(World, ETraversalDebugPrimitive::Capsule, Center, FVector::ZeroVector,
				Color, RequiredLevel, HalfHeight, Radius, FString(), Stage);
		}
	}
}

void String(const UWorld* World, const FVector& Location, const FString& Text, const FColor& Color,
	int32 RequiredLevel, float Duration, EStage Stage, FName Key, const AActor* Owner)
{
	if (World && IsEnabled(RequiredLevel) && IsStageEnabled(Stage))
	{
		const AActor* Actor = Owner ? Owner : TraversalSnapshot.World.Get() == World ? TraversalSnapshot.Actor.Get() : nullptr;
		if (World->GetNetMode() == NM_DedicatedServer)
		{
			DrawDebugString(World, Location, Text, nullptr, Color, ResolveDuration(Duration), true,
				FMath::Clamp(CVarIMGTraversalDebugTextScale.GetValueOnGameThread(), 0.25f, 1.5f));
		}
		else
		{
			QueueText(World, Location, Text, Color, RequiredLevel, Duration, Stage, Key, Actor);
		}
		if (Duration == -2.f)
		{
			RecordCommand(World, ETraversalDebugPrimitive::String, Location, FVector::ZeroVector,
				Color, RequiredLevel, 0.f, 0.f, Text, Stage, Key, Actor);
		}
		if (Duration == -2.f && CVarIMGTraversalDebugLog.GetValueOnGameThread() >= 2)
		{
			UE_LOG(LogIMG, Log, TEXT("[Traversal][%s][Stage %d] %s"),
				*GetNameSafe(TraversalSnapshot.Actor.Get()), static_cast<int32>(Stage), *Text);
		}
	}
}
}

#endif
