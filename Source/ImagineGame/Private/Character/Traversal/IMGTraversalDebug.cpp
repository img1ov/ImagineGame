#include "IMGTraversalDebug.h"
DEFINE_LOG_CATEGORY(LogIMGTraversal);
#if IMG_TRAVERSAL_DEBUG_ENABLED
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "AbilitySystemComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraTypes.h"
#include "Abilities/GameplayAbility.h"
#include "Character/IMGCharacter.h"
#include "Character/Traversal/IMGTraversalComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "HAL/PlatformTime.h"
#include "IMGTraversalDebugTrace.h"
#include "MotionWarpingComponent.h"

namespace IMGTraversalDebug
{
namespace
{
constexpr int32 MaxActors = 64;
constexpr int32 MaxEvents = 512;
TMap<TWeakObjectPtr<const AActor>, TSharedPtr<FObservation>> Observations;
FScope* CurrentScope = nullptr;
uint64 NextAttempt = 0;
uint64 NextHeightCapture = 0;
FDelegateHandle TickHandle, CleanupHandle;
void Flush(const AActor* Actor, FObservation& Observation);

bool Matches(const AActor* Actor)
{
	return IsValid(Actor) && Actor->GetWorld() && Actor->GetWorld()->IsGameWorld();
}
bool IsLocal(const AActor* Actor)
{
	const APawn* Pawn = Cast<APawn>(Actor);
	const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	return PC && PC->IsLocalController();
}
TSharedRef<FObservation> Observe(const AActor* Actor)
{
	if (!Observations.Contains(Actor) && Observations.Num() >= MaxActors)
	{
		TWeakObjectPtr<const AActor> Oldest;
		double Time = MAX_dbl;
		for (const auto& Pair : Observations)
		{
			if (!Pair.Key.IsValid())
			{
				Oldest = Pair.Key;
				break;
			}
			if (Pair.Value->UpdatedAt < Time)
			{
				Oldest = Pair.Key;
				Time = Pair.Value->UpdatedAt;
			}
		}
		const auto Evicted = Observations.FindRef(Oldest);
		if (Evicted && Evicted->bDirty && Matches(Oldest.Get()))
		{
			Flush(Oldest.Get(), *Evicted);
		}
		Observations.Remove(Oldest);
	}
	auto& Entry = Observations.FindOrAdd(Actor);
	if (!Entry)
	{
		Entry = MakeShared<FObservation>();
	}
	const uint64 Serial = IMGTraversalDebugTrace::GetRecordingSerial();
	if (Serial != Entry->RecordingSerial)
	{
		Entry->RecordingSerial = Serial;
		Entry->LastSignals.Reset();
		const auto* Traversal = UIMGTraversalComponent::Get(Actor);
		if (!Traversal || !Traversal->IsTraversalActive())
		{
			Entry->PlacementGeometry.Reset();
			Entry->HeightPath.Reset();
			Entry->HeightMontage.Reset();
			Entry->bHeightStarted = false;
		}
		Entry->Data.Events.Reset();
		Entry->Data.Candidates.Reset();
		Entry->LiveCommands.Reset();
		Entry->PendingGeometry.Reset();
		Entry->History.Reset();
	}
	Entry->Actor = Actor;
	Entry->UpdatedAt = FPlatformTime::Seconds();
	return Entry.ToSharedRef();
}
uint64 AttemptFor(const AActor* Actor)
{
	if (CurrentScope && CurrentScope->GetActor() == Actor)
	{
		return CurrentScope->GetAttempt();
	}
	const auto* Entry = Observations.Find(Actor);
	return Entry ? (*Entry)->Attempt : 0;
}
void Snapshot(const AActor* Actor, FObservation& Observation)
{
	auto& Data = Observation.Data;
	Data.Actor = Actor->GetName();
	Data.World = Actor->GetWorld()->GetName();
	Data.Role = Actor->GetLocalRole();
	Data.NetMode = Actor->GetNetMode();
	Data.Location = Actor->GetActorLocation();
	Data.Velocity = Actor->GetVelocity();
	Data.Attempt = Observation.Attempt;
	Data.Dropped = Observation.Dropped;
	Data.Frame = GFrameCounter;
	Data.Time = Actor->GetWorld()->GetTimeSeconds();
	Data.DeltaSeconds = Actor->GetWorld()->GetDeltaSeconds();
	Data.bHasHeightSample = false;
	Data.HeightCapture = 0;
	Data.HeightReferenceZ = Data.MontageRootHeight = Data.WarpedCapsuleHeight = 0;
	if (const auto* Traversal = UIMGTraversalComponent::Get(Actor))
	{
		const auto& State = Traversal->GetActiveTraversalState();
		Data.AbilitySpec = State.AbilitySpec.ToString();
		Data.PredictionKey = State.ActivationPredictionKey;
		Data.Sequence = State.Sequence;
		Data.MontageInstance = State.MontagePlayInstanceId;
		Data.bActive = State.bActive;
		Data.Action = UEnum::GetValueAsString(State.Action);
		Data.Montage = GetNameSafe(State.Montage.Get());
		Data.MontageAssetPath = GetPathNameSafe(State.Montage.Get());
		Data.AbilityClassPath.Reset();
		const AIMGCharacter* Character = Cast<AIMGCharacter>(Actor);
		if (const auto* ASC = Character ? Character->GetAbilitySystemComponent() : nullptr)
		{
			if (const auto* Spec = ASC->FindAbilitySpecFromHandle(State.AbilitySpec))
			{
				Data.AbilityClassPath = Spec->Ability ? Spec->Ability->GetClass()->GetPathName() : FString();
			}
		}
		if (Character && Character->GetIMGMovementComponent())
		{
			Data.MovementMode =
			    UEnum::GetValueAsString(Character->GetIMGMovementComponent()->MovementMode.GetValue());
		}
		const UAnimInstance* Anim =
		    Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
		const FAnimMontageInstance* MontageInstance =
		    Anim && State.Montage ? Anim->GetActiveInstanceForMontage(State.Montage) : nullptr;
		if (!MontageInstance && Data.bActive && Anim)
		{
			// Montage_GetPosition returns zero after Stop; the blending root-motion instance may still exist.
			const FAnimMontageInstance* RootInstance = Anim->GetRootMotionMontageInstance();
			if (RootInstance && RootInstance->Montage == State.Montage)
			{
				MontageInstance = RootInstance;
			}
		}
		Data.MontagePosition = MontageInstance ? MontageInstance->GetPosition() : 0.f;
		Data.MontageLength = State.Montage ? State.Montage->GetPlayLength() : 0.f;
		Data.MontagePhase =
		    Data.bActive && Data.MontageLength > 0 ? Data.MontagePosition / Data.MontageLength : 0.f;
		if ((IMGTraversalDebugTrace::IsRecording() || IMGTraversalDebugPresentation::IsGraphShown()) &&
		    Data.bActive && MontageInstance && Observation.bHeightStarted &&
		    Observation.HeightMontage.IsValid() && State.Montage.Get() == Observation.HeightMontage.Get())
		{
			FTransform Pose = Observation.HeightInitialPose;
			bool bReachedPosition = false;
			for (const FVector2f& Range : Observation.HeightPath)
			{
				const bool bCurrentRange = Data.MontagePosition >= Range.X && Data.MontagePosition <= Range.Y;
				const float End = bCurrentRange ? Data.MontagePosition : Range.Y;
				const FTransform MeshWorld = Observation.HeightMeshRelative * Pose;
				const FTransform Motion =
				    UMotionWarpingUtilities::ExtractRootMotionFromAnimation(State.Montage, Range.X, End);
				Pose = Pose.GetRelativeTransform(MeshWorld) * (Motion * MeshWorld);
				if (bCurrentRange)
				{
					bReachedPosition = true;
					break;
				}
			}
			if (bReachedPosition && Pose.IsValid() && !Data.Location.ContainsNaN())
			{
				Data.bHasHeightSample = true;
				Data.HeightCapture = Observation.HeightCapture;
				Data.HeightReferenceZ = Observation.HeightInitialPose.GetLocation().Z;
				Data.MontageRootHeight = Pose.GetLocation().Z - Data.HeightReferenceZ;
				Data.WarpedCapsuleHeight = Data.Location.Z - Data.HeightReferenceZ;
			}
		}
	}
	Data.Geometry = Observation.PendingGeometry;
	if (Data.bActive && Observation.PlacementAttempt == Observation.Attempt)
	{
		for (const auto& Capsule : Observation.PlacementGeometry)
		{
			if (!Data.Geometry.ContainsByPredicate([&Capsule](const auto& Shape) {
				    return Shape.Purpose == Capsule.Purpose && Shape.Start == Capsule.Start &&
				        Shape.Size == Capsule.Size && Shape.Radius == Capsule.Radius;
			    }))
			{
				Data.Geometry.Add(Capsule);
			}
		}
	}
}
void Flush(const AActor* Actor, FObservation& Observation)
{
	Snapshot(Actor, Observation);
	IMGTraversalDebugTrace::Output(Actor, Observation.Data);
	if (IsLocal(Actor))
	{
		if (!Observation.History.IsEmpty() && Observation.History.Last().Time > Observation.Data.Time)
		{
			Observation.History.Reset();
		}
		const bool bReplace =
		    !Observation.History.IsEmpty() && Observation.History.Last().Frame == Observation.Data.Frame;
		if (!bReplace && Observation.History.Num() >= 240)
		{
			Observation.History.RemoveAt(0, 1, EAllowShrinking::No);
		}
		auto& Sample = bReplace ? Observation.History.Last() : Observation.History.AddDefaulted_GetRef();
		Sample.Frame = Observation.Data.Frame;
		Sample.Time = Observation.Data.Time;
		Sample.Velocity = Observation.Data.Velocity;
		Sample.MontagePhase = Observation.Data.MontagePhase;
		Sample.Queries = Observation.Data.Queries;
		Sample.DetectionMilliseconds = Observation.Data.DetectionMilliseconds;
		Sample.DeltaSeconds = Observation.Data.DeltaSeconds;
		Sample.bHasHeightSample = Observation.Data.bHasHeightSample;
		Sample.HeightCapture = Observation.Data.HeightCapture;
		Sample.HeightReferenceZ = Observation.Data.HeightReferenceZ;
		Sample.MontageRootHeight = Observation.Data.MontageRootHeight;
		Sample.WarpedCapsuleHeight = Observation.Data.WarpedCapsuleHeight;
	}
	Observation.Data.Events.Reset();
	Observation.Data.Candidates.Reset();
	Observation.PendingGeometry.Reset();
	Observation.bDirty = false;
}
void Submit(const UWorld* World, FCommand Command, ELifetime Lifetime)
{
	const AActor* Actor = CurrentScope ? CurrentScope->GetActor() : nullptr;
	if (!Matches(Actor) || World != Actor->GetWorld() || !IsEnabled())
	{
		return;
	}
	if (Command.Start.ContainsNaN() || Command.End.ContainsNaN() || !FMath::IsFinite(Command.Size) ||
	    !FMath::IsFinite(Command.Radius))
	{
		return;
	}
	const auto Handle = Observe(Actor);
	auto& Observation = Handle.Get();
	Command.Attempt = AttemptFor(Actor);
	if (!Command.Purpose.IsEmpty())
	{
		Observation.PlacementAttempt = Command.Attempt;
		if (auto* Existing = Observation.PlacementGeometry.FindByPredicate(
		        [&Command](const auto& Shape) { return Shape.Purpose == Command.Purpose; }))
		{
			*Existing = Command;
		}
		else if (Observation.PlacementGeometry.Num() < 8)
		{
			Observation.PlacementGeometry.Add(Command);
		}
	}
	if (Lifetime == ELifetime::Live && Observation.LiveFrame != GFrameCounter)
	{
		Observation.LiveFrame = GFrameCounter;
		Observation.LiveCommands.Reset();
	}
	if (Lifetime == ELifetime::Live)
	{
		if (Observation.LiveCommands.ContainsByPredicate([&Command](const auto& Shape) {
			    return Shape.Primitive == Command.Primitive && Shape.Start == Command.Start &&
			        Shape.End == Command.End && Shape.Size == Command.Size && Shape.Radius == Command.Radius;
		    }))
		{
			return;
		}
		if (Observation.LiveCommands.Num() < 256)
		{
			Observation.LiveCommands.Add(Command);
		}
	}
	if (Observation.PendingGeometry.Num() < 4096)
	{
		Observation.PendingGeometry.Add(Command);
	}
	else
	{
		++Observation.Dropped;
	}
	Observation.bDirty = true;
}
void Tick(UWorld* World, ELevelTick TickType, float)
{
	if (!World || !World->IsGameWorld() || TickType == LEVELTICK_ViewportsOnly)
	{
		return;
	}
#if WITH_EDITOR
	if (World->bDebugPauseExecution)
	{
		return;
	}
#endif
	IMGTraversalDebugPresentation::Clear(World);
	if (IMGTraversalDebugPresentation::IsGraphShown())
	{
		for (auto It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const auto* PC = It->Get();
			if (PC && PC->IsLocalController() && UIMGTraversalComponent::Get(PC->GetPawn()))
			{
				Observe(PC->GetPawn());
			}
		}
	}
	TArray<TSharedPtr<FObservation>> Entries;
	Observations.GenerateValueArray(Entries);
	for (const auto& Entry : Entries)
	{
		const AActor* Actor = Entry->Actor.Get();
		if (!Matches(Actor))
		{
			Observations.Remove(Entry->Actor);
			continue;
		}
		if (Actor->GetWorld() != World)
		{
			continue;
		}
		if (!Entry->bDirty && FPlatformTime::Seconds() - Entry->UpdatedAt > .25)
		{
			Observations.Remove(Entry->Actor);
			continue;
		}
		Flush(Actor, *Entry);
		if (IsLocal(Actor))
		{
			IMGTraversalDebugPresentation::Draw(World, Entry->Data);
			if (IMGTraversalDebugPresentation::IsGraphShown())
			{
				const auto* Pawn = Cast<APawn>(Actor);
				const auto* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
				if (PC && PC->PlayerCameraManager)
				{
					FMinimalViewInfo View = PC->PlayerCameraManager->GetCameraCacheView();
					int32 SizeX = 0, SizeY = 0;
					PC->GetViewportSize(SizeX, SizeY);
					if (!View.bConstrainAspectRatio && SizeX > 0 && SizeY > 0)
					{
						View.AspectRatio = static_cast<float>(SizeX) / SizeY;
					}
					IMGTraversalDebugPresentation::DrawGraphs(World, View, Entry->History);
				}
			}
		}
	}
}
void WorldCleanup(UWorld* World, bool, bool)
{
	IMGTraversalDebugPresentation::Clear(World);
	for (auto It = Observations.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || It.Key()->GetWorld() == World)
		{
			It.RemoveCurrent();
		}
	}
}
} // namespace

bool IsObserving(const AActor* Actor)
{
	return Matches(Actor) &&
	    (IMGTraversalDebugTrace::IsRecording() ||
	        ((IMGTraversalDebugPresentation::IsShown() || IMGTraversalDebugPresentation::IsGraphShown()) &&
	            IsLocal(Actor)));
}
bool IsEnabled()
{
	return CurrentScope && IsObserving(CurrentScope->GetActor());
}
FScope::FScope(const AActor* InActor, EPhase InPhase) : Actor(InActor), Previous(CurrentScope), Phase(InPhase)
{
	Attempt = Previous && Previous->GetActor() == InActor ? Previous->GetAttempt() : AttemptFor(InActor);
	CurrentScope = this;
	if (IsEnabled())
	{
		StartTime = FPlatformTime::Seconds();
		if (Phase == EPhase::Placement)
		{
			Observe(InActor)->PlacementGeometry.Reset();
		}
	}
}
FScope::FScope(const FIMGTraversalQuery& Query, const FIMGTraversalCandidate& InCandidate,
    const EIMGTraversalFailure& InFailure)
    : FScope(Query.Character.Get(),
          CurrentScope && CurrentScope->GetActor() == Query.Character.Get() ? CurrentScope->GetPhase()
                                                                            : EPhase::External)
{
	Candidate = &InCandidate;
	DetectionQuery = &Query;
	Failure = &InFailure;
	if (StartTime > 0)
	{
		Attempt = ++NextAttempt;
		for (FScope* Scope = Previous; Scope && Scope->GetActor() == GetActor() && !Scope->Candidate;
		    Scope = Scope->Previous)
		{
			Scope->Attempt = Attempt;
		}
		auto Handle = Observe(GetActor());
		auto& Observation = Handle.Get();
		Observation.Attempt = Attempt;
		Observation.Data.Source = UEnum::GetValueAsString(Query.Source);
		Observation.Data.Direction = Query.Direction;
	}
}
FScope::~FScope()
{
	if (StartTime > 0 && Matches(GetActor()))
	{
		const double Ms = (FPlatformTime::Seconds() - StartTime) * 1000;
		if (Candidate && Failure)
		{
			auto Handle = Observe(GetActor());
			auto& Data = Handle->Data;
			Data.Failure = UEnum::GetValueAsString(*Failure);
			Data.DetectionMilliseconds = Ms;
			Data.Queries = Queries;
			Data.ObstacleHeight = Candidate->ObstacleHeight;
			Data.ObstacleDepth = Candidate->ObstacleDepth;
			Data.DropHeight = Candidate->DropHeight;
			Data.HorizontalClearance = Candidate->HorizontalClearance;
			Data.VerticalClearance = Candidate->VerticalClearance;
			Event(GetActor(), TEXT("DetectionResult"), Data.Failure, false, EStage::Output);
		}
		else if (Phase == EPhase::Placement)
		{
			Event(GetActor(), TEXT("PlacementProfile"),
			    FString::Printf(TEXT("ExplicitQueries=%d ElapsedMs=%.3f"), Queries, Ms), false,
			    EStage::Execution);
		}
	}
	check(CurrentScope == this);
	CurrentScope = Previous;
}
void CountQuery()
{
	if (CurrentScope)
	{
		CurrentScope->CountQuery();
	}
}
void Event(const AActor* Actor, const TCHAR* Name, const FString& Detail, bool bWarning, EStage Stage,
    const FString& AbilitySpec, int32 PredictionKey)
{
	if (!Actor && CurrentScope)
	{
		Actor = CurrentScope->GetActor();
	}
	if (!IsObserving(Actor))
	{
		return;
	}
	auto Handle = Observe(Actor);
	auto& Observation = Handle.Get();
	if (Stage == EStage::Network || FCString::Strcmp(Name, TEXT("PreflightGate")) == 0)
	{
		const FName Key(Name);
		const auto* Last = Observation.LastSignals.Find(Key);
		if (Last && *Last == Detail)
		{
			return;
		}
		if (Observation.LastSignals.Num() >= 32 && !Last)
		{
			Observation.LastSignals.Reset();
		}
		Observation.LastSignals.Add(Key, Detail);
	}
	if (Observation.Data.Events.Num() >= MaxEvents)
	{
		++Observation.Dropped;
		return;
	}
	auto& Value = Observation.Data.Events.AddDefaulted_GetRef();
	Value.Name = Name;
	Value.Detail = Detail;
	Value.Stage = Stage;
	Value.Attempt = AttemptFor(Actor);
	Value.bWarning = bWarning;
	Value.Phase = CurrentScope ? CurrentScope->GetPhase() : EPhase::External;
	Value.Time = Actor->GetWorld()->GetTimeSeconds();
	Value.AbilitySpec = AbilitySpec;
	Value.PredictionKey = PredictionKey;
	if (const auto* Component = UIMGTraversalComponent::Get(Actor))
	{
		const auto& State = Component->GetActiveTraversalState();
		Value.Sequence = State.Sequence;
		Value.MontageInstance = State.MontagePlayInstanceId;
		Value.bActive = State.bActive;
		if (Value.AbilitySpec.IsEmpty() && Value.Phase != EPhase::Preflight && State.bActive)
		{
			Value.AbilitySpec = State.AbilitySpec.ToString();
			Value.PredictionKey = State.ActivationPredictionKey;
		}
	}
	Observation.bDirty = true;
}
void State(const AActor* Actor, const TCHAR* Name, const FIMGTraversalRepState& State, const TCHAR* Reason)
{
	Event(Actor, Name,
	    FString::Printf(TEXT("Sequence=%u Active=%d Instance=%u | %s"), State.Sequence, State.bActive,
	        State.MontagePlayInstanceId, Reason),
	    false, EStage::Network, State.AbilitySpec.ToString(), State.ActivationPredictionKey);
	const auto* Entry = Observations.Find(Actor);
	if (Entry && !(*Entry)->Data.Events.IsEmpty() && (*Entry)->Data.Events.Last().Name == Name)
	{
		auto& Event = (*Entry)->Data.Events.Last();
		Event.Sequence = State.Sequence;
		Event.MontageInstance = State.MontagePlayInstanceId;
		Event.bActive = State.bActive;
	}
}
void Startup()
{
	IMGTraversalDebugTrace::Startup();
	TickHandle = FWorldDelegates::OnWorldPostActorTick.AddStatic(&Tick);
	CleanupHandle = FWorldDelegates::OnWorldCleanup.AddStatic(&WorldCleanup);
}
void Shutdown()
{
	for (const auto& Pair : Observations)
	{
		if (const auto* Actor = Pair.Key.Get())
		{
			IMGTraversalDebugPresentation::Clear(Actor->GetWorld());
		}
	}
	IMGTraversalDebugTrace::Shutdown();
	FWorldDelegates::OnWorldPostActorTick.Remove(TickHandle);
	FWorldDelegates::OnWorldCleanup.Remove(CleanupHandle);
	TickHandle.Reset();
	CleanupHandle.Reset();
	Observations.Reset();
}
void ReleaseActor(const AActor* Actor)
{
	const auto* Entry = Observations.Find(Actor);
	if (Entry && Matches(Actor))
	{
		auto Keep = *Entry;
		Flush(Actor, *Keep);
	}
	Observations.Remove(Actor);
}
void FlushRecording()
{
	TArray<TSharedPtr<FObservation>> Entries;
	Observations.GenerateValueArray(Entries);
	for (const auto& Entry : Entries)
	{
		if (Entry->bDirty && Matches(Entry->Actor.Get()))
		{
			Flush(Entry->Actor.Get(), *Entry);
		}
	}
}
void CandidateOutput(const UWorld* World, const FIMGTraversalCandidate& Candidate, const FString& Label)
{
	if (!World || !IsEnabled())
	{
		return;
	}
	const FVector Front = Candidate.FrontLedge.GetLocation();
	Point(World, Front, FColor::Yellow, 3.f, ELifetime::Snapshot, EStage::Output);
	Arrow(World, Front, Front + Candidate.FrontLedge.GetUnitAxis(EAxis::X) * 30.f, FColor::Yellow,
	    ELifetime::Snapshot, EStage::Output);
	if (Candidate.bHasBackLedge)
	{
		Point(World, Candidate.BackLedge.GetLocation(), FColor::Orange, 3.f, ELifetime::Snapshot,
		    EStage::Output);
	}
	if (Candidate.bHasBackFloor)
	{
		Point(World, Candidate.BackFloor.GetLocation(), FColor::Green, 3.f, ELifetime::Snapshot,
		    EStage::Output);
	}
	auto Handle = Observe(CurrentScope->GetActor());
	if (Handle->Data.Candidates.Num() < 32)
	{
		auto& Value = Handle->Data.Candidates.AddDefaulted_GetRef();
		Value.Attempt = AttemptFor(CurrentScope->GetActor());
		Value.Phase = CurrentScope->GetPhase();
		Value.Label = Label;
		Value.Source = UEnum::GetValueAsString(Candidate.RequestSource);
		Value.RecommendedAction = UEnum::GetValueAsString(Candidate.RecommendedAction);
		Value.bCanMantle = Candidate.bCanMantle;
		Value.bCanVault = Candidate.bCanVault;
		Value.bCanHurdle = Candidate.bCanHurdle;
		Value.ObstacleHeight = Candidate.ObstacleHeight;
		Value.ObstacleDepth = Candidate.ObstacleDepth;
		Value.DropHeight = Candidate.DropHeight;
		Value.BackLedgeHeight = Candidate.BackLedgeHeight;
		Value.HorizontalClearance = Candidate.HorizontalClearance;
		Value.VerticalClearance = Candidate.VerticalClearance;
		Value.Distance3D = Candidate.DistanceToLedge;
		Value.DistanceXY = Candidate.PlanarDistanceToLedge;
		Value.EdgeSpan = Candidate.FrontEdgeVerticalSpan;
		Value.MidHeightClearance = Candidate.MidHeightApproachClearance;
		Value.bHighOpenApproach = Candidate.bHasHighOpenApproach;
		Value.ClimbAngle = Candidate.ClimbAngle;
		Value.GroundSlopeAngle = Candidate.GroundSlopeAngle;
		Value.bHasBackLedge = Candidate.bHasBackLedge;
		Value.bHasBackFloor = Candidate.bHasBackFloor;
		Value.FrontLedge = Candidate.FrontLedge;
		Value.BackLedge = Candidate.BackLedge;
		Value.BackFloor = Candidate.BackFloor;
		Value.FrontNormal = Candidate.FrontLedgeNormal;
		Value.BackNormal = Candidate.BackLedgeNormal;
		Value.GroundNormal = Candidate.GroundNormal;
		Value.GroundLocation = Candidate.GroundLocation;
		for (const auto& Component : Candidate.ObstacleComponents)
		{
			Value.Components.Add(GetPathNameSafe(Component.Get()));
		}
	}
	else
	{
		++Handle->Dropped;
	}
	Event(CurrentScope->GetActor(), TEXT("Candidate"), Label, false, EStage::Output);
	Measure(World, Front, Front - FVector::UpVector * Candidate.ObstacleHeight, TEXT("Height"),
	    Candidate.ObstacleHeight, FColor::Yellow);
	if (Candidate.bHasBackLedge)
	{
		Measure(World, Front, Candidate.BackLedge.GetLocation(), TEXT("DepthXY"), Candidate.ObstacleDepth,
		    FColor::Orange);
	}
}

void PrepareHeightCapture(const AActor* Actor, const FIMGTraversalAnimationSelection& Selection,
    const FTransform& InitialPose, TConstArrayView<FVector2f> PlaybackRanges)
{
	// A later unobserved execution must not inherit the previous execution's origin.
	if (const auto* Existing = Observations.Find(Actor))
	{
		(*Existing)->bHeightStarted = false;
		(*Existing)->HeightPreparedFrame = MAX_uint64;
	}
	const AIMGCharacter* Character = Cast<AIMGCharacter>(Actor);
	if (!IsObserving(Actor) || !Character || !Character->GetMesh() || PlaybackRanges.IsEmpty())
	{
		return;
	}
	const auto Entry = Observe(Actor);
	Entry->HeightMontage = Selection.Montage.Get();
	Entry->HeightPath.Reset();
	Entry->HeightPath.Append(PlaybackRanges.GetData(), PlaybackRanges.Num());
	Entry->HeightPath[0].X = Selection.StartTime;
	Entry->HeightInitialPose = InitialPose;
	Entry->HeightMeshRelative = FTransform(Character->GetBaseRotationOffset(),
	    Character->GetBaseTranslationOffset(), Character->GetMesh()->GetRelativeScale3D());
	Entry->HeightCapture = ++NextHeightCapture;
	Entry->HeightPreparedFrame = GFrameCounter;
	Entry->bHeightStarted = false;
}
void ExecutionStarted(const AActor* Actor, const FIMGTraversalCandidate& Candidate,
    const FIMGTraversalAnimationSelection& Selection)
{
	FScope Scope(Actor, EPhase::Execution);
	if (IsObserving(Actor))
	{
		const auto Entry = Observe(Actor);
		Entry->bHeightStarted = Entry->HeightPreparedFrame == GFrameCounter &&
		    Entry->HeightMontage.Get() == Selection.Montage.Get() && !Entry->HeightPath.IsEmpty();
	}
	CandidateOutput(Actor ? Actor->GetWorld() : nullptr, Candidate, TEXT("Execution geometry"));
	Event(Actor, TEXT("Selection"),
	    FString::Printf(TEXT("Action=%s Montage=%s Start=%.3f Rate=%.3f"),
	        *UEnum::GetValueAsString(Selection.Action), *GetNameSafe(Selection.Montage.Get()),
	        Selection.StartTime, Selection.PlayRate),
	    false, EStage::Execution);
}
void Message(const FString& Text, EStage Stage, FName Key, const AActor* Owner)
{
	const AActor* Actor = Owner ? Owner : CurrentScope ? CurrentScope->GetActor() : nullptr;
	Event(Actor, Key.IsNone() ? TEXT("Detail") : *Key.ToString(), Text, false, Stage);
}
void MessageAt(const FVector& Location, const FString& Text, EStage Stage, FName Key, const AActor* Owner)
{
	const AActor* Actor = Owner ? Owner : CurrentScope ? CurrentScope->GetActor() : nullptr;
	if (IsObserving(Actor))
	{
		Event(Actor, Key.IsNone() ? TEXT("Detail") : *Key.ToString(),
		    FString::Printf(TEXT("%s At=%s"), *Text, *Location.ToCompactString()), false, Stage);
	}
}

void Line(const UWorld* World, const FVector& Start, const FVector& End, const FColor& Color, float Thickness,
    ELifetime Lifetime, EStage Stage)
{
	FCommand Command;
	Command.Primitive = EPrimitive::Line;
	Command.Start = Start;
	Command.End = End;
	Command.Color = Color;

	Command.Stage = Stage;
	Command.Thickness = Thickness;
	Submit(World, Command, Lifetime);
}
void Arrow(const UWorld* World, const FVector& Start, const FVector& End, const FColor& Color,
    ELifetime Lifetime, EStage Stage)
{
	FCommand Command;
	Command.Primitive = EPrimitive::Arrow;
	Command.Start = Start;
	Command.End = End;
	Command.Color = Color;

	Command.Stage = Stage;
	Submit(World, Command, Lifetime);
}
void Point(const UWorld* World, const FVector& Location, const FColor& Color, float Size, ELifetime Lifetime,
    EStage Stage)
{
	FCommand Command;
	Command.Primitive = EPrimitive::Point;
	Command.Start = Location;
	Command.Size = Size;
	Command.Color = Color;

	Command.Stage = Stage;
	Submit(World, Command, Lifetime);
}
void Sphere(const UWorld* World, const FVector& Location, float Radius, const FColor& Color,
    ELifetime Lifetime, EStage Stage)
{
	FCommand Command;
	Command.Primitive = EPrimitive::Sphere;
	Command.Start = Location;
	Command.Size = Radius;
	Command.Color = Color;

	Command.Stage = Stage;
	Submit(World, Command, Lifetime);
}
void Capsule(const UWorld* World, const FVector& Location, float HalfHeight, float Radius,
    const FColor& Color, ELifetime Lifetime, EStage Stage)
{
	FCommand Command;
	Command.Primitive = EPrimitive::Capsule;
	Command.Start = Location;
	Command.Size = HalfHeight;
	Command.Radius = Radius;
	Command.Color = Color;

	Command.Stage = Stage;
	Submit(World, Command, Lifetime);
}
void PlacementCapsule(const UWorld* World, const FVector& Center, float HalfHeight, float Radius,
    const FColor& Color, const TCHAR* Purpose)
{
	FCommand Command;
	Command.Primitive = EPrimitive::Capsule;
	Command.Stage = EStage::Execution;
	Command.Start = Center;
	Command.Size = HalfHeight;
	Command.Radius = Radius;
	Command.Color = Color;
	Command.Purpose = Purpose;
	Submit(World, Command, ELifetime::Snapshot);
}

void Intersection(
    const UWorld* World, const FHitResult& Hit, bool bAccepted, const FString& Label, EStage Stage)
{
	if (!IsEnabled())
	{
		return;
	}
	const FColor Color = bAccepted ? FColor::Cyan : FColor::Red;
	Point(World, Hit.ImpactPoint, Color, 2.f, ELifetime::Snapshot, Stage);
	Arrow(
	    World, Hit.ImpactPoint, Hit.ImpactPoint + Hit.ImpactNormal * 10.f, Color, ELifetime::Snapshot, Stage);
	Message(FString::Printf(TEXT("%s Component=%s Accepted=%d Penetrating=%d Impact=%s Normal=%s"), *Label,
	            *GetNameSafe(Hit.GetComponent()), bAccepted, Hit.bStartPenetrating,
	            *Hit.ImpactPoint.ToCompactString(), *Hit.ImpactNormal.ToCompactString()),
	    Stage);
}
void SweepSphere(const UWorld* World, const FVector& Start, const FVector& End, float Radius,
    const FHitResult* Hit, const FString& Label, const FColor& Color, EStage Stage)
{
	if (!IsEnabled())
	{
		return;
	}
	Line(World, Start, End, Color, 1.f, ELifetime::Snapshot, Stage);
	Sphere(World, Start, Radius, Color, ELifetime::Snapshot, Stage);
	Sphere(World, End, Radius, Color, ELifetime::Snapshot, Stage);
	Message(FString::Printf(TEXT("%s R=%.2f Start=%s End=%s Hit=%d"), *Label, Radius,
	            *Start.ToCompactString(), *End.ToCompactString(), Hit != nullptr),
	    Stage);
	if (Hit)
	{
		Sphere(World, Hit->bStartPenetrating ? Start : Hit->Location, Radius, FColor::Red,
		    ELifetime::Snapshot, Stage);
		Intersection(World, *Hit, true, Label, Stage);
	}
}
void Measure(const UWorld* World, const FVector& Start, const FVector& End, const FString& Label, float Value,
    const FColor& Color)
{
	if (!IsEnabled())
	{
		return;
	}
	Line(World, Start, End, Color, 1.f, ELifetime::Snapshot, EStage::Output);
	Message(FString::Printf(TEXT("%s %.2f cm"), *Label, Value), EStage::Output);
}
} // namespace IMGTraversalDebug
#endif
