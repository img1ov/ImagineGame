// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Components/GameFrameworkInitStateInterface.h"
#include "Components/PawnComponent.h"
#include "IMGPawnExtensionComponent.generated.h"

#define UE_API IMAGINEGAME_API

class UIMGPawnData;
class UIMGAbilitySystemComponent;
class AActor;

/**
 * Component that adds functionality to all Pawn classes so it can be used for characters/vehicles/etc.
 * This coordinates the initialization of other components.
 */
UCLASS(MinimalAPI)
class UIMGPawnExtensionComponent : public UPawnComponent, public IGameFrameworkInitStateInterface
{
	GENERATED_BODY()

public:
	
	UE_API UIMGPawnExtensionComponent(const class FObjectInitializer& ObjectInitializer);

	/** The name of this overall feature, this one depends on the other named component features */
	static const FName NAME_ActorFeatureName;

	//~ Begin I_GameFrameworkInitStateInterface interface
	virtual FName GetFeatureName() const override { return NAME_ActorFeatureName; }
	UE_API virtual bool CanChangeInitState(UGameFrameworkComponentManager* Manager, FGameplayTag CurrentState, FGameplayTag DesiredState) const override;
	UE_API virtual void HandleChangeInitState(UGameFrameworkComponentManager* Manager, FGameplayTag CurrentState, FGameplayTag DesiredState) override;
	UE_API virtual void OnActorInitStateChanged(const FActorInitStateChangedParams& Params) override;
	UE_API virtual void CheckDefaultInitialization() override;
	//~ End I_GameFrameworkInitStateInterface interface
	
	/** Returns the pawn extension component if one exists on the specified actor. */
	UFUNCTION(BlueprintPure, Category = "IMG|Pawn")
	static UIMGPawnExtensionComponent* FindPawnExtensionComponent(const AActor* Actor) { return (Actor ? Actor->FindComponentByClass<UIMGPawnExtensionComponent>() : nullptr); }

	/** Gets the pawn data, which is used to specify pawn properties in data */
	template<class T>
	const T* GetPawnData() const { return Cast<T>(PawnData); }

	/** Gets the pawn data */
	const UIMGPawnData* GetPawnData() const { return PawnData; }

	/** Sets the current pawn data */
	UE_API void SetPawnData(const UIMGPawnData* InPawnData);

	UFUNCTION(BlueprintPure, Category= "IMG|Pawn")
	UIMGAbilitySystemComponent* GetIMGAbilitySystemComponent() const { return AbilitySystemComponent; }

	/** Should be called by the owning pawn to become the avatar of the ability system. */
	UE_API void InitializeAbilitySystem(UIMGAbilitySystemComponent* InASC, AActor* InOwnerActor);

	/** Should be called by the owning pawn to remove itself as the avatar of the ability system. */
	UE_API void UninitializeAbilitySystem();

	/** Should be called by the owning pawn when the pawn's controller changes. */
	UE_API void HandleControllerChanged();

	/** Should be called by the owning pawn when the player state has been replicated. */
	UE_API void HandlePlayerStateReplicated();

	/** Should be called by the owning pawn when the input component is setup. */
	UE_API void SetupPlayerInputComponent();
	
	/** Register with the OnAbilitySystemInitialized delegate and broadcast if our pawn has been registered with the ability system component */
	UE_API void OnAbilitySystemInitialized_RegisterAndCall(FSimpleMulticastDelegate::FDelegate Delegate);

	/** Register with the OnAbilitySystemUninitialized delegate fired when our pawn is removed as the ability system's avatar actor */
	UE_API void OnAbilitySystemUninitialized_Register(FSimpleMulticastDelegate::FDelegate Delegate);

protected:
	
	UE_API virtual void OnRegister() override;
	UE_API virtual void BeginPlay() override;
	UE_API virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	
	UFUNCTION()
	UE_API void OnRep_PawnData();
	
protected:
	
	/** Delegate fired when our pawn becomes the ability system's avatar actor */
	FSimpleMulticastDelegate OnAbilitySystemInitialized;

	/** Delegate fired when our pawn is removed as the ability system's avatar actor */
	FSimpleMulticastDelegate OnAbilitySystemUninitialized;
	
	UPROPERTY(EditInstanceOnly, ReplicatedUsing = OnRep_PawnData, Category = "IMG|Pawn")
	TObjectPtr<const UIMGPawnData> PawnData;
	
	UPROPERTY(transient)
	TObjectPtr<UIMGAbilitySystemComponent> AbilitySystemComponent;
};

#undef UE_API