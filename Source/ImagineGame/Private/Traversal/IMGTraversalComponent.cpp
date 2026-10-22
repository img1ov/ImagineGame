#include "Traversal/IMGTraversalComponent.h"

#include "AbilitySystem/IMGAbilitySystemComponent.h"
#include "Animation/AnimMontage.h"
#include "Character/IMGCharacter.h"
#include "Character/IMGCharacterMovementComponent.h"
#include "Character/IMGPawnExtensionComponent.h"
#include "MotionWarpingComponent.h"
#include "Net/UnrealNetwork.h"
#include "Traversal/IMGTraversalDetector.h"

#if ENABLE_DRAW_DEBUG
#include "IMGTraversalDebug.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGTraversalComponent)

namespace
{
const FName FrontLedgeName(TEXT("FrontLedge"));
const FName BackLedgeName(TEXT("BackLedge"));
const FName BackFloorName(TEXT("BackFloor"));
}

UIMGTraversalComponent::UIMGTraversalComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

UIMGTraversalComponent* UIMGTraversalComponent::Get(const AActor* Actor)
{
	return IsValid(Actor) ? Actor->FindComponentByClass<UIMGTraversalComponent>() : nullptr;
}

bool UIMGTraversalComponent::Detect(FIMGTraversalCandidate& OutCandidate, EIMGTraversalFailure& OutFailure, FVector Direction) const
{
	const AIMGCharacter* Character = Cast<AIMGCharacter>(GetOwner());
	FIMGTraversalQuery Query;
	Query.Character = const_cast<AIMGCharacter*>(Character);
	if (Direction.IsNearlyZero() && Character)
	{
		Direction = Character->GetActorForwardVector();
	}
	Query.Direction = Direction;
	Query.Settings = DetectorSettings;
	return FIMGTraversalDetector::Detect(Query, OutCandidate, OutFailure);
}

UMotionWarpingComponent* UIMGTraversalComponent::GetMotionWarpingComponent() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UMotionWarpingComponent>() : nullptr;
}

void UIMGTraversalComponent::BeginPlay()
{
	Super::BeginPlay();
	if (const AIMGCharacter* Character = Cast<AIMGCharacter>(GetOwner()))
	{
		if (UIMGCharacterMovementComponent* Movement = Character->GetIMGMovementComponent())
		{
			Movement->AddTickPrerequisiteComponent(this);
		}
	}
	if (UIMGPawnExtensionComponent* PawnExtension = UIMGPawnExtensionComponent::FindPawnExtensionComponent(GetOwner()))
	{
		PawnExtension->OnAbilitySystemUninitialized_Register(
			FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &ThisClass::HandleAbilitySystemUninitialized));
		PawnExtension->OnAbilitySystemInitialized_RegisterAndCall(
			FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &ThisClass::HandleAbilitySystemInitialized));
	}
}

void UIMGTraversalComponent::BeginTraversal(const FIMGTraversalCandidate& Candidate,
	const FIMGTraversalAnimationSelection& Selection)
{
	ActiveState.Sequence = ReplicatedState.Sequence + 1;
	ActiveState.Action = Selection.Action;
	ActiveState.Montage = Selection.Montage;
	ActiveState.MontagePlayInstanceId = 0;
	ActiveState.MontageStartTime = Selection.StartTime;
	ActiveState.HitComponent = Candidate.HitComponent;
	ActiveState.FrontLedgeComponent = Candidate.FrontLedgeComponent;
	ActiveState.BackLedgeComponent = Candidate.BackLedgeComponent;
	ActiveState.ObstacleComponents = Candidate.ObstacleComponents;
	ActiveState.BackFloorComponent = Candidate.BackFloorComponent;
	ActiveState.FrontLedge = Candidate.FrontLedge;
	ActiveState.BackLedge = Candidate.BackLedge;
	ActiveState.BackFloor = Candidate.BackFloor;
	ActiveState.LocalFrontLedge = Candidate.LocalFrontLedge;
	ActiveState.LocalBackLedge = Candidate.LocalBackLedge;
	ActiveState.LocalBackFloor = Candidate.LocalBackFloor;
	ActiveState.bHasBackLedge = Candidate.bHasBackLedge;
	ActiveState.bHasBackFloor = Candidate.bHasBackFloor;
	ActiveState.bActive = true;
	for (UPrimitiveComponent* ObstacleComponent : ActiveState.ObstacleComponents)
	{
		if (IsValid(ObstacleComponent))
		{
			AddTickPrerequisiteComponent(ObstacleComponent);
		}
	}
	if (UPrimitiveComponent* BackFloorComponent = Candidate.BackFloorComponent.Get();
		BackFloorComponent && !ActiveState.ObstacleComponents.Contains(BackFloorComponent))
	{
		AddTickPrerequisiteComponent(BackFloorComponent);
	}
	RefreshWarpTargets();
	SetComponentTickEnabled(true);
}

bool UIMGTraversalComponent::PublishTraversalState(const uint8 MontagePlayInstanceId)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !ActiveState.bActive || !IsValid(ActiveState.Montage))
	{
		return false;
	}

	ActiveState.MontagePlayInstanceId = MontagePlayInstanceId;
	ReplicatedState = ActiveState;
	GetOwner()->ForceNetUpdate();
	return true;
}

void UIMGTraversalComponent::EndTraversal()
{
	if (!ActiveState.bActive)
	{
		return;
	}
	const uint16 NextSequence = ReplicatedState.Sequence + 1;
	for (UPrimitiveComponent* ObstacleComponent : ActiveState.ObstacleComponents)
	{
		if (IsValid(ObstacleComponent))
		{
			RemoveTickPrerequisiteComponent(ObstacleComponent);
		}
	}
	if (UPrimitiveComponent* BackFloorComponent = ActiveState.BackFloorComponent.Get();
		BackFloorComponent && !ActiveState.ObstacleComponents.Contains(BackFloorComponent))
	{
		RemoveTickPrerequisiteComponent(BackFloorComponent);
	}
	RemoveWarpTargets();
	ActiveState = FIMGTraversalRepState();
	ActiveState.Sequence = NextSequence;
	SetComponentTickEnabled(false);
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		ReplicatedState = ActiveState;
		GetOwner()->ForceNetUpdate();
	}
	if (UIMGAbilitySystemComponent* ASC = BoundAbilitySystemComponent.Get())
	{
		ASC->RetryReplicatedMontage();
	}
}

void UIMGTraversalComponent::OnRep_TraversalState()
{
	for (UPrimitiveComponent* PreviousTarget : ActiveState.ObstacleComponents)
	{
		if (IsValid(PreviousTarget))
		{
			RemoveTickPrerequisiteComponent(PreviousTarget);
		}
	}
	if (UPrimitiveComponent* PreviousBackFloor = ActiveState.BackFloorComponent.Get();
		PreviousBackFloor && !ActiveState.ObstacleComponents.Contains(PreviousBackFloor))
	{
		RemoveTickPrerequisiteComponent(PreviousBackFloor);
	}
	RemoveWarpTargets();
	ActiveState = ReplicatedState;
	if (ActiveState.bActive)
	{
		for (UPrimitiveComponent* TargetComponent : ActiveState.ObstacleComponents)
		{
			if (IsValid(TargetComponent))
			{
				AddTickPrerequisiteComponent(TargetComponent);
			}
		}
		if (UPrimitiveComponent* BackFloorComponent = ActiveState.BackFloorComponent.Get();
			BackFloorComponent && !ActiveState.ObstacleComponents.Contains(BackFloorComponent))
		{
			AddTickPrerequisiteComponent(BackFloorComponent);
		}
		RefreshWarpTargets();
	}
	SetComponentTickEnabled(ActiveState.bActive);
	if (UIMGAbilitySystemComponent* ASC = BoundAbilitySystemComponent.Get())
	{
		ASC->RetryReplicatedMontage();
	}
}

void UIMGTraversalComponent::HandleAbilitySystemInitialized()
{
	HandleAbilitySystemUninitialized();
	if (!GetOwner() || GetOwner()->GetLocalRole() != ROLE_SimulatedProxy)
	{
		return;
	}
	const UIMGPawnExtensionComponent* PawnExtension =
		UIMGPawnExtensionComponent::FindPawnExtensionComponent(GetOwner());
	UIMGAbilitySystemComponent* ASC = PawnExtension ? PawnExtension->GetIMGAbilitySystemComponent() : nullptr;
	if (!ASC)
	{
		return;
	}

	BoundAbilitySystemComponent = ASC;
	ASC->SetReplicatedMontageReadyDelegate(FIMGReplicatedMontageReadyDelegate::CreateUObject(
		this, &ThisClass::IsReplicatedMontageReady));
	ASC->RetryReplicatedMontage();
}

void UIMGTraversalComponent::HandleAbilitySystemUninitialized()
{
	if (UIMGAbilitySystemComponent* ASC = BoundAbilitySystemComponent.Get())
	{
		ASC->ClearReplicatedMontageReadyDelegate(this);
	}
	BoundAbilitySystemComponent.Reset();
}

bool UIMGTraversalComponent::IsReplicatedMontageReady(
	const UAnimMontage* Montage, const uint8 MontagePlayInstanceId, const bool bIsStopped,
	float& OutLogicalStartTime) const
{
	OutLogicalStartTime = -1.f;
	if (!Montage || bIsStopped)
	{
		return true;
	}

	const bool bMatchesTraversalState = ActiveState.bActive && ActiveState.Montage == Montage
		&& ActiveState.MontagePlayInstanceId == MontagePlayInstanceId;
	if (!bMatchesTraversalState)
	{
		TArray<FMotionWarpingWindowData> FrontLedgeWindows;
		UMotionWarpingUtilities::GetMotionWarpingWindowsForWarpTargetFromAnimation(
			Montage, FrontLedgeName, FrontLedgeWindows);
		return FrontLedgeWindows.IsEmpty();
	}

	const UMotionWarpingComponent* Warping = GetMotionWarpingComponent();
	if (!Warping || !Warping->FindWarpTarget(FrontLedgeName))
	{
		return false;
	}

	TArray<FMotionWarpingWindowData> BackLedgeWindows;
	UMotionWarpingUtilities::GetMotionWarpingWindowsForWarpTargetFromAnimation(
		Montage, BackLedgeName, BackLedgeWindows);
	const bool bNeedsBackLedge = !BackLedgeWindows.IsEmpty();
	if (bNeedsBackLedge && !Warping->FindWarpTarget(BackLedgeName))
	{
		return false;
	}

	if (ActiveState.Action == EIMGTraversalAction::Hurdle
		&& !Warping->FindWarpTarget(BackFloorName))
	{
		return false;
	}

	OutLogicalStartTime = ActiveState.MontageStartTime;
	return true;
}

void UIMGTraversalComponent::RefreshWarpTargets()
{
	UMotionWarpingComponent* Warping = GetMotionWarpingComponent();
	if (!Warping)
	{
		return;
	}
	UPrimitiveComponent* FrontComponent = ActiveState.FrontLedgeComponent.Get();
	if (!IsValid(FrontComponent))
	{
		FrontComponent = ActiveState.HitComponent.Get();
	}
	FTransform FrontLedge = IsValid(FrontComponent)
		? ActiveState.LocalFrontLedge * FrontComponent->GetComponentTransform() : ActiveState.FrontLedge;
	FrontLedge.AddToTranslation(FVector::UpVector * FrontLedgeVerticalOffset);
	Warping->AddOrUpdateWarpTargetFromTransform(FrontLedgeName, FrontLedge);
#if ENABLE_DRAW_DEBUG
	IMGTraversalDebug::Point(GetWorld(), FrontLedge.GetLocation(), FColor::Yellow, 1, 16.f, 0.f);
	IMGTraversalDebug::Arrow(GetWorld(), FrontLedge.GetLocation(),
		FrontLedge.GetLocation() + FrontLedge.GetUnitAxis(EAxis::X) * 45.f, FColor::Yellow, 1, 0.f);
#endif
	if (ActiveState.bHasBackLedge
		&& (ActiveState.Action == EIMGTraversalAction::Hurdle || ActiveState.Action == EIMGTraversalAction::Vault))
	{
		UPrimitiveComponent* BackComponent = ActiveState.BackLedgeComponent.Get();
		if (!IsValid(BackComponent))
		{
			BackComponent = ActiveState.HitComponent.Get();
		}
		const FTransform BackLedge = IsValid(BackComponent)
			? ActiveState.LocalBackLedge * BackComponent->GetComponentTransform() : ActiveState.BackLedge;
		Warping->AddOrUpdateWarpTargetFromTransform(BackLedgeName, BackLedge);
#if ENABLE_DRAW_DEBUG
		IMGTraversalDebug::Point(GetWorld(), BackLedge.GetLocation(), FColor::Orange, 1, 16.f, 0.f);
		IMGTraversalDebug::Arrow(GetWorld(), BackLedge.GetLocation(),
			BackLedge.GetLocation() + BackLedge.GetUnitAxis(EAxis::X) * 45.f, FColor::Orange, 1, 0.f);
#endif
	}
	if (ActiveState.bHasBackFloor && ActiveState.Action == EIMGTraversalAction::Hurdle)
	{
		UPrimitiveComponent* BackFloorComponent = ActiveState.BackFloorComponent.Get();
		const FTransform BackFloor = IsValid(BackFloorComponent)
			? ActiveState.LocalBackFloor * BackFloorComponent->GetComponentTransform()
			: ActiveState.BackFloor;
		Warping->AddOrUpdateWarpTargetFromTransform(BackFloorName, BackFloor);
#if ENABLE_DRAW_DEBUG
		IMGTraversalDebug::Point(GetWorld(), BackFloor.GetLocation(), FColor::Green, 1, 16.f, 0.f);
		IMGTraversalDebug::Arrow(GetWorld(), BackFloor.GetLocation(),
			BackFloor.GetLocation() + BackFloor.GetUnitAxis(EAxis::X) * 45.f, FColor::Green, 1, 0.f);
#endif
	}
#if ENABLE_DRAW_DEBUG
	if (GetOwner() && IMGTraversalDebug::IsEnabled(1))
	{
		IMGTraversalDebug::String(GetWorld(), GetOwner()->GetActorLocation() + FVector::UpVector * 120.f,
			FString::Printf(TEXT("Traversal Active: %s  Parts %d"),
				*StaticEnum<EIMGTraversalAction>()->GetNameStringByValue(static_cast<int64>(ActiveState.Action)),
				ActiveState.ObstacleComponents.Num()),
			FColor::White, 1, 0.f);
		if (IMGTraversalDebug::IsEnabled(3))
		{
			for (const UPrimitiveComponent* ObstacleComponent : ActiveState.ObstacleComponents)
			{
				if (!IsValid(ObstacleComponent))
				{
					continue;
				}
				const FVector Location = ObstacleComponent->GetComponentLocation();
				IMGTraversalDebug::Point(GetWorld(), Location, FColor::Purple, 3, 9.f, 0.f);
				IMGTraversalDebug::String(GetWorld(), Location + FVector::UpVector * 10.f,
					FString::Printf(TEXT("%s  V %.0f  W %.0f"), *ObstacleComponent->GetName(),
						ObstacleComponent->GetComponentVelocity().Size(),
						ObstacleComponent->GetPhysicsAngularVelocityInDegrees().Size()), FColor::Purple, 3, 0.f);
			}
		}
	}
#endif
}

void UIMGTraversalComponent::RemoveWarpTargets()
{
	if (UMotionWarpingComponent* Warping = GetMotionWarpingComponent())
	{
		Warping->RemoveWarpTarget(FrontLedgeName);
		Warping->RemoveWarpTarget(BackLedgeName);
		Warping->RemoveWarpTarget(BackFloorName);
	}
}

void UIMGTraversalComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (ActiveState.bActive)
	{
		RefreshWarpTargets();
	}
}

void UIMGTraversalComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UIMGPawnExtensionComponent* PawnExtension = UIMGPawnExtensionComponent::FindPawnExtensionComponent(GetOwner()))
	{
		PawnExtension->UnregisterAbilitySystemDelegates(this);
	}
	HandleAbilitySystemUninitialized();
	if (const AIMGCharacter* Character = Cast<AIMGCharacter>(GetOwner()))
	{
		if (UIMGCharacterMovementComponent* Movement = Character->GetIMGMovementComponent())
		{
			Movement->RemoveTickPrerequisiteComponent(this);
		}
	}
	EndTraversal();
	Super::EndPlay(EndPlayReason);
}

void UIMGTraversalComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UIMGTraversalComponent, ReplicatedState, COND_SkipOwner);
}
