

#pragma once

#include "IMGTeamInfoBase.h"

#include "IMGTeamPublicInfo.generated.h"

class UIMGTeamCreationComponent;
class UIMGTeamDisplayAsset;
class UObject;
struct FFrame;

UCLASS()
class AIMGTeamPublicInfo : public AIMGTeamInfoBase
{
	GENERATED_BODY()

	friend UIMGTeamCreationComponent;

public:
	AIMGTeamPublicInfo(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UIMGTeamDisplayAsset* GetTeamDisplayAsset() const { return TeamDisplayAsset; }

private:
	UFUNCTION()
	void OnRep_TeamDisplayAsset();

	void SetTeamDisplayAsset(TObjectPtr<UIMGTeamDisplayAsset> NewDisplayAsset);

private:
	UPROPERTY(ReplicatedUsing=OnRep_TeamDisplayAsset)
	TObjectPtr<UIMGTeamDisplayAsset> TeamDisplayAsset;
};
