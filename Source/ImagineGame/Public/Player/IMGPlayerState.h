// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystemInterface.h"
#include "Character/IMGPawnData.h"
#include "ModularPlayerState.h"
#include "System/GameplayTagStack.h"
#include "Teams/IMGTeamAgentInterface.h"

#include "IMGPlayerState.generated.h"

#define UE_API IMAGINEGAME_API

struct FIMGVerbMessage;

class AController;
class AIMGPlayerController;
class APlayerState;
class FName;
class UAbilitySystemComponent;
class UIMGAbilitySystemComponent;
class UIMGExperienceDefinition;
class UIMGPawnData;
class UObject;
struct FFrame;
struct FGameplayTag;

/** Defines the types of client connected */
UENUM()
enum class EIMGPlayerConnectionType : uint8
{
	// An active player
	Player = 0,

	// Spectator connected to a running game
	LiveSpectator,

	// Spectating a demo recording offline
	ReplaySpectator,

	// A deactivated player (disconnected)
	InactivePlayer
};

/**
 * AIMGPlayerState
 *
 *	Base player state class used by this project.
 */
UCLASS(MinimalAPI, Config = Game)
class AIMGPlayerState : public AModularPlayerState, public IAbilitySystemInterface, public IIMGTeamAgentInterface
{
	GENERATED_BODY()

public:
	UE_API AIMGPlayerState(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintCallable, Category = "IMG|PlayerState")
	UE_API AIMGPlayerController* GetIMGPlayerController() const;

	UFUNCTION(BlueprintCallable, Category = "IMG|PlayerState")
	UIMGAbilitySystemComponent* GetIMGAbilitySystemComponent() const { return AbilitySystemComponent; }
	UE_API virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	template <class T>
	const T* GetPawnData() const { return Cast<T>(PawnData); }

	UE_API void SetPawnData(const UIMGPawnData* InPawnData);

	//~AActor interface
	UE_API virtual void PreInitializeComponents() override;
	UE_API virtual void PostInitializeComponents() override;
	//~End of AActor interface

	//~APlayerState interface
	UE_API virtual void Reset() override;
	UE_API virtual void ClientInitialize(AController* C) override;
	UE_API virtual void CopyProperties(APlayerState* PlayerState) override;
	UE_API virtual void OnDeactivated() override;
	UE_API virtual void OnReactivated() override;
	//~End of APlayerState interface

	//~IIMGTeamAgentInterface interface
	UE_API virtual void SetGenericTeamId(const FGenericTeamId& NewTeamID) override;
	UE_API virtual FGenericTeamId GetGenericTeamId() const override;
	UE_API virtual FOnIMGTeamIndexChangedDelegate* GetOnTeamIndexChangedDelegate() override;
	//~End of IIMGTeamAgentInterface interface

	static UE_API const FName NAME_IMGAbilityReady;

	UE_API void SetPlayerConnectionType(EIMGPlayerConnectionType NewType);
	EIMGPlayerConnectionType GetPlayerConnectionType() const { return MyPlayerConnectionType; }

	/** Returns the Squad ID of the squad the player belongs to. */
	UFUNCTION(BlueprintCallable)
	int32 GetSquadId() const
	{
		return MySquadID;
	}

	/** Returns the Team ID of the team the player belongs to. */
	UFUNCTION(BlueprintCallable)
	int32 GetTeamId() const
	{
		return GenericTeamIdToInteger(MyTeamID);
	}

	UE_API void SetSquadID(int32 NewSquadID);

	// Adds a specified number of stacks to the tag (does nothing if StackCount is below 1)
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category=Teams)
	UE_API void AddStatTagStack(FGameplayTag Tag, int32 StackCount);

	// Removes a specified number of stacks from the tag (does nothing if StackCount is below 1)
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category=Teams)
	UE_API void RemoveStatTagStack(FGameplayTag Tag, int32 StackCount);

	// Returns the stack count of the specified tag (or 0 if the tag is not present)
	UFUNCTION(BlueprintCallable, Category=Teams)
	UE_API int32 GetStatTagStackCount(FGameplayTag Tag) const;

	// Returns true if there is at least one stack of the specified tag
	UFUNCTION(BlueprintCallable, Category=Teams)
	UE_API bool HasStatTag(FGameplayTag Tag) const;

	// Send a message to just this player
	// (use only for client notifications like accolades, quest toasts, etc... that can handle being occasionally lost)
	UFUNCTION(Client, Unreliable, BlueprintCallable, Category = "IMG|PlayerState")
	UE_API void ClientBroadcastMessage(const FIMGVerbMessage Message);

	// Gets the replicated view rotation of this player, used for spectating
	UE_API FRotator GetReplicatedViewRotation() const;

	// Sets the replicated view rotation, only valid on the server
	UE_API void SetReplicatedViewRotation(const FRotator& NewRotation);

private:
	UE_API void OnExperienceLoaded(const UIMGExperienceDefinition* CurrentExperience);

protected:
	UFUNCTION()
	UE_API void OnRep_PawnData();

protected:

	UPROPERTY(ReplicatedUsing = OnRep_PawnData)
	TObjectPtr<const UIMGPawnData> PawnData;

private:

	// The ability system component sub-object used by player characters.
	UPROPERTY(VisibleAnywhere, Category = "IMG|PlayerState")
	TObjectPtr<UIMGAbilitySystemComponent> AbilitySystemComponent;

	// Health attribute set used by this actor.
	UPROPERTY()
	TObjectPtr<const class UIMGHealthSet> HealthSet;
	// Combat attribute set used by this actor.
	UPROPERTY()
	TObjectPtr<const class UIMGCombatSet> CombatSet;

	UPROPERTY(Replicated)
	EIMGPlayerConnectionType MyPlayerConnectionType;

	UPROPERTY()
	FOnIMGTeamIndexChangedDelegate OnTeamChangedDelegate;

	UPROPERTY(ReplicatedUsing=OnRep_MyTeamID)
	FGenericTeamId MyTeamID;

	UPROPERTY(ReplicatedUsing=OnRep_MySquadID)
	int32 MySquadID;

	UPROPERTY(Replicated)
	FGameplayTagStackContainer StatTags;

	UPROPERTY(Replicated)
	FRotator ReplicatedViewRotation;

private:
	UFUNCTION()
	UE_API void OnRep_MyTeamID(FGenericTeamId OldTeamID);

	UFUNCTION()
	UE_API void OnRep_MySquadID();
};

#undef UE_API
