// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Engine/DeveloperSettingsBackedByCVars.h"
#include "System/IMGReplicationGraphTypes.h"
#include "IMGReplicationGraphSettings.generated.h"

/**
 * Default settings for the IMG replication graph
 */
UCLASS(config=Game, MinimalAPI)
class UIMGReplicationGraphSettings : public UDeveloperSettingsBackedByCVars
{
	GENERATED_BODY()

public:
	UIMGReplicationGraphSettings();

public:

	UPROPERTY(config, EditAnywhere, Category = ReplicationGraph)
	bool bDisableReplicationGraph = true;

	UPROPERTY(config, EditAnywhere, Category = ReplicationGraph, meta = (MetaClass = "/Script/ImagineGame.IMGReplicationGraph"))
	FSoftClassPath DefaultReplicationGraphClass;

	UPROPERTY(EditAnywhere, Category = FastSharedPath, meta = (ConsoleVariable = "IMG.RepGraph.EnableFastSharedPath"))
	bool bEnableFastSharedPath = true;

	// How much bandwidth to use for FastShared movement updates. This is counted independently of the NetDriver's target bandwidth.
	UPROPERTY(EditAnywhere, Category = FastSharedPath, meta = (ForceUnits=Kilobytes, ConsoleVariable = "IMG.RepGraph.TargetKBytesSecFastSharedPath"))
	int32 TargetKBytesSecFastSharedPath = 10;

	UPROPERTY(EditAnywhere, Category = FastSharedPath, meta = (ConsoleVariable = "IMG.RepGraph.FastSharedPathCullDistPct"))
	float FastSharedPathCullDistPct = 0.80f;

	UPROPERTY(EditAnywhere, Category = DestructionInfo, meta = (ForceUnits = cm, ConsoleVariable = "IMG.RepGraph.DestructInfo.MaxDist"))
	float DestructionInfoMaxDist = 30000.f;

	UPROPERTY(EditAnywhere, Category=SpatialGrid, meta=(ForceUnits=cm, ConsoleVariable = "IMG.RepGraph.CellSize"))
	float SpatialGridCellSize = 10000.0f;

	// Essentially "Min X" for replication. This is just an initial value. The system will reset itself if actors appears outside of this.
	UPROPERTY(EditAnywhere, Category=SpatialGrid, meta=(ForceUnits=cm, ConsoleVariable = "IMG.RepGraph.SpatialBiasX"))
	float SpatialBiasX = -200000.0f;

	// Essentially "Min Y" for replication. This is just an initial value. The system will reset itself if actors appears outside of this.
	UPROPERTY(EditAnywhere, Category=SpatialGrid, meta=(ForceUnits=cm, ConsoleVariable = "IMG.RepGraph.SpatialBiasY"))
	float SpatialBiasY = -200000.0f;

	UPROPERTY(EditAnywhere, Category=SpatialGrid, meta = (ConsoleVariable = "IMG.RepGraph.DisableSpatialRebuilds"))
	bool bDisableSpatialRebuilds = true;

	// How many buckets to spread dynamic, spatialized actors across.
	// High number = more buckets = smaller effective replication frequency.
	// This happens before individual actors do their own NetUpdateFrequency check.
	UPROPERTY(EditAnywhere, Category = DynamicSpatialFrequency, meta = (ConsoleVariable = "IMG.RepGraph.DynamicActorFrequencyBuckets"))
	int32 DynamicActorFrequencyBuckets = 3;

	// Array of Custom Settings for Specific Classes
	UPROPERTY(config, EditAnywhere, Category = ReplicationGraph)
	TArray<FRepGraphActorClassSettings> ClassSettings;
};
