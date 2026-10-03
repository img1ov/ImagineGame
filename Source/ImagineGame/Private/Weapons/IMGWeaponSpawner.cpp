#include "Weapons/IMGWeaponSpawner.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Equipment/IMGPickupDefinition.h"
#include "Equipment/IMGQuickBarComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Inventory/IMGInventoryItemInstance.h"
#include "Inventory/IMGInventoryManagerComponent.h"
#include "Inventory/InventoryFragment_SetStats.h"
#include "Kismet/GameplayStatics.h"
#include "IMGLogChannels.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGWeaponSpawner)

class FLifetimeProperty;
class UNiagaraSystem;
class USoundBase;
struct FHitResult;

// Sets default values
AIMGWeaponSpawner::AIMGWeaponSpawner()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	RootComponent = CollisionVolume = CreateDefaultSubobject<UCapsuleComponent>(TEXT("CollisionVolume"));
	CollisionVolume->InitCapsuleSize(80.f, 80.f);
	CollisionVolume->OnComponentBeginOverlap.AddDynamic(this, &AIMGWeaponSpawner::OnOverlapBegin);

	PadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PadMesh"));
	PadMesh->SetupAttachment(RootComponent);

	WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	WeaponMesh->SetupAttachment(RootComponent);

	WeaponMeshRotationSpeed = 40.0f;
	CoolDownTime = 30.0f;
	CheckExistingOverlapDelay = 0.25f;
	bIsWeaponAvailable = true;
	bReplicates = true;
}

// Called when the game starts or when spawned
void AIMGWeaponSpawner::BeginPlay()
{
	Super::BeginPlay();

	if (WeaponDefinition && WeaponDefinition->InventoryItemDefinition)
	{
		CoolDownTime = WeaponDefinition->SpawnCoolDownSeconds;	
	}
	else if (const UWorld* World = GetWorld())
	{
		if (!World->IsPlayingReplay())
		{
			UE_LOG(LogIMG, Error, TEXT("'%s' does not have a valid weapon definition! Make sure to set this data on the instance!"), *GetNameSafe(this));	
		}
	}
}

void AIMGWeaponSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CoolDownTimerHandle);
		World->GetTimerManager().ClearTimer(CheckOverlapsDelayTimerHandle);
	}
	
	Super::EndPlay(EndPlayReason);
}

// Called every frame
void AIMGWeaponSpawner::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	//Update the CoolDownPercentage property to drive respawn time indicators
	UWorld* World = GetWorld();
	if (World->GetTimerManager().IsTimerActive(CoolDownTimerHandle))
	{
		CoolDownPercentage = 1.0f - World->GetTimerManager().GetTimerRemaining(CoolDownTimerHandle)/CoolDownTime;
	}

	WeaponMesh->AddRelativeRotation(FRotator(0.0f, World->GetDeltaSeconds() * WeaponMeshRotationSpeed, 0.0f));
}

void AIMGWeaponSpawner::OnConstruction(const FTransform& Transform)
{
	if (WeaponDefinition != nullptr && WeaponDefinition->DisplayMesh != nullptr)
	{
		WeaponMesh->SetStaticMesh(WeaponDefinition->DisplayMesh);
		WeaponMesh->SetRelativeLocation(WeaponDefinition->WeaponMeshOffset);
		WeaponMesh->SetRelativeScale3D(WeaponDefinition->WeaponMeshScale);
	}	
}

void AIMGWeaponSpawner::OnOverlapBegin(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepHitResult)
{
	APawn* OverlappingPawn = Cast<APawn>(OtherActor);
	if (GetLocalRole() == ROLE_Authority && bIsWeaponAvailable && OverlappingPawn != nullptr)
	{
		AttemptPickUpWeapon(OverlappingPawn);
	}
}

void AIMGWeaponSpawner::CheckForExistingOverlaps()
{
	TArray<AActor*> OverlappingActors;
	GetOverlappingActors(OverlappingActors, APawn::StaticClass());

	for (AActor* OverlappingActor : OverlappingActors)
	{
		AttemptPickUpWeapon(Cast<APawn>(OverlappingActor));
	}
}

void AIMGWeaponSpawner::AttemptPickUpWeapon(APawn* Pawn)
{
	if (GetLocalRole() == ROLE_Authority && bIsWeaponAvailable && UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Pawn))
	{
		TSubclassOf<UIMGInventoryItemDefinition> WeaponItemDefinition = WeaponDefinition ? WeaponDefinition->InventoryItemDefinition : nullptr;
		if (WeaponItemDefinition != nullptr)
		{
			//Attempt to grant the weapon
			if (GiveWeapon(WeaponItemDefinition, Pawn))
			{
				//Weapon picked up by pawn
				bIsWeaponAvailable = false;
				SetWeaponPickupVisibility(false);
				PlayPickupEffects();
				StartCoolDown();
			}
		}		
	}
}

bool AIMGWeaponSpawner::GiveWeapon(TSubclassOf<UIMGInventoryItemDefinition> WeaponItemClass, APawn* ReceivingPawn)
{
	if (!HasAuthority() || !WeaponItemClass || !ReceivingPawn)
	{
		return false;
	}

	AController* Controller = ReceivingPawn->GetController();
	UIMGInventoryManagerComponent* Inventory = Controller ? Controller->FindComponentByClass<UIMGInventoryManagerComponent>() : nullptr;
	UIMGQuickBarComponent* QuickBar = Controller ? Controller->FindComponentByClass<UIMGQuickBarComponent>() : nullptr;
	if (!Inventory || !QuickBar)
	{
		return false;
	}

	if (WeaponDefinition && WeaponDefinition->bGiveAmmoForDuplicateWeapons)
	{
		if (UIMGInventoryItemInstance* ExistingItem = Inventory->FindFirstItemStackByDefinition(WeaponItemClass))
		{
			const FGameplayTag AmmoTag = WeaponDefinition->SpareAmmoTag;
			if (!AmmoTag.IsValid())
			{
				return false;
			}

			const int32 DesiredAmmo = GetDefaultStatFromItemDef(WeaponItemClass, AmmoTag);
			const int32 AmmoToAdd = DesiredAmmo - ExistingItem->GetStatTagStackCount(AmmoTag);
			if (AmmoToAdd <= 0)
			{
				return false;
			}

			ExistingItem->AddStatTagStack(AmmoTag, AmmoToAdd);
			return true;
		}
	}

	if (!Inventory->CanAddItemDefinition(WeaponItemClass, 1))
	{
		return false;
	}

	const int32 SlotIndex = QuickBar->GetNextFreeItemSlot();
	if (SlotIndex == INDEX_NONE)
	{
		return false;
	}

	UIMGInventoryItemInstance* Item = Inventory->AddItemDefinition(WeaponItemClass, 1);
	if (!Item)
	{
		return false;
	}

	QuickBar->AddItemToSlot(SlotIndex, Item);
	if (QuickBar->GetSlots()[SlotIndex] != Item)
	{
		Inventory->RemoveItemInstance(Item);
		return false;
	}

	const FGameplayTag BlockAutoEquipTag = WeaponDefinition ? WeaponDefinition->BlockAutoEquipTag : FGameplayTag();
	const UAbilitySystemComponent* AbilitySystem = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(ReceivingPawn);
	if (!BlockAutoEquipTag.IsValid() || !AbilitySystem || !AbilitySystem->HasMatchingGameplayTag(BlockAutoEquipTag))
	{
		QuickBar->SetActiveSlotIndex(SlotIndex);
	}
	return true;
}

void AIMGWeaponSpawner::StartCoolDown()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(CoolDownTimerHandle, this, &AIMGWeaponSpawner::OnCoolDownTimerComplete, CoolDownTime);
	}
}

void AIMGWeaponSpawner::ResetCoolDown()
{
	UWorld* World = GetWorld();

	if (World)
	{
		World->GetTimerManager().ClearTimer(CoolDownTimerHandle);
	}

	if (GetLocalRole() == ROLE_Authority)
	{
		bIsWeaponAvailable = true;
		PlayRespawnEffects();
		SetWeaponPickupVisibility(true);

		if (World)
		{
			World->GetTimerManager().SetTimer(CheckOverlapsDelayTimerHandle, this, &AIMGWeaponSpawner::CheckForExistingOverlaps, CheckExistingOverlapDelay);
		}
	}

	CoolDownPercentage = 0.0f;
}

void AIMGWeaponSpawner::OnCoolDownTimerComplete()
{
	ResetCoolDown();
}

void AIMGWeaponSpawner::SetWeaponPickupVisibility(bool bShouldBeVisible)
{
	WeaponMesh->SetVisibility(bShouldBeVisible, true);
}

void AIMGWeaponSpawner::PlayPickupEffects_Implementation()
{
	if (WeaponDefinition != nullptr)
	{
		USoundBase* PickupSound = WeaponDefinition->PickedUpSound;
		if (PickupSound != nullptr)
		{
			UGameplayStatics::PlaySoundAtLocation(this, PickupSound, GetActorLocation());
		}

		UNiagaraSystem* PickupEffect = WeaponDefinition->PickedUpEffect;
		if (PickupEffect != nullptr)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, PickupEffect, WeaponMesh->GetComponentLocation());
		}
	}
}

void AIMGWeaponSpawner::PlayRespawnEffects_Implementation()
{
	if (WeaponDefinition != nullptr)
	{
		USoundBase* RespawnSound = WeaponDefinition->RespawnedSound;
		if (RespawnSound != nullptr)
		{
			UGameplayStatics::PlaySoundAtLocation(this, RespawnSound, GetActorLocation());
		}

		UNiagaraSystem* RespawnEffect = WeaponDefinition->RespawnedEffect;
		if (RespawnEffect != nullptr)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, RespawnEffect, WeaponMesh->GetComponentLocation());
		}
	}
}

void AIMGWeaponSpawner::OnRep_WeaponAvailability()
{
	if (bIsWeaponAvailable)
	{
		PlayRespawnEffects();
		SetWeaponPickupVisibility(true);
	}
	else
	{
		SetWeaponPickupVisibility(false);
		StartCoolDown();
		PlayPickupEffects();
	}	
}

void AIMGWeaponSpawner::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AIMGWeaponSpawner, bIsWeaponAvailable);
}

int32 AIMGWeaponSpawner::GetDefaultStatFromItemDef(const TSubclassOf<UIMGInventoryItemDefinition> WeaponItemClass, FGameplayTag StatTag)
{
	if (WeaponItemClass != nullptr)
	{
		if (UIMGInventoryItemDefinition* WeaponItemCDO = WeaponItemClass->GetDefaultObject<UIMGInventoryItemDefinition>())
		{
			if (const UInventoryFragment_SetStats* ItemStatsFragment = Cast<UInventoryFragment_SetStats>( WeaponItemCDO->FindFragmentByClass(UInventoryFragment_SetStats::StaticClass()) ))
			{
				return ItemStatsFragment->GetItemStatByTag(StatTag);
			}
		}
	}

	return 0;
}
