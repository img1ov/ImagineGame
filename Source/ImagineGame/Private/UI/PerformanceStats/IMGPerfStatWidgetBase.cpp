#include "UI/PerformanceStats/IMGPerfStatWidgetBase.h"

#include "Engine/GameInstance.h"
#include "Performance/IMGPerformanceStatSubsystem.h"
#include "Styling/CoreStyle.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGPerfStatWidgetBase)

void SIMGLatencyGraph::Construct(const FArguments& InArgs)
{
	DesiredSize = InArgs._DesiredSize;
	MaxYAxisOfGraph = InArgs._MaxLatencyToGraph;
	LineColor = InArgs._LineColor;
	BackgroundColor = InArgs._BackgroundColor;
}

int32 SIMGLatencyGraph::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyClippingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 MaxLayerId = LayerId;

	// Draw the background
	FSlateDrawElement::MakeRotatedBox(
		OutDrawElements,
		MaxLayerId,
		AllottedGeometry.ToPaintGeometry(),
		FCoreStyle::Get().GetBrush("BlackBrush"),
		ESlateDrawEffect::NoPixelSnapping,
		0,
		TOptional<FVector2D>(),
		FSlateDrawElement::RelativeToElement,
		BackgroundColor);

	// We need to actually draw the graph plot on top of the background
	// so increment the layer
	MaxLayerId++;

	// Actually draw the graph plot
	DrawTotalLatency(AllottedGeometry, OutDrawElements, MaxLayerId);
	
	MaxLayerId++;

	return MaxLayerId;
}

FVector2D SIMGLatencyGraph::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	return DesiredSize;
}

void SIMGLatencyGraph::DrawTotalLatency(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	const TSharedPtr<const FSampledStatCache> Samples = GraphData.Pin();
	if (!Samples || !FMath::IsFinite(MaxYAxisOfGraph) || MaxYAxisOfGraph <= UE_DOUBLE_SMALL_NUMBER || !FMath::IsFinite(ScaleFactor))
	{
		return;
	}
	
	Points.Reset(Samples->GetSampleSize() + 1);
	
	const FVector2D WidgetSize = AllottedGeometry.GetLocalSize();
	const float LineThickness = 1.0f;
	const double XSlice = WidgetSize.X / static_cast<double>(Samples->GetSampleSize());
	const double Border = 1.0;

	int32 i = 0;
	
	Samples->ForEachCurrentSample([&](const double Stat)
	{
		double Y = WidgetSize.Y - FMath::Clamp((Stat * ScaleFactor), 0.0, MaxYAxisOfGraph) / MaxYAxisOfGraph * WidgetSize.Y;
		Y = FMath::Clamp(Y, Border, WidgetSize.Y - Border);

		Points.Emplace(XSlice * double(++i), Y);
	});

	// Why does this not just draw a straight line?? 
	FSlateDrawElement::MakeLines(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToPaintGeometry(),
		Points,
		ESlateDrawEffect::NoPixelSnapping,
		LineColor,
		false,
		LineThickness);
}

//////////////////////////////////////////////////////////////////////
// UIMGPerfStatGraph

UIMGPerfStatGraph::UIMGPerfStatGraph(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetClipping(EWidgetClipping::ClipToBounds);
}

TSharedRef<SWidget> UIMGPerfStatGraph::RebuildWidget()
{
	SAssignNew(SlateLatencyGraph, SIMGLatencyGraph)
		.LineColor(CachedLineColor)
		.BackgroundColor(CachedBackgroundColor)
		.MaxLatencyToGraph(CachedMaxYValue);
	SlateLatencyGraph->UpdateGraphData(CachedGraphData.Pin(), CachedScaleFactor);
	return SlateLatencyGraph.ToSharedRef();
}

void UIMGPerfStatGraph::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	SlateLatencyGraph.Reset();
}

void UIMGPerfStatGraph::SetLineColor(const FColor& InColor)
{
	CachedLineColor = InColor;
	if (SlateLatencyGraph)
	{
		SlateLatencyGraph->SetLineColor(InColor);
	}
}

void UIMGPerfStatGraph::SetMaxYValue(const float InValue)
{
	CachedMaxYValue = InValue;
	if (SlateLatencyGraph)
	{
		SlateLatencyGraph->SetMaxYValue(InValue);
	}
}

void UIMGPerfStatGraph::SetBackgroundColor(const FColor& InValue)
{
	CachedBackgroundColor = InValue;
	if (SlateLatencyGraph)
	{
		SlateLatencyGraph->SetBackgroundColor(InValue);
	}
}

void UIMGPerfStatGraph::UpdateGraphData(TSharedPtr<const FSampledStatCache> StatData, const float ScaleFactor)
{
	CachedGraphData = StatData;
	CachedScaleFactor = ScaleFactor;
	if (SlateLatencyGraph)
	{
		SlateLatencyGraph->UpdateGraphData(StatData, ScaleFactor);
	}
}

//////////////////////////////////////////////////////////////////////
// UIMGPerfStatWidgetBase

double UIMGPerfStatWidgetBase::FetchStatValue()
{
	if (UIMGPerformanceStatSubsystem* Subsystem = GetStatSubsystem())
	{
		return Subsystem->GetCachedStat(StatToDisplay);
	}
	else
	{
		return 0.0;
	}
}

void UIMGPerfStatWidgetBase::UpdateGraphData(const float ScaleFactor)
{
	GraphScaleFactor = ScaleFactor;
	if (PerfStatGraph)
	{
		const UIMGPerformanceStatSubsystem* Subsystem = GetStatSubsystem();
		PerfStatGraph->UpdateGraphData(Subsystem ? Subsystem->GetCachedStatData(StatToDisplay) : TSharedPtr<const FSampledStatCache>(), ScaleFactor);
	}
}

void UIMGPerfStatWidgetBase::NativeConstruct()
{
	Super::NativeConstruct();

	// Cache the subsystem on construct, which will also make sure the graph is up to date
	GetStatSubsystem();
	
	if (PerfStatGraph)
	{
		PerfStatGraph->SetLineColor(GraphLineColor);
		PerfStatGraph->SetMaxYValue(GraphMaxYValue);
		PerfStatGraph->SetBackgroundColor(GraphBackgroundColor);
	}
	UpdateGraphData(GraphScaleFactor);
}

void UIMGPerfStatWidgetBase::NativeDestruct()
{
	if (PerfStatGraph)
	{
		PerfStatGraph->UpdateGraphData(TSharedPtr<const FSampledStatCache>(), GraphScaleFactor);
	}
	CachedStatSubsystem = nullptr;
	Super::NativeDestruct();
}

UIMGPerformanceStatSubsystem* UIMGPerfStatWidgetBase::GetStatSubsystem()
{
	if (CachedStatSubsystem == nullptr)
	{
		if (UWorld* World = GetWorld())
		{
			if (UGameInstance* GameInstance = World->GetGameInstance())
			{
				CachedStatSubsystem = GameInstance->GetSubsystem<UIMGPerformanceStatSubsystem>();
			}
		}
	}

	return CachedStatSubsystem;
}
