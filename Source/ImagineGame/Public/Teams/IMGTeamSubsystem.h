

#pragma once

#include "Subsystems/WorldSubsystem.h"

#include "IMGTeamSubsystem.generated.h"

#define UE_API IMAGINEGAME_API

class AActor;
class AIMGPlayerState;
class AIMGTeamInfoBase;
class AIMGTeamPrivateInfo;
class AIMGTeamPublicInfo;
class FSubsystemCollectionBase;
class UIMGTeamDisplayAsset;
struct FFrame;
struct FGameplayTag;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnActTeamDisplayAssetChangedDelegate, const UIMGTeamDisplayAsset*, DisplayAsset);

USTRUCT()
struct FTeamTrackingInfo
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<AIMGTeamPublicInfo> PublicInfo = nullptr;

	UPROPERTY()
	TObjectPtr<AIMGTeamPrivateInfo> PrivateInfo = nullptr;

	UPROPERTY()
	TObjectPtr<UIMGTeamDisplayAsset> DisplayAsset = nullptr;

	UPROPERTY()
	FOnActTeamDisplayAssetChangedDelegate OnTeamDisplayAssetChanged;

public:
	void SetTeamInfo(AIMGTeamInfoBase* Info);
	void RemoveTeamInfo(AIMGTeamInfoBase* Info);
};

// Result of comparing the team affiliation for two actors
UENUM(BlueprintType)
enum class ETeamComparison : uint8
{
	// Both actors are members of the same team
	OnSameTeam,

	// The actors are members of opposing teams
	DifferentTeams,

	// One (or both) of the actors was invalid or not part of any team
	InvalidArgument
};

/** A subsystem for easy access to team information for team-based actors (e.g., pawns or player states) */
UCLASS(MinimalAPI)
class UIMGTeamSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UE_API UIMGTeamSubsystem();

	//~USubsystem interface
	UE_API virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	UE_API virtual void Deinitialize() override;
	//~End of USubsystem interface

	// Tries to registers a new team
	UE_API bool RegisterTeamInfo(AIMGTeamInfoBase* TeamInfo);

	// Tries to unregister a team, will return false if it didn't work
	UE_API bool UnregisterTeamInfo(AIMGTeamInfoBase* TeamInfo);

	// Changes the team associated with this actor if possible
	// Note: This function can only be called on the authority
	UE_API bool ChangeTeamForActor(AActor* ActorToChange, int32 NewTeamId);

	// Returns the team this object belongs to, or INDEX_NONE if it is not part of a team
	UE_API int32 FindTeamFromObject(const UObject* TestObject) const;

	// Returns the associated player state for this actor, or INDEX_NONE if it is not associated with a player
	UE_API const AIMGPlayerState* FindPlayerStateFromActor(const AActor* PossibleTeamActor) const;

	// Returns the team this object belongs to, or INDEX_NONE if it is not part of a team
	UFUNCTION(BlueprintCallable, BlueprintPure=false, Category=Teams, meta=(Keywords="Get"))
	UE_API void FindTeamFromActor(const UObject* TestActor, bool& bIsPartOfTeam, int32& TeamId) const;

	// Compare the teams of two actors and returns a value indicating if they are on same teams, different teams, or one/both are invalid
	UFUNCTION(BlueprintCallable, BlueprintPure=false, Category=Teams, meta=(ExpandEnumAsExecs=ReturnValue))
	UE_API ETeamComparison CompareTeams(const UObject* A, const UObject* B, int32& TeamIdA, int32& TeamIdB) const;

	// Compare the teams of two actors and returns a value indicating if they are on same teams, different teams, or one/both are invalid
	UE_API ETeamComparison CompareTeams(const UObject* A, const UObject* B) const;

	// Returns true if the instigator can damage the target, taking into account the friendly fire settings
	UE_API bool CanCauseDamage(const UObject* Instigator, const UObject* Target, bool bAllowDamageToSelf = true) const;

	// Adds a specified number of stacks to the tag (does nothing if StackCount is below 1)
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category=Teams)
	UE_API void AddTeamTagStack(int32 TeamId, FGameplayTag Tag, int32 StackCount);

	// Removes a specified number of stacks from the tag (does nothing if StackCount is below 1)
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category=Teams)
	UE_API void RemoveTeamTagStack(int32 TeamId, FGameplayTag Tag, int32 StackCount);

	// Returns the stack count of the specified tag (or 0 if the tag is not present)
	UFUNCTION(BlueprintCallable, Category=Teams)
	UE_API int32 GetTeamTagStackCount(int32 TeamId, FGameplayTag Tag) const;

	// Returns true if there is at least one stack of the specified tag
	UFUNCTION(BlueprintCallable, Category=Teams)
	UE_API bool TeamHasTag(int32 TeamId, FGameplayTag Tag) const;

	// Returns true if the specified team exists
	UFUNCTION(BlueprintCallable, Category=Teams)
	UE_API bool DoesTeamExist(int32 TeamId) const;

	// Gets the team display asset for the specified team, from the perspective of the specified team
	// (You have to specify a viewer too, in case the game mode is in a 'local player is always blue team' sort of situation)
	UFUNCTION(BlueprintCallable, Category=Teams)
	UE_API UIMGTeamDisplayAsset* GetTeamDisplayAsset(int32 TeamId, int32 ViewerTeamId);

	// Gets the team display asset for the specified team, from the perspective of the specified team
	// (You have to specify a viewer too, in case the game mode is in a 'local player is always blue team' sort of situation)
	UFUNCTION(BlueprintCallable, Category = Teams)
	UE_API UIMGTeamDisplayAsset* GetEffectiveTeamDisplayAsset(int32 TeamId, UObject* ViewerTeamAgent);

	// Gets the list of teams
	UFUNCTION(BlueprintCallable, BlueprintPure=false, Category=Teams)
	UE_API TArray<int32> GetTeamIDs() const;

	// Called when a team display asset has been edited, causes all team color observers to update
	UE_API void NotifyTeamDisplayAssetModified(UIMGTeamDisplayAsset* ModifiedAsset);

	// Register for a team display asset notification for the specified team ID
	UE_API FOnActTeamDisplayAssetChangedDelegate& GetTeamDisplayAssetChangedDelegate(int32 TeamId);

private:
	UPROPERTY()
	TMap<int32, FTeamTrackingInfo> TeamMap;

	FDelegateHandle CheatManagerRegistrationHandle;
};

#undef UE_API
