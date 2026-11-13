#pragma once
#include "CoreMinimal.h"
#include "Character/Traversal/IMGTraversalDebugTypes.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/ModuleService.h"

namespace IMGTraversalRewind
{
class FProvider final : public TraceServices::IProvider
{
public:
	static const FName Name;
	explicit FProvider(TraceServices::IAnalysisSession& InSession) : Session(InSession) {}
	virtual void BeginRead() const override
	{
		Session.BeginRead();
	}
	virtual void EndRead() const override
	{
		Session.EndRead();
	}
	virtual void ReadAccessCheck() const override
	{
		Session.ReadAccessCheck();
	}
	void Append(FIMGTraversalDebugFrame&& Frame);
	bool HasData(uint64 ActorId) const;
	uint64 GetRevision() const
	{
		return Revision;
	}
	TSharedPtr<const FIMGTraversalDebugFrame> At(uint64 ActorId, double Time) const;
	void Enumerate(uint64 ActorId, double Start, double End,
	    TFunctionRef<void(const FIMGTraversalDebugFrame&)> Callback) const;
	void ReadFrames(uint64 ActorId, double Start, double End,
	    TArray<TSharedPtr<const FIMGTraversalDebugFrame>>& OutFrames) const;

private:
	TraceServices::IAnalysisSession& Session;
	TMap<uint64, TArray<TSharedPtr<const FIMGTraversalDebugFrame>>> Frames;
	uint64 Revision = 0;
};
class FTraceModule final : public TraceServices::IModule
{
	virtual void GetModuleInfo(TraceServices::FModuleInfo& Info) override;
	virtual void GetLoggers(TArray<const TCHAR*>& Loggers) override;
	virtual void OnAnalysisBegin(TraceServices::IAnalysisSession& Session) override;
};
} // namespace IMGTraversalRewind
