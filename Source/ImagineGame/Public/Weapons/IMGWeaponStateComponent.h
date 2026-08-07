#pragma once

#include "Components/ControllerComponent.h"
#include "GameplayTagContainer.h"

#include "IMGWeaponStateComponent.generated.h"

class UObject;
struct FFrame;
struct FGameplayAbilityTargetDataHandle;
struct FGameplayEffectContextHandle;
struct FHitResult;

// Hit markers are shown for ranged weapon impacts in the reticle
// A 'successful' hit marker is shown for impacts that damaged an enemy
struct FIMGScreenSpaceHitLocation
{
	/** Hit location in viewport screenspace */
	FVector2D Location;	
	FGameplayTag HitZone;
	bool bShowAsSuccess = false;
};

struct FIMGServerSideHitMarkerBatch
{
	FIMGServerSideHitMarkerBatch() { }

	FIMGServerSideHitMarkerBatch(uint8 InUniqueId) :
		UniqueId(InUniqueId)
	{ }

	TArray<FIMGScreenSpaceHitLocation> Markers;

	uint8 UniqueId = 0;
};

// Tracks weapon state and recent confirmed hit markers to display on screen
UCLASS()
class UIMGWeaponStateComponent : public UControllerComponent
{
	GENERATED_BODY()

public:

	UIMGWeaponStateComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(Client, Reliable)
	void ClientConfirmTargetData(uint16 UniqueId, bool bSuccess, const TArray<uint8>& HitReplaces);

	void AddUnconfirmedServerSideHitMarkers(const FGameplayAbilityTargetDataHandle& InTargetData, const TArray<FHitResult>& FoundHits);

	/** Updates this player's last damage instigated time */
	void UpdateDamageInstigatedTime(const FGameplayEffectContextHandle& EffectContext);

	/** Gets the array of most recent locations this player instigated damage, in screen-space */
	void GetLastWeaponDamageScreenLocations(TArray<FIMGScreenSpaceHitLocation>& WeaponDamageScreenLocations)
	{
		WeaponDamageScreenLocations = LastWeaponDamageScreenLocations;
	}

	/** Returns the elapsed time since the last (outgoing) damage hit notification occurred */
	double GetTimeSinceLastHitNotification() const;

	int32 GetUnconfirmedServerSideHitMarkerCount() const
	{
		return UnconfirmedServerSideHitMarkers.Num();
	}

protected:
	// This is called to filter hit results to determine whether they should be considered as a successful hit or not
	// The default behavior is to treat it as a success if being done to a team actor that belongs to a different team
	// to the owning controller's pawn
	virtual bool ShouldShowHitAsSuccess(const FHitResult& Hit) const;

	virtual bool ShouldUpdateDamageInstigatedTime(const FGameplayEffectContextHandle& EffectContext) const;

	void ActuallyUpdateDamageInstigatedTime();

private:
	/** Last time this controller instigated weapon damage */
	double LastWeaponDamageInstigatedTime = 0.0;

	/** Screen-space locations of our most recently instigated weapon damage (the confirmed hits) */
	TArray<FIMGScreenSpaceHitLocation> LastWeaponDamageScreenLocations;

	/** The unconfirmed hits */
	TArray<FIMGServerSideHitMarkerBatch> UnconfirmedServerSideHitMarkers;
};
