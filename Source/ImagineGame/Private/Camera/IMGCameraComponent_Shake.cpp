#include "Camera/IMGCameraComponent.h"

#include "Camera/IMGCameraMode.h"
#include "Camera/IMGCameraShakeInstance.h"
#include "Camera/IMGCameraShakeDefinition.h"
#include "Camera/IMGCameraShakePattern.h"
#include "CoreGlobals.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/ActorComponent.h"
#include "IMGLogChannels.h"

namespace
{
	bool IsValidShakeSource(UObject* Source, bool* bHasBegunPlay = nullptr)
	{
		if (!IsValid(Source)) { return false; }
		const AActor* Actor = Cast<AActor>(Source);
		if (Actor && Actor->IsActorBeingDestroyed()) { return false; }
		const UActorComponent* Component = Cast<UActorComponent>(Source);
		if (!Component) { return true; }
		if (!Component->IsRegistered() || !IsValid(Component->GetOwner()) || Component->GetOwner()->IsActorBeingDestroyed()) { return false; }
		if (bHasBegunPlay)
		{
			if (*bHasBegunPlay && !Component->HasBegunPlay()) { return false; }
			*bHasBegunPlay |= Component->HasBegunPlay();
		}
		return true;
	}

	bool AreValidShakeScales(float Amplitude, float Frequency)
	{
		return FMath::IsFinite(Amplitude) && Amplitude >= 0.f && FMath::IsFinite(Frequency) && Frequency >= 0.f;
	}
}

bool UIMGCameraComponent::CanAcceptCameraShakeRequests() const
{
	const AActor* Target = GetTargetActor();
	return !bEndingPlay && !bUnregistering && IsRegistered() && IsActive() && GetWorld()
		&& GetWorld()->GetNetMode() != NM_DedicatedServer && IsValid(Target) && !Target->IsActorBeingDestroyed()
		&& !IsXRHeadTrackedCamera();
}

void UIMGCameraComponent::RemoveCameraShake(UIMGCameraShakeInstance* Shake)
{
	if (!CameraShakes.RemoveSingle(Shake)) { return; }
	if (IsValid(Shake))
	{
		Shake->UnbindSourceOwner();
		if (Shake->IsActive())
		{
			Shake->StopShake(true);
			Shake->TeardownShake();
		}
	}
}

void UIMGCameraComponent::StopCameraShake(UIMGCameraShakeInstance* Shake, bool bImmediately)
{
	if (!IsValid(Shake) || !CameraShakes.Contains(Shake)) { return; }
	if (bImmediately || !Shake->IsActive() || Shake->IsFinished()) { RemoveCameraShake(Shake); }
	else
	{
		Shake->StopShake(false);
		if (Shake->IsFinished()) { RemoveCameraShake(Shake); }
	}
}

void UIMGCameraComponent::StopCameraShakesFromSource(UObject* SourceOwner, bool bImmediately)
{
	if (!SourceOwner) { return; }
	const TWeakObjectPtr<UObject> SourceKey(SourceOwner);
	const auto Snapshot = CameraShakes;
	for (UIMGCameraShakeInstance* Shake : Snapshot)
	{
		if (IsValid(Shake) && Shake->SourceOwner.HasSameIndexAndSerialNumber(SourceKey)) { StopCameraShake(Shake, bImmediately); }
	}
}

void UIMGCameraComponent::StopAllCameraShakes(bool bImmediately)
{
	const auto Snapshot = CameraShakes;
	for (UIMGCameraShakeInstance* Shake : Snapshot) { StopCameraShake(Shake, bImmediately); }
	CameraShakes.RemoveAll([](const auto& Shake) { return !IsValid(Shake.Get()); });
	if (bImmediately) { ShakeReferenceLocation.Reset(); LastShakeStartTimes.Reset(); }
}

void UIMGCameraComponent::SynchronizeCameraShakeContext()
{
	const AActor* Target = GetTargetActor();
	const APawn* Pawn = Cast<APawn>(Target);
	const AController* CandidateController = Pawn ? Pawn->GetController() : nullptr;
	const AController* Controller = IsValid(CandidateController) && !CandidateController->IsActorBeingDestroyed() ? CandidateController : nullptr;
	const TWeakObjectPtr<const AActor> CurrentTarget(Target);
	const TWeakObjectPtr<const AActor> CurrentController(Controller);
	if (!ShakeTarget.HasSameIndexAndSerialNumber(CurrentTarget) || !ShakeController.HasSameIndexAndSerialNumber(CurrentController))
	{
		StopAllCameraShakes(true);
		ShakeTarget = CurrentTarget;
		ShakeController = CurrentController;
	}
	const APlayerController* PC = Cast<APlayerController>(Controller);
	if (PC && PC->PlayerCameraManager && PC->PlayerCameraManager->bGameCameraCutThisFrame && LastShakeCutFrame != GFrameCounter)
	{
		LastShakeCutFrame = GFrameCounter;
		DiscardCameraShakesBeforeCurrentFrame();
	}
	if (LastCameraUpdateFrame != MAX_uint64 && LastCameraUpdateFrame != GFrameCounter && LastCameraUpdateFrame + 1 != GFrameCounter)
	{
		DiscardCameraShakesBeforeCurrentFrame();
	}
}

void UIMGCameraComponent::DiscardCameraShakesBeforeCurrentFrame()
{
	ShakeReferenceLocation.Reset();
	const auto Snapshot = CameraShakes;
	for (UIMGCameraShakeInstance* Shake : Snapshot)
	{
		// Preserve requests made for the new frame after the discontinuity.
		if (!IsValid(Shake) || Shake->StartFrame != GFrameCounter) { RemoveCameraShake(Shake); }
	}
}

UIMGCameraShakeInstance* UIMGCameraComponent::PlayCameraShake(const UIMGCameraShakeDefinition* Definition, float AmplitudeScale, float FrequencyScale)
{
	return CreateCameraShake(Definition, nullptr, {}, false, AmplitudeScale, FrequencyScale);
}

UIMGCameraShakeInstance* UIMGCameraComponent::StartCameraShake(const UIMGCameraShakeDefinition* Definition, UObject* SourceOwner, float AmplitudeScale, float FrequencyScale)
{
	if (!IsValidShakeSource(SourceOwner)) { return nullptr; }
	return CreateCameraShake(Definition, SourceOwner, {}, true, AmplitudeScale, FrequencyScale);
}

UIMGCameraShakeInstance* UIMGCameraComponent::PlayCameraShakeAtLocation(const UIMGCameraShakeDefinition* Definition, FVector SourceLocation, float AmplitudeScale, float FrequencyScale)
{
	if (SourceLocation.ContainsNaN()) { return nullptr; }
	return CreateCameraShake(Definition, nullptr, TOptional<FVector>(SourceLocation), false, AmplitudeScale, FrequencyScale);
}

UIMGCameraShakeInstance* UIMGCameraComponent::CreateCameraShake(const UIMGCameraShakeDefinition* Definition, UObject* SourceOwner,
	TOptional<FVector> SourceLocation, bool bContinuous, float AmplitudeScale, float FrequencyScale)
{
	AActor* Target = GetTargetActor();
	if (!CanAcceptCameraShakeRequests() || !AreValidShakeScales(AmplitudeScale, FrequencyScale)
		|| !IsValid(Definition))
	{
		return nullptr;
	}
	SynchronizeCameraShakeContext();
	if (!Definition->IsValidForPlayback(bContinuous, SourceLocation.IsSet()))
	{
		UE_LOG(LogIMG, Warning, TEXT("Camera shake definition %s has invalid scheduling/pattern settings or no finite one-shot duration."), *GetNameSafe(Definition));
		return nullptr;
	}
	const auto Snapshot = CameraShakes;
	TArray<UIMGCameraShakeInstance*> Existing;
	for (UIMGCameraShakeInstance* Shake : Snapshot)
	{
		if (!IsValid(Shake)) { CameraShakes.RemoveSingle(Shake); continue; }
		if (!Shake->IsActive() || Shake->IsFinished() || !Shake->GetIMGPattern() || !IsValid(Shake->Definition)) { RemoveCameraShake(Shake); continue; }
		if (Shake->Definition == Definition)
		{
			if (bContinuous && Shake->bContinuous && !Shake->bSourceEndedPlay && Shake->SourceOwner.Get() == SourceOwner)
			{
				return UpdateCameraShake(Shake, AmplitudeScale, FrequencyScale) ? Shake : nullptr;
			}
			Existing.Add(Shake);
		}
	}
	const double Now = GetWorld()->GetTimeSeconds();
	for (auto It = LastShakeStartTimes.CreateIterator(); It; ++It)
	{
		if (!It.Key().ResolveObjectPtr()) { It.RemoveCurrent(); }
	}
	const TObjectKey<const UIMGCameraShakeDefinition> DefinitionKey(Definition);
	if (const double* LastStart = LastShakeStartTimes.Find(DefinitionKey);
		LastStart && Now - *LastStart < Definition->MinTimeBetweenInstances)
	{
		return nullptr;
	}

	UIMGCameraShakeInstance* NewShake = NewObject<UIMGCameraShakeInstance>(this);
	NewShake->Definition = Definition;
	NewShake->bContinuous = bContinuous;
	NewShake->SourceLocation = SourceLocation;
	NewShake->ShakeScale = AmplitudeScale;
	const FVector Reference = ShakeReferenceLocation.Get(GetComponentLocation());
	const FVector TargetLocation = Target->GetActorLocation();
	auto EffectiveStrength = [&](const UIMGCameraShakeInstance* Shake)
	{
		const auto* Pattern = Shake->GetIMGPattern();
		return Shake->ShakeScale * Shake->GetSpatialScale(Reference, TargetLocation) * (Pattern ? Pattern->GetReleaseLevel() : 1.0);
	};
	const double NewStrength = EffectiveStrength(NewShake);
	if (SourceLocation.IsSet() && ShakeReferenceLocation.IsSet() && NewStrength <= UE_SMALL_NUMBER) { return nullptr; }
	TArray<UIMGCameraShakeInstance*> Victims;
	while (Existing.Num() >= Definition->MaxInstances)
	{
		int32 Index = 0;
		if (Definition->Overflow == EIMGCameraShakeOverflow::ReplaceWeakest)
		{
			for (int32 Candidate = 1; Candidate < Existing.Num(); ++Candidate)
			{
				if (EffectiveStrength(Existing[Candidate]) < EffectiveStrength(Existing[Index])) { Index = Candidate; }
			}
			if (EffectiveStrength(Existing[Index]) >= NewStrength) { return nullptr; }
		}
		Victims.Add(Existing[Index]);
		Existing.RemoveAt(Index);
	}
	auto* Pattern = NewShake->GetIMGPattern();
	Pattern->Prepare(Definition->Settings, bContinuous, FMath::Rand());
	Pattern->SetFrequencyScale(FrequencyScale);
	if (bContinuous) { NewShake->BindSourceOwner(SourceOwner); }
	NewShake->StartTime = Now;
	NewShake->StartFrame = GFrameCounter;
	NewShake->StartShake(nullptr, AmplitudeScale, ECameraShakePlaySpace::CameraLocal);
	if (NewShake->IsFinished() || !CanAcceptCameraShakeRequests() || GetTargetActor() != Target
		|| (bContinuous && (NewShake->bSourceEndedPlay || !IsValidShakeSource(SourceOwner, &NewShake->bSourceComponentHasBegunPlay))))
	{
		NewShake->UnbindSourceOwner();
		if (NewShake->IsActive()) { NewShake->StopShake(true); }
		NewShake->TeardownShake();
		return nullptr;
	}
	// Commit replacement only after the new instance has successfully initialized.
	for (auto* Victim : Victims) { RemoveCameraShake(Victim); }
	CameraShakes.Add(NewShake);
	if (Definition->MinTimeBetweenInstances > 0.f) { LastShakeStartTimes.Add(DefinitionKey, Now); }
	return NewShake;
}

bool UIMGCameraComponent::UpdateCameraShake(UIMGCameraShakeInstance* Shake, float AmplitudeScale, float FrequencyScale, bool bAutoStart)
{
	if (!CanAcceptCameraShakeRequests()) { StopAllCameraShakes(true); return false; }
	SynchronizeCameraShakeContext();
	if (!IsValid(Shake) || !CameraShakes.Contains(Shake) || !Shake->IsActive() || Shake->IsFinished()) { return false; }
	if (!Shake->GetIMGPattern() || !IsValid(Shake->Definition)) { RemoveCameraShake(Shake); return false; }
	if (Shake->bContinuous && !IsValidShakeSource(Shake->SourceOwner.Get(), &Shake->bSourceComponentHasBegunPlay)) { Shake->bSourceEndedPlay = true; }
	if (!AreValidShakeScales(AmplitudeScale, FrequencyScale) || (Shake->bContinuous && Shake->bSourceEndedPlay))
	{
		StopCameraShake(Shake, false);
		return false;
	}
	Shake->ShakeScale = AmplitudeScale;
	Shake->GetIMGPattern()->SetFrequencyScale(FrequencyScale);
	if (Shake->bContinuous || bAutoStart) { Shake->GetIMGPattern()->Resume(); }
	return true;
}

void UIMGCameraComponent::UpdateCameraShakes(float DeltaTime, const FIMGCameraModeView& CameraModeView, FMinimalViewInfo& DesiredView)
{
	if (!CanAcceptCameraShakeRequests() || !FMath::IsFinite(DeltaTime) || DeltaTime < 0.f) { StopAllCameraShakes(true); return; }
	SynchronizeCameraShakeContext();
	const FVector ReferenceLocation = DesiredView.Location;
	ShakeReferenceLocation = ReferenceLocation;
	const FVector TargetLocation = GetTargetActor()->GetActorLocation();
	const auto Snapshot = CameraShakes;
	for (UIMGCameraShakeInstance* Shake : Snapshot)
	{
		if (!IsValid(Shake)) { CameraShakes.RemoveSingle(Shake); continue; }
		if (!Shake->IsActive() || Shake->IsFinished() || !Shake->GetIMGPattern() || !IsValid(Shake->Definition)) { RemoveCameraShake(Shake); continue; }
		if (Shake->bContinuous && (Shake->bSourceEndedPlay || !IsValidShakeSource(Shake->SourceOwner.Get(), &Shake->bSourceComponentHasBegunPlay)))
		{
			Shake->bSourceEndedPlay = true;
			Shake->StopShake(false);
		}
		const float* CategoryScale = CameraModeView.CameraShakeCategoryScales.Find(Shake->Definition->ShakeCategory);
		const float Scale = (CategoryScale ? *CategoryScale : CameraModeView.CameraShakeScale)
			* Shake->GetSpatialScale(ReferenceLocation, TargetLocation);
		float StepTime = DeltaTime;
		if (!Shake->bHasUpdatedView)
		{
			// Requests can precede the first rendered view; elapsed finite effects must expire instead of replaying late.
			const double Age = FMath::Max(0.0, GetWorld()->GetTimeSeconds() - Shake->StartTime);
			StepTime = float(FMath::Max(double(DeltaTime), FMath::Min(Age, double(MAX_flt))));
			Shake->bHasUpdatedView = true;
		}
		Shake->UpdateAndApplyCameraShake(StepTime, FMath::IsFinite(Scale) ? FMath::Max(0.f, Scale) : 0.f, DesiredView);
		if (Shake->IsFinished()) { RemoveCameraShake(Shake); }
	}
}
