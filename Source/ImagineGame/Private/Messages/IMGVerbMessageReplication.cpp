// Copyright Epic Games, Inc. All Rights Reserved.

#include "Messages/IMGVerbMessageReplication.h"

#include "GameFramework/GameplayMessageSubsystem.h"
#include "Messages/IMGVerbMessage.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGVerbMessageReplication)

//////////////////////////////////////////////////////////////////////
// FIMGVerbMessageReplicationEntry

FString FIMGVerbMessageReplicationEntry::GetDebugString() const
{
	return Message.ToString();
}

//////////////////////////////////////////////////////////////////////
// FIMGVerbMessageReplication

void FIMGVerbMessageReplication::AddMessage(const FIMGVerbMessage& Message)
{
	FIMGVerbMessageReplicationEntry& NewStack = CurrentMessages.Emplace_GetRef(Message);
	MarkItemDirty(NewStack);
}

void FIMGVerbMessageReplication::PreReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize)
{
// 	for (int32 Index : RemovedIndices)
// 	{
// 		const FGameplayTag Tag = CurrentMessages[Index].Tag;
// 		TagToCountMap.Remove(Tag);
// 	}
}

void FIMGVerbMessageReplication::PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize)
{
	for (int32 Index : AddedIndices)
	{
		const FIMGVerbMessageReplicationEntry& Entry = CurrentMessages[Index];
		RebroadcastMessage(Entry.Message);
	}
}

void FIMGVerbMessageReplication::PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize)
{
	for (int32 Index : ChangedIndices)
	{
		const FIMGVerbMessageReplicationEntry& Entry = CurrentMessages[Index];
		RebroadcastMessage(Entry.Message);
	}
}

void FIMGVerbMessageReplication::RebroadcastMessage(const FIMGVerbMessage& Message)
{
	check(Owner);
	UGameplayMessageSubsystem& MessageSystem = UGameplayMessageSubsystem::Get(Owner);
	MessageSystem.BroadcastMessage(Message.Verb, Message);
}
