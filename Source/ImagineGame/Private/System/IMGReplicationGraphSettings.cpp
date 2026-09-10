// Copyright Epic Games, Inc. All Rights Reserved.

#include "System/IMGReplicationGraphSettings.h"
#include "Misc/App.h"
#include "System/IMGReplicationGraph.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGReplicationGraphSettings)

UIMGReplicationGraphSettings::UIMGReplicationGraphSettings()
{
	CategoryName = TEXT("Game");
	DefaultReplicationGraphClass = UIMGReplicationGraph::StaticClass();
}
