// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/IMGCharacter.h"

#include "IMGGameplayTags.h"
#include "IMGLogChannels.h"
#include "AbilitySystem/IMGAbilitySystemComponent.h"
#include "Camera/IMGCameraComponent.h"
#include "Camera/IMGSpringArmComponent.h"
#include "Character/IMGCharacterMovementComponent.h"
#include "Character/IMGHealthComponent.h"
#include "Character/IMGPawnExtensionComponent.h"
#include "Components/CapsuleComponent.h"
#include "Net/UnrealNetwork.h"
#include "Player/IMGPlayerController.h"
#include "Player/IMGPlayerState.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGCharacter)

static FName NAME_CharacterCollisionProfile_Capsule(TEXT("IMGPawnCapsule"));
static FName NAME_CharacterCollisionProfile_Mesh(TEXT("IMGPawnMesh"));

FSharedRepMovement::FSharedRepMovement()
{
	RepMovement.LocationQuantizationLevel = EVectorQuantization::RoundTwoDecimals;
}

bool FSharedRepMovement::FillForCharacter(ACharacter* Character)
{
	if (USceneComponent* PawnRootComponent = Character->GetRootComponent())
	{
		UCharacterMovementComponent* CharacterMovement = Character->GetCharacterMovement();

		RepMovement.Location = FRepMovement::RebaseOntoZeroOrigin(PawnRootComponent->GetComponentLocation(), Character);
		RepMovement.Rotation = PawnRootComponent->GetComponentRotation();
		RepMovement.LinearVelocity = CharacterMovement->Velocity;
		RepMovementMode = CharacterMovement->PackNetworkMovementMode();
		bProxyIsJumpForceApplied = Character->GetProxyIsJumpForceApplied() || (Character->JumpForceTimeRemaining > 0.0f);
		bIsCrouched = Character->IsCrouched();

		// Timestamp is sent as zero if unused
		if ((CharacterMovement->NetworkSmoothingMode == ENetworkSmoothingMode::Linear) || CharacterMovement->bNetworkAlwaysReplicateTransformUpdateTimestamp)
		{
			RepTimeStamp = CharacterMovement->GetServerLastTransformUpdateTimeStamp();
		}
		else
		{
			RepTimeStamp = 0.f;
		}

		return true;
	}
	return false;
}

bool FSharedRepMovement::Equals(const FSharedRepMovement& Other, ACharacter* Character) const
{
	if (RepMovement.Location != Other.RepMovement.Location)
	{
		return false;
	}

	if (RepMovement.Rotation != Other.RepMovement.Rotation)
	{
		return false;
	}

	if (RepMovement.LinearVelocity != Other.RepMovement.LinearVelocity)
	{
		return false;
	}

	if (RepMovementMode != Other.RepMovementMode)
	{
		return false;
	}

	if (bProxyIsJumpForceApplied != Other.bProxyIsJumpForceApplied)
	{
		return false;
	}

	if (bIsCrouched != Other.bIsCrouched)
	{
		return false;
	}

	return true;
}

bool FSharedRepMovement::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	bOutSuccess = true;
	RepMovement.NetSerialize(Ar, Map, bOutSuccess);
	Ar << RepMovementMode;
	Ar << bProxyIsJumpForceApplied;
	Ar << bIsCrouched;

	// Timestamp, if non-zero.
	uint8 bHasTimeStamp = (RepTimeStamp != 0.f);
	Ar.SerializeBits(&bHasTimeStamp, 1);
	if (bHasTimeStamp)
	{
		Ar << RepTimeStamp;
	}
	else
	{
		RepTimeStamp = 0.f;
	}

	return true;
}

AIMGCharacter::AIMGCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UIMGCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	SetNetUpdateFrequency(150.0f);
	SetMinNetUpdateFrequency(100.0f);

	SetNetCullDistanceSquared(900000000.f);
	GetReplicatedMovement_Mutable().RotationQuantizationLevel = ERotatorQuantization::ShortComponents;

	UCapsuleComponent* CapsuleComp = GetCapsuleComponent();
	check(CapsuleComp);
	CapsuleComp->SetCollisionProfileName(NAME_CharacterCollisionProfile_Capsule);

	USkeletalMeshComponent* MeshComp = GetMesh();
	MeshComp->SetCollisionProfileName(NAME_CharacterCollisionProfile_Mesh);
	
	PawnExtComponent = CreateDefaultSubobject<UIMGPawnExtensionComponent>(TEXT("PawnExtensionComponent"));
	PawnExtComponent->OnAbilitySystemInitialized_RegisterAndCall(FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &ThisClass::OnAbilitySystemInitialized));
	PawnExtComponent->OnAbilitySystemUninitialized_Register(FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &ThisClass::OnAbilitySystemUninitialized));
	
	HealthComponent = CreateDefaultSubobject<UIMGHealthComponent>(TEXT("HealthComponent"));
	HealthComponent->OnDeathStarted.AddDynamic(this, &ThisClass::OnDeathStarted);
	HealthComponent->OnDeathFinished.AddDynamic(this, &ThisClass::OnDeathFinished);

	CameraSpringArmComponent = CreateDefaultSubobject<UIMGSpringArmComponent>(TEXT("CameraSpringArmComponent"));
	CameraSpringArmComponent->SetupAttachment(GetRootComponent());
	CameraSpringArmComponent->SetRelativeLocation(FVector(0, 0, 80.f));
	CameraSpringArmComponent->bUsePawnControlRotation = true;
	
	CameraComponent = CreateDefaultSubobject<UIMGCameraComponent>(TEXT("CameraComponent"));
	CameraComponent->SetupAttachment(CameraSpringArmComponent);
	
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	BaseEyeHeight = 80.f;
	CrouchedEyeHeight = 50.0f;
}

AIMGPlayerController* AIMGCharacter::GetIMGPlayerController() const
{
	return CastChecked<AIMGPlayerController>(GetController(), ECastCheckedType::NullAllowed);
}

AIMGPlayerState* AIMGCharacter::GetIMGPlayerState() const
{
	return CastChecked<AIMGPlayerState>(GetPlayerState(), ECastCheckedType::NullAllowed);
}

UIMGCharacterMovementComponent* AIMGCharacter::GetIMGMovementComponent() const
{
	return CastChecked<UIMGCharacterMovementComponent>(GetCharacterMovement(), ECastCheckedType::NullAllowed);
}

UIMGAbilitySystemComponent* AIMGCharacter::GetIMGAbilitySystemComponent() const
{
	return Cast<UIMGAbilitySystemComponent>(GetAbilitySystemComponent());
}

UAbilitySystemComponent* AIMGCharacter::GetAbilitySystemComponent() const
{
	if (PawnExtComponent == nullptr)
	{
		return nullptr;
	}

	return PawnExtComponent->GetIMGAbilitySystemComponent();
}

void AIMGCharacter::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
	if (const UIMGAbilitySystemComponent* ActASC = GetIMGAbilitySystemComponent())
	{
		ActASC->GetOwnedGameplayTags(TagContainer);
	}
}

bool AIMGCharacter::HasMatchingGameplayTag(FGameplayTag TagToCheck) const
{
	if (const UIMGAbilitySystemComponent* IMGASC = GetIMGAbilitySystemComponent())
	{
		return IMGASC->HasMatchingGameplayTag(TagToCheck);
	}

	return false;
}

bool AIMGCharacter::HasAllMatchingGameplayTags(const FGameplayTagContainer& TagContainer) const
{
	if (const UIMGAbilitySystemComponent* IMGASC = GetIMGAbilitySystemComponent())
	{
		return IMGASC->HasAllMatchingGameplayTags(TagContainer);
	}

	return false;
}

bool AIMGCharacter::HasAnyMatchingGameplayTags(const FGameplayTagContainer& TagContainer) const
{
	if (const UIMGAbilitySystemComponent* IMGASC = GetIMGAbilitySystemComponent())
	{
		return IMGASC->HasAnyMatchingGameplayTags(TagContainer);
	}

	return false;
}

void AIMGCharacter::ToggleCrouch()
{
	const UIMGCharacterMovementComponent* IMGMoveComp = CastChecked<UIMGCharacterMovementComponent>(GetCharacterMovement());

	if (IsCrouched() || IMGMoveComp->bWantsToCrouch)
	{
		UnCrouch();
	}
	else if (IMGMoveComp->IsMovingOnGround())
	{
		Crouch();
	}
}

void AIMGCharacter::PreInitializeComponents()
{
	Super::PreInitializeComponents();
}

void AIMGCharacter::BeginPlay()
{
	Super::BeginPlay();
	
}

void AIMGCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void AIMGCharacter::Reset()
{
	DisableMovementAndCollision();

	K2_OnReset();

	UninitAndDestroy();
}

void AIMGCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	
	DOREPLIFETIME_CONDITION(ThisClass, ReplicatedAcceleration, COND_SimulatedOnly);
	DOREPLIFETIME(ThisClass, MyTeamID);
}

void AIMGCharacter::PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);
	
	if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
	{
		// Compress Acceleration: XY components as direction + magnitude, Z component as direct value
		const double MaxAccel = MovementComponent->MaxAcceleration;
		const FVector CurrentAccel = MovementComponent->GetCurrentAcceleration();
		double AccelXYRadians, AccelXYMagnitude;
		FMath::CartesianToPolar(CurrentAccel.X, CurrentAccel.Y, AccelXYMagnitude, AccelXYRadians);

		ReplicatedAcceleration.AccelXYRadians   = FMath::FloorToInt((AccelXYRadians / TWO_PI) * 255.0);     // [0, 2PI] -> [0, 255]
		ReplicatedAcceleration.AccelXYMagnitude = FMath::FloorToInt((AccelXYMagnitude / MaxAccel) * 255.0);	// [0, MaxAccel] -> [0, 255]
		ReplicatedAcceleration.AccelZ           = FMath::FloorToInt((CurrentAccel.Z / MaxAccel) * 127.0);   // [-MaxAccel, MaxAccel] -> [-127, 127]
	}
}

void AIMGCharacter::NotifyControllerChanged()
{
	const FGenericTeamId OldTeamId = GetGenericTeamId();
	
	Super::NotifyControllerChanged();
	
	// Update our team ID based on the controller
	if (HasAuthority() && (GetController() != nullptr))
	{
		if (const IIMGTeamAgentInterface* ControllerWithTeam = Cast<IIMGTeamAgentInterface>(GetController()))
		{
			MyTeamID = ControllerWithTeam->GetGenericTeamId();
			ConditionalBroadcastTeamChanged(this, OldTeamId, MyTeamID);
		}
	}
}

void AIMGCharacter::SetGenericTeamId(const FGenericTeamId& NewTeamID)
{
	if (GetController() == nullptr)
	{
		if (HasAuthority())
		{
			const FGenericTeamId OldTeamID = MyTeamID;
			MyTeamID = NewTeamID;
			ConditionalBroadcastTeamChanged(this, OldTeamID, MyTeamID);
		}
		else
		{
			UE_LOG(LogIMGTeams, Error, TEXT("You can't set the team ID on a character (%s) except on the authority"), *GetPathNameSafe(this));
		}
	}
	else
	{
		UE_LOG(LogIMGTeams, Error, TEXT("You can't set the team ID on a possessed character (%s); it's driven by the associated controller"), *GetPathNameSafe(this));
	}
}

FGenericTeamId AIMGCharacter::GetGenericTeamId() const
{
	return MyTeamID;
}

FOnIMGTeamIndexChangedDelegate* AIMGCharacter::GetOnTeamIndexChangedDelegate()
{
	return &OnTeamChangedDelegate;
}

void AIMGCharacter::OnControllerChangedTeam(UObject* TeamAgent, int32 OldTeam, int32 NewTeam)
{
	const FGenericTeamId MyOldTeamID = MyTeamID;
	MyTeamID = IntegerToGenericTeamId(NewTeam);
	ConditionalBroadcastTeamChanged(this, MyOldTeamID, MyTeamID);
}

void AIMGCharacter::OnRep_ReplicatedAcceleration()
{
	if (UIMGCharacterMovementComponent* IMGMovementComponent = Cast<UIMGCharacterMovementComponent>(GetCharacterMovement()))
	{
		// Decompress Acceleration
		const double MaxAccel         = IMGMovementComponent->MaxAcceleration;
		const double AccelXYMagnitude = double(ReplicatedAcceleration.AccelXYMagnitude) * MaxAccel / 255.0; // [0, 255] -> [0, MaxAccel]
		const double AccelXYRadians   = double(ReplicatedAcceleration.AccelXYRadians) * TWO_PI / 255.0;     // [0, 255] -> [0, 2PI]

		FVector UnpackedAcceleration(FVector::ZeroVector);
		FMath::PolarToCartesian(AccelXYMagnitude, AccelXYRadians, UnpackedAcceleration.X, UnpackedAcceleration.Y);
		UnpackedAcceleration.Z = double(ReplicatedAcceleration.AccelZ) * MaxAccel / 127.0; // [-127, 127] -> [-MaxAccel, MaxAccel]

		IMGMovementComponent->SetReplicatedAcceleration(UnpackedAcceleration);
	}
}

void AIMGCharacter::OnRep_MyTeamID(FGenericTeamId OldTeamID)
{
	ConditionalBroadcastTeamChanged(this, OldTeamID, MyTeamID);
}

void AIMGCharacter::OnAbilitySystemInitialized()
{
	UIMGAbilitySystemComponent* IMGASC = GetIMGAbilitySystemComponent();
	check(IMGASC);

	HealthComponent->InitializeWithAbilitySystem(IMGASC);

	InitializeGameplayTags();
}

void AIMGCharacter::OnAbilitySystemUninitialized()
{
	HealthComponent->UninitializeFromAbilitySystem();
}

void AIMGCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// TODO : Fix: OffsetRootBone one-frame “flick” on Listen Server (CMC timing bug)
	if (IsNetMode(NM_ListenServer)
		&& GetRemoteRole() == ROLE_AutonomousProxy
		&& !IsLocallyControlled())
	{
		if (USkeletalMeshComponent* MeshComp = GetMesh())
		{
			MeshComp->bOnlyAllowAutonomousTickPose = false;
		}
	}
	
	const FGenericTeamId OldTeamID = MyTeamID;

	PawnExtComponent->HandleControllerChanged();
	
	// Grab the current team ID and listen for future changes
	if (IIMGTeamAgentInterface* ControllerAsTeamProvider = Cast<IIMGTeamAgentInterface>(NewController))
	{
		MyTeamID = ControllerAsTeamProvider->GetGenericTeamId();
		ControllerAsTeamProvider->GetTeamChangedDelegateChecked().AddDynamic(this, &ThisClass::OnControllerChangedTeam);
	}
	ConditionalBroadcastTeamChanged(this, OldTeamID, MyTeamID);
}

void AIMGCharacter::UnPossessed()
{
	AController* const OldController = GetController();

	// Stop listening for changes from the old controller
	const FGenericTeamId OldTeamID = MyTeamID;
	if (IIMGTeamAgentInterface* ControllerAsTeamProvider = Cast<IIMGTeamAgentInterface>(OldController))
	{
		ControllerAsTeamProvider->GetTeamChangedDelegateChecked().RemoveAll(this);
	}
	
	Super::UnPossessed();

	PawnExtComponent->HandleControllerChanged();
	
	// Determine what the new team ID should be afterwards
	MyTeamID = DetermineNewTeamAfterPossessionEnds(OldTeamID);
	ConditionalBroadcastTeamChanged(this, OldTeamID, MyTeamID);
}

void AIMGCharacter::OnRep_Controller()
{
	Super::OnRep_Controller();

	PawnExtComponent->HandleControllerChanged();
}

void AIMGCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	
	PawnExtComponent->HandlePlayerStateReplicated();
}

void AIMGCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	PawnExtComponent->SetupPlayerInputComponent();
}

void AIMGCharacter::InitializeGameplayTags()
{
	if (UIMGAbilitySystemComponent* IMGASC = GetIMGAbilitySystemComponent())
	{
		// Clear tags that may be lingering on the ability system from the previous pawn.
		for (const TPair<uint8, FGameplayTag>& TagMapping : IMGGameplayTags::MovementModeTagMap)
		{
			if (TagMapping.Value.IsValid())
			{
				IMGASC->SetLooseGameplayTagCount(TagMapping.Value, 0);
			}
		}

		for (const TPair<uint8, FGameplayTag>& TagMapping : IMGGameplayTags::CustomMovementModeTagMap)
		{
			if (TagMapping.Value.IsValid())
			{
				IMGASC->SetLooseGameplayTagCount(TagMapping.Value, 0);
			}
		}

		UIMGCharacterMovementComponent* IMGMoveComp = CastChecked<UIMGCharacterMovementComponent>(GetCharacterMovement());
		SetMovementModeTag(IMGMoveComp->MovementMode, IMGMoveComp->CustomMovementMode, true);
	}
}

void AIMGCharacter::FellOutOfWorld(const class UDamageType& dmgType)
{
	//HealthComponent->DamageSelfDestruct(/*bFellOutOfWorld=*/ true);
	Super::FellOutOfWorld(dmgType);
}

void AIMGCharacter::OnDeathStarted(AActor* OwningActor)
{
	DisableMovementAndCollision();
}

void AIMGCharacter::OnDeathFinished(AActor* OwningActor)
{
	GetWorld()->GetTimerManager().SetTimerForNextTick(this, &ThisClass::DestroyDueToDeath);
}

void AIMGCharacter::DisableMovementAndCollision()
{
	if (GetController())
	{
		GetController()->SetIgnoreMoveInput(true);
	}

	UCapsuleComponent* CapsuleComp = GetCapsuleComponent();
	check(CapsuleComp);
	CapsuleComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CapsuleComp->SetCollisionResponseToAllChannels(ECR_Ignore);

	UIMGCharacterMovementComponent* IMGMoveComp = CastChecked<UIMGCharacterMovementComponent>(GetCharacterMovement());
	IMGMoveComp->StopMovementImmediately();
	IMGMoveComp->DisableMovement();
}

void AIMGCharacter::DestroyDueToDeath()
{
	K2_OnDeathFinished();

	UninitAndDestroy();
}

void AIMGCharacter::UninitAndDestroy()
{
	if (GetLocalRole() == ROLE_Authority)
	{
		DetachFromControllerPendingDestroy();
		SetLifeSpan(0.1f);
	}

	// Uninitialize the ASC if we're still the avatar actor (otherwise another pawn already did it when they became the avatar actor)
	if (UIMGAbilitySystemComponent* ActASC = GetIMGAbilitySystemComponent())
	{
		if (ActASC->GetAvatarActor() == this)
		{
			PawnExtComponent->UninitializeAbilitySystem();
		}
	}

	SetActorHiddenInGame(true);
}

void AIMGCharacter::OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PrevMovementMode, PreviousCustomMode);

	UIMGCharacterMovementComponent* IMGMoveComp = CastChecked<UIMGCharacterMovementComponent>(GetCharacterMovement());

	SetMovementModeTag(PrevMovementMode, PreviousCustomMode, false);
	SetMovementModeTag(IMGMoveComp->MovementMode, IMGMoveComp->CustomMovementMode, true);
}

void AIMGCharacter::SetMovementModeTag(EMovementMode MovementMode, uint8 CustomMovementMode, bool bTagEnabled)
{
	if (UIMGAbilitySystemComponent* IMGASC = GetIMGAbilitySystemComponent())
	{
		const FGameplayTag* MovementModeTag = nullptr;
		if (MovementMode == MOVE_Custom)
		{
			MovementModeTag = IMGGameplayTags::CustomMovementModeTagMap.Find(CustomMovementMode);
		}
		else
		{
			MovementModeTag = IMGGameplayTags::MovementModeTagMap.Find(MovementMode);
		}

		if (MovementModeTag && MovementModeTag->IsValid())
		{
			IMGASC->SetLooseGameplayTagCount(*MovementModeTag, (bTagEnabled ? 1 : 0));
		}
	}
}

void AIMGCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	if (UIMGAbilitySystemComponent* IMGASC = GetIMGAbilitySystemComponent())
	{
		IMGASC->SetLooseGameplayTagCount(IMGGameplayTags::Status_Crouching, 1);
	}

	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
}

void AIMGCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	if (UIMGAbilitySystemComponent* IMGASC = GetIMGAbilitySystemComponent())
	{
		IMGASC->SetLooseGameplayTagCount(IMGGameplayTags::Status_Crouching, 0);
	}

	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
}

bool AIMGCharacter::CanJumpInternal_Implementation() const
{
	// same as ACharacter's implementation but without the crouch check
	return JumpIsAllowedInternal();
}