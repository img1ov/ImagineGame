#include "Character/Traversal/Trace/IMGTraversalTraceProvider.h"
#include "Algo/BinarySearch.h"
#include "Trace/Analyzer.h"
#include "Serialization/MemoryReader.h"

namespace IMGTraversalRewind
{
const FName FProvider::Name(TEXT("IMGTraversal"));
void FProvider::Append(FIMGTraversalDebugFrame&& Frame)
{
	Session.WriteAccessCheck();
	Frames.FindOrAdd(Frame.ActorId).Add(MakeShared<FIMGTraversalDebugFrame>(MoveTemp(Frame)));
	++Revision;
}
bool FProvider::HasData(uint64 ActorId) const
{
	Session.ReadAccessCheck();
	return Frames.Contains(ActorId);
}
TSharedPtr<const FIMGTraversalDebugFrame> FProvider::At(uint64 ActorId, double Time) const
{
	Session.ReadAccessCheck();
	const auto* Values = Frames.Find(ActorId);
	if (!Values)
	{
		return {};
	}
	const int32 Index = Algo::UpperBoundBy(*Values, Time, [](const auto& Frame) { return Frame->Time; }) - 1;
	// Never carry old geometry or state into an unrelated idle/recording segment.
	return Values->IsValidIndex(Index) &&
	        Time - (*Values)[Index]->Time <= FMath::Max(.001f, (*Values)[Index]->DeltaSeconds) + .002
	    ? (*Values)[Index]
	    : nullptr;
}
void FProvider::Enumerate(uint64 ActorId, double Start, double End,
    TFunctionRef<void(const FIMGTraversalDebugFrame&)> Callback) const
{
	Session.ReadAccessCheck();
	const auto* Values = Frames.Find(ActorId);
	if (!Values)
	{
		return;
	}
	int32 Index =
	    FMath::Max(0, Algo::UpperBoundBy(*Values, Start, [](const auto& Frame) { return Frame->Time; }) - 1);
	for (; Index < Values->Num() && (*Values)[Index]->Time <= End; ++Index)
	{
		Callback(*(*Values)[Index]);
	}
}
void FProvider::ReadFrames(uint64 ActorId, double Start, double End,
    TArray<TSharedPtr<const FIMGTraversalDebugFrame>>& OutFrames) const
{
	Session.ReadAccessCheck();
	OutFrames.Reset();
	const auto* Values = Frames.Find(ActorId);
	if (!Values)
	{
		return;
	}
	// Include every flush at a boundary time, even when multiple records share a timestamp.
	for (const auto& Frame : *Values)
	{
		if (Frame->Time > End)
		{
			break;
		}
		if (Frame->Time + FMath::Max(0.f, Frame->DeltaSeconds) >= Start)
		{
			OutFrames.Add(Frame);
		}
	}
}
class FAnalyzer final : public UE::Trace::IAnalyzer
{
public:
	FAnalyzer(TraceServices::IAnalysisSession& InSession, FProvider& InProvider)
	    : Session(InSession), Provider(InProvider)
	{
	}

private:
	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override
	{
		Context.InterfaceBuilder.RouteEvent(0, "IMGTraversal", "FrameV2");
		Context.InterfaceBuilder.RouteEvent(1, "IMGTraversal", "FrameV3");
		Context.InterfaceBuilder.RouteEvent(2, "IMGTraversal", "FrameV4");
	}
	virtual bool OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context) override
	{
		auto Bytes = Context.EventData.GetArrayView<uint8>("Data");
		if (Bytes.Num() > 4 * 1024 * 1024)
		{
			return true;
		}
		FMemoryReaderView Reader(Bytes);
		FIMGTraversalDebugFrame Frame;
		IMGTraversalDebugSerialization::SerializeFrame(Reader, Frame, static_cast<uint8>(RouteId + 2));
		if (Reader.IsError() || !FMath::IsFinite(Frame.Time))
		{
			return true;
		}
		TraceServices::FAnalysisSessionEditScope Lock(Session);
		Provider.Append(MoveTemp(Frame));
		return true;
	}
	TraceServices::IAnalysisSession& Session;
	FProvider& Provider;
};
void FTraceModule::GetModuleInfo(TraceServices::FModuleInfo& Info)
{
	Info.Name = FProvider::Name;
	Info.DisplayName = TEXT("Traversal");
}
void FTraceModule::GetLoggers(TArray<const TCHAR*>& Loggers)
{
	Loggers.Add(TEXT("IMGTraversal"));
}
void FTraceModule::OnAnalysisBegin(TraceServices::IAnalysisSession& Session)
{
	auto Provider = MakeShared<FProvider>(Session);
	Session.AddProvider(FProvider::Name, Provider);
	Session.AddAnalyzer(new FAnalyzer(Session, *Provider));
}
} // namespace IMGTraversalRewind
