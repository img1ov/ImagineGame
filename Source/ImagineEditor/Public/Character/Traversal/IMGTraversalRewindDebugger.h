#pragma once
#include "Character/Traversal/IMGTraversalDebugTypes.h"
#include "CoreMinimal.h"
#include "IRewindDebuggerTrackCreator.h"
#include "RewindDebuggerTrack.h"
#include "SEventTimelineView.h"

namespace IMGTraversalRewind
{
class SDetails;
class FTrack final : public RewindDebugger::FRewindDebuggerTrack
{
public:
	FTrack(uint64 InActorId, int32 InLane = -1);
	TSharedPtr<const FIMGTraversalDebugFrame> Current;
	TArray<FIMGTraversalDebugFrame> GraphFrames;
	void Refresh();
	void Reset();
	bool HasSelection() const;

private:
	virtual FName GetNameInternal() const override;
	virtual FText GetDisplayNameInternal() const override;
	virtual uint64 GetObjectIdInternal() const override
	{
		return ActorId;
	}
	virtual bool UpdateInternal() override;
	virtual TSharedPtr<SWidget> GetTimelineViewInternal() override;
	virtual TSharedPtr<SWidget> GetDetailsViewInternal() override;
	virtual TConstArrayView<TSharedPtr<FRewindDebuggerTrack>> GetChildrenInternal(
	    TArray<TSharedPtr<FRewindDebuggerTrack>>&) const override
	{
		return Children;
	}
	TWeakPtr<const TraceServices::IAnalysisSession> Analysis;
	uint64 ActorId;
	int32 Lane;
	TArray<TSharedPtr<FRewindDebuggerTrack>> Children;
	TSharedPtr<SEventTimelineView::FTimelineEventData> Timeline;
	TSharedPtr<SDetails> Details;
	uint64 Revision = MAX_uint64;
	uint64 GraphRevision = MAX_uint64;
	double ViewStart = -1, ViewEnd = -1, GraphTime = -1;
};
class FTrackCreator final : public RewindDebugger::IRewindDebuggerTrackCreator
{
	virtual FName GetNameInternal() const override
	{
		return TEXT("Traversal");
	}
	virtual FName GetTargetTypeNameInternal() const override
	{
		return TEXT("Object");
	}
	virtual void GetTrackTypesInternal(
	    TArray<RewindDebugger::FRewindDebuggerTrackType>& Types) const override;
	virtual bool HasDebugInfoInternal(const RewindDebugger::FObjectId& Id) const override;
	virtual TSharedPtr<RewindDebugger::FRewindDebuggerTrack> CreateTrackInternal(
	    const RewindDebugger::FObjectId& Id) const override;
};
void Startup();
void Shutdown();
} // namespace IMGTraversalRewind
