#include "Character/Traversal/IMGTraversalRewindDebugger.h"
#include "Character/Traversal/IMGTraversalRewindData.h"
#include "Engine/World.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraTypes.h"
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "Features/IModularFeatures.h"
#include "GameFramework/PlayerController.h"
#include "IRewindDebugger.h"
#include "IRewindDebuggerExtension.h"
#include "Character/Traversal/Trace/IMGTraversalTraceProvider.h"
#include "Character/Traversal/Widgets/SIMGTraversalDetails.h"

namespace IMGTraversalRewind
{
namespace
{
TArray<TWeakPtr<FTrack>> Tracks;
TSharedPtr<FTrackCreator> Creator;
TSharedPtr<FTraceModule> Module;

FLinearColor LaneColor(int32 Lane)
{
	const FLinearColor Colors[] = {FLinearColor(.8f, .6f, .1f), FLinearColor(.1f, .7f, 1.f),
	    FLinearColor(.2f, .8f, .3f), FLinearColor(.8f, .3f, .9f)};
	return Colors[FMath::Clamp(Lane, 0, 3)];
}
class FExtension final : public IRewindDebuggerExtension
{
public:
	TWeakPtr<FTrack> DisplayTrack;
	TWeakObjectPtr<UWorld> DisplayWorld;
	virtual FString GetName() override
	{
		return TEXT("Traversal");
	}
	virtual void Update(float, IRewindDebugger* Debugger) override
	{
		IMGTraversalDebugPresentation::Clear(DisplayWorld.Get());
		DisplayTrack.Reset();
		DisplayWorld.Reset();
		Tracks.RemoveAll([](const auto& Track) { return !Track.IsValid(); });
		if (Debugger->IsPIESimulating() || Debugger->IsRecording())
		{
			return;
		}
		UWorld* World = Debugger->GetWorldToVisualize();
		if (!World)
		{
			return;
		}
		IMGTraversalDebugPresentation::Clear(World);
		DisplayWorld = World;
		int32 GraphPriority = -1;
		for (const auto& WeakTrack : Tracks)
		{
			const auto Track = WeakTrack.Pin();
			if (!Track || !Track->IsVisible() || !Debugger->IsObjectCurrentlyDebugged(Track->GetUObjectId()))
			{
				continue;
			}
			Track->Refresh();
			if (Track->Current)
			{
				IMGTraversalDebugPresentation::Draw(World, *Track->Current);
			}
			const int32 Priority = Track->HasSelection()               ? 2
			    : Debugger->GetRootObjectId() == Track->GetUObjectId() ? 1
			                                                           : 0;
			if (Priority > GraphPriority)
			{
				GraphPriority = Priority;
				DisplayTrack = Track;
				DisplayWorld = World;
			}
		}
		if (const auto Track = DisplayTrack.Pin(); Track && IMGTraversalDebugPresentation::IsGraphShown())
		{
			FMinimalViewInfo View;
			bool bHasView = false;
			if (const auto* Viewport = GCurrentLevelEditingViewportClient;
			    Viewport && Viewport->GetWorld() == World)
			{
				View.Location = Viewport->GetViewLocation();
				View.Rotation = Viewport->GetViewRotation();
				View.FOV = Viewport->ViewFOV;
				if (Viewport->Viewport)
				{
					const FIntPoint Size = Viewport->Viewport->GetSizeXY();
					if (Size.Y > 0)
						View.AspectRatio = static_cast<float>(Size.X) / Size.Y;
				}
				bHasView = true;
			}
			else
			{
				for (auto It = World->GetPlayerControllerIterator(); It; ++It)
				{
					const auto* PC = It->Get();
					if (PC && PC->IsLocalController() && PC->PlayerCameraManager)
					{
						View = PC->PlayerCameraManager->GetCameraCacheView();
						bHasView = true;
						break;
					}
				}
			}
			if (bHasView)
				IMGTraversalDebugPresentation::DrawGraphs(
				    World, View, Track->GraphFrames, Debugger->GetScrubTime());
		}
	}
	virtual void AnalysisSessionClosed(IRewindDebugger*) override
	{
		Clear(nullptr);
	}
	virtual void Clear(IRewindDebugger*) override
	{
		IMGTraversalDebugPresentation::Clear(DisplayWorld.Get());
		DisplayTrack.Reset();
		DisplayWorld.Reset();
		for (const auto& WeakTrack : Tracks)
		{
			if (auto Track = WeakTrack.Pin())
			{
				Track->Reset();
			}
		}
	}
};
TSharedPtr<FExtension> Extension;
} // namespace
FTrack::FTrack(uint64 InActorId, int32 InLane) : ActorId(InActorId), Lane(InLane)
{
	Timeline = MakeShared<SEventTimelineView::FTimelineEventData>();
	if (Lane < 0)
	{
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Children.Add(MakeShared<FTrack>(ActorId, Index));
		}
	}
}
FText FTrack::GetDisplayNameInternal() const
{
	const TCHAR* Names[] = {
	    TEXT("Requests / GA"), TEXT("Detection"), TEXT("Execution"), TEXT("Network / Replay")};
	return FText::FromString(Lane < 0 ? TEXT("Traversal") : Names[Lane]);
}
FName FTrack::GetNameInternal() const
{
	static const FName Names[] = {TEXT("Traversal.Requests"), TEXT("Traversal.Detection"),
	    TEXT("Traversal.Execution"), TEXT("Traversal.NetworkReplay")};
	return Lane < 0 ? FName(TEXT("Traversal")) : Names[Lane];
}
void FTrack::Reset()
{
	Current.Reset();
	GraphFrames.Reset();
	Analysis.Reset();
	Revision = MAX_uint64;
	GraphRevision = MAX_uint64;
	GraphTime = -1;
	Timeline->EventPoints.Reset();
	Timeline->EventWindows.Reset();
	if (Details)
	{
		Details->SetFrame(nullptr);
	}
	for (const auto& Child : Children)
	{
		StaticCastSharedPtr<FTrack>(Child)->Reset();
	}
}
bool FTrack::HasSelection() const
{
	if (GetIsSelected())
	{
		return true;
	}
	for (const auto& Child : Children)
	{
		if (Child->GetIsSelected())
		{
			return true;
		}
	}
	return false;
}
TSharedPtr<SWidget> FTrack::GetTimelineViewInternal()
{
	return SNew(SEventTimelineView)
	    .ViewRange_Lambda([]() { return IRewindDebugger::Instance()->GetCurrentViewRange(); })
	    .EventData(Timeline);
}
TSharedPtr<SWidget> FTrack::GetDetailsViewInternal()
{
	if (!Details)
	{
		SAssignNew(Details, SDetails).Lane(Lane).ActorId(ActorId);
	}
	Details->SetFrame(Current);
	return Details;
}
void FTrack::Refresh()
{
	IRewindDebugger* Debugger = IRewindDebugger::Instance();
	auto Session = Debugger ? Debugger->GetAnalysisSessionAsShared() : nullptr;
	if (!Session)
	{
		Reset();
		return;
	}
	if (!Analysis.IsValid() || Analysis.Pin() != Session)
	{
		Analysis = Session;
		Revision = MAX_uint64;
		GraphTime = -1;
		GraphRevision = MAX_uint64;
		Timeline->EventPoints.Reset();
		Timeline->EventWindows.Reset();
	}
	TraceServices::FAnalysisSessionReadScope Lock(*Session);
	const auto* Provider = Session->ReadProvider<FProvider>(FProvider::Name);
	if (!Provider)
	{
		Reset();
		return;
	}
	const double Time = Debugger->GetScrubTime();
	Current = Provider->At(ActorId, Time);
	if (Details)
	{
		Details->SetFrame(Current);
	}
	const bool bBuildGraph = Lane < 0 && IMGTraversalDebugPresentation::IsGraphShown() &&
	    !Debugger->IsPIESimulating() && !Debugger->IsRecording();
	if (bBuildGraph && (GraphTime != Time || GraphRevision != Provider->GetRevision()))
	{
		GraphFrames.Reset();
		const double HistoryStart = FMath::Max(0., Time - 5.);
		Provider->Enumerate(ActorId, HistoryStart, Time, [this, HistoryStart](const auto& Frame) {
			if (Frame.Time < HistoryStart)
			{
				return;
			}
			FIMGTraversalDebugFrame Sample;
			Sample.Frame = Frame.Frame;
			Sample.Time = Frame.Time;
			Sample.Velocity = Frame.Velocity;
			Sample.MontagePhase = Frame.MontagePhase;
			Sample.DetectionMilliseconds = Frame.DetectionMilliseconds;
			Sample.Queries = Frame.Queries;
			Sample.DeltaSeconds = Frame.DeltaSeconds;
			Sample.bHasHeightSample = Frame.bHasHeightSample;
			Sample.HeightCapture = Frame.HeightCapture;
			Sample.HeightReferenceZ = Frame.HeightReferenceZ;
			Sample.MontageRootHeight = Frame.MontageRootHeight;
			Sample.WarpedCapsuleHeight = Frame.WarpedCapsuleHeight;
			if (!GraphFrames.IsEmpty() && GraphFrames.Last().Frame == Sample.Frame)
			{
				GraphFrames.Last() = MoveTemp(Sample);
			}
			else
			{
				GraphFrames.Add(MoveTemp(Sample));
			}
		});
		if (GraphFrames.Num() > 240)
		{
			GraphFrames.RemoveAt(0, GraphFrames.Num() - 240, EAllowShrinking::No);
		}
		GraphTime = Time;
		GraphRevision = Provider->GetRevision();
	}
	else if (Lane < 0 && !bBuildGraph)
	{
		GraphFrames.Reset();
		GraphTime = -1;
		GraphRevision = MAX_uint64;
	}
	const auto Range = Debugger->GetCurrentViewRange();
	const double Start = Range.GetLowerBoundValue(), End = Range.GetUpperBoundValue();
	if (Revision == Provider->GetRevision() && ViewStart == Start && ViewEnd == End)
	{
		return;
	}
	Revision = Provider->GetRevision();
	ViewStart = Start;
	ViewEnd = End;
	Timeline->EventPoints.Reset();
	Timeline->EventWindows.Reset();
	Provider->Enumerate(ActorId, Start, End, [this, End](const auto& Frame) {
		for (const auto& Event : Frame.Events)
		{
			const int32 EventLane = GetEventLane(Event);
			if (!IsTimelineEvent(Event) || (Lane >= 0 && EventLane != Lane))
			{
				continue;
			}
			Timeline->EventPoints.Add({Event.Time, FText::FromString(Event.Name),
			    FText::FromString(FString::Printf(TEXT("Attempt %lld | %s"), Event.Attempt, *Event.Detail)),
			    Event.bWarning ? FLinearColor::Red : LaneColor(EventLane)});
		}
		if (Frame.bActive && (Lane < 0 || Lane == 2))
		{
			Timeline->EventWindows.Add({Frame.Time, FMath::Min(Frame.Time + Frame.DeltaSeconds, End),
			    FText::FromString(Frame.Action), FText::FromString(Frame.Montage), LaneColor(2)});
		}
	});
}
bool FTrack::UpdateInternal()
{
	Refresh();
	// FRewindDebuggerTrack::Update only dispatches this node; owners update their
	// children.
	bool bChanged = false;
	for (const auto& Child : Children)
	{
		bChanged |= Child->Update();
	}
	return bChanged;
}
void FTrackCreator::GetTrackTypesInternal(TArray<RewindDebugger::FRewindDebuggerTrackType>& Types) const
{
	Types.Add({TEXT("Traversal"), NSLOCTEXT("IMGTraversal", "Track", "Traversal")});
}
bool FTrackCreator::HasDebugInfoInternal(const RewindDebugger::FObjectId& Id) const
{
	auto* Debugger = IRewindDebugger::Instance();
	const auto* Session = Debugger ? Debugger->GetAnalysisSession() : nullptr;
	if (!Session)
	{
		return false;
	}
	TraceServices::FAnalysisSessionReadScope Lock(*Session);
	const auto* Provider = Session->ReadProvider<FProvider>(FProvider::Name);
	return Provider && Provider->HasData(Id.GetMainId());
}
TSharedPtr<RewindDebugger::FRewindDebuggerTrack> FTrackCreator::CreateTrackInternal(
    const RewindDebugger::FObjectId& Id) const
{
	auto Track = MakeShared<FTrack>(Id.GetMainId());
	Tracks.Add(Track);
	return Track;
}
void Startup()
{
	Module = MakeShared<FTraceModule>();
	Creator = MakeShared<FTrackCreator>();
	Extension = MakeShared<FExtension>();
	auto& Features = IModularFeatures::Get();
	Features.RegisterModularFeature(TraceServices::ModuleFeatureName, Module.Get());
	Features.RegisterModularFeature(
	    RewindDebugger::IRewindDebuggerTrackCreator::ModularFeatureName, Creator.Get());
	Features.RegisterModularFeature(IRewindDebuggerExtension::ModularFeatureName, Extension.Get());
}
void Shutdown()
{
	Extension->Clear(nullptr);
	auto& Features = IModularFeatures::Get();
	Features.UnregisterModularFeature(IRewindDebuggerExtension::ModularFeatureName, Extension.Get());
	Features.UnregisterModularFeature(
	    RewindDebugger::IRewindDebuggerTrackCreator::ModularFeatureName, Creator.Get());
	Features.UnregisterModularFeature(TraceServices::ModuleFeatureName, Module.Get());
	Tracks.Reset();
	Extension.Reset();
	Creator.Reset();
	Module.Reset();
}
} // namespace IMGTraversalRewind
