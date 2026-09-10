// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Feedback/NumberPops/IMGNumberPopComponent.h"

#include "IMGNumberPopComponent_NiagaraText.generated.h"

class UIMGDamagePopStyleNiagara;
class UNiagaraComponent;
class UObject;

UCLASS(Blueprintable)
class IMAGINEGAME_API UIMGNumberPopComponent_NiagaraText : public UIMGNumberPopComponent
{
	GENERATED_BODY()

public:

	UIMGNumberPopComponent_NiagaraText(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~UIMGNumberPopComponent interface
	virtual void AddNumberPop(const FIMGNumberPopRequest& NewRequest) override;
	//~End of UIMGNumberPopComponent interface

protected:

	TArray<int32> DamageNumberArray;

	/** Style patterns to attempt to apply to the incoming number pops */
	UPROPERTY(EditDefaultsOnly, Category = "Number Pop|Style")
	TObjectPtr<UIMGDamagePopStyleNiagara> Style;

	//Niagara Component used to display the damage
	UPROPERTY(EditDefaultsOnly, Category = "Number Pop|Style")
	TObjectPtr<UNiagaraComponent> NiagaraComp;
};
