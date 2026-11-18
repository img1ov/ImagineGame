
#include "Camera/IMGCameraMode.h"

#include "Engine/Canvas.h"
#include "GameFramework/Pawn.h"
#include "Camera/IMGCameraComponent.h"
#include "Camera/IMGPlayerCameraManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGCameraMode)


//////////////////////////////////////////////////////////////////////////
// FIMGCameraModeView
//////////////////////////////////////////////////////////////////////////
FIMGCameraModeView::FIMGCameraModeView()
	: Location(ForceInit)
	, Rotation(ForceInit)
	, ControlRotation(ForceInit)
	, FieldOfView(IMG_CAMERA_DEFAULT_FOV)
{
}

void FIMGCameraModeView::Blend(const FIMGCameraModeView& Other, float OtherWeight)
{
	if (OtherWeight <= 0.0f)
	{
		return;
	}
	else if (OtherWeight >= 1.0f)
	{
		*this = Other;
		return;
	}

	Location = FMath::Lerp(Location, Other.Location, OtherWeight);

	const FRotator DeltaRotation = (Other.Rotation - Rotation).GetNormalized();
	Rotation = Rotation + (OtherWeight * DeltaRotation);

	const FRotator DeltaControlRotation = (Other.ControlRotation - ControlRotation).GetNormalized();
	ControlRotation = ControlRotation + (OtherWeight * DeltaControlRotation);

	FieldOfView = FMath::Lerp(FieldOfView, Other.FieldOfView, OtherWeight);
	TMap<FGameplayTag, float> BlendedShakeScales;
	for (const auto& Pair : CameraShakeCategoryScales)
	{
		const float* OtherScale = Other.CameraShakeCategoryScales.Find(Pair.Key);
		BlendedShakeScales.Add(Pair.Key, FMath::Lerp(Pair.Value, OtherScale ? *OtherScale : Other.CameraShakeScale, OtherWeight));
	}
	for (const auto& Pair : Other.CameraShakeCategoryScales)
	{
		if (!CameraShakeCategoryScales.Contains(Pair.Key))
		{
			BlendedShakeScales.Add(Pair.Key, FMath::Lerp(CameraShakeScale, Pair.Value, OtherWeight));
		}
	}
	CameraShakeScale = FMath::Lerp(CameraShakeScale, Other.CameraShakeScale, OtherWeight);
	CameraShakeCategoryScales = MoveTemp(BlendedShakeScales);
	Framing.Blend(Other.Framing, OtherWeight);

	if (Other.Collision.IsSet())
	{
		FIMGCameraCollisionRequest Request = Other.Collision.GetValue();
		if (Collision.IsSet() && Collision->Target.HasSameIndexAndSerialNumber(Request.Target)
			&& Collision->Anchor.HasSameIndexAndSerialNumber(Request.Anchor))
		{
			Request.Input.Origin = FMath::Lerp(Collision->Input.Origin, Request.Input.Origin, OtherWeight);
			Request.Input.Fallback = FMath::Lerp(Collision->Input.Fallback, Request.Input.Fallback, OtherWeight);
			Request.Input.Pivot = FMath::Lerp(Collision->Input.Pivot, Request.Input.Pivot, OtherWeight);
			Request.Input.MaxProbeRadius = FMath::Min(Collision->Input.MaxProbeRadius, Request.Input.MaxProbeRadius);
			for (const auto& Actor : Collision->Input.IgnoredActors) { Request.Input.IgnoredActors.AddUnique(Actor); }
			for (const auto& Component : Collision->Input.IgnoredOcclusionComponents) { Request.Input.IgnoredOcclusionComponents.AddUnique(Component); }
			for (const auto& Actor : Collision->Input.PushBeyondActors) { Request.Input.PushBeyondActors.AddUnique(Actor); }
		}
		Request.Input.bForcePushIn = true;
		Collision = MoveTemp(Request);
	}
	else if (Collision.IsSet())
	{
		Collision->Input.bForcePushIn = true;
	}
}


//////////////////////////////////////////////////////////////////////////
// UIMGCameraMode
//////////////////////////////////////////////////////////////////////////
UIMGCameraMode::UIMGCameraMode()
{
	FieldOfView = IMG_CAMERA_DEFAULT_FOV;
	ViewPitchMin = IMG_CAMERA_DEFAULT_PITCH_MIN;
	ViewPitchMax = IMG_CAMERA_DEFAULT_PITCH_MAX;

	BlendTime = 0.5f;
	BlendFunction = EIMGCameraModeBlendFunction::EaseOut;
	BlendExponent = 4.0f;
	BlendAlpha = 1.0f;
	BlendWeight = 1.0f;
}

UIMGCameraComponent* UIMGCameraMode::GetIMGCameraComponent() const
{
	return CastChecked<UIMGCameraComponent>(GetOuter());
}

UWorld* UIMGCameraMode::GetWorld() const
{
	return HasAnyFlags(RF_ClassDefaultObject) ? nullptr : GetOuter()->GetWorld();
}

AActor* UIMGCameraMode::GetTargetActor() const
{
	const UIMGCameraComponent* IMGCameraComponent = GetIMGCameraComponent();

	return IMGCameraComponent->GetTargetActor();
}

FVector UIMGCameraMode::GetPivotLocation() const
{
	const AActor* TargetActor = GetTargetActor();
	check(TargetActor);

	if (const APawn* TargetPawn = Cast<APawn>(TargetActor))
	{
		return TargetPawn->GetPawnViewLocation();
	}

	return TargetActor->GetActorLocation();
}

FRotator UIMGCameraMode::GetPivotRotation() const
{
	const AActor* TargetActor = GetTargetActor();
	check(TargetActor);

	if (const APawn* TargetPawn = Cast<APawn>(TargetActor))
	{
		return TargetPawn->GetViewRotation();
	}

	return TargetActor->GetActorRotation();
}

bool UIMGCameraMode::UpdateCameraMode(float DeltaTime)
{
	const uint32 ResetRevision = InterpolationResetRevision;
	View.Framing = FIMGCameraFramingResult();
	View.Collision.Reset();
	View.CameraShakeScale = FMath::IsFinite(CameraShakeScale) ? FMath::Max(0.f, CameraShakeScale) : 0.f;
	View.CameraShakeCategoryScales.Reset();
	for (const auto& Pair : CameraShakeCategoryScales)
	{
		const float Scale = View.CameraShakeScale * Pair.Value;
		View.CameraShakeCategoryScales.Add(Pair.Key, FMath::IsFinite(Scale) ? FMath::Max(0.f, Scale) : 0.f);
	}
	UpdateView(DeltaTime);
	// A callback invalidating this sample must not publish a partially rebuilt view.
	if (ResetRevision != InterpolationResetRevision)
	{
		View.Collision.Reset();
		return false;
	}
	bResetInterpolation = false;
	UpdateBlending(DeltaTime);
	if (ResetRevision != InterpolationResetRevision)
	{
		View.Collision.Reset();
		return false;
	}
	return true;
}

void UIMGCameraMode::ResetInterpolation()
{
	bResetInterpolation = true;
	++InterpolationResetRevision;
	if (UIMGCameraComponent* Component = Cast<UIMGCameraComponent>(GetOuter())) { Component->ResetCollision(); }
}

void UIMGCameraMode::UpdateView(float DeltaTime)
{
	FVector PivotLocation = GetPivotLocation();
	FRotator PivotRotation = GetPivotRotation();

	PivotRotation.Pitch = FMath::ClampAngle(PivotRotation.Pitch, ViewPitchMin, ViewPitchMax);

	View.Location = PivotLocation;
	View.Rotation = PivotRotation;
	View.ControlRotation = View.Rotation;
	View.FieldOfView = FieldOfView;
}

void UIMGCameraMode::SetBlendWeight(float Weight)
{
	BlendWeight = FMath::IsFinite(Weight) ? FMath::Clamp(Weight, 0.0f, 1.0f) : 1.f;

	// Since we're setting the blend weight directly, we need to calculate the blend alpha to account for the blend function.
	const float InvExponent = FMath::IsFinite(BlendExponent) && BlendExponent > 0.f ? 1.f / BlendExponent : 1.f;

	switch (BlendFunction)
	{
	case EIMGCameraModeBlendFunction::Linear:
		BlendAlpha = BlendWeight;
		break;

	case EIMGCameraModeBlendFunction::EaseIn:
		BlendAlpha = FMath::InterpEaseIn(0.0f, 1.0f, BlendWeight, InvExponent);
		break;

	case EIMGCameraModeBlendFunction::EaseOut:
		BlendAlpha = FMath::InterpEaseOut(0.0f, 1.0f, BlendWeight, InvExponent);
		break;

	case EIMGCameraModeBlendFunction::EaseInOut:
		BlendAlpha = FMath::InterpEaseInOut(0.0f, 1.0f, BlendWeight, InvExponent);
		break;

	default:
		BlendAlpha = BlendWeight;
		break;
	}
}

void UIMGCameraMode::UpdateBlending(float DeltaTime)
{
	if (FMath::IsFinite(DeltaTime) && DeltaTime >= 0.f && FMath::IsFinite(BlendTime) && BlendTime > 0.f)
	{
		BlendAlpha += (DeltaTime / BlendTime);
		BlendAlpha = FMath::IsFinite(BlendAlpha) ? FMath::Clamp(BlendAlpha, 0.f, 1.f) : 1.f;
	}
	else
	{
		BlendAlpha = 1.0f;
	}

	const float Exponent = FMath::IsFinite(BlendExponent) && BlendExponent > 0.f ? BlendExponent : 1.f;

	switch (BlendFunction)
	{
	case EIMGCameraModeBlendFunction::Linear:
		BlendWeight = BlendAlpha;
		break;

	case EIMGCameraModeBlendFunction::EaseIn:
		BlendWeight = FMath::InterpEaseIn(0.0f, 1.0f, BlendAlpha, Exponent);
		break;

	case EIMGCameraModeBlendFunction::EaseOut:
		BlendWeight = FMath::InterpEaseOut(0.0f, 1.0f, BlendAlpha, Exponent);
		break;

	case EIMGCameraModeBlendFunction::EaseInOut:
		BlendWeight = FMath::InterpEaseInOut(0.0f, 1.0f, BlendAlpha, Exponent);
		break;

	default:
		BlendWeight = BlendAlpha;
		break;
	}
}

void UIMGCameraMode::DrawDebug(UCanvas* Canvas) const
{
	check(Canvas);

	FDisplayDebugManager& DisplayDebugManager = Canvas->DisplayDebugManager;

	DisplayDebugManager.SetDrawColor(FColor::White);
	DisplayDebugManager.DrawString(FString::Printf(TEXT("      IMGCameraMode: %s (%f)"), *GetName(), BlendWeight));
}


//////////////////////////////////////////////////////////////////////////
// UIMGCameraModeStack
//////////////////////////////////////////////////////////////////////////
UIMGCameraModeStack::UIMGCameraModeStack()
{
	bIsActive = true;
}

void UIMGCameraModeStack::ActivateStack()
{
	if (!bIsActive)
	{
		bIsActive = true;
		++StackRevision;

		// Notify camera modes that they are being activated.
		const auto Modes = CameraModeStack;
		for (UIMGCameraMode* CameraMode : Modes)
		{
			if (!bIsActive) { break; }
			if (!IsValid(CameraMode) || !CameraModeStack.Contains(CameraMode)) { continue; }
			CameraMode->OnActivation();
		}
	}
}

void UIMGCameraModeStack::DeactivateStack()
{
	if (bIsActive)
	{
		bIsActive = false;
		++StackRevision;

		// Notify camera modes that they are being deactivated.
		const auto Modes = CameraModeStack;
		for (UIMGCameraMode* CameraMode : Modes)
		{
			if (bIsActive) { break; }
			if (!IsValid(CameraMode)) { continue; }
			CameraMode->OnDeactivation();
		}
	}
}

void UIMGCameraModeStack::PushCameraMode(TSubclassOf<UIMGCameraMode> CameraModeClass)
{
	if (!CameraModeClass || CameraModeClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		return;
	}
	if (CameraModeStack.RemoveAll([](const UIMGCameraMode* Mode) { return !IsValid(Mode); }) > 0)
	{
		++StackRevision;
	}

	UIMGCameraMode* CameraMode = GetCameraModeInstance(CameraModeClass);
	check(CameraMode);

	int32 StackSize = CameraModeStack.Num();

	if ((StackSize > 0) && (CameraModeStack[0] == CameraMode))
	{
		// Already top of stack.
		return;
	}

	// See if it's already in the stack and remove it.
	// Figure out how much it was contributing to the stack.
	int32 ExistingStackIndex = INDEX_NONE;
	float ExistingStackContribution = 1.0f;

	for (int32 StackIndex = 0; StackIndex < StackSize; ++StackIndex)
	{
		if (CameraModeStack[StackIndex] == CameraMode)
		{
			ExistingStackIndex = StackIndex;
			ExistingStackContribution *= CameraMode->GetBlendWeight();
			break;
		}
		else
		{
			ExistingStackContribution *= (1.0f - CameraModeStack[StackIndex]->GetBlendWeight());
		}
	}

	if (ExistingStackIndex != INDEX_NONE)
	{
		CameraModeStack.RemoveAt(ExistingStackIndex);
		StackSize--;
	}
	else
	{
		ExistingStackContribution = 0.0f;
	}

	// Decide what initial weight to start with.
	const bool bShouldBlend = ((CameraMode->GetBlendTime() > 0.0f) && (StackSize > 0));
	const float BlendWeight = (bShouldBlend ? ExistingStackContribution : 1.0f);

	CameraMode->SetBlendWeight(BlendWeight);

	// Add new entry to top of stack.
	CameraModeStack.Insert(CameraMode, 0);
	++StackRevision;

	// Make sure stack bottom is always weighted 100%.
	CameraModeStack.Last()->SetBlendWeight(1.0f);

	// Let the camera mode know if it's being added to the stack.
	if (bIsActive && ExistingStackIndex == INDEX_NONE)
	{
		CameraMode->OnActivation();
	}
}

bool UIMGCameraModeStack::EvaluateStack(float DeltaTime, FIMGCameraModeView& OutCameraModeView)
{
	if (!bIsActive)
	{
		return false;
	}

	if (!UpdateStack(DeltaTime)) { return false; }
	BlendStack(OutCameraModeView);
	if (OutCameraModeView.Collision.IsSet() && CameraModeStack.Num() > 1)
	{
		OutCameraModeView.Collision->Input.bForcePushIn = true;
	}

	return true;
}

void UIMGCameraModeStack::ResetInterpolation()
{
	++StackRevision;
	for (UIMGCameraMode* CameraMode : CameraModeStack)
	{
		if (IsValid(CameraMode)) { CameraMode->ResetInterpolation(); }
	}
}

UIMGCameraMode* UIMGCameraModeStack::GetCameraModeInstance(TSubclassOf<UIMGCameraMode> CameraModeClass)
{
	check(CameraModeClass);

	// First see if we already created one.
	for (UIMGCameraMode* CameraMode : CameraModeInstances)
	{
		if (IsValid(CameraMode) && CameraMode->GetClass() == CameraModeClass)
		{
			return CameraMode;
		}
	}

	// Not found, so we need to create it.
	UIMGCameraMode* NewCameraMode = NewObject<UIMGCameraMode>(GetOuter(), CameraModeClass, NAME_None, RF_NoFlags);
	check(NewCameraMode);

	CameraModeInstances.Add(NewCameraMode);

	return NewCameraMode;
}

bool UIMGCameraModeStack::UpdateStack(float DeltaTime)
{
	const int32 StackSize = CameraModeStack.Num();
	if (StackSize <= 0)
	{
		return false;
	}

	int32 RemoveCount = 0;
	int32 RemoveIndex = INDEX_NONE;
	uint32 Revision = StackRevision;

	for (int32 StackIndex = 0; StackIndex < StackSize; ++StackIndex)
	{
		UIMGCameraMode* CameraMode = CameraModeStack[StackIndex];
		if (!IsValid(CameraMode)) { return false; }

		if (!CameraMode->UpdateCameraMode(DeltaTime)) { return false; }
		if (!bIsActive || Revision != StackRevision || !IsValid(CameraMode)) { return false; }

		if (CameraMode->GetBlendWeight() >= 1.0f)
		{
			// Everything below this mode is now irrelevant and can be removed.
			RemoveIndex = (StackIndex + 1);
			RemoveCount = (StackSize - RemoveIndex);
			break;
		}
	}

	if (RemoveCount > 0)
	{
		TArray<TObjectPtr<UIMGCameraMode>> RemovedModes;
		for (int32 StackIndex = RemoveIndex; StackIndex < StackSize; ++StackIndex)
		{
			RemovedModes.Add(CameraModeStack[StackIndex]);
		}
		CameraModeStack.RemoveAt(RemoveIndex, RemoveCount);
		Revision = ++StackRevision;
		for (UIMGCameraMode* CameraMode : RemovedModes)
		{
			if (IsValid(CameraMode) && !CameraModeStack.Contains(CameraMode)) { CameraMode->OnDeactivation(); }
		}
	}
	return bIsActive && Revision == StackRevision && !CameraModeStack.IsEmpty();
}

void UIMGCameraModeStack::BlendStack(FIMGCameraModeView& OutCameraModeView) const
{
	const int32 StackSize = CameraModeStack.Num();
	if (StackSize <= 0)
	{
		return;
	}

	// Start at the bottom and blend up the stack
	const UIMGCameraMode* CameraMode = CameraModeStack[StackSize - 1];
	check(CameraMode);

	OutCameraModeView = CameraMode->GetCameraModeView();

	for (int32 StackIndex = (StackSize - 2); StackIndex >= 0; --StackIndex)
	{
		CameraMode = CameraModeStack[StackIndex];
		check(CameraMode);

		OutCameraModeView.Blend(CameraMode->GetCameraModeView(), CameraMode->GetBlendWeight());
	}
}

void UIMGCameraModeStack::DrawDebug(UCanvas* Canvas) const
{
	check(Canvas);

	FDisplayDebugManager& DisplayDebugManager = Canvas->DisplayDebugManager;

	DisplayDebugManager.SetDrawColor(FColor::Green);
	DisplayDebugManager.DrawString(FString(TEXT("   --- Camera Modes (Begin) ---")));

	for (const UIMGCameraMode* CameraMode : CameraModeStack)
	{
		if (IsValid(CameraMode)) { CameraMode->DrawDebug(Canvas); }
	}

	DisplayDebugManager.SetDrawColor(FColor::Green);
	DisplayDebugManager.DrawString(FString::Printf(TEXT("   --- Camera Modes (End) ---")));
}

void UIMGCameraModeStack::GetBlendInfo(float& OutWeightOfTopLayer, FGameplayTag& OutTagOfTopLayer) const
{
	if (!bIsActive || CameraModeStack.IsEmpty())
	{
		OutWeightOfTopLayer = 0.0f;
		OutTagOfTopLayer = FGameplayTag();
		return;
	}
	else
	{
		UIMGCameraMode* TopEntry = CameraModeStack[0];
		if (!IsValid(TopEntry))
		{
			OutWeightOfTopLayer = 0.f;
			OutTagOfTopLayer = FGameplayTag();
			return;
		}
		OutWeightOfTopLayer = TopEntry->GetBlendWeight();
		OutTagOfTopLayer = TopEntry->GetCameraTypeTag();
	}
}
