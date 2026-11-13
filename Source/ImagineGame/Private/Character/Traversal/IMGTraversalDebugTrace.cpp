#include "IMGTraversalDebugTrace.h"
#include "IMGTraversalDebug.h"
#include "ObjectTraceDefines.h"
#if IMG_TRAVERSAL_DEBUG_ENABLED && OBJECT_TRACE_ENABLED
#include "Features/IModularFeatures.h"
#include "ObjectTrace.h"
#include "RewindDebuggerRuntimeInterface/IRewindDebuggerRuntimeExtension.h"
#include "Serialization/MemoryWriter.h"
#include "Trace/Trace.inl"

UE_TRACE_CHANNEL_DEFINE(IMGTraversalChannel, "Structured Traversal debugging data.");
UE_TRACE_EVENT_BEGIN(IMGTraversal, FrameV4)
UE_TRACE_EVENT_FIELD(uint8[], Data)
UE_TRACE_EVENT_END()

namespace
{
TAtomic<uint64> RecordingSerial(0);
class FTraversalRecordingExtension final : public IRewindDebuggerRuntimeExtension
{
	virtual void RecordingStarted() override
	{
		++RecordingSerial;
		UE::Trace::ToggleChannel(TEXT("IMGTraversal"), true);
	}
	virtual void RecordingStopped() override
	{
		IMGTraversalDebug::FlushRecording();
		UE::Trace::ToggleChannel(TEXT("IMGTraversal"), false);
	}
};
FTraversalRecordingExtension RecordingExtension;
} // namespace
#endif

void IMGTraversalDebugTrace::Startup()
{
#if IMG_TRAVERSAL_DEBUG_ENABLED && OBJECT_TRACE_ENABLED
	IModularFeatures::Get().RegisterModularFeature(
	    IRewindDebuggerRuntimeExtension::ModularFeatureName, &RecordingExtension);
#endif
}
void IMGTraversalDebugTrace::Shutdown()
{
#if IMG_TRAVERSAL_DEBUG_ENABLED && OBJECT_TRACE_ENABLED
	IModularFeatures::Get().UnregisterModularFeature(
	    IRewindDebuggerRuntimeExtension::ModularFeatureName, &RecordingExtension);
	UE::Trace::ToggleChannel(TEXT("IMGTraversal"), false);
#endif
}
bool IMGTraversalDebugTrace::IsRecording()
{
#if IMG_TRAVERSAL_DEBUG_ENABLED && OBJECT_TRACE_ENABLED
	return UE_TRACE_CHANNELEXPR_IS_ENABLED(IMGTraversalChannel);
#else
	return false;
#endif
}
uint64 IMGTraversalDebugTrace::GetRecordingSerial()
{
#if IMG_TRAVERSAL_DEBUG_ENABLED && OBJECT_TRACE_ENABLED
	return RecordingSerial.Load();
#else
	return 0;
#endif
}
void IMGTraversalDebugTrace::Output(const UObject* Actor, FIMGTraversalDebugFrame& Frame)
{
#if IMG_TRAVERSAL_DEBUG_ENABLED && OBJECT_TRACE_ENABLED
	if (!IsRecording())
	{
		return;
	}
	TRACE_OBJECT(Actor);
	Frame.ActorId = FObjectTrace::GetObjectId(Actor);
	const double RecordedTime = FObjectTrace::GetObjectWorldElapsedTime(Actor);
	const double Offset = RecordedTime - Frame.Time;
	for (auto& Event : Frame.Events)
	{
		Event.Time += Offset;
	}
	Frame.Time = RecordedTime;
	Frame.Cycle = FPlatformTime::Cycles64();
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	Writer << Frame;
	UE_TRACE_LOG(IMGTraversal, FrameV4, IMGTraversalChannel) << FrameV4.Data(Bytes.GetData(), Bytes.Num());
#endif
}
