#include "Camera/IMGCameraShakeInstance.h"

#include "Camera/IMGCameraShakePattern.h"
#include "Camera/IMGCameraShakeDefinition.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGCameraShakeInstance)

UIMGCameraShakeInstance::UIMGCameraShakeInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UIMGCameraShakePattern>(TEXT("RootShakePattern")))
{
}

UIMGCameraShakePattern* UIMGCameraShakeInstance::GetIMGPattern() const
{
	return Cast<UIMGCameraShakePattern>(GetRootShakePattern());
}

void UIMGCameraShakeInstance::BindSourceOwner(UObject* Source)
{
	UnbindSourceOwner();
	SourceOwner = Source;
	bSourceEndedPlay = false;
	UActorComponent* Component = Cast<UActorComponent>(Source);
	bSourceComponentHasBegunPlay = Component && Component->HasBegunPlay();
	AActor* Actor = Component ? Component->GetOwner() : Cast<AActor>(Source);
	if (IsValid(Actor))
	{
		SourceActor = Actor;
		Actor->OnEndPlay.AddUniqueDynamic(this, &ThisClass::HandleSourceActorEndPlay);
	}
}

void UIMGCameraShakeInstance::UnbindSourceOwner()
{
	if (AActor* Actor = SourceActor.Get())
	{
		Actor->OnEndPlay.RemoveDynamic(this, &ThisClass::HandleSourceActorEndPlay);
	}
	SourceActor.Reset();
}

void UIMGCameraShakeInstance::HandleSourceActorEndPlay(AActor*, EEndPlayReason::Type)
{
	bSourceEndedPlay = true;
	UnbindSourceOwner();
	if (IsActive() && !IsFinished()) { StopShake(false); }
}

bool UIMGCameraShakeInstance::IsReleasing() const
{
	const auto* Pattern = GetIMGPattern();
	return Pattern && Pattern->IsReleasing();
}

float UIMGCameraShakeInstance::GetSpatialScale(const FVector& CameraLocation, const FVector& TargetLocation) const
{
	if (!SourceLocation.IsSet()) { return 1.f; }
	if (!Definition) { return 0.f; }
	const double Distance = FVector::Distance(CameraLocation, *SourceLocation);
	const double Ratio = FMath::Clamp((Definition->OuterRadius - Distance) / (Definition->OuterRadius - Definition->InnerRadius), 0.0, 1.0);
	const double SmoothRatio = 0.5 * (1.0 - FMath::Cos(UE_PI * Ratio));
	const double DistanceScale = 1.0 - FMath::Cos(UE_HALF_PI * SmoothRatio);
	const double TargetDistance = FVector::Distance(CameraLocation, TargetLocation);
	const double TargetScale = Definition->IdealTargetDistance > 0.f && TargetDistance > Definition->IdealTargetDistance
		? FMath::Sqrt(Definition->IdealTargetDistance / TargetDistance) : 1.0;
	return float(DistanceScale * TargetScale);
}
