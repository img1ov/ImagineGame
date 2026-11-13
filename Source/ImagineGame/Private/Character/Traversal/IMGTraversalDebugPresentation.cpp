#include "Character/Traversal/IMGTraversalDebugTypes.h"
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#include "Components/LineBatchComponent.h"
#include "DrawDebugLibrary.h"
#include "Camera/CameraTypes.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace
{
constexpr uint32 TraversalBatchId = 0x494D4754;
constexpr UWorld::ELineBatcherType BatcherTypes[] = {
    UWorld::ELineBatcherType::World, UWorld::ELineBatcherType::Foreground};
TAutoConsoleVariable<bool> Show(TEXT("IMG.Traversal.DebugDraw"), false,
    TEXT("Global Traversal geometry show/hide, including Rewind. Never "
         "affects recording."),
    ECVF_Cheat);
TAutoConsoleVariable<bool> Graph(TEXT("IMG.Traversal.DebugGraph"), false,
    TEXT("Show/hide Traversal graphs in the game "
         "and Rewind. Never affects recording."),
    ECVF_Cheat);
void WithOwnedBatch(UWorld* World, TFunctionRef<void()> Render)
{
	int32 LineStarts[2] = {}, PointStarts[2] = {};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(BatcherTypes); ++Index)
	{
		if (const auto* Batcher = World->GetLineBatcher(BatcherTypes[Index]))
		{
			LineStarts[Index] = Batcher->BatchedLines.Num();
			PointStarts[Index] = Batcher->BatchedPoints.Num();
		}
	}
	Render();
	// DrawDebugLibrary appends synchronously on the game thread; claim only our
	// new primitives.
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(BatcherTypes); ++Index)
	{
		if (auto* Batcher = World->GetLineBatcher(BatcherTypes[Index]))
		{
			for (int32 Line = LineStarts[Index]; Line < Batcher->BatchedLines.Num(); ++Line)
			{
				Batcher->BatchedLines[Line].BatchID = TraversalBatchId;
			}
			for (int32 Point = PointStarts[Index]; Point < Batcher->BatchedPoints.Num(); ++Point)
			{
				Batcher->BatchedPoints[Point].BatchID = TraversalBatchId;
			}
		}
	}
}
} // namespace
bool IMGTraversalDebugPresentation::IsShown()
{
	return Show.GetValueOnGameThread();
}
bool IMGTraversalDebugPresentation::IsGraphShown()
{
	return Graph.GetValueOnGameThread();
}
void IMGTraversalDebugPresentation::Clear(UWorld* World)
{
	if (!World)
	{
		return;
	}
	for (const auto Type : BatcherTypes)
	{
		if (auto* Batcher = World->GetLineBatcher(Type))
		{
			Batcher->ClearBatch(TraversalBatchId);
		}
	}
}
void IMGTraversalDebugPresentation::Draw(UWorld* World, const FIMGTraversalDebugFrame& Frame)
{
	if (!World || !IsShown())
	{
		return;
	}
	WithOwnedBatch(World, [&]() {
		const FDebugDrawer Drawer(World);
		for (const FIMGTraversalDebugShape& Shape : Frame.Geometry)
		{
			const FDrawDebugLineStyle Style(FLinearColor(Shape.Color), 0.f);
			switch (Shape.Primitive)
			{
				case EIMGTraversalDebugPrimitive::Line:
					UDrawDebugLibrary::DrawDebugLine(Drawer, Shape.Start, Shape.End, Style);
					break;
				case EIMGTraversalDebugPrimitive::Arrow:
					UDrawDebugLibrary::DrawDebugArrow(Drawer, Shape.Start, Shape.End, Style);
					break;
				case EIMGTraversalDebugPrimitive::Point:
					UDrawDebugLibrary::DrawDebugPoint(Drawer, Shape.Start,
					    FDrawDebugPointStyle(FLinearColor(Shape.Color), FMath::Min(Shape.Size, 3.f)));
					break;
				case EIMGTraversalDebugPrimitive::Sphere:
					UDrawDebugLibrary::DrawDebugSphere(
					    Drawer, Shape.Start, FRotator::ZeroRotator, Style, true, Shape.Size, 12);
					break;
				case EIMGTraversalDebugPrimitive::Capsule:
					UDrawDebugLibrary::DrawDebugCapsule(Drawer, Shape.Start, FRotator::ZeroRotator, Style,
					    true, Shape.Radius, FMath::Max(0.f, Shape.Size - Shape.Radius), 12);
					break;
			}
		}
	});
}

void IMGTraversalDebugPresentation::DrawGraphs(UWorld* World, const FMinimalViewInfo& View,
    TConstArrayView<FIMGTraversalDebugFrame> Frames, double CursorTime)
{
	if (!World || !IsGraphShown() || View.Location.ContainsNaN() || View.Rotation.ContainsNaN())
	{
		return;
	}
	WithOwnedBatch(World, [&]() {
		const FDebugDrawer Drawer(World);
		const FRotator Rotation = View.Rotation;
		const FVector Forward = Rotation.Vector();
		const FVector Right = Rotation.RotateVector(FVector::RightVector);
		const FVector Up = Rotation.RotateVector(FVector::UpVector);
		constexpr float Distance = 150.f;
		const float FOV = FMath::IsFinite(View.FOV) ? FMath::Clamp(View.FOV, 30.f, 140.f) : 90.f;
		const float Aspect =
		    FMath::IsFinite(View.AspectRatio) && View.AspectRatio > 0.f ? View.AspectRatio : 16.f / 9.f;
		const float HalfWidth = Distance * FMath::Tan(FMath::DegreesToRadians(FOV * .5f));
		const float HalfHeight = HalfWidth / Aspect;
		const float Width = HalfWidth * .28f, Height = HalfHeight * .26f;
		const FVector Anchor =
		    View.Location + Forward * Distance + Right * (HalfWidth * .08f) + Up * (HalfHeight * .14f);
		FDrawDebugStringSettings TextSettings;
		TextSettings.Height = Height * .09f;
		const FDrawDebugLineStyle White(FLinearColor::White), Border(FLinearColor(.08f, .08f, .08f));
		if (Frames.IsEmpty())
		{
			UDrawDebugLibrary::DrawDebugString(Drawer, TEXT("Traversal Graph: no recorded samples"), Anchor,
			    Rotation, White, false, TextSettings);
			return;
		}
		const double EndTime =
		    FMath::IsFinite(CursorTime) && CursorTime >= 0 ? CursorTime : Frames.Last().Time;
		constexpr float MinTime = -5.f;
		const int32 Start = FMath::Max(0, Frames.Num() - 240);
		auto InWindow = [&](const FIMGTraversalDebugFrame& Frame) {
			return Frame.Time >= EndTime + MinTime && Frame.Time <= EndTime;
		};
		const FIMGTraversalDebugFrame* Latest = nullptr;
		for (int32 Index = Start; Index < Frames.Num(); ++Index)
		{
			if (InWindow(Frames[Index]))
				Latest = &Frames[Index];
		}
		if (!Latest)
		{
			UDrawDebugLibrary::DrawDebugString(Drawer, TEXT("Traversal Graph: no samples in last 5 seconds"),
			    Anchor, Rotation, White, false, TextSettings);
			return;
		}
		const bool bAtCursor = EndTime - Latest->Time <= FMath::Max(.001f, Latest->DeltaSeconds) + .002;
		if (!bAtCursor)
		{
			UDrawDebugLibrary::DrawDebugString(Drawer, TEXT("History only: no sample at cursor"),
			    Anchor + Up * (Height * 1.8f), Rotation, White, false, TextSettings);
		}
		// Every curve uses the same cursor and gap policy, including the two height series.
		auto PlotCurve = [&](const FVector& Origin, float PlotWidth, float MinY, float MaxY,
		                     FLinearColor Color, TFunctionRef<float(const FIMGTraversalDebugFrame&)> Value,
		                     TFunctionRef<bool(const FIMGTraversalDebugFrame&)> Accept) {
			TArray<float, TInlineAllocator<240>> Times, Values;
			const FDrawDebugLineStyle Style(Color);
			auto Flush = [&]() {
				if (Times.Num() == 1)
				{
					UDrawDebugLibrary::DrawDebugPoint(Drawer,
					    Origin + Right * (PlotWidth * (Times[0] - MinTime) / -MinTime) +
					        Up * (Height * (Values[0] - MinY) / (MaxY - MinY)),
					    FDrawDebugPointStyle(Color, 3.f), false);
				}
				else if (Times.Num() > 1)
				{
					UDrawDebugLibrary::DrawDebugGraphLineArrayView(Drawer, Origin, Rotation, Times, Values,
					    MinTime, 0.f, MinY, MaxY, PlotWidth, Height, Style, false);
				}
				Times.Reset();
				Values.Reset();
			};
			double PreviousTime = 0;
			float PreviousDelta = 0;
			for (int32 Index = Start; Index < Frames.Num(); ++Index)
			{
				const auto& Frame = Frames[Index];
				if (!InWindow(Frame) || !Accept(Frame) || !FMath::IsFinite(Value(Frame)))
				{
					Flush();
					continue;
				}
				if (!Times.IsEmpty() &&
				    Frame.Time - PreviousTime > FMath::Max(PreviousDelta, Frame.DeltaSeconds) + .002)
				{
					Flush();
				}
				Times.Add(static_cast<float>(Frame.Time - EndTime));
				Values.Add(Value(Frame));
				PreviousTime = Frame.Time;
				PreviousDelta = Frame.DeltaSeconds;
			}
			Flush();
		};
		auto DrawAxes = [&](const FVector& Origin, float PlotWidth, const FString& YLabel) {
			FDrawDebugGraphAxesSettings Axes;
			Axes.bDrawAxesBox = true;
			Axes.XaxisLabel = TEXT("Time (-5..0 s)");
			Axes.YaxisLabel = YLabel;
			Axes.AxisLabelSettings.Height = Height * .07f;
			UDrawDebugLibrary::DrawDebugGraphAxes(
			    Drawer, Origin, Rotation, PlotWidth, Height, White, Border, false, Axes);
		};
		const TCHAR* Titles[] = {
		    TEXT("Speed (cm/s)"), TEXT("Montage progress"), TEXT("Detect (ms)"), TEXT("Detect queries")};
		const FLinearColor Colors[] = {
		    FLinearColor::Green, FLinearColor::Yellow, FLinearColor(FColor::Cyan), FLinearColor::White};
		for (int32 Metric = 0; Metric < 4; ++Metric)
		{
			auto Value = [Metric](const FIMGTraversalDebugFrame& Frame) -> float {
				return Metric == 0 ? static_cast<float>(Frame.Velocity.Size2D())
				    : Metric == 1  ? Frame.MontagePhase
				    : Metric == 2  ? Frame.DetectionMilliseconds
				                   : static_cast<float>(Frame.Queries);
			};
			float Max = Metric == 1 ? 1.f : .001f;
			for (int32 Index = Start; Index < Frames.Num(); ++Index)
			{
				if (InWindow(Frames[Index]) && FMath::IsFinite(Value(Frames[Index])))
					Max = FMath::Max(Max, Value(Frames[Index]));
			}
			const FVector Origin = Anchor + Right * ((Metric % 2) * (Width + HalfWidth * .05f)) -
			    Up * ((Metric / 2) * (Height + HalfHeight * .18f));
			DrawAxes(Origin, Width, FString::Printf(TEXT("0..%.2f"), Max));
			PlotCurve(Origin, Width, 0.f, Max, Colors[Metric], Value, [](const auto&) { return true; });
			const FLinearColor LegendColors[] = {Colors[Metric]};
			const FString LegendLabels[] = {bAtCursor && FMath::IsFinite(Value(*Latest))
			        ? FString::Printf(TEXT("%s %.2f"), Titles[Metric], Value(*Latest))
			        : FString::Printf(TEXT("%s n/a"), Titles[Metric])};
			const FDrawDebugLineStyle Plot(Colors[Metric]);
			UDrawDebugLibrary::DrawDebugGraphLegendArrayView(Drawer, Origin + Up * (Height * 1.12f), Rotation,
			    LegendColors, LegendLabels, 0.f, 0.f, Plot, Plot, Height * .06f, false, TextSettings);
		}
		const FIMGTraversalDebugFrame* LatestHeight = nullptr;
		for (int32 Index = Start; Index < Frames.Num(); ++Index)
		{
			const auto& Frame = Frames[Index];
			if (InWindow(Frame) && Frame.bHasHeightSample && Frame.HeightCapture != 0 &&
			    FMath::IsFinite(Frame.MontageRootHeight) && FMath::IsFinite(Frame.WarpedCapsuleHeight))
				LatestHeight = &Frame;
		}
		const FVector Origin = Anchor - Up * (2.f * (Height + HalfHeight * .18f));
		const float FullWidth = 2.f * Width + HalfWidth * .05f;
		DrawAxes(Origin, FullWidth, TEXT("Height (cm)"));
		if (!LatestHeight)
		{
			UDrawDebugLibrary::DrawDebugString(Drawer, TEXT("Height: no recorded samples"),
			    Origin + Up * (Height * 1.12f), Rotation, White, false, TextSettings);
			return;
		}
		auto AcceptHeight = [&](const FIMGTraversalDebugFrame& Frame) {
			return Frame.bHasHeightSample && Frame.HeightCapture == LatestHeight->HeightCapture &&
			    FMath::IsFinite(Frame.MontageRootHeight) && FMath::IsFinite(Frame.WarpedCapsuleHeight);
		};
		float Min = 0, Max = 0;
		for (int32 Index = Start; Index < Frames.Num(); ++Index)
		{
			const auto& Frame = Frames[Index];
			if (InWindow(Frame) && AcceptHeight(Frame))
			{
				Min = FMath::Min3(Min, static_cast<float>(Frame.MontageRootHeight),
				    static_cast<float>(Frame.WarpedCapsuleHeight));
				Max = FMath::Max3(Max, static_cast<float>(Frame.MontageRootHeight),
				    static_cast<float>(Frame.WarpedCapsuleHeight));
			}
		}
		const float Padding = FMath::Max(1.f, (Max - Min) * .05f);
		Min -= Padding;
		Max += Padding;
		const FLinearColor RootColor(FColor::Orange), CapsuleColor(FColor::Green);
		PlotCurve(
		    Origin, FullWidth, Min, Max, RootColor,
		    [](const auto& Frame) -> float { return static_cast<float>(Frame.MontageRootHeight); },
		    AcceptHeight);
		PlotCurve(
		    Origin, FullWidth, Min, Max, CapsuleColor,
		    [](const auto& Frame) -> float { return static_cast<float>(Frame.WarpedCapsuleHeight); },
		    AcceptHeight);
		const bool bHeightAtCursor = bAtCursor && AcceptHeight(*Latest);
		const TCHAR* Suffix = bHeightAtCursor ? TEXT("") : TEXT(" (last)");
		const FLinearColor HeightColors[] = {RootColor, CapsuleColor};
		const FString HeightLabels[] = {
		    FString::Printf(TEXT("Montage root%s: %.1f cm"), Suffix, LatestHeight->MontageRootHeight),
		    FString::Printf(
		        TEXT("Executed (Warp + CMC)%s: %.1f cm"), Suffix, LatestHeight->WarpedCapsuleHeight)};
		UDrawDebugLibrary::DrawDebugGraphLegendArrayView(Drawer, Origin + Up * (Height * 1.12f), Rotation,
		    HeightColors, HeightLabels, 0.f, 0.f, White, White, Height * .06f, false, TextSettings);
		const FString RangeLabel = FString::Printf(TEXT("%.1f..%.1f"), Min, Max);
		UDrawDebugLibrary::DrawDebugString(Drawer, RangeLabel,
		    Origin + Right * (FullWidth * .65f) + Up * (Height * 1.12f), Rotation, White, false,
		    TextSettings);
	});
}

#else
bool IMGTraversalDebugPresentation::IsShown()
{
	return false;
}
bool IMGTraversalDebugPresentation::IsGraphShown()
{
	return false;
}
void IMGTraversalDebugPresentation::Draw(UWorld*, const FIMGTraversalDebugFrame&) {}
void IMGTraversalDebugPresentation::Clear(UWorld*) {}
void IMGTraversalDebugPresentation::DrawGraphs(
    UWorld*, const FMinimalViewInfo&, TConstArrayView<FIMGTraversalDebugFrame>, double)
{
}
#endif
