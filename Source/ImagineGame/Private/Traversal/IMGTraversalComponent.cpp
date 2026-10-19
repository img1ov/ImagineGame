#include "Traversal/IMGTraversalComponent.h"

#include "Character/IMGCharacter.h"
#include "Character/IMGCharacterMovementComponent.h"
#include "MotionWarpingComponent.h"
#include "Net/UnrealNetwork.h"
#include "Traversal/IMGTraversalDetector.h"

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
	FIMGTraversalQuery Query = QuerySettings;
	Query.Character = const_cast<AIMGCharacter*>(Character);
	if (Direction.IsNearlyZero() && Character)
	{
		Direction = Character->GetActorForwardVector();
	}
	Query.Direction = Direction;
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
}

void UIMGTraversalComponent::BeginTraversal(const FIMGTraversalCandidate& Candidate, EIMGTraversalAction Action)
{
	ActiveState.Sequence = ReplicatedState.Sequence + 1;
	ActiveState.Action = Action;
	ActiveState.HitComponent = Candidate.HitComponent;
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
	if (UPrimitiveComponent* TargetComponent = Candidate.HitComponent.Get())
	{
		AddTickPrerequisiteComponent(TargetComponent);
	}
	if (UPrimitiveComponent* BackFloorComponent = Candidate.BackFloorComponent.Get();
		BackFloorComponent && BackFloorComponent != Candidate.HitComponent.Get())
	{
		AddTickPrerequisiteComponent(BackFloorComponent);
	}
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		ReplicatedState = ActiveState;
		GetOwner()->ForceNetUpdate();
	}
	RefreshWarpTargets();
	SetComponentTickEnabled(true);
}

void UIMGTraversalComponent::EndTraversal()
{
	if (!ActiveState.bActive)
	{
		return;
	}
	const uint16 NextSequence = ReplicatedState.Sequence + 1;
	if (UPrimitiveComponent* TargetComponent = ActiveState.HitComponent.Get())
	{
		RemoveTickPrerequisiteComponent(TargetComponent);
	}
	if (UPrimitiveComponent* BackFloorComponent = ActiveState.BackFloorComponent.Get();
		BackFloorComponent && BackFloorComponent != ActiveState.HitComponent.Get())
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
}

void UIMGTraversalComponent::OnRep_TraversalState()
{
	if (UPrimitiveComponent* PreviousTarget = ActiveState.HitComponent.Get())
	{
		RemoveTickPrerequisiteComponent(PreviousTarget);
	}
	if (UPrimitiveComponent* PreviousBackFloor = ActiveState.BackFloorComponent.Get();
		PreviousBackFloor && PreviousBackFloor != ActiveState.HitComponent.Get())
	{
		RemoveTickPrerequisiteComponent(PreviousBackFloor);
	}
	RemoveWarpTargets();
	ActiveState = ReplicatedState;
	if (ActiveState.bActive)
	{
		if (UPrimitiveComponent* TargetComponent = ActiveState.HitComponent.Get())
		{
			AddTickPrerequisiteComponent(TargetComponent);
		}
		if (UPrimitiveComponent* BackFloorComponent = ActiveState.BackFloorComponent.Get();
			BackFloorComponent && BackFloorComponent != ActiveState.HitComponent.Get())
		{
			AddTickPrerequisiteComponent(BackFloorComponent);
		}
		RefreshWarpTargets();
	}
	SetComponentTickEnabled(ActiveState.bActive);
}

void UIMGTraversalComponent::RefreshWarpTargets()
{
	UPrimitiveComponent* HitComponent = ActiveState.HitComponent.Get();
	UMotionWarpingComponent* Warping = GetMotionWarpingComponent();
	if (!Warping)
	{
		return;
	}
	const bool bUseLocalTargets = IsValid(HitComponent);
	const FTransform ComponentWorld = bUseLocalTargets ? HitComponent->GetComponentTransform() : FTransform::Identity;
	Warping->AddOrUpdateWarpTargetFromTransform(FrontLedgeName,
		bUseLocalTargets ? ActiveState.LocalFrontLedge * ComponentWorld : ActiveState.FrontLedge);
	if (ActiveState.bHasBackLedge
		&& (ActiveState.Action == EIMGTraversalAction::Hurdle || ActiveState.Action == EIMGTraversalAction::Vault))
	{
		Warping->AddOrUpdateWarpTargetFromTransform(BackLedgeName,
			bUseLocalTargets ? ActiveState.LocalBackLedge * ComponentWorld : ActiveState.BackLedge);
	}
	if (ActiveState.bHasBackFloor && ActiveState.Action == EIMGTraversalAction::Hurdle)
	{
		UPrimitiveComponent* BackFloorComponent = ActiveState.BackFloorComponent.Get();
		Warping->AddOrUpdateWarpTargetFromTransform(BackFloorName,
			IsValid(BackFloorComponent)
				? ActiveState.LocalBackFloor * BackFloorComponent->GetComponentTransform()
				: ActiveState.BackFloor);
	}
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
