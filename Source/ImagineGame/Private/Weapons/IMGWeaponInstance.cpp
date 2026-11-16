#include "Weapons/IMGWeaponInstance.h"

#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/AssertionMacros.h"
#include "GameFramework/InputDeviceSubsystem.h"
#include "GameFramework/InputDeviceProperties.h"
#include "Character/IMGHealthComponent.h"
#include "Animation/AnimInstance.h"
#include "Components/GameFrameworkComponentManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Cosmetics/IMGPawnComponent_CharacterParts.h"
#include "GameFramework/Character.h"
#include "IMGLogChannels.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGWeaponInstance)

class UAnimInstance;
struct FGameplayTagContainer;

UIMGWeaponInstance::UIMGWeaponInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Listen for death of the owning pawn so that any device properties can be removed if we
	// die and can't unequip
	if (APawn* Pawn = GetPawn())
	{
		// We only need to do this for player controlled pawns, since AI and others won't have input devices on the client
		if (Pawn->IsPlayerControlled())
		{
			if (UIMGHealthComponent* HealthComponent = UIMGHealthComponent::FindHealthComponent(GetPawn()))
			{
				HealthComponent->OnDeathStarted.AddDynamic(this, &ThisClass::OnDeathStarted);
			}
		}
	}
}

void UIMGWeaponInstance::OnEquipped()
{
	// Only enqueue recovery here. Blueprint still gets the first chance to link/play presentation.
	// Observing before the event also lets a synchronous unequip cancel this activation's recovery.
	StartAnimLayerObservation();
	Super::OnEquipped();

	UWorld* World = GetWorld();
	check(World);
	TimeLastEquipped = World->GetTimeSeconds();

	ApplyDeviceProperties();
}

void UIMGWeaponInstance::OnUnequipped()
{
	// Cancel recovery first; the Blueprint may need the current layer for its unequip montage.
	StopAnimLayerObservation();
	Super::OnUnequipped();

	RemoveDeviceProperties();
}

void UIMGWeaponInstance::StartAnimLayerObservation()
{
	APawn* Pawn = GetPawn();
	if (bObservingAnimLayer || !Pawn || Pawn->IsNetMode(NM_DedicatedServer) ||
		(!EquippedAnimSet.DefaultLayer && EquippedAnimSet.LayerRules.IsEmpty()))
	{
		return;
	}

	bObservingAnimLayer = true;
	if (UGameFrameworkComponentManager* Manager = UGameFrameworkComponentManager::GetForActor(Pawn))
	{
		PawnExtensionHandle = Manager->AddExtensionHandler(Pawn->GetClass(),
			UGameFrameworkComponentManager::FExtensionHandlerDelegate::CreateUObject(this, &ThisClass::HandlePawnExtension));
	}
	QueueAnimLayerRefresh();
}

void UIMGWeaponInstance::StopAnimLayerObservation()
{
	bObservingAnimLayer = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AnimLayerRefreshTimer);
	}
	PawnExtensionHandle.Reset();
	if (UIMGPawnComponent_CharacterParts* Customizer = ObservedPawnCustomizer.Get())
	{
		Customizer->OnCharacterPartsChanged.RemoveDynamic(this, &ThisClass::HandleCharacterPartsChanged);
	}
	ObservedPawnCustomizer.Reset();
	if (USkeletalMeshComponent* Mesh = ObservedMesh.Get())
	{
		Mesh->OnAnimInitialized.RemoveDynamic(this, &ThisClass::HandleAnimInitialized);
	}
	ObservedMesh.Reset();
	bWarnedMissingAnimationDependency = false;
	// Linked instances belong to the mesh. Blueprint owns layer transitions, including unequip/death.
	FallbackLinkedMesh.Reset();
	FallbackLinkedInstance.Reset();
}

void UIMGWeaponInstance::HandlePawnExtension(AActor* Actor, FName EventName)
{
	if (!bObservingAnimLayer || Actor != GetPawn())
	{
		return;
	}
	if (EventName == UGameFrameworkComponentManager::NAME_ReceiverRemoved)
	{
		StopAnimLayerObservation();
	}
	else if (EventName == UIMGPawnComponent_CharacterParts::NAME_CharacterPartsRemoved)
	{
		if (UIMGPawnComponent_CharacterParts* Customizer = ObservedPawnCustomizer.Get())
		{
			Customizer->OnCharacterPartsChanged.RemoveDynamic(this, &ThisClass::HandleCharacterPartsChanged);
		}
		ObservedPawnCustomizer.Reset();
		QueueAnimLayerRefresh();
	}
	else if (EventName == UIMGPawnComponent_CharacterParts::NAME_CharacterPartsReady)
	{
		QueueAnimLayerRefresh();
	}
}

void UIMGWeaponInstance::HandleCharacterPartsChanged(UIMGPawnComponent_CharacterParts* Component)
{
	if (Component == ObservedPawnCustomizer.Get())
	{
		QueueAnimLayerRefresh();
	}
}

void UIMGWeaponInstance::HandleAnimInitialized()
{
	QueueAnimLayerRefresh();
}

void UIMGWeaponInstance::QueueAnimLayerRefresh()
{
	if (bObservingAnimLayer && !AnimLayerRefreshTimer.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			// Parts may be replacing the mesh inside InitAnim; link after that stack has unwound.
			AnimLayerRefreshTimer = World->GetTimerManager().SetTimerForNextTick(this, &ThisClass::RefreshEquippedAnimLayer);
		}
	}
}

void UIMGWeaponInstance::RefreshEquippedAnimLayer()
{
	AnimLayerRefreshTimer.Invalidate();
	APawn* Pawn = GetPawn();
	if (!bObservingAnimLayer || !IsValid(Pawn) || Pawn->IsActorBeingDestroyed())
	{
		return;
	}
	if (const UIMGHealthComponent* Health = UIMGHealthComponent::FindHealthComponent(Pawn); Health && Health->IsDeadOrDying())
	{
		StopAnimLayerObservation();
		return;
	}

	ACharacter* Character = Cast<ACharacter>(Pawn);
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	if (ObservedMesh.Get() != Mesh)
	{
		FallbackLinkedMesh.Reset();
		FallbackLinkedInstance.Reset();
		if (USkeletalMeshComponent* OldMesh = ObservedMesh.Get())
		{
			OldMesh->OnAnimInitialized.RemoveDynamic(this, &ThisClass::HandleAnimInitialized);
		}
		ObservedMesh = Mesh;
		if (Mesh)
		{
			Mesh->OnAnimInitialized.AddUniqueDynamic(this, &ThisClass::HandleAnimInitialized);
		}
	}

	UIMGPawnComponent_CharacterParts* Customizer = Pawn->FindComponentByClass<UIMGPawnComponent_CharacterParts>();
	if (!IsValid(Customizer) || !Customizer->IsRegistered() || !Customizer->HasBegunPlay() || Customizer->IsBeingDestroyed())
	{
		Customizer = nullptr;
	}
	if (ObservedPawnCustomizer.Get() != Customizer)
	{
		if (UIMGPawnComponent_CharacterParts* OldCustomizer = ObservedPawnCustomizer.Get())
		{
			OldCustomizer->OnCharacterPartsChanged.RemoveDynamic(this, &ThisClass::HandleCharacterPartsChanged);
		}
		ObservedPawnCustomizer = Customizer;
		if (Customizer)
		{
			Customizer->OnCharacterPartsChanged.AddUniqueDynamic(this, &ThisClass::HandleCharacterPartsChanged);
		}
	}

	if (!Customizer || !Mesh || !Mesh->IsRegistered() || Mesh->IsBeingDestroyed() || !Mesh->GetAnimInstance())
	{
		if (!bWarnedMissingAnimationDependency)
		{
			bWarnedMissingAnimationDependency = true;
			UE_LOG(LogIMG, Warning, TEXT("Weapon %s: cannot refresh equipped animation on Pawn %s (Pawn Parts: %s, Mesh: %s, AnimInstance: %s). Waiting for Parts/animation initialization; check Game Feature components and the character mesh."),
				*GetPathName(), *GetPathNameSafe(Pawn), *GetNameSafe(Customizer), *GetNameSafe(Mesh), *GetNameSafe(Mesh ? Mesh->GetAnimInstance() : nullptr));
		}
		return;
	}

	// Preserve any valid equipped layer chosen by Blueprint, even if its tags differ from this snapshot.
	// A layer created by recovery may still need updating when the actual parts arrive after the component.
	const auto HasBlueprintLayer = [this, Mesh](TSubclassOf<UAnimInstance> Candidate)
	{
		UAnimInstance* Instance = Candidate ? Mesh->GetLinkedAnimLayerInstanceByClass(Candidate) : nullptr;
		return Instance && (FallbackLinkedMesh.Get() != Mesh || FallbackLinkedInstance.Get() != Instance);
	};
	if (HasBlueprintLayer(EquippedAnimSet.DefaultLayer) || EquippedAnimSet.LayerRules.ContainsByPredicate(
		[&HasBlueprintLayer](const FIMGAnimLayerSelectionEntry& Rule) { return HasBlueprintLayer(Rule.Layer); }))
	{
		bWarnedMissingAnimationDependency = false;
		return;
	}

	const TSubclassOf<UAnimInstance> Layer = PickBestAnimLayer(true, Customizer->GetCombinedTags(FGameplayTag()));
	if (!Layer)
	{
		if (!bWarnedMissingAnimationDependency)
		{
			bWarnedMissingAnimationDependency = true;
			UE_LOG(LogIMG, Warning, TEXT("Weapon %s: EquippedAnimSet has no matching layer or DefaultLayer for Pawn %s. Check animation layer rules and cosmetic tags."),
				*GetPathName(), *GetPathNameSafe(Pawn));
		}
		return;
	}

	if (!Mesh->GetLinkedAnimLayerInstanceByClass(Layer))
	{
		UAnimInstance* AnimInstance = Mesh->GetAnimInstance();
		Mesh->LinkAnimClassLayers(Layer);
		if (!bObservingAnimLayer || ObservedMesh.Get() != Mesh || ObservedPawnCustomizer.Get() != Customizer ||
			Mesh->GetAnimInstance() != AnimInstance)
		{
			return;
		}
		FallbackLinkedMesh = Mesh;
		FallbackLinkedInstance = Mesh->GetLinkedAnimLayerInstanceByClass(Layer);
		if (!FallbackLinkedInstance.IsValid())
		{
			if (!bWarnedMissingAnimationDependency)
			{
				bWarnedMissingAnimationDependency = true;
				UE_LOG(LogIMG, Warning, TEXT("Weapon %s: failed to link equipped layer %s on Pawn %s. Check skeleton compatibility and the animation layer interface."),
					*GetPathName(), *GetNameSafe(Layer.Get()), *GetPathNameSafe(Pawn));
			}
			return;
		}
	}
	bWarnedMissingAnimationDependency = false;
}

void UIMGWeaponInstance::UpdateFiringTime()
{
	UWorld* World = GetWorld();
	check(World);
	TimeLastFired = World->GetTimeSeconds();
}

float UIMGWeaponInstance::GetTimeSinceLastInteractedWith() const
{
	UWorld* World = GetWorld();
	check(World);
	const double WorldTime = World->GetTimeSeconds();

	double Result = WorldTime - TimeLastEquipped;

	if (TimeLastFired > 0.0)
	{
		const double TimeSinceFired = WorldTime - TimeLastFired;
		Result = FMath::Min(Result, TimeSinceFired);
	}

	return Result;
}

TSubclassOf<UAnimInstance> UIMGWeaponInstance::PickBestAnimLayer(bool bEquipped, const FGameplayTagContainer& CosmeticTags) const
{
	const FIMGAnimLayerSelectionSet& SetToQuery = (bEquipped ? EquippedAnimSet : UneuippedAnimSet);
	return SetToQuery.SelectBestLayer(CosmeticTags);
}

const FPlatformUserId UIMGWeaponInstance::GetOwningUserId() const
{
	if (const APawn* Pawn = GetPawn())
	{
		return Pawn->GetPlatformUserId();
	}
	return PLATFORMUSERID_NONE;
}

void UIMGWeaponInstance::ApplyDeviceProperties()
{
	const FPlatformUserId UserId = GetOwningUserId();

	if (UserId.IsValid())
	{
		if (UInputDeviceSubsystem* InputDeviceSubsystem = UInputDeviceSubsystem::Get())
		{
			for (TObjectPtr<UInputDeviceProperty>& DeviceProp : ApplicableDeviceProperties)
			{
				FActivateDevicePropertyParams Params = {};
				Params.UserId = UserId;

				// By default, the device property will be played on the Platform User's Primary Input Device.
				// If you want to override this and set a specific device, then you can set the DeviceId parameter.
				//Params.DeviceId = <some specific device id>;
				
				// Don't remove this property it was evaluated. We want the properties to be applied as long as we are holding the 
				// weapon, and will remove them manually in OnUnequipped
				Params.bLooping = true;
			
				DevicePropertyHandles.Emplace(InputDeviceSubsystem->ActivateDeviceProperty(DeviceProp, Params));
			}
		}	
	}
}

void UIMGWeaponInstance::RemoveDeviceProperties()
{
	const FPlatformUserId UserId = GetOwningUserId();
	
	if (UserId.IsValid() && !DevicePropertyHandles.IsEmpty())
	{
		// Remove any device properties that have been applied
		if (UInputDeviceSubsystem* InputDeviceSubsystem = UInputDeviceSubsystem::Get())
		{
			InputDeviceSubsystem->RemoveDevicePropertyHandles(DevicePropertyHandles);
			DevicePropertyHandles.Empty();
		}
	}
}

void UIMGWeaponInstance::OnDeathStarted(AActor* OwningActor)
{
	StopAnimLayerObservation();
	// Remove any possibly active device properties when we die to make sure that there aren't any lingering around
	RemoveDeviceProperties();
}
