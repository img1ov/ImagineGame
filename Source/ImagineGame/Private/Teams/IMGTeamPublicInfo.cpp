

#include "Teams/IMGTeamPublicInfo.h"

#include "Net/UnrealNetwork.h"
#include "Teams/IMGTeamInfoBase.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGTeamPublicInfo)

class FLifetimeProperty;

AIMGTeamPublicInfo::AIMGTeamPublicInfo(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void AIMGTeamPublicInfo::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(ThisClass, TeamDisplayAsset, COND_InitialOnly);
}

void AIMGTeamPublicInfo::SetTeamDisplayAsset(TObjectPtr<UIMGTeamDisplayAsset> NewDisplayAsset)
{
	check(HasAuthority());
	check(TeamDisplayAsset == nullptr);

	TeamDisplayAsset = NewDisplayAsset;

	TryRegisterWithTeamSubsystem();
}

void AIMGTeamPublicInfo::OnRep_TeamDisplayAsset()
{
	TryRegisterWithTeamSubsystem();
}

