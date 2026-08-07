// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Teams/IMGTeamInfoBase.h"

#include "IMGTeamPrivateInfo.generated.h"

class UObject;

UCLASS()
class IMAGINEGAME_API AIMGTeamPrivateInfo : public AIMGTeamInfoBase
{
	GENERATED_BODY()
	
public:
	AIMGTeamPrivateInfo(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
