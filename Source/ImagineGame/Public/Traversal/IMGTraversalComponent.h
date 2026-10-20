#pragma once

#include "Components/PawnComponent.h"
#include "Traversal/IMGTraversalTypes.h"

#include "IMGTraversalComponent.generated.h"

class UMotionWarpingComponent;

/** Configuration, network snapshot and warp-target bridge for a traversal ability. */
UCLASS(BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class IMAGINEGAME_API UIMGTraversalComponent : public UPawnComponent
{
	GENERATED_BODY()

public:
	UIMGTraversalComponent(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintPure, Category = "IMG|Traversal", meta = (DefaultToSelf = "Actor"))
	static UIMGTraversalComponent* Get(const AActor* Actor);

	UFUNCTION(BlueprintCallable, Category = "IMG|Traversal")
	bool Detect(FIMGTraversalCandidate& OutCandidate, EIMGTraversalFailure& OutFailure, FVector Direction) const;

	UFUNCTION(BlueprintPure, Category = "IMG|Traversal")
	UMotionWarpingComponent* GetMotionWarpingComponent() const;

	const FIMGTraversalQuery& GetQuerySettings() const { return QuerySettings; }
	void BeginTraversal(const FIMGTraversalCandidate& Candidate, EIMGTraversalAction Action);
	void EndTraversal();
	bool IsTraversalActive() const { return ActiveState.bActive; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Traversal|Detection")
	FIMGTraversalQuery QuerySettings;

	/** Matches the existing GASP FrontLedge warp contract without contaminating geometric measurements. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IMG|Traversal|Warping")
	float FrontLedgeVerticalOffset = 0.5f;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void OnRep_TraversalState();

	void RefreshWarpTargets();
	void RemoveWarpTargets();

	UPROPERTY(ReplicatedUsing = OnRep_TraversalState)
	FIMGTraversalRepState ReplicatedState;

	FIMGTraversalRepState ActiveState;
};
