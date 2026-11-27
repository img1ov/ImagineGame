#include "Character/Ragdoll/IMGRagdollComponent_PhysicsControl.h"

#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "PhysicsControlAsset.h"
#include "PhysicsControlComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGRagdollComponent_PhysicsControl)

UPhysicsControlComponent* UIMGRagdollComponent_PhysicsControl::GetPhysicsControlComponent() const { return RuntimeControlComponent; }

bool UIMGRagdollComponent_PhysicsControl::CanExecuteBody(USkeletalMeshComponent& Mesh) const
{
	if (!Super::CanExecuteBody(Mesh) || !ControlAsset || SimulatedBodyModifierSet.IsNone()
		|| (!RagdollProfile.IsNone() && !ControlAsset->Profiles.Contains(RagdollProfile))) { return false; }
	if (bOwnsControlSetup && IsBodySimulationValid()) { return true; }
	const bool bExplicit = !ControlComponent.ComponentProperty.IsNone() || !ControlComponent.PathToComponent.IsEmpty()
		|| ControlComponent.OverrideComponent.IsValid() || ControlComponent.OtherActor.IsValid();
	if (bExplicit)
	{
		const UPhysicsControlComponent* Control = Cast<UPhysicsControlComponent>(ControlComponent.GetComponent(GetOwner()));
		return IsValid(Control) && Control->GetOwner() == GetOwner() && Control->IsRegistered() && !Control->IsBeingDestroyed()
			&& Control->GetAllControlNames().IsEmpty() && Control->GetAllBodyModifierNames().IsEmpty();
	}
	if (bAutoCreateControlComponent) { return true; }
	TArray<UPhysicsControlComponent*> Candidates;
	GetOwner()->GetComponents(Candidates);
	int32 AvailableCount = 0;
	for (const UPhysicsControlComponent* Control : Candidates)
	{
		if (IsValid(Control) && Control->IsRegistered() && !Control->IsBeingDestroyed()
			&& Control->GetAllControlNames().IsEmpty() && Control->GetAllBodyModifierNames().IsEmpty()) { ++AvailableCount; }
	}
	return AvailableCount == 1;
}

bool UIMGRagdollComponent_PhysicsControl::ResolveControlComponent()
{
	if (IsValid(RuntimeControlComponent) && RuntimeControlComponent->IsRegistered() && !RuntimeControlComponent->IsBeingDestroyed()) { return true; }
	if (bCreatedControlComponent && IsValid(RuntimeControlComponent) && RuntimeControlComponent->GetAllControlNames().IsEmpty()
		&& RuntimeControlComponent->GetAllBodyModifierNames().IsEmpty())
	{
		GetOwner()->RemoveInstanceComponent(RuntimeControlComponent);
		RuntimeControlComponent->DestroyComponent();
	}
	RuntimeControlComponent = nullptr;
	bCreatedControlComponent = false;
	const bool bExplicit = !ControlComponent.ComponentProperty.IsNone() || !ControlComponent.PathToComponent.IsEmpty()
		|| ControlComponent.OverrideComponent.IsValid() || ControlComponent.OtherActor.IsValid();
	if (bExplicit)
	{
		RuntimeControlComponent = Cast<UPhysicsControlComponent>(ControlComponent.GetComponent(GetOwner()));
		if (!IsValid(RuntimeControlComponent) || RuntimeControlComponent->GetOwner() != GetOwner()
			|| !RuntimeControlComponent->IsRegistered() || RuntimeControlComponent->IsBeingDestroyed())
		{
			RuntimeControlComponent = nullptr;
			SetBodyIssue(TEXT("InvalidPhysicsControlComponent"));
			return false;
		}
	}
	else
	{
		TArray<UPhysicsControlComponent*> Candidates;
		GetOwner()->GetComponents(Candidates);
		for (UPhysicsControlComponent* Candidate : Candidates)
		{
			if (!IsValid(Candidate) || !Candidate->IsRegistered() || Candidate->IsBeingDestroyed()
				|| !Candidate->GetAllControlNames().IsEmpty() || !Candidate->GetAllBodyModifierNames().IsEmpty()) { continue; }
			if (RuntimeControlComponent) { RuntimeControlComponent = nullptr; break; }
			RuntimeControlComponent = Candidate;
		}
	}
	if (!RuntimeControlComponent && bAutoCreateControlComponent)
	{
		RuntimeControlComponent = NewObject<UPhysicsControlComponent>(GetOwner(), NAME_None, RF_Transient);
		RuntimeControlComponent->SetIsReplicated(false);
		RuntimeControlComponent->PrimaryComponentTick.bStartWithTickEnabled = false;
		GetOwner()->AddInstanceComponent(RuntimeControlComponent);
		RuntimeControlComponent->RegisterComponent();
		RuntimeControlComponent->SetComponentTickEnabled(false);
		bCreatedControlComponent = true;
	}
	if (!IsValid(RuntimeControlComponent) || !RuntimeControlComponent->IsRegistered())
	{
		SetBodyIssue(TEXT("MissingPhysicsControlComponent"));
		return false;
	}
	return true;
}

bool UIMGRagdollComponent_PhysicsControl::ValidateCreatedControls() const
{
	if (!RuntimeControlComponent || OwnedModifiers.IsEmpty()
		|| RuntimeControlComponent->GetBodyModifierNamesInSet(SimulatedBodyModifierSet).IsEmpty()) { return false; }
	for (FName Set : RequiredControlSets) { if (RuntimeControlComponent->GetControlNamesInSet(Set).IsEmpty()) { return false; } }
	for (FName Set : RequiredBodyModifierSets) { if (RuntimeControlComponent->GetBodyModifierNamesInSet(Set).IsEmpty()) { return false; } }
	for (const auto& Control : ControlAsset->AdditionalControlsAndModifiers.Controls)
	{
		if (!OwnedControls.Contains(Control.Key)) { return false; }
	}
	for (const auto& Modifier : ControlAsset->AdditionalControlsAndModifiers.Modifiers)
	{
		if (!OwnedModifiers.Contains(Modifier.Key)) { return false; }
	}
	return DisabledControlSet.IsNone() || !RuntimeControlComponent->GetControlNamesInSet(DisabledControlSet).IsEmpty();
}

bool UIMGRagdollComponent_PhysicsControl::StartBodySimulation(USkeletalMeshComponent& Mesh)
{
	if (!ControlAsset || !ResolveControlComponent()) { return false; }
	UPhysicsControlComponent& Control = *RuntimeControlComponent;
	if (!Control.GetAllControlNames().IsEmpty() || !Control.GetAllBodyModifierNames().IsEmpty())
	{
		SetBodyIssue(TEXT("PhysicsControlComponentBusy"));
		return false;
	}
	PreviousControlAsset = Control.PhysicsControlAsset;
	bPreviousTickEnabled = Control.IsComponentTickEnabled();
	bOwnsControlSetup = true;
	Control.SetComponentTickEnabled(false);
	Control.PhysicsControlAsset = ControlAsset.Get();
	ActiveControlAsset = ControlAsset.Get();
	const bool bCreated = Control.CreateControlsAndBodyModifiersFromPhysicsControlAsset(&Mesh, nullptr, NAME_None);
	OwnedControls = Control.GetAllControlNames();
	OwnedModifiers = Control.GetAllBodyModifierNames();
	if (!bCreated || !ValidateCreatedControls()
		|| (!RagdollProfile.IsNone() && !Control.InvokeControlProfile(RagdollProfile)))
	{
		ClearControls();
		SetBodyIssue(TEXT("PhysicsControlSetupFailed"));
		return false;
	}
	if (!DisabledControlSet.IsNone()) { Control.SetControlsInSetEnabled(DisabledControlSet, false); }
	if (!Control.SetBodyModifiersInSetMovementType(SimulatedBodyModifierSet, EPhysicsMovementType::Simulated)
		|| !Control.SetBodyModifiersInSetPhysicsBlendWeight(SimulatedBodyModifierSet, 1.f))
	{
		ClearControls();
		return false;
	}
	Control.UpdateTargetCaches(0.f);
	Control.UpdateControls(0.f);
	// The component API also updates the mesh's EndPhysics tick registration after per-body modifier writes.
	Mesh.SetAllBodiesPhysicsBlendWeight(1.f);
	return Mesh.IsSimulatingPhysics(PelvisBone);
}

bool UIMGRagdollComponent_PhysicsControl::IsBodySimulationValid() const
{
	if (!Super::IsBodySimulationValid() || !bOwnsControlSetup || !IsValid(RuntimeControlComponent)
		|| !RuntimeControlComponent->IsRegistered() || RuntimeControlComponent->IsBeingDestroyed()) { return false; }
	for (FName Name : OwnedControls) { if (!RuntimeControlComponent->GetAllControlNames().Contains(Name)) { return false; } }
	for (FName Name : OwnedModifiers) { if (!RuntimeControlComponent->GetAllBodyModifierNames().Contains(Name)) { return false; } }
	return true;
}

void UIMGRagdollComponent_PhysicsControl::UpdateBodySimulation(float DeltaTime)
{
	if (!bOwnsControlSetup || !IsValid(RuntimeControlComponent) || !RuntimeControlComponent->IsRegistered()
		|| RuntimeControlComponent->IsBeingDestroyed())
	{
		SetBodyIssue(TEXT("PhysicsControlResourceLost"));
		return;
	}
	// Base PrePhysics tick is ordered after movement and mesh animation; never tick these controls a second time.
	RuntimeControlComponent->SetComponentTickEnabled(false);
	RuntimeControlComponent->UpdateTargetCaches(DeltaTime);
	RuntimeControlComponent->UpdateControls(DeltaTime);
}

void UIMGRagdollComponent_PhysicsControl::StopBodySimulation(USkeletalMeshComponent&) { ClearControls(); }

void UIMGRagdollComponent_PhysicsControl::ClearControls()
{
	if (!bOwnsControlSetup) { return; }
	bOwnsControlSetup = false;
	if (IsValid(RuntimeControlComponent) && !RuntimeControlComponent->IsBeingDestroyed())
	{
		for (FName Name : OwnedControls) { RuntimeControlComponent->DestroyControl(Name, true, false); }
		for (FName Name : OwnedModifiers) { RuntimeControlComponent->DestroyBodyModifier(Name, true, false); }
		if (RuntimeControlComponent->PhysicsControlAsset == ActiveControlAsset)
		{
			RuntimeControlComponent->PhysicsControlAsset = PreviousControlAsset;
		}
		if (!RuntimeControlComponent->IsComponentTickEnabled()) { RuntimeControlComponent->SetComponentTickEnabled(bPreviousTickEnabled); }
	}
	OwnedControls.Reset();
	OwnedModifiers.Reset();
	PreviousControlAsset.Reset();
	ActiveControlAsset.Reset();
}

void UIMGRagdollComponent_PhysicsControl::ReleaseBodyResources()
{
	ClearControls();
	if (bCreatedControlComponent && IsValid(RuntimeControlComponent) && RuntimeControlComponent->GetAllControlNames().IsEmpty()
		&& RuntimeControlComponent->GetAllBodyModifierNames().IsEmpty())
	{
		GetOwner()->RemoveInstanceComponent(RuntimeControlComponent);
		RuntimeControlComponent->DestroyComponent();
	}
	RuntimeControlComponent = nullptr;
	bCreatedControlComponent = false;
	Super::ReleaseBodyResources();
}
