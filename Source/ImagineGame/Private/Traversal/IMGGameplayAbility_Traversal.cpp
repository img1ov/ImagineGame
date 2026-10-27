#include "Traversal/IMGGameplayAbility_Traversal.h"

#include "AbilitySystem/IMGAbilitySystemComponent.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "AnimationWarpingLibrary.h"
#include "Animation/AnimMontage.h"
#include "Character/IMGCharacter.h"
#include "Character/IMGCharacterMovementComponent.h"
#include "Character/IMGHealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "RootMotionModifier.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "MotionWarpingComponent.h"
#include "Traversal/IMGTraversalComponent.h"
#include "Traversal/IMGTraversalDetector.h"
#include "Traversal/IMGTraversalTargetData.h"

#if ENABLE_DRAW_DEBUG
#include "IMGTraversalDebug.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGGameplayAbility_Traversal)

DEFINE_LOG_CATEGORY_STATIC(LogIMGTraversal, Log, All);

#if ENABLE_DRAW_DEBUG
namespace
{
void DrawAbilityMessage(const AActor* Avatar, const TCHAR* Message, const FColor& Color)
{
	if (Avatar && IMGTraversalDebug::IsEnabled(1))
	{
		IMGTraversalDebug::String(
		    Avatar->GetWorld(), Avatar->GetActorLocation() + FVector::UpVector * 145.f, Message, Color, 1);
	}
}
} // namespace
#endif

UIMGGameplayAbility_Traversal::UIMGGameplayAbility_Traversal(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	ActivationGroup = EIMGAbilityActivationGroup::Exclusive_Replaceable;
	bServerRespectsRemoteAbilityCancellation = false;
}

bool UIMGGameplayAbility_Traversal::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
    const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags) || !ActorInfo ||
	    !ActorInfo->AvatarActor.IsValid())
	{
		return false;
	}
	const AIMGCharacter* Character = Cast<AIMGCharacter>(ActorInfo->AvatarActor.Get());
	const UIMGTraversalComponent* Component = UIMGTraversalComponent::Get(Character);
	const UIMGHealthComponent* Health = UIMGHealthComponent::FindHealthComponent(Character);
	return Component && Component->IsRegistered() && !Component->IsTraversalActive() &&
	    IsValid(Component->GetMotionWarpingComponent()) && Component->GetMotionWarpingComponent()->IsRegistered() &&
	    Character->GetIMGMovementComponent() && (!Health || !Health->IsDeadOrDying());
}

void UIMGGameplayAbility_Traversal::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
    const FGameplayEventData* TriggerEventData)
{
	bLocalRequest = false;
	bTraversalStarted = false;
	bMontageCompleted = false;
	bTargetDataSent = false;
	bTargetDataCancellationSent = false;
	ActiveObstacleComponents.Reset();
	PreviousObstacleTransforms.Reset();
	LocalCandidate = FIMGTraversalCandidate();
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActive() || bIsAbilityEnding)
	{
		CancelPendingTargetData();
		return;
	}
	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	UIMGTraversalComponent* Component = UIMGTraversalComponent::Get(GetAvatarActorFromActorInfo());
	if (!ASC || !Component)
	{
		CancelPendingTargetData();
		return;
	}
	if (ActorInfo->IsNetAuthority())
	{
		TargetDataDelegateHandle =
		    ASC->AbilityTargetDataSetDelegate(Handle, ActivationInfo.GetActivationPredictionKey())
		        .AddUObject(this, &ThisClass::OnTargetDataReady);
		TargetDataCancelledDelegateHandle =
		    ASC->AbilityTargetDataCancelledDelegate(Handle, ActivationInfo.GetActivationPredictionKey())
		        .AddUObject(this, &ThisClass::OnTargetDataCancelled);
		ASC->CallReplicatedTargetDataDelegatesIfSet(Handle, ActivationInfo.GetActivationPredictionKey());
		if (!IsActive() || bIsAbilityEnding || bTraversalStarted)
		{
			return;
		}
	}
	AIMGCharacter* Character = GetIMGCharacterFromActorInfo();
	const bool bServerControlledAI =
	    ActorInfo->IsNetAuthority() && Character && Character->GetController() && !Character->IsPlayerControlled();
	const bool bShouldBuildLocalRequest = ActorInfo->IsLocallyControlled() || bServerControlledAI;
	if (!bShouldBuildLocalRequest)
	{
		return;
	}
	bLocalRequest = true;
	EIMGTraversalFailure Failure;
	const EIMGTraversalRequestSource Source = bServerControlledAI ? EIMGTraversalRequestSource::AI : RequestSource;
	if (!Component->Detect(LocalCandidate, Failure, FVector::ZeroVector, Source) ||
	    !Component->CanInitiate(LocalCandidate))
	{
		CancelPendingTargetData();
		return;
	}
	PreSelectTraversalAnimation(LocalCandidate);
	if (!IsActive() || bIsAbilityEnding)
	{
		CancelPendingTargetData();
		return;
	}
	const FIMGTraversalAnimationSelection Selection = SelectTraversalAnimation(LocalCandidate);
	if (!IsActive() || bIsAbilityEnding || !IsSelectionValid(Selection, LocalCandidate, false))
	{
#if ENABLE_DRAW_DEBUG
		DrawAbilityMessage(Character, TEXT("Traversal GA: animation selection rejected"), FColor::Red);
#endif
		CancelPendingTargetData();
		return;
	}
	auto* Request = new FIMGTraversalTargetData();
	Request->Direction = LocalCandidate.ApproachDirection;
	Request->Source = LocalCandidate.RequestSource;
	Request->Action = Selection.Action;
	Request->Montage = Selection.Montage;
	Request->StartTime = Selection.StartTime;
	Request->PlayRate = Selection.PlayRate;
	FGameplayAbilityTargetDataHandle Data;
	Data.Add(Request);
	OnTargetDataReady(Data, FGameplayTag());
}

void UIMGGameplayAbility_Traversal::SendPendingTargetDataCancellation()
{
	if (!bTargetDataSent && !bTargetDataCancellationSent && CurrentActorInfo &&
	    CurrentActorInfo->IsLocallyControlled() && !CurrentActorInfo->IsNetAuthority())
	{
		if (UAbilitySystemComponent* ASC = CurrentActorInfo->AbilitySystemComponent.Get())
		{
			FScopedPredictionWindow PredictionWindow(ASC, true);
			ASC->ServerSetReplicatedTargetDataCancelled(
			    CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey(), ASC->ScopedPredictionKey);
			bTargetDataCancellationSent = true;
		}
	}
}

void UIMGGameplayAbility_Traversal::CancelPendingTargetData()
{
	SendPendingTargetDataCancellation();
	if (IsActive())
	{
		K2_EndAbility();
	}
}

void UIMGGameplayAbility_Traversal::OnTargetDataCancelled()
{
	if (!CurrentActorInfo || !CurrentActorInfo->AbilitySystemComponent.IsValid())
	{
		return;
	}
	UAbilitySystemComponent* ASC = CurrentActorInfo->AbilitySystemComponent.Get();
	ASC->ConsumeClientReplicatedTargetData(CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey());
	// Only release the server instance while it is waiting; active traversal remains authority-owned.
	if (IsActive() && !bTraversalStarted)
	{
		K2_EndAbility();
	}
}

void UIMGGameplayAbility_Traversal::OnTargetDataReady(
    const FGameplayAbilityTargetDataHandle& Data, FGameplayTag ApplicationTag)
{
	if (!IsActive() || bIsAbilityEnding || bTraversalStarted || !CurrentActorInfo ||
	    !CurrentActorInfo->AbilitySystemComponent.IsValid())
	{
		return;
	}
	UAbilitySystemComponent* ASC = CurrentActorInfo->AbilitySystemComponent.Get();
	FScopedPredictionWindow PredictionWindow(ASC);
	const FIMGTraversalTargetData* Request =
	    Data.Num() == 1 && Data.Get(0) && Data.Get(0)->GetScriptStruct() == FIMGTraversalTargetData::StaticStruct()
	    ? static_cast<const FIMGTraversalTargetData*>(Data.Get(0))
	    : nullptr;
	UIMGTraversalComponent* Component = UIMGTraversalComponent::Get(GetAvatarActorFromActorInfo());
	AIMGCharacter* Character = GetIMGCharacterFromActorInfo();
	if (!Request || !Component || !Character)
	{
		CancelPendingTargetData();
		return;
	}
	FIMGTraversalCandidate Candidate;
	const bool bRemoteRequest = CurrentActorInfo->IsNetAuthority() && !bLocalRequest;
	if (bRemoteRequest)
	{
		EIMGTraversalFailure Failure;
		if (Request->Source != RequestSource ||
		    FVector::DotProduct(FVector(Request->Direction).GetSafeNormal2D(), Character->GetActorForwardVector()) <
		        0.25f ||
		    !Component->Detect(Candidate, Failure, Request->Direction, RequestSource))
		{
			CancelPendingTargetData();
			return;
		}
	}
	else
	{
		Candidate = LocalCandidate;
	}
	FIMGTraversalAnimationSelection Selection;
	Selection.Action = Request->Action;
	Selection.Montage = Request->Montage;
	Selection.StartTime = Request->StartTime;
	Selection.PlayRate = Request->PlayRate;
	if (!IsSelectionValid(Selection, Candidate, bRemoteRequest))
	{
#if ENABLE_DRAW_DEBUG
		DrawAbilityMessage(Character, TEXT("Traversal GA: selection or server allow-list rejected"), FColor::Red);
#endif
		CancelPendingTargetData();
		return;
	}
	TArray<FIMGTraversalWarpTarget> WarpTargets;
	if (!PrepareWarpTargets(Candidate, Selection, WarpTargets))
	{
		CancelPendingTargetData();
		return;
	}
	if (!CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo))
	{
#if ENABLE_DRAW_DEBUG
		DrawAbilityMessage(Character, TEXT("Traversal GA: CommitAbility failed"), FColor::Red);
#endif
		CancelPendingTargetData();
		return;
	}
	if (!IsActive() || bIsAbilityEnding || GetAvatarActorFromActorInfo() != Character ||
	    !IsValid(Component) || !IsSelectionValid(Selection, Candidate, bRemoteRequest))
	{
		CancelPendingTargetData();
		return;
	}
	for (const FIMGTraversalWarpTarget& Target : WarpTargets)
	{
		if (!FIMGTraversalDetector::CanUseSurface(
		        Target.Component, Component->GetDetectorSettings(), Candidate.RequestSource))
		{
			CancelPendingTargetData();
			return;
		}
	}
	LocalCandidate = Candidate;
	ActiveComponent = Component;
	ActiveObstacleComponents.Reset();
	PreviousObstacleTransforms.Reset();
	for (UPrimitiveComponent* ObstacleComponent : Candidate.ObstacleComponents)
	{
		if (IsValid(ObstacleComponent))
		{
			ActiveObstacleComponents.Add(ObstacleComponent);
			PreviousObstacleTransforms.Add(ObstacleComponent->GetComponentTransform());
		}
	}
	if (ActiveObstacleComponents.IsEmpty() && IsValid(Candidate.HitComponent))
	{
		ActiveObstacleComponents.Add(Candidate.HitComponent);
		PreviousObstacleTransforms.Add(Candidate.HitComponent->GetComponentTransform());
	}
	for (const FIMGTraversalWarpTarget& Target : WarpTargets)
	{
		if (!ActiveObstacleComponents.Contains(Target.Component.Get()))
		{
			ActiveObstacleComponents.Add(Target.Component.Get());
			PreviousObstacleTransforms.Add(Target.Component->GetComponentTransform());
		}
	}
	PreviousTargetCheckTime = GetWorld()->GetTimeSeconds();
	bTraversalStarted = true;
	Component->BeginTraversal(Candidate, Selection, WarpTargets);
	Character->GetIMGMovementComponent()->SetMovementMode(MOVE_Flying);
	if (!IsActive() || bIsAbilityEnding || !IsValid(Component) || !Component->IsTraversalActive() ||
	    !IsValid(Character) || GetAvatarActorFromActorInfo() != Character ||
	    !Character->GetIMGMovementComponent() || !Character->GetIMGMovementComponent()->IsFlying())
	{
		CancelPendingTargetData();
		return;
	}
	GetWorld()->GetTimerManager().SetTimer(TargetCheckTimer, this, &ThisClass::CheckActiveTarget,
	    FMath::Max(Component->GetTargetValidationInterval(), 0.01f), true);
	UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
	    this, NAME_None, Selection.Montage, Selection.PlayRate, NAME_None, true, 1.f, Selection.StartTime, true);
	MontageTask->OnCompleted.AddDynamic(this, &ThisClass::OnMontageCompleted);
	MontageTask->OnInterrupted.AddDynamic(this, &ThisClass::OnMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(this, &ThisClass::OnMontageInterrupted);
	MontageTask->ReadyForActivation();
	if (!IsActive() || bIsAbilityEnding || !IsValid(Component) || !Component->IsTraversalActive())
	{
		CancelPendingTargetData();
		return;
	}
	if (CurrentActorInfo->IsNetAuthority())
	{
		uint8 MontagePlayInstanceId = 0;
		UIMGAbilitySystemComponent* IMGASC = Cast<UIMGAbilitySystemComponent>(ASC);
		if (!IMGASC || !IMGASC->GetActiveReplicatedMontagePlayInstanceId(Selection.Montage, MontagePlayInstanceId) ||
		    !Component->PublishTraversalState(MontagePlayInstanceId))
		{
			K2_EndAbility();
			return;
		}
	}
#if ENABLE_DRAW_DEBUG
	IMGTraversalDebug::SuccessfulTraversal(Character, Candidate, Selection);
#endif
	OnTraversalStarted(Candidate, Selection.Action);
	if (!IsActive() || bIsAbilityEnding)
	{
		CancelPendingTargetData();
		return;
	}
	if (CurrentActorInfo->IsLocallyControlled() && !CurrentActorInfo->IsNetAuthority())
	{
		bTargetDataSent = true;
		ASC->CallServerSetReplicatedTargetData(CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey(),
		    Data, ApplicationTag, ASC->ScopedPredictionKey);
	}
	ASC->ConsumeClientReplicatedTargetData(CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey());
}

bool UIMGGameplayAbility_Traversal::IsSelectionValid(const FIMGTraversalAnimationSelection& Selection,
    const FIMGTraversalCandidate& Candidate, bool bRemoteRequest) const
{
	const UIMGTraversalComponent* Traversal = UIMGTraversalComponent::Get(GetAvatarActorFromActorInfo());
	const AIMGCharacter* Character = GetIMGCharacterFromActorInfo();
	const UIMGCharacterMovementComponent* Movement = Character ? Character->GetIMGMovementComponent() : nullptr;
	const UIMGHealthComponent* Health = UIMGHealthComponent::FindHealthComponent(Character);
	if (!Traversal || !Traversal->IsRegistered() || Traversal->IsTraversalActive() ||
	    !IsValid(Traversal->GetMotionWarpingComponent()) || !Traversal->GetMotionWarpingComponent()->IsRegistered() ||
	    !Movement || Character->GetStance() != EIMGStance::Stand ||
	    !(Movement->IsMovingOnGround() || Movement->IsFalling()) || (Health && Health->IsDeadOrDying()))
	{
		return false;
	}
	for (const UPrimitiveComponent* Obstacle : Candidate.ObstacleComponents)
	{
		if (!FIMGTraversalDetector::CanUseSurface(Obstacle, Traversal->GetDetectorSettings(), Candidate.RequestSource))
		{
			return false;
		}
	}
	const UWorld* World = GetWorld();
	const double CandidateAge = World ? World->GetTimeSeconds() - Candidate.DetectionTime : BIG_NUMBER;
	const bool bCandidateFresh =
	    Traversal && World && CandidateAge >= 0.0 && CandidateAge <= Traversal->GetDetectorSettings().MaxCandidateAge;
#if ENABLE_DRAW_DEBUG
	if (!bCandidateFresh)
	{
		if (Character)
		{
			IMGTraversalDebug::String(Character->GetWorld(), Character->GetActorLocation() + FVector::UpVector * 160.f,
			    FString::Printf(TEXT("Traversal Validate: stale candidate %.3fs"), CandidateAge), FColor::Red, 1);
		}
	}
#endif
	const bool bMatchesRemoteWhitelist =
	    AllowedMontages.ContainsByPredicate([&Selection](const FIMGTraversalAllowedMontage& Allowed)
	        { return Allowed.Action == Selection.Action && Allowed.Montage == Selection.Montage; });
	const bool bAllowedRemoteSelection = !bRemoteRequest || AllowedMontages.IsEmpty() || bMatchesRemoteWhitelist;
#if ENABLE_DRAW_DEBUG
	if (bRemoteRequest && !bAllowedRemoteSelection)
	{
		DrawAbilityMessage(GetIMGCharacterFromActorInfo(),
		    TEXT("Traversal Validate: remote montage is not in AllowedMontages"), FColor::Red);
	}
#endif
	const FIMGTraversalAnimationSettings* Settings =
	    Traversal ? Traversal->FindAnimationSettings(Selection.Action, Selection.Montage) : nullptr;
	return Settings && Traversal->CanInitiate(Candidate, bRemoteRequest) &&
	    FMath::IsFinite(Traversal->GetTargetValidationInterval()) &&
	    Traversal->GetTargetValidationInterval() >= 0.01f && FMath::IsFinite(Settings->MinPlayRate) &&
	    FMath::IsFinite(Settings->MaxPlayRate) && Settings->MinPlayRate > 0.f &&
	    Settings->MaxPlayRate >= Settings->MinPlayRate && FMath::IsFinite(Settings->MinVerticalClearance) &&
	    Settings->MinVerticalClearance >= 0.f && FMath::IsFinite(Settings->MinHorizontalClearance) &&
	    Settings->MinHorizontalClearance >= 0.f && Candidate.VerticalClearance >= Settings->MinVerticalClearance &&
	    Candidate.HorizontalClearance >= Settings->MinHorizontalClearance && bCandidateFresh &&
	    Candidate.bHasFrontLedge && IsValid(Candidate.HitComponent) && Candidate.Supports(Selection.Action) &&
	    IsValid(Selection.Montage) && FMath::IsFinite(Selection.StartTime) && Selection.StartTime >= 0.f &&
	    Selection.StartTime < Selection.Montage->GetPlayLength() && FMath::IsFinite(Selection.PlayRate) &&
	    Selection.PlayRate >= Settings->MinPlayRate && Selection.PlayRate <= Settings->MaxPlayRate &&
	    bAllowedRemoteSelection;
}

bool UIMGGameplayAbility_Traversal::PrepareWarpTargets(FIMGTraversalCandidate& Candidate,
    const FIMGTraversalAnimationSelection& Selection, TArray<FIMGTraversalWarpTarget>& OutTargets) const
{
	OutTargets.Reset();
	AIMGCharacter* Character = GetIMGCharacterFromActorInfo();
	const UIMGTraversalComponent* Traversal = UIMGTraversalComponent::Get(Character);
	const FIMGTraversalAnimationSettings* Settings =
	    Traversal ? Traversal->FindAnimationSettings(Selection.Action, Selection.Montage) : nullptr;
	if (!Character || !Character->GetWorld() || !Character->GetCapsuleComponent() || !Settings)
	{
		return false;
	}
#if ENABLE_DRAW_DEBUG
	IMGTraversalDebug::BeginTraversalSnapshot(Character, false);
#endif
	auto Reject = [Character, &Selection](const FString& Reason)
	{
		UE_LOG(LogIMGTraversal, Warning, TEXT("%s: %s"), *GetNameSafe(Selection.Montage.Get()), *Reason);
#if ENABLE_DRAW_DEBUG
		DrawAbilityMessage(Character, *Reason, FColor::Red);
#endif
		return false;
	};
	auto GetAnchor = [&Candidate](EIMGTraversalAnchor Anchor, FTransform& Transform, UPrimitiveComponent*& Component)
	{
		switch (Anchor)
		{
		case EIMGTraversalAnchor::FrontLedge:
			Transform = Candidate.FrontLedge;
			Component = Candidate.FrontLedgeComponent;
			return Candidate.bHasFrontLedge && IsValid(Component);
		case EIMGTraversalAnchor::BackLedge:
			Transform = Candidate.BackLedge;
			Component = Candidate.BackLedgeComponent;
			return Candidate.bHasBackLedge && IsValid(Component);
		case EIMGTraversalAnchor::BackFloor:
			Transform = Candidate.BackFloor;
			Component = Candidate.BackFloorComponent;
			return Candidate.bHasBackFloor && IsValid(Component);
		default:
			return false;
		}
	};
	TSet<FName> BindingNames;
	for (const FIMGTraversalWarpBinding& Binding : Settings->WarpBindings)
	{
		if (Binding.TargetName.IsNone() || BindingNames.Contains(Binding.TargetName) || !Binding.Offset.IsValid())
		{
			return Reject(
			    FString::Printf(TEXT("Warp binding invalid or duplicated: %s"), *Binding.TargetName.ToString()));
		}
		BindingNames.Add(Binding.TargetName);
	}
	TArray<FIMGTraversalWarpWindow> Windows;
	Traversal->GetWarpWindows(Selection.Montage, Windows);
	TSet<FName> UsedTargets;
	for (const FIMGTraversalWarpWindow& Window : Windows)
	{
		if (Window.EndTime <= Selection.StartTime)
		{
			continue;
		}
		if (!FMath::IsFinite(Window.StartTime) || !FMath::IsFinite(Window.EndTime) ||
		    Window.EndTime <= Window.StartTime || Window.StartTime < 0.f ||
		    Window.EndTime > Selection.Montage->GetPlayLength())
		{
			return Reject(TEXT("Warp window has an invalid time range"));
		}
		const FName Name = Window.Modifier->WarpTargetName;
		if (!BindingNames.Contains(Name))
		{
			return Reject(FString::Printf(TEXT("Warp target has no geometry binding: %s"), *Name.ToString()));
		}
		UsedTargets.Add(Name);
	}
	for (FName Required : Settings->RequiredTargets)
	{
		if (!UsedTargets.Contains(Required))
		{
			return Reject(FString::Printf(TEXT("Required warp target %s has no remaining window at %.3f"),
			    *Required.ToString(), Selection.StartTime));
		}
	}
	auto FindBinding = [Settings](FName Name)
	{
		return Settings->WarpBindings.FindByPredicate(
		    [Name](const FIMGTraversalWarpBinding& Binding) { return Binding.TargetName == Name; });
	};
	auto SelectWindow = [&Windows](FName Name, int32 Index, float MinEnd, float MaxEnd)
	{ return UIMGTraversalComponent::FindTranslationWindow(Windows, Name, Index, MinEnd, MaxEnd); };
	auto AlignmentTime = [&Selection](const FIMGTraversalWarpWindow& Window)
	{ return Window.Modifier->bSubtractRemainingRootMotion ? Selection.Montage->GetPlayLength() : Window.EndTime; };
	const FIMGTraversalLandingSettings& Landing = Settings->Landing;
	if (static_cast<uint8>(Landing.Mode) > static_cast<uint8>(EIMGTraversalLandingMode::DistanceCurve))
	{
		return Reject(TEXT("Landing mode is invalid"));
	}
	if (Landing.Mode != EIMGTraversalLandingMode::None)
	{
		if (!FMath::IsFinite(Landing.CapsuleClearance) || Landing.CapsuleClearance < 0.f)
		{
			return Reject(TEXT("Landing capsule clearance is invalid"));
		}
		const FIMGTraversalWarpBinding* LandingBinding = FindBinding(Landing.TargetName);
		const FIMGTraversalWarpWindow* LandingWindow = SelectWindow(
		    Landing.TargetName, Landing.LandingWindowIndex, Selection.StartTime, Selection.Montage->GetPlayLength());
		if (!LandingBinding || LandingBinding->Anchor != EIMGTraversalAnchor::BackFloor || !LandingWindow)
		{
			return Reject(TEXT("Landing needs a BackFloor binding and a remaining translation window"));
		}
		FVector LandingPoint = Candidate.BackFloor.GetLocation();
		FTransform PathAnchor = Candidate.BackLedge;
		if (Landing.Mode == EIMGTraversalLandingMode::DistanceCurve)
		{
			const FIMGTraversalWarpWindow* AnchorWindow =
			    SelectWindow(Landing.AnchorTargetName, Landing.AnchorWindowIndex, -1.f, LandingWindow->EndTime);
			if (!AnchorWindow && !SelectWindow(Landing.AnchorTargetName, -1, -1.f, Selection.Montage->GetPlayLength()))
			{
				AnchorWindow = SelectWindow(
				    Landing.FallbackAnchorTargetName, Landing.AnchorWindowIndex, -1.f, LandingWindow->EndTime);
			}
			const FIMGTraversalWarpBinding* AnchorBinding =
			    AnchorWindow ? FindBinding(AnchorWindow->Modifier->WarpTargetName) : nullptr;
			UPrimitiveComponent* AnchorComponent = nullptr;
			if (!AnchorBinding || AnchorBinding->Anchor == EIMGTraversalAnchor::BackFloor ||
			    !GetAnchor(AnchorBinding->Anchor, PathAnchor, AnchorComponent) ||
			    AlignmentTime(*AnchorWindow) >= AlignmentTime(*LandingWindow))
			{
				return Reject(TEXT("Landing contact window is missing, ambiguous in order, or has no geometry"));
			}
			float AnchorValue = 0.f;
			float LandingValue = 0.f;
			if (!UAnimationWarpingLibrary::GetCurveValueFromAnimation(
			        Selection.Montage, Landing.DistanceCurveName, AlignmentTime(*AnchorWindow), AnchorValue) ||
			    !UAnimationWarpingLibrary::GetCurveValueFromAnimation(
			        Selection.Montage, Landing.DistanceCurveName, AlignmentTime(*LandingWindow), LandingValue) ||
			    !FMath::IsFinite(AnchorValue) || !FMath::IsFinite(LandingValue))
			{
				return Reject(FString::Printf(
				    TEXT("Landing curve %s is missing or non-finite"), *Landing.DistanceCurveName.ToString()));
			}
			const float Distance =
			    Landing.bAbsoluteCurveDistance ? FMath::Abs(LandingValue - AnchorValue) : LandingValue - AnchorValue;
			LandingPoint = PathAnchor.GetLocation() + PathAnchor.GetUnitAxis(EAxis::X).GetSafeNormal2D() * Distance;
#if ENABLE_DRAW_DEBUG
			IMGTraversalDebug::String(Character->GetWorld(), LandingPoint + FVector::UpVector * 25.f,
			    FString::Printf(TEXT("Landing %s %.3f -> %s %.3f | Curve %.1f -> %.1f"),
			        *AnchorWindow->Modifier->WarpTargetName.ToString(), AnchorWindow->EndTime,
			        *Landing.TargetName.ToString(), LandingWindow->EndTime, AnchorValue, LandingValue),
			    FColor::Yellow, 3);
#endif
		}
		else if (!Candidate.bHasBackFloor)
		{
			return Reject(TEXT("Detected-floor landing has no sampled walkable support"));
		}
		const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		const float Radius = Capsule->GetScaledCapsuleRadius();
		const FIMGTraversalDetectorSettings& Geometry = Traversal->GetDetectorSettings();
		const float TopZ = FMath::Max(Candidate.FrontLedge.GetLocation().Z, Candidate.BackLedge.GetLocation().Z);
		const FVector TraceStart(LandingPoint.X, LandingPoint.Y, TopZ + HalfHeight);
		const FVector TraceEnd(LandingPoint.X, LandingPoint.Y, Candidate.FrontLedge.GetLocation().Z - Geometry.MaxDrop);
		FHitResult Floor;
		FCollisionQueryParams FloorParams(SCENE_QUERY_STAT(IMGTraversalSelectedLanding), false, Character);
		const FCollisionResponseParams Responses(Capsule->GetCollisionResponseToChannels());
		const bool bHit = Character->GetWorld()->LineTraceSingleByChannel(
		    Floor, TraceStart, TraceEnd, Geometry.TraceChannel, FloorParams, Responses);
#if ENABLE_DRAW_DEBUG
		IMGTraversalDebug::Line(Character->GetWorld(), TraceStart, bHit ? Floor.ImpactPoint : TraceEnd,
		    bHit ? FColor::Green : FColor::Red, 3);
		if (bHit)
		{
			IMGTraversalDebug::Intersection(Character->GetWorld(), Floor,
			    Character->GetIMGMovementComponent()->IsWalkable(Floor) &&
			        FIMGTraversalDetector::CanUseSurface(Floor.GetComponent(), Geometry, Candidate.RequestSource),
			    TEXT("Animation-selected floor"), IMGTraversalDebug::EStage::Execution);
		}
#endif
		UPrimitiveComponent* FloorComponent = Floor.GetComponent();
		if (!bHit || !Character->GetIMGMovementComponent()->IsWalkable(Floor) ||
		    !FIMGTraversalDetector::CanUseSurface(FloorComponent, Geometry, Candidate.RequestSource))
		{
			return Reject(TEXT("Selected landing floor is unusable"));
		}
		const float Drop = Candidate.FrontLedge.GetLocation().Z - Floor.ImpactPoint.Z;
		if (Drop < 0.f || Drop > Geometry.MaxDrop)
		{
			return Reject(TEXT("Selected landing exceeds the configured drop range"));
		}
		LandingPoint.Z = Floor.ImpactPoint.Z;
		FTransform FloorTarget = Candidate.BackFloor;
		FloorTarget.SetRotation(PathAnchor.GetRotation());
		FloorTarget.SetLocation(LandingPoint);
		FTransform RootTarget = LandingBinding->Offset * FloorTarget;
		const URootMotionModifier_Warp* Modifier = LandingWindow->Modifier;
		FTransform RootOffset = FTransform::Identity;
		if (Modifier->WarpPointAnimProvider == EWarpPointAnimProvider::Bone)
		{
			if (!Character->GetMesh() || !Character->GetMesh()->GetAnimInstance() ||
			    Character->GetMesh()->GetBoneIndex(Modifier->WarpPointAnimBoneName) == INDEX_NONE)
			{
				return Reject(TEXT("Landing warp-point bone is missing"));
			}
			RootOffset = UMotionWarpingUtilities::CalculateRootTransformRelativeToWarpPointAtTime(
			    *Character, Selection.Montage, LandingWindow->EndTime, Modifier->WarpPointAnimBoneName);
		}
		else if (Modifier->WarpPointAnimProvider == EWarpPointAnimProvider::Static)
		{
			RootOffset = UMotionWarpingUtilities::CalculateRootTransformRelativeToWarpPointAtTime(
			    *Character, Selection.Montage, LandingWindow->EndTime, Modifier->WarpPointAnimTransform);
		}
		static const IConsoleVariable* UseMeshScale =
		    IConsoleManager::Get().FindConsoleVariable(TEXT("a.MotionWarping.UseMeshScale"));
		if (UseMeshScale && UseMeshScale->GetInt() != 0 && Character->GetMesh())
		{
			RootOffset.ScaleTranslation(Character->GetMesh()->GetComponentScale());
		}
		RootTarget = RootOffset * RootTarget;
		if (!RootTarget.IsValid())
		{
			return Reject(TEXT("Landing root transform is invalid"));
		}
		const FVector LandingCenter =
		    RootTarget.GetLocation() + FVector::UpVector * (HalfHeight + Landing.CapsuleClearance);
		const bool bBlocked = Character->GetWorld()->OverlapBlockingTestByChannel(LandingCenter, FQuat::Identity,
		    Capsule->GetCollisionObjectType(), FCollisionShape::MakeCapsule(Radius, HalfHeight), FloorParams,
		    Responses);
#if ENABLE_DRAW_DEBUG
		IMGTraversalDebug::Capsule(
		    Character->GetWorld(), LandingCenter, HalfHeight, Radius, bBlocked ? FColor::Red : FColor::Green, 3);
#endif
		if (bBlocked)
		{
			return Reject(TEXT("Selected landing capsule is blocked"));
		}
		// An animation warp-point offset may place the capsule beyond the floor traced at the contact point.
		FFindFloorResult RootFloor;
		const float FloorDistance = Landing.CapsuleClearance + UCharacterMovementComponent::MAX_FLOOR_DIST;
		Character->GetIMGMovementComponent()->ComputeFloorDist(
		    LandingCenter, FloorDistance, FloorDistance, RootFloor, Radius);
		UPrimitiveComponent* RootSupport = RootFloor.HitResult.GetComponent();
		const float RootDrop = Candidate.FrontLedge.GetLocation().Z - RootFloor.HitResult.ImpactPoint.Z;
#if ENABLE_DRAW_DEBUG
		IMGTraversalDebug::Line(Character->GetWorld(), FloorTarget.GetLocation(), RootTarget.GetLocation(), FColor::Yellow, 3);
		IMGTraversalDebug::String(Character->GetWorld(), LandingCenter + FVector::UpVector * HalfHeight,
		    FString::Printf(TEXT("Landing contact -> binding/warp-point -> capsule | floor reach %.1f"), FloorDistance),
		    FColor::Yellow, 3);
		if (RootFloor.bBlockingHit)
		{
			IMGTraversalDebug::Intersection(Character->GetWorld(), RootFloor.HitResult,
			    RootFloor.IsWalkableFloor() && RootDrop >= 0.f && RootDrop <= Geometry.MaxDrop &&
			        FIMGTraversalDetector::CanUseSurface(RootSupport, Geometry, Candidate.RequestSource),
			    TEXT("Actual capsule support"), IMGTraversalDebug::EStage::Execution);
		}
#endif
		if (!RootFloor.IsWalkableFloor() || RootDrop < 0.f || RootDrop > Geometry.MaxDrop ||
		    !FIMGTraversalDetector::CanUseSurface(RootSupport, Geometry, Candidate.RequestSource))
		{
			return Reject(TEXT("Selected landing capsule has no usable floor support"));
		}
		FloorComponent = RootSupport;
		if (Landing.bCheckStraightPath)
		{
			FCollisionQueryParams PathParams(FloorParams);
			for (const UPrimitiveComponent* Component : Candidate.ObstacleComponents)
			{
				PathParams.AddIgnoredComponent(Component);
			}
			FHitResult PathHit;
			const FVector Start =
			    PathAnchor.GetLocation() + FVector::UpVector * (HalfHeight + Landing.CapsuleClearance);
			const bool bPathBlocked = Character->GetWorld()->SweepSingleByChannel(PathHit, Start, LandingCenter, FQuat::Identity,
			        Capsule->GetCollisionObjectType(), FCollisionShape::MakeCapsule(Radius, HalfHeight), PathParams,
			        Responses);
#if ENABLE_DRAW_DEBUG
			IMGTraversalDebug::Capsule(Character->GetWorld(), Start, HalfHeight, Radius, FColor::Cyan, 3);
			IMGTraversalDebug::Line(Character->GetWorld(), Start, LandingCenter,
			    bPathBlocked ? FColor::Red : FColor::Cyan, 3);
			if (bPathBlocked)
			{
				IMGTraversalDebug::Intersection(Character->GetWorld(), PathHit, false,
				    TEXT("Optional straight capsule path"), IMGTraversalDebug::EStage::Execution);
			}
#endif
			if (bPathBlocked)
			{
				return Reject(TEXT("Configured straight landing path is blocked"));
			}
		}
		Candidate.BackFloor = FloorTarget;
		Candidate.BackFloorComponent = FloorComponent;
		Candidate.LocalBackFloor = FloorTarget.GetRelativeTransform(FloorComponent->GetComponentTransform());
		Candidate.bHasBackFloor = true;
		Candidate.DropHeight = Drop;
		Candidate.BackLedgeHeight = Candidate.bHasBackLedge ? Candidate.BackLedge.GetLocation().Z - LandingPoint.Z : 0.f;
	}
	for (const FIMGTraversalWarpBinding& Binding : Settings->WarpBindings)
	{
		if (!UsedTargets.Contains(Binding.TargetName))
		{
			continue;
		}
		FTransform Anchor;
		UPrimitiveComponent* Component = nullptr;
		if (!GetAnchor(Binding.Anchor, Anchor, Component))
		{
			return Reject(
			    FString::Printf(TEXT("Warp target %s has no valid geometric anchor"), *Binding.TargetName.ToString()));
		}
		FIMGTraversalWarpTarget& Target = OutTargets.AddDefaulted_GetRef();
		Target.Name = Binding.TargetName;
		Target.Component = Component;
		const FTransform Transform = Binding.Offset * Anchor;
		if (!Transform.IsValid())
		{
			return Reject(TEXT("Resolved warp transform is invalid"));
		}
		Target.LocalTransform = Transform.GetRelativeTransform(Component->GetComponentTransform());
		if (!Target.LocalTransform.IsValid())
		{
			return Reject(TEXT("Warp carrier transform is invalid"));
		}
#if ENABLE_DRAW_DEBUG
		IMGTraversalDebug::Line(Character->GetWorld(), Anchor.GetLocation(), Transform.GetLocation(), FColor::Yellow, 3);
		IMGTraversalDebug::Arrow(Character->GetWorld(), Transform.GetLocation(),
		    Transform.GetLocation() + Transform.GetUnitAxis(EAxis::X) * 30.f, FColor::Yellow, 3);
		IMGTraversalDebug::String(Character->GetWorld(), Transform.GetLocation() + FVector::UpVector * 15.f,
		    FString::Printf(TEXT("%s <- %s + offset | %s"), *Binding.TargetName.ToString(),
		        *StaticEnum<EIMGTraversalAnchor>()->GetNameStringByValue(static_cast<int64>(Binding.Anchor)), *GetNameSafe(Component)),
		    FColor::White, 3);
#endif
	}
	return true;
}

void UIMGGameplayAbility_Traversal::CheckActiveTarget()
{
	UIMGTraversalComponent* Component = ActiveComponent.Get();
	const UWorld* World = GetWorld();
	const AIMGCharacter* Character = GetIMGCharacterFromActorInfo();
	const UIMGHealthComponent* Health = UIMGHealthComponent::FindHealthComponent(Character);
	if (Health && Health->IsDeadOrDying())
	{
#if ENABLE_DRAW_DEBUG
		DrawAbilityMessage(Character, TEXT("Traversal Abort: character died"), FColor::Red);
#endif
		K2_EndAbility();
		return;
	}
	if (!IsValid(Component) || !Component->IsRegistered() || !World || !IsValid(Character) ||
	    Component->GetOwner() != Character || !IsValid(Component->GetMotionWarpingComponent()) ||
	    !Component->GetMotionWarpingComponent()->IsRegistered() ||
	    !Character->GetIMGMovementComponent() || !Character->GetIMGMovementComponent()->IsFlying() ||
	    !Component->IsTraversalActive() || ActiveObstacleComponents.IsEmpty() ||
	    ActiveObstacleComponents.Num() != PreviousObstacleTransforms.Num())
	{
#if ENABLE_DRAW_DEBUG
		DrawAbilityMessage(
		    GetAvatarActorFromActorInfo(), TEXT("Traversal Abort: obstacle set or component lost"), FColor::Red);
#endif
		K2_EndAbility();
		return;
	}
	const FIMGTraversalDetectorSettings& Settings = Component->GetDetectorSettings();
	const float DeltaTime = FMath::Max(World->GetTimeSeconds() - PreviousTargetCheckTime, KINDA_SMALL_NUMBER);
	auto IsMovingTooFast = [&Settings, DeltaTime](const UPrimitiveComponent& Component,
	                           const FTransform& CurrentTransform, const FTransform& PreviousTransform)
	{
		const float Speed = FVector::Dist(CurrentTransform.GetLocation(), PreviousTransform.GetLocation()) / DeltaTime;
		const float AngularSpeed =
		    FMath::RadiansToDegrees(CurrentTransform.GetRotation().AngularDistance(PreviousTransform.GetRotation())) /
		    DeltaTime;
		return CurrentTransform.ContainsNaN() || !FMath::IsFinite(Speed) || !FMath::IsFinite(AngularSpeed) ||
		    (Component.IsSimulatingPhysics() && !Settings.bAllowSimulatingPhysics) || Speed > Settings.MaxMovingSpeed ||
		    AngularSpeed > Settings.MaxAngularSpeedDegrees;
	};
	for (int32 Index = 0; Index < ActiveObstacleComponents.Num(); ++Index)
	{
		UPrimitiveComponent* Target = ActiveObstacleComponents[Index].Get();
		if (!FIMGTraversalDetector::CanUseSurface(Target, Settings, LocalCandidate.RequestSource))
		{
#if ENABLE_DRAW_DEBUG
			DrawAbilityMessage(
			    GetAvatarActorFromActorInfo(), TEXT("Traversal Abort: obstacle component lost"), FColor::Red);
#endif
			K2_EndAbility();
			return;
		}
		const FTransform Current = Target->GetComponentTransform();
		if (IsMovingTooFast(*Target, Current, PreviousObstacleTransforms[Index]))
		{
#if ENABLE_DRAW_DEBUG
			DrawAbilityMessage(
			    GetAvatarActorFromActorInfo(), TEXT("Traversal Abort: obstacle moved too fast"), FColor::Red);
			if (IMGTraversalDebug::IsEnabled(3))
			{
				const float Speed =
				    FVector::Dist(Current.GetLocation(), PreviousObstacleTransforms[Index].GetLocation()) / DeltaTime;
				const float AngularSpeed = FMath::RadiansToDegrees(Current.GetRotation().AngularDistance(
				                               PreviousObstacleTransforms[Index].GetRotation())) /
				    DeltaTime;
				IMGTraversalDebug::String(World, Target->GetComponentLocation() + FVector::UpVector * 18.f,
				    FString::Printf(TEXT("Abort %s: V %.0f/%.0f  W %.0f/%.0f"), *Target->GetName(), Speed,
				        Settings.MaxMovingSpeed, AngularSpeed, Settings.MaxAngularSpeedDegrees),
				    FColor::Red, 3);
			}
#endif
			K2_EndAbility();
			return;
		}
		PreviousObstacleTransforms[Index] = Current;
	}
	PreviousTargetCheckTime = World->GetTimeSeconds();
}

void UIMGGameplayAbility_Traversal::OnMontageCompleted()
{
	bMontageCompleted = true;
	K2_EndAbility();
}

void UIMGGameplayAbility_Traversal::OnMontageInterrupted()
{
	if (!bTargetDataSent)
	{
		CancelPendingTargetData();
	}
	else
	{
		K2_EndAbility();
	}
}

void UIMGGameplayAbility_Traversal::EndAbility(const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
    bool bReplicateEndAbility, bool bWasCancelled)
{
	if (!IsEndAbilityValid(Handle, ActorInfo))
	{
		return;
	}
	if (ScopeLockCount > 0)
	{
		WaitingToExecute.Add(FPostLockDelegate::CreateUObject(this, &ThisClass::EndAbility,
		    Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled));
		return;
	}
	// Cleanup and Blueprint notifications can synchronously request another end.
	{
		TGuardValue<bool> EndingGuard(bIsAbilityEnding, true);
		SendPendingTargetDataCancellation();
#if ENABLE_DRAW_DEBUG
		IMGTraversalDebug::DiscardTraversalSnapshot(GetAvatarActorFromActorInfo());
#endif
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(TargetCheckTimer);
		}
		if (ActorInfo && ActorInfo->AbilitySystemComponent.IsValid())
		{
			UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
			if (TargetDataDelegateHandle.IsValid())
			{
				ASC->AbilityTargetDataSetDelegate(Handle, ActivationInfo.GetActivationPredictionKey())
				    .Remove(TargetDataDelegateHandle);
			}
			if (TargetDataCancelledDelegateHandle.IsValid())
			{
				ASC->AbilityTargetDataCancelledDelegate(Handle, ActivationInfo.GetActivationPredictionKey())
				    .Remove(TargetDataCancelledDelegateHandle);
			}
			ASC->ConsumeClientReplicatedTargetData(Handle, ActivationInfo.GetActivationPredictionKey());
		}
		TargetDataDelegateHandle.Reset();
		TargetDataCancelledDelegateHandle.Reset();
		const bool bNotifyTraversalEnded = bTraversalStarted;
		const bool bCompleted = bMontageCompleted && !bWasCancelled;
		bTraversalStarted = false;
		if (bNotifyTraversalEnded)
		{
			AIMGCharacter* Character = ActiveComponent.IsValid()
			    ? Cast<AIMGCharacter>(ActiveComponent->GetOwner()) : GetIMGCharacterFromActorInfo();
			if (UIMGTraversalComponent* Component = ActiveComponent.Get())
			{
				Component->EndTraversal();
			}
			if (IsValid(Character))
			{
				const UIMGHealthComponent* Health = UIMGHealthComponent::FindHealthComponent(Character);
				if ((!Health || !Health->IsDeadOrDying()) && Character->GetIMGMovementComponent())
				{
					UIMGCharacterMovementComponent* Movement = Character->GetIMGMovementComponent();
					if (Movement->MovementMode == MOVE_Flying)
					{
						FFindFloorResult Floor;
						Movement->FindFloor(Character->GetActorLocation(), Floor, false);
						Movement->SetMovementMode(Floor.IsWalkableFloor() ? MOVE_Walking : MOVE_Falling);
					}
				}
			}
		}
		bMontageCompleted = false;
		bLocalRequest = false;
		ActiveComponent.Reset();
		ActiveObstacleComponents.Reset();
		PreviousObstacleTransforms.Reset();
		LocalCandidate = FIMGTraversalCandidate();
		if (bNotifyTraversalEnded)
		{
			OnTraversalEnded(bCompleted);
		}
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
