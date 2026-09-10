// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayTagContainer.h"
#include "Messages/IMGVerbMessage.h"
#include "Net/Serialization/FastArraySerializer.h"

#include "IMGVerbMessageReplication.generated.h"

class UObject;
struct FIMGVerbMessageReplication;
struct FNetDeltaSerializeInfo;

/**
 * Represents one verb message
 */
USTRUCT(BlueprintType)
struct FIMGVerbMessageReplicationEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	FIMGVerbMessageReplicationEntry()
	{}

	FIMGVerbMessageReplicationEntry(const FIMGVerbMessage& InMessage)
		: Message(InMessage)
	{
	}

	FString GetDebugString() const;

private:
	friend FIMGVerbMessageReplication;

	UPROPERTY()
	FIMGVerbMessage Message;
};

/** Container of verb messages to replicate */
USTRUCT(BlueprintType)
struct FIMGVerbMessageReplication : public FFastArraySerializer
{
	GENERATED_BODY()

	FIMGVerbMessageReplication()
	{
	}

public:
	void SetOwner(UObject* InOwner) { Owner = InOwner; }

	// Broadcasts a message from server to clients
	void AddMessage(const FIMGVerbMessage& Message);

	//~FFastArraySerializer contract
	void PreReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize);
	void PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize);
	void PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize);
	//~End of FFastArraySerializer contract

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FIMGVerbMessageReplicationEntry, FIMGVerbMessageReplication>(CurrentMessages, DeltaParms, *this);
	}

private:
	void RebroadcastMessage(const FIMGVerbMessage& Message);

private:
	// Replicated list of gameplay tag stacks
	UPROPERTY()
	TArray<FIMGVerbMessageReplicationEntry> CurrentMessages;

	// Owner (for a route to a world)
	UPROPERTY()
	TObjectPtr<UObject> Owner = nullptr;
};

template<>
struct TStructOpsTypeTraits<FIMGVerbMessageReplication> : public TStructOpsTypeTraitsBase2<FIMGVerbMessageReplication>
{
	enum
	{
		WithNetDeltaSerializer = true,
	};
};
