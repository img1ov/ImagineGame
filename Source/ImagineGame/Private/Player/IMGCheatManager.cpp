// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/IMGCheatManager.h"
#include "GameFramework/Pawn.h"
#include "Player/IMGPlayerController.h"
#include "Player/IMGDebugCameraController.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Console.h"
#include "GameFramework/HUD.h"
#include "System/IMGAssetManager.h"
#include "System/IMGGameData.h"
#include "IMGGameplayTags.h"
#include "AbilitySystem/IMGAbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Character/IMGHealthComponent.h"
#include "Character/IMGPawnExtensionComponent.h"
#include "System/IMGSystemStatics.h"
#include "Development/IMGDeveloperSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGCheatManager)

DEFINE_LOG_CATEGORY(LogIMGCheat);

namespace IMGCheat
{
	static const FName NAME_Fixed = FName(TEXT("Fixed"));

	static bool bEnableDebugCameraCycling = false;
	static FAutoConsoleVariableRef CVarEnableDebugCameraCycling(
		TEXT("IMGCheat.EnableDebugCameraCycling"),
		bEnableDebugCameraCycling,
		TEXT("If true then you can cycle the debug camera while running the game."),
		ECVF_Cheat);

	static bool bStartInGodMode = false;
	static FAutoConsoleVariableRef CVarStartInGodMode(
		TEXT("IMGCheat.StartInGodMode"),
		bStartInGodMode,
		TEXT("If true then the God cheat will be applied on begin play"),
		ECVF_Cheat);
};


UIMGCheatManager::UIMGCheatManager()
{
	DebugCameraControllerClass = AIMGDebugCameraController::StaticClass();
}

void UIMGCheatManager::InitCheatManager()
{
	Super::InitCheatManager();

#if WITH_EDITOR
	if (GIsEditor)
	{
		APlayerController* PC = GetOuterAPlayerController();
		for (const FIMGCheatToRun& CheatRow : GetDefault<UIMGDeveloperSettings>()->CheatsToRun)
		{
			if (CheatRow.Phase == ECheatExecutionTime::OnCheatManagerCreated)
			{
				PC->ConsoleCommand(CheatRow.Cheat, /*bWriteToLog=*/ true);
			}
		}
	}
#endif

	if (IMGCheat::bStartInGodMode)
	{
		God();
	}
}

void UIMGCheatManager::CheatOutputText(const FString& TextToOutput)
{
#if USING_CHEAT_MANAGER
	// Output to the console.
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->ViewportConsole)
	{
		GEngine->GameViewport->ViewportConsole->OutputText(TextToOutput);
	}

	// Output to log.
	UE_LOG(LogIMGCheat, Display, TEXT("%s"), *TextToOutput);
#endif // USING_CHEAT_MANAGER
}

void UIMGCheatManager::Cheat(const FString& Msg)
{
	if (AIMGPlayerController* IMGPC = Cast<AIMGPlayerController>(GetOuterAPlayerController()))
	{
		IMGPC->ServerCheat(Msg.Left(128));
	}
}

void UIMGCheatManager::CheatAll(const FString& Msg)
{
	if (AIMGPlayerController* IMGPC = Cast<AIMGPlayerController>(GetOuterAPlayerController()))
	{
		IMGPC->ServerCheatAll(Msg.Left(128));
	}
}

void UIMGCheatManager::PlayNextGame()
{
	UIMGSystemStatics::PlayNextGame(this);
}

void UIMGCheatManager::EnableDebugCamera()
{
	Super::EnableDebugCamera();
}

void UIMGCheatManager::DisableDebugCamera()
{
	FVector DebugCameraLocation;
	FRotator DebugCameraRotation;

	ADebugCameraController* DebugCC = Cast<ADebugCameraController>(GetOuter());
	APlayerController* OriginalPC = nullptr;

	if (DebugCC)
	{
		OriginalPC = DebugCC->OriginalControllerRef;
		DebugCC->GetPlayerViewPoint(DebugCameraLocation, DebugCameraRotation);
	}

	Super::DisableDebugCamera();

	if (OriginalPC && OriginalPC->PlayerCameraManager && (OriginalPC->PlayerCameraManager->CameraStyle == IMGCheat::NAME_Fixed))
	{
		OriginalPC->SetInitialLocationAndRotation(DebugCameraLocation, DebugCameraRotation);

		OriginalPC->PlayerCameraManager->ViewTarget.POV.Location = DebugCameraLocation;
		OriginalPC->PlayerCameraManager->ViewTarget.POV.Rotation = DebugCameraRotation;
		OriginalPC->PlayerCameraManager->PendingViewTarget.POV.Location = DebugCameraLocation;
		OriginalPC->PlayerCameraManager->PendingViewTarget.POV.Rotation = DebugCameraRotation;
	}
}

bool UIMGCheatManager::InDebugCamera() const
{
	return (Cast<ADebugCameraController>(GetOuter()) ? true : false);
}

void UIMGCheatManager::EnableFixedCamera()
{
	const ADebugCameraController* DebugCC = Cast<ADebugCameraController>(GetOuter());
	APlayerController* PC = (DebugCC ? ToRawPtr(DebugCC->OriginalControllerRef) : GetOuterAPlayerController());

	if (PC && PC->PlayerCameraManager)
	{
		PC->SetCameraMode(IMGCheat::NAME_Fixed);
	}
}

void UIMGCheatManager::DisableFixedCamera()
{
	const ADebugCameraController* DebugCC = Cast<ADebugCameraController>(GetOuter());
	APlayerController* PC = (DebugCC ? ToRawPtr(DebugCC->OriginalControllerRef) : GetOuterAPlayerController());

	if (PC && PC->PlayerCameraManager)
	{
		PC->SetCameraMode(NAME_Default);
	}
}

bool UIMGCheatManager::InFixedCamera() const
{
	const ADebugCameraController* DebugCC = Cast<ADebugCameraController>(GetOuter());
	const APlayerController* PC = (DebugCC ? ToRawPtr(DebugCC->OriginalControllerRef) : GetOuterAPlayerController());

	if (PC && PC->PlayerCameraManager)
	{
		return (PC->PlayerCameraManager->CameraStyle == IMGCheat::NAME_Fixed);
	}

	return false;
}

void UIMGCheatManager::ToggleFixedCamera()
{
	if (InFixedCamera())
	{
		DisableFixedCamera();
	}
	else
	{
		EnableFixedCamera();
	}
}

void UIMGCheatManager::CycleDebugCameras()
{
	if (!IMGCheat::bEnableDebugCameraCycling)
	{
		return;
	}

	if (InDebugCamera())
	{
		EnableFixedCamera();
		DisableDebugCamera();
	}
	else if (InFixedCamera())
	{
		DisableFixedCamera();
		DisableDebugCamera();
	}
	else
	{
		EnableDebugCamera();
		DisableFixedCamera();
	}
}

void UIMGCheatManager::CycleAbilitySystemDebug()
{
	APlayerController* PC = Cast<APlayerController>(GetOuterAPlayerController());

	if (PC && PC->MyHUD)
	{
		if (!PC->MyHUD->bShowDebugInfo || !PC->MyHUD->DebugDisplay.Contains(TEXT("AbilitySystem")))
		{
			PC->MyHUD->ShowDebug(TEXT("AbilitySystem"));
		}

		PC->ConsoleCommand(TEXT("AbilitySystem.Debug.NextCategory"));
	}
}

void UIMGCheatManager::CancelActivatedAbilities()
{
	if (UIMGAbilitySystemComponent* IMGASC = GetPlayerAbilitySystemComponent())
	{
		const bool bReplicateCancelAbility = true;
		IMGASC->CancelInputActivatedAbilities(bReplicateCancelAbility);
	}
}

void UIMGCheatManager::AddTagToSelf(FString TagName)
{
	FGameplayTag Tag = IMGGameplayTags::FindTagByString(TagName, true);
	if (Tag.IsValid())
	{
		if (UIMGAbilitySystemComponent* IMGASC = GetPlayerAbilitySystemComponent())
		{
			IMGASC->AddDynamicTagGameplayEffect(Tag);
		}
	}
	else
	{
		UE_LOG(LogIMGCheat, Display, TEXT("AddTagToSelf: Could not find any tag matching [%s]."), *TagName);
	}
}

void UIMGCheatManager::RemoveTagFromSelf(FString TagName)
{
	FGameplayTag Tag = IMGGameplayTags::FindTagByString(TagName, true);
	if (Tag.IsValid())
	{
		if (UIMGAbilitySystemComponent* IMGASC = GetPlayerAbilitySystemComponent())
		{
			IMGASC->RemoveDynamicTagGameplayEffect(Tag);
		}
	}
	else
	{
		UE_LOG(LogIMGCheat, Display, TEXT("RemoveTagFromSelf: Could not find any tag matching [%s]."), *TagName);
	}
}

void UIMGCheatManager::DamageSelf(float DamageAmount)
{
	if (UIMGAbilitySystemComponent* IMGASC = GetPlayerAbilitySystemComponent())
	{
		ApplySetByCallerDamage(IMGASC, DamageAmount);
	}
}

void UIMGCheatManager::DamageTarget(float DamageAmount)
{
	if (AIMGPlayerController* IMGPC = Cast<AIMGPlayerController>(GetOuterAPlayerController()))
	{
		if (IMGPC->GetNetMode() == NM_Client)
		{
			// Automatically send cheat to server for convenience.
			IMGPC->ServerCheat(FString::Printf(TEXT("DamageTarget %.2f"), DamageAmount));
			return;
		}

		FHitResult TargetHitResult;
		AActor* TargetActor = GetTarget(IMGPC, TargetHitResult);

		if (UIMGAbilitySystemComponent* IMGTargetASC = Cast<UIMGAbilitySystemComponent>(UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(TargetActor)))
		{
			ApplySetByCallerDamage(IMGTargetASC, DamageAmount);
		}
	}
}

void UIMGCheatManager::ApplySetByCallerDamage(UIMGAbilitySystemComponent* IMGASC, float DamageAmount)
{
	check(IMGASC);

	TSubclassOf<UGameplayEffect> DamageGE = UIMGAssetManager::GetSubclass(UIMGGameData::Get().DamageGameplayEffect_SetByCaller);
	FGameplayEffectSpecHandle SpecHandle = IMGASC->MakeOutgoingSpec(DamageGE, 1.0f, IMGASC->MakeEffectContext());

	if (SpecHandle.IsValid())
	{
		SpecHandle.Data->SetSetByCallerMagnitude(IMGGameplayTags::SetByCaller_Damage, DamageAmount);
		IMGASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
	}
}

void UIMGCheatManager::HealSelf(float HealAmount)
{
	if (UIMGAbilitySystemComponent* IMGASC = GetPlayerAbilitySystemComponent())
	{
		ApplySetByCallerHeal(IMGASC, HealAmount);
	}
}

void UIMGCheatManager::HealTarget(float HealAmount)
{
	if (AIMGPlayerController* IMGPC = Cast<AIMGPlayerController>(GetOuterAPlayerController()))
	{
		FHitResult TargetHitResult;
		AActor* TargetActor = GetTarget(IMGPC, TargetHitResult);

		if (UIMGAbilitySystemComponent* IMGTargetASC = Cast<UIMGAbilitySystemComponent>(UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(TargetActor)))
		{
			ApplySetByCallerHeal(IMGTargetASC, HealAmount);
		}
	}
}

void UIMGCheatManager::ApplySetByCallerHeal(UIMGAbilitySystemComponent* IMGASC, float HealAmount)
{
	check(IMGASC);

	TSubclassOf<UGameplayEffect> HealGE = UIMGAssetManager::GetSubclass(UIMGGameData::Get().HealGameplayEffect_SetByCaller);
	FGameplayEffectSpecHandle SpecHandle = IMGASC->MakeOutgoingSpec(HealGE, 1.0f, IMGASC->MakeEffectContext());

	if (SpecHandle.IsValid())
	{
		SpecHandle.Data->SetSetByCallerMagnitude(IMGGameplayTags::SetByCaller_Heal, HealAmount);
		IMGASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
	}
}

UIMGAbilitySystemComponent* UIMGCheatManager::GetPlayerAbilitySystemComponent() const
{
	if (AIMGPlayerController* IMGPC = Cast<AIMGPlayerController>(GetOuterAPlayerController()))
	{
		return IMGPC->GetIMGAbilitySystemComponent();
	}
	return nullptr;
}

void UIMGCheatManager::DamageSelfDestruct()
{
	if (AIMGPlayerController* IMGPC = Cast<AIMGPlayerController>(GetOuterAPlayerController()))
	{
 		if (const UIMGPawnExtensionComponent* PawnExtComp = UIMGPawnExtensionComponent::FindPawnExtensionComponent(IMGPC->GetPawn()))
		{
			if (PawnExtComp->HasReachedInitState(IMGGameplayTags::InitState_GameplayReady))
			{
				if (UIMGHealthComponent* HealthComponent = UIMGHealthComponent::FindHealthComponent(IMGPC->GetPawn()))
				{
					HealthComponent->DamageSelfDestruct();
				}
			}
		}
	}
}

void UIMGCheatManager::God()
{
	if (AIMGPlayerController* IMGPC = Cast<AIMGPlayerController>(GetOuterAPlayerController()))
	{
		if (IMGPC->GetNetMode() == NM_Client)
		{
			// Automatically send cheat to server for convenience.
			IMGPC->ServerCheat(FString::Printf(TEXT("God")));
			return;
		}

		if (UIMGAbilitySystemComponent* IMGASC = IMGPC->GetIMGAbilitySystemComponent())
		{
			const FGameplayTag Tag = IMGGameplayTags::Cheat_GodMode;
			const bool bHasTag = IMGASC->HasMatchingGameplayTag(Tag);

			if (bHasTag)
			{
				IMGASC->RemoveDynamicTagGameplayEffect(Tag);
			}
			else
			{
				IMGASC->AddDynamicTagGameplayEffect(Tag);
			}
		}
	}
}

void UIMGCheatManager::UnlimitedHealth(int32 Enabled)
{
	if (UIMGAbilitySystemComponent* IMGASC = GetPlayerAbilitySystemComponent())
	{
		const FGameplayTag Tag = IMGGameplayTags::Cheat_UnlimitedHealth;
		const bool bHasTag = IMGASC->HasMatchingGameplayTag(Tag);

		if ((Enabled == -1) || ((Enabled > 0) && !bHasTag) || ((Enabled == 0) && bHasTag))
		{
			if (bHasTag)
			{
				IMGASC->RemoveDynamicTagGameplayEffect(Tag);
			}
			else
			{
				IMGASC->AddDynamicTagGameplayEffect(Tag);
			}
		}
	}
}
