// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "ModularPlayerState.h"
#include "AbilitySystemInterface.h"
#include "Character/IMGPawnData.h"
#include "Teams/IMGTeamAgentInterface.h"

#include "IMGPlayerState.generated.h"

#define UE_API IMAGINEGAME_API

class UIMGExperienceDefinition;
class AController;
class AIMGPlayerController;
class APlayerState;
class UAbilitySystemComponent;
class UIMGAbilitySystemComponent;
class UIMGPawnData;

class UObject;
struct FFrame;
struct FGameplayTag;

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
	
	AIMGPlayerState(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintCallable, Category = "IMG|PlayerState")
	AIMGPlayerController* GetIMGPlayerController() const;

	UFUNCTION(BlueprintCallable, Category = "IMG|PlayerState")
	UIMGAbilitySystemComponent* GetIMGAbilitySystemComponent() const { return AbilitySystemComponent; }
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	template<class T>
	const T* GetPawnData() const { return Cast<T>(PawnData); }

	void SetPawnData(const UIMGPawnData* InPawnData);
	
	//~AActor interface
	virtual void PreInitializeComponents() override;
	virtual void PostInitializeComponents() override;
	//~End of AActor interface
	
	//~IIMGTeamAgentInterface interface
	UE_API virtual void SetGenericTeamId(const FGenericTeamId& NewTeamID) override;
	UE_API virtual FGenericTeamId GetGenericTeamId() const override;
	UE_API virtual FOnIMGTeamIndexChangedDelegate* GetOnTeamIndexChangedDelegate() override;
	//~End of IIMGTeamAgentInterface interface
	
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
	
	// Gets the replicated view rotation of this player, used for spectating
	UE_API FRotator GetReplicatedViewRotation() const;

	// Sets the replicated view rotation, only valid on the server
	UE_API void SetReplicatedViewRotation(const FRotator& NewRotation);

protected:
	
	UFUNCTION()
	void OnRep_PawnData();

private:
	
	void OnExperienceLoaded(const UIMGExperienceDefinition* CurrentExperience);

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
	
	UPROPERTY()
	FOnIMGTeamIndexChangedDelegate OnTeamChangedDelegate;
	
	UPROPERTY(ReplicatedUsing=OnRep_MyTeamID)
	FGenericTeamId MyTeamID;
	
	UPROPERTY(ReplicatedUsing=OnRep_MySquadID)
	int32 MySquadID;
	
	UPROPERTY(Replicated)
	FRotator ReplicatedViewRotation;
	
private:
	
	UFUNCTION()
	UE_API void OnRep_MyTeamID(FGenericTeamId OldTeamID);
	
	UFUNCTION()
	UE_API void OnRep_MySquadID();
	
};

#undef UE_API
