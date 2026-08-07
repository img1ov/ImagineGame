#include "Performance/IMGPerformanceStatSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/NetConnection.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "GameModes/IMGGameState.h"
#include "Performance/IMGPerformanceStatTypes.h"
#include "Performance/LatencyMarkerModule.h"
#include "ProfilingDebugging/CsvProfiler.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGPerformanceStatSubsystem)

CSV_DEFINE_CATEGORY(IMGPerformance, /*bIsEnabledByDefault=*/false);

class FSubsystemCollectionBase;

//////////////////////////////////////////////////////////////////////
// FIMGPerformanceStatCache

void FIMGPerformanceStatCache::StartCharting()
{
}

void FIMGPerformanceStatCache::ProcessFrame(const FFrameData& FrameData)
{
	// Record stats about the frame data
	{
		RecordStat(
			EIMGDisplayablePerformanceStat::ClientFPS,
			(FrameData.TrueDeltaSeconds != 0.0) ?
			1.0 / FrameData.TrueDeltaSeconds :
			0.0);

		RecordStat(EIMGDisplayablePerformanceStat::IdleTime, FrameData.IdleSeconds);
		RecordStat(EIMGDisplayablePerformanceStat::FrameTime, FrameData.TrueDeltaSeconds);
		RecordStat(EIMGDisplayablePerformanceStat::FrameTime_GameThread, FrameData.GameThreadTimeSeconds);
		RecordStat(EIMGDisplayablePerformanceStat::FrameTime_RenderThread, FrameData.RenderThreadTimeSeconds);
		RecordStat(EIMGDisplayablePerformanceStat::FrameTime_RHIThread, FrameData.RHIThreadTimeSeconds);
		RecordStat(EIMGDisplayablePerformanceStat::FrameTime_GPU, FrameData.GPUTimeSeconds);	
	}

	if (UWorld* World = MySubsystem->GetGameInstance()->GetWorld())
	{
		// Record some networking related stats
		if (const AIMGGameState* GameState = World->GetGameState<AIMGGameState>())
		{
			RecordStat(EIMGDisplayablePerformanceStat::ServerFPS, GameState->GetServerFPS());
		}

		if (APlayerController* LocalPC = GEngine->GetFirstLocalPlayerController(World))
		{
			if (APlayerState* PS = LocalPC->GetPlayerState<APlayerState>())
			{
				RecordStat(EIMGDisplayablePerformanceStat::Ping, PS->GetPingInMilliseconds());
			}

			if (UNetConnection* NetConnection = LocalPC->GetNetConnection())
			{
				const UNetConnection::FNetConnectionPacketLoss& InLoss = NetConnection->GetInLossPercentage();
				RecordStat(EIMGDisplayablePerformanceStat::PacketLoss_Incoming, InLoss.GetAvgLossPercentage());
				
				const UNetConnection::FNetConnectionPacketLoss& OutLoss = NetConnection->GetOutLossPercentage();
				RecordStat(EIMGDisplayablePerformanceStat::PacketLoss_Outgoing, OutLoss.GetAvgLossPercentage());
				
				RecordStat(EIMGDisplayablePerformanceStat::PacketRate_Incoming, NetConnection->InPacketsPerSecond);
				RecordStat(EIMGDisplayablePerformanceStat::PacketRate_Outgoing, NetConnection->OutPacketsPerSecond);

				RecordStat(EIMGDisplayablePerformanceStat::PacketSize_Incoming, (NetConnection->InPacketsPerSecond != 0) ? (NetConnection->InBytesPerSecond / (float)NetConnection->InPacketsPerSecond) : 0.0f);
				RecordStat(EIMGDisplayablePerformanceStat::PacketSize_Outgoing, (NetConnection->OutPacketsPerSecond != 0) ? (NetConnection->OutBytesPerSecond / (float)NetConnection->OutPacketsPerSecond) : 0.0f);
			}
			
			// Finally, record some input latency related stats if they are enabled
			TArray<ILatencyMarkerModule*> LatencyMarkerModules = IModularFeatures::Get().GetModularFeatureImplementations<ILatencyMarkerModule>(ILatencyMarkerModule::GetModularFeatureName());
			for (ILatencyMarkerModule* LatencyMarkerModule : LatencyMarkerModules)
			{
				if (LatencyMarkerModule->GetEnabled())
				{
					const float TotalLatencyMs = LatencyMarkerModule->GetTotalLatencyInMs();
					if (TotalLatencyMs > 0.0f)
					{
						// Record some stats about the latency of the game
						RecordStat(EIMGDisplayablePerformanceStat::Latency_Total, TotalLatencyMs);
						RecordStat(EIMGDisplayablePerformanceStat::Latency_Game, LatencyMarkerModule->GetGameLatencyInMs());
						RecordStat(EIMGDisplayablePerformanceStat::Latency_Render, LatencyMarkerModule->GetRenderLatencyInMs());

						// Record some CSV profile stats.
						// You can see these by using the following commands
						// Start and stop the profile:
						//	CsvProfile Start
						//	CsvProfile Stop
						//
						// Or, you can profile for a certain number of frames:
						// CsvProfile Frames=10
						//
						// And this will output a .csv file to the Saved\Profiling\CSV folder
#if CSV_PROFILER
						if (FCsvProfiler* Profiler = FCsvProfiler::Get())
						{
							static const FName TotalLatencyStatName = TEXT("IMG_Latency_Total");
							Profiler->RecordCustomStat(TotalLatencyStatName, CSV_CATEGORY_INDEX(IMGPerformance), TotalLatencyMs, ECsvCustomStatOp::Set);

							static const FName GameLatencyStatName = TEXT("IMG_Latency_Game");
							Profiler->RecordCustomStat(GameLatencyStatName, CSV_CATEGORY_INDEX(IMGPerformance), LatencyMarkerModule->GetGameLatencyInMs(), ECsvCustomStatOp::Set);

							static const FName RenderLatencyStatName = TEXT("IMG_Latency_Render");
							Profiler->RecordCustomStat(RenderLatencyStatName, CSV_CATEGORY_INDEX(IMGPerformance), LatencyMarkerModule->GetRenderLatencyInMs(), ECsvCustomStatOp::Set);
						}
#endif

						// Some more fine grain latency numbers can be found on the marker module if desired
						//LatencyMarkerModule->GetRenderLatencyInMs()));
						//LatencyMarkerModule->GetDriverLatencyInMs()));
						//LatencyMarkerModule->GetOSRenderQueueLatencyInMs()));
						//LatencyMarkerModule->GetGPURenderLatencyInMs()));
						break;	
					}
				}
			}
		}
	}
}

void FIMGPerformanceStatCache::StopCharting()
{
}

void FIMGPerformanceStatCache::RecordStat(const EIMGDisplayablePerformanceStat Stat, const double Value)
{
	PerfStateCache.FindOrAdd(Stat).RecordSample(Value);
}

double FIMGPerformanceStatCache::GetCachedStat(EIMGDisplayablePerformanceStat Stat) const
{
	static_assert((int32)EIMGDisplayablePerformanceStat::Count == 18, "Need to update this function to deal with new performance stats");

	if (const FSampledStatCache* Cache = GetCachedStatData(Stat))
	{
		return Cache->GetLastCachedStat();
	}

	return 0.0;
}

const FSampledStatCache* FIMGPerformanceStatCache::GetCachedStatData(const EIMGDisplayablePerformanceStat Stat) const
{
	static_assert((int32)EIMGDisplayablePerformanceStat::Count == 18, "Need to update this function to deal with new performance stats");
	
	return PerfStateCache.Find(Stat);
}

//////////////////////////////////////////////////////////////////////
// UIMGPerformanceStatSubsystem

void UIMGPerformanceStatSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Tracker = MakeShared<FIMGPerformanceStatCache>(this);
	GEngine->AddPerformanceDataConsumer(Tracker);
}

void UIMGPerformanceStatSubsystem::Deinitialize()
{
	GEngine->RemovePerformanceDataConsumer(Tracker);
	Tracker.Reset();
}

double UIMGPerformanceStatSubsystem::GetCachedStat(EIMGDisplayablePerformanceStat Stat) const
{
	return Tracker->GetCachedStat(Stat);
}

const FSampledStatCache* UIMGPerformanceStatSubsystem::GetCachedStatData(const EIMGDisplayablePerformanceStat Stat) const
{
	return Tracker->GetCachedStatData(Stat);
}

