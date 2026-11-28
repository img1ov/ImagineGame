
#include "Feedback/NumberPops/IMGNumberPopComponent_NiagaraText.h"

#include "Feedback/NumberPops/IMGNumberPopComponent.h"
#include "Feedback/NumberPops/IMGDamagePopStyleNiagara.h"
#include "GameFramework/PlayerController.h"
#include "IMGLogChannels.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGNumberPopComponent_NiagaraText)

UIMGNumberPopComponent_NiagaraText::UIMGNumberPopComponent_NiagaraText(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{


}

void UIMGNumberPopComponent_NiagaraText::AddNumberPop(const FIMGNumberPopRequest& NewRequest)
{
	if (!IsValid(Style) || !Style->TextNiagara || !GetWorld())
	{
		return;
	}
	const APlayerController* PC = GetController<APlayerController>();
	if (PC && !PC->IsLocalController())
	{
		return;
	}
	float LocalDamage = static_cast<float>(NewRequest.NumberToDisplay);

	//Change Damage to negative to differentiate Critial vs Normal hit
	if (NewRequest.bIsCriticalDamage)
	{
		LocalDamage *= -1;
	}

	//Add a NiagaraComponent if we don't already have one
	if (!IsValid(NiagaraComp))
	{
		NiagaraComp = NewObject<UNiagaraComponent>(GetOwner());
		NiagaraComp->SetAsset(Style->TextNiagara);
		NiagaraComp->bAutoActivate = false;
		NiagaraComp->SetupAttachment(nullptr);
		check(NiagaraComp);
		NiagaraComp->RegisterComponent();
	}


	NiagaraComp->Activate(false);
	NiagaraComp->SetWorldLocation(NewRequest.WorldLocation);

	UE_LOG(LogIMG, VeryVerbose, TEXT("DamageHit location : %s"), *(NewRequest.WorldLocation.ToString()));
	//Add Damage information to the current Niagara list - Damage informations are packed inside a FVector4 where XYZ = Position, W = Damage
	TArray<FVector4> DamageList = UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayVector4(NiagaraComp, Style->NiagaraArrayName);
	DamageList.Add(FVector4(NewRequest.WorldLocation.X, NewRequest.WorldLocation.Y, NewRequest.WorldLocation.Z, LocalDamage));
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector4(NiagaraComp, Style->NiagaraArrayName, DamageList);

}

void UIMGNumberPopComponent_NiagaraText::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsValid(NiagaraComp))
	{
		NiagaraComp->DestroyComponent();
	}
	NiagaraComp = nullptr;
	Super::EndPlay(EndPlayReason);
}
