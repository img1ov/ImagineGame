#include "Character/IMGCharacterMovementComponent.h"

#include "GameFramework/Character.h"
#include "Templates/UnrealTemplate.h"

namespace IMGMovementNet
{
	static uint8 QuantizeInputMagnitude(const FVector& InputAcceleration, float MaxAcceleration)
	{
		if (!FMath::IsFinite(MaxAcceleration) || MaxAcceleration <= UE_SMALL_NUMBER || InputAcceleration.ContainsNaN())
		{
			return 0;
		}
		return static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(InputAcceleration.Size() / MaxAcceleration, 0.0, 1.0) * 255.0));
	}
}

void FIMGMovementRequestRuntime::ResetHistory()
{
	if (++Generation == 0)
	{
		++Generation;
	}
	bHasRequest = false;
	Request = 0;
	RequestInput = FVector::ZeroVector;
	InputMagnitude = 0;
	bHasRequestInput = false;
	Context = FMovementSimulationContext();
	AuthorityContext = FMovementSimulationContext();
	AuthorityParameterRevisions.Reset();
	AuthorityContextGeneration = 0;
	bContextApplied = false;
	ParameterHash = 0;
	ContextTimeStamp = 0.f;
	AdjustmentContext = FMovementSimulationContext();
	AdjustmentGeneration = 0;
	AdjustmentTimeStamp = 0.f;
}

class FSavedMove_IMG final : public FSavedMove_Character
{
public:
	EIMGStance SavedStance = EIMGStance::Stand;
	uint8 SavedInputMagnitude = 0;
	bool bSavedHasMovementRequest = false;
	uint8 SavedMovementRequest = 0;
	uint32 SavedMovementRequestGeneration = 0;
	FMovementSimulationContext SavedMovementContext;
	FCharacterMovementParameters SavedMovementParameters;
	uint32 SavedMovementParameterHash = 0;
	float SavedBrakingFriction = 0.f;
	float SavedBrakingFrictionFactor = 0.f;
	bool bSavedUseSeparateBrakingFriction = false;
	FIMGRagdollMovementInput SavedRagdollInput;
	TArray<FMovementReplaySetup> SavedReplaySetups;
	TArray<FMovementReplayRestore> ReplayRestores;

	virtual void Clear() override
	{
		FSavedMove_Character::Clear();
		SavedStance = EIMGStance::Stand;
		SavedInputMagnitude = 0;
		bSavedHasMovementRequest = false;
		SavedMovementRequest = 0;
		SavedMovementRequestGeneration = 0;
		SavedMovementContext = FMovementSimulationContext();
		SavedMovementParameters = FCharacterMovementParameters();
		SavedMovementParameterHash = 0;
		SavedBrakingFriction = 0.f;
		SavedBrakingFrictionFactor = 0.f;
		bSavedUseSeparateBrakingFriction = false;
		SavedRagdollInput = FIMGRagdollMovementInput();
		SavedReplaySetups.Reset();
		ReplayRestores.Reset();
	}

	virtual uint8 GetCompressedFlags() const override
	{
		uint8 Flags = FSavedMove_Character::GetCompressedFlags();
		Flags |= (SavedStance == EIMGStance::Crawl) ? FLAG_Custom_1 : 0;
		return Flags;
	}

	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override
	{
		const FSavedMove_IMG* OtherMove = static_cast<const FSavedMove_IMG*>(NewMove.Get());
		return bSavedHasMovementRequest == OtherMove->bSavedHasMovementRequest
			&& SavedMovementRequest == OtherMove->SavedMovementRequest
			&& SavedMovementRequestGeneration == OtherMove->SavedMovementRequestGeneration
			&& SavedMovementContext.Revision == OtherMove->SavedMovementContext.Revision
			&& SavedMovementContext.HasSameParameters(OtherMove->SavedMovementContext)
			&& SavedRagdollInput.Session == 0 && OtherMove->SavedRagdollInput.Session == 0
			&& SavedStance == OtherMove->SavedStance && SavedInputMagnitude == OtherMove->SavedInputMagnitude
			&& SavedMovementParameters.Equals(OtherMove->SavedMovementParameters, bSavedHasMovementRequest)
			&& SavedBrakingFriction == OtherMove->SavedBrakingFriction
			&& SavedBrakingFrictionFactor == OtherMove->SavedBrakingFrictionFactor
			&& bSavedUseSeparateBrakingFriction == OtherMove->bSavedUseSeparateBrakingFriction
			&& FSavedMove_Character::CanCombineWith(NewMove, Character, MaxDelta);
	}

	virtual bool IsImportantMove(const FSavedMovePtr& LastAckedMove) const override
	{
		if (!LastAckedMove.IsValid())
		{
			return true;
		}
		const FSavedMove_IMG* LastMove = static_cast<const FSavedMove_IMG*>(LastAckedMove.Get());
		return bSavedHasMovementRequest != LastMove->bSavedHasMovementRequest
			|| SavedMovementRequest != LastMove->SavedMovementRequest
			|| SavedMovementRequestGeneration != LastMove->SavedMovementRequestGeneration
			|| SavedMovementContext.Revision != LastMove->SavedMovementContext.Revision
			|| !SavedMovementContext.HasSameParameters(LastMove->SavedMovementContext)
			|| SavedInputMagnitude != LastMove->SavedInputMagnitude
			|| SavedRagdollInput.Session != LastMove->SavedRagdollInput.Session
			|| SavedRagdollInput.Status != LastMove->SavedRagdollInput.Status
			|| (SavedRagdollInput.IsValid()
				&& (SavedRagdollInput.PelvisLocation != LastMove->SavedRagdollInput.PelvisLocation
					|| SavedRagdollInput.FacingYaw != LastMove->SavedRagdollInput.FacingYaw
					|| SavedRagdollInput.bFaceUp != LastMove->SavedRagdollInput.bFaceUp))
			|| FSavedMove_Character::IsImportantMove(LastAckedMove);
	}

	virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override
	{
		FSavedMove_Character::SetMoveFor(Character, InDeltaTime, NewAccel, ClientData);
		UIMGCharacterMovementComponent* Movement = CastChecked<UIMGCharacterMovementComponent>(Character->GetCharacterMovement());
		FIMGMovementRequestRuntime& Runtime = Movement->MovementRequestRuntime;

		SavedReplaySetups.Reset();
		Movement->MovementReplayCapture.Broadcast(*this, SavedReplaySetups);
		bForceNoCombine |= !SavedReplaySetups.IsEmpty();

		// Match the rounded acceleration that ReplicateMoveToServer will use for the initial prediction.
		SavedInputMagnitude = IMGMovementNet::QuantizeInputMagnitude(Acceleration, Movement->GetMaxAcceleration());
		bSavedHasMovementRequest = Runtime.bHasRequest && Movement->HasMovementRequestProvider();
		SavedMovementRequest = bSavedHasMovementRequest ? Runtime.Request : 0;
		SavedMovementRequestGeneration = Runtime.Generation;
		SavedMovementContext = bSavedHasMovementRequest ? Runtime.Context : FMovementSimulationContext();
		if (bSavedHasMovementRequest)
		{
			// Use the same rounded direction and input strength as the packed move, before any rewind.
			Runtime.RequestInput = Acceleration;
			Runtime.InputMagnitude = SavedInputMagnitude;
			Runtime.bHasRequestInput = true;
		}

		SavedMovementParameters = FCharacterMovementParameters::Capture(*Movement);
		SavedBrakingFriction = Movement->BrakingFriction;
		SavedBrakingFrictionFactor = Movement->BrakingFrictionFactor;
		bSavedUseSeparateBrakingFriction = Movement->bUseSeparateBrakingFriction;
		SavedStance = Movement->GetDesiredStance();
		SavedRagdollInput = Movement->CaptureRagdollInput();
		Movement->RagdollMode.Input = SavedRagdollInput;
		bForceNoCombine |= SavedRagdollInput.Session != 0;
	}

	virtual void PrepMoveFor(ACharacter* Character) override
	{
		FSavedMove_Character::PrepMoveFor(Character);
		for (const FMovementReplaySetup& Setup : SavedReplaySetups)
		{
			ReplayRestores.Add(Setup());
		}

		UIMGCharacterMovementComponent* Movement = CastChecked<UIMGCharacterMovementComponent>(Character->GetCharacterMovement());
		FIMGMovementRequestRuntime& Runtime = Movement->MovementRequestRuntime;
		Runtime.bHasRequest = Movement->HasMovementRequestProvider()
			&& bSavedHasMovementRequest
			&& SavedMovementRequestGeneration == Runtime.Generation;
		Runtime.Request = Runtime.bHasRequest ? SavedMovementRequest : 0;
		Runtime.Context = Runtime.bHasRequest ? SavedMovementContext : FMovementSimulationContext();
		Movement->ResolveMovementContext(Runtime.Context);

		// An unloaded provider's old moves must not undo its parameter cleanup.
		if (!bSavedHasMovementRequest || Runtime.bHasRequest)
		{
			SavedMovementParameters.Apply(*Movement, Runtime.bHasRequest);
		}
		Movement->BrakingFriction = SavedBrakingFriction;
		Movement->BrakingFrictionFactor = SavedBrakingFrictionFactor;
		Movement->bUseSeparateBrakingFriction = bSavedUseSeparateBrakingFriction;
		Movement->SetDesiredStanceFromMove(SavedStance);
		Movement->RagdollMode.Input = SavedRagdollInput;
	}

	virtual void PostUpdate(ACharacter* Character, EPostUpdateMode PostUpdateMode) override
	{
		if (PostUpdateMode == PostUpdate_Replay)
		{
			for (int32 Index = ReplayRestores.Num() - 1; Index >= 0; --Index)
			{
				if (ReplayRestores[Index])
				{
					ReplayRestores[Index]();
				}
			}
			ReplayRestores.Reset();
		}

		FSavedMove_Character::PostUpdate(Character, PostUpdateMode);
		const UIMGCharacterMovementComponent* Movement = CastChecked<UIMGCharacterMovementComponent>(Character->GetCharacterMovement());
		const FIMGMovementRequestRuntime& Runtime = Movement->MovementRequestRuntime;
		SavedMovementParameterHash = Runtime.bContextApplied
			&& bSavedHasMovementRequest
			&& SavedMovementRequestGeneration == Runtime.Generation
			? Runtime.ParameterHash
			: 0;
	}
};

class FNetworkPredictionData_Client_IMG final : public FNetworkPredictionData_Client_Character
{
public:
	explicit FNetworkPredictionData_Client_IMG(const UCharacterMovementComponent& Movement)
		: FNetworkPredictionData_Client_Character(Movement)
	{
	}

	virtual FSavedMovePtr AllocateNewMove() override
	{
		return FSavedMovePtr(new FSavedMove_IMG());
	}

	virtual void FreeMove(const FSavedMovePtr& Move) override
	{
		if (Move.IsValid())
		{
			static_cast<FSavedMove_IMG*>(Move.Get())->SavedReplaySetups.Reset();
		}
		FNetworkPredictionData_Client_Character::FreeMove(Move);
	}
};

void FIMGCharacterNetworkMoveData::ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType)
{
	FCharacterNetworkMoveData::ClientFillNetworkMoveData(ClientMove, MoveType);
	const FSavedMove_IMG& IMGMove = static_cast<const FSavedMove_IMG&>(ClientMove);
	InputMagnitude = IMGMove.SavedInputMagnitude;
	bHasMovementRequest = IMGMove.bSavedHasMovementRequest;
	MovementRequest = IMGMove.SavedMovementRequest;
	MovementContextRevision = IMGMove.SavedMovementContext.Revision;
	MovementContextGeneration = IMGMove.SavedMovementRequestGeneration;
	MovementContextHash = IMGMove.SavedMovementContext.GetParameterHash();
	MovementParameterHash = IMGMove.SavedMovementParameterHash;
	RagdollInput = IMGMove.SavedRagdollInput;
}

bool FIMGCharacterNetworkMoveData::Serialize(
	UCharacterMovementComponent& CharacterMovement,
	FArchive& Ar,
	UPackageMap* PackageMap,
	ENetworkMoveType MoveType)
{
	const bool bBaseSuccess = FCharacterNetworkMoveData::Serialize(CharacterMovement, Ar, PackageMap, MoveType);
	Ar << InputMagnitude;
	Ar.SerializeBits(&bHasMovementRequest, 1);
	if (bHasMovementRequest)
	{
		Ar << MovementRequest;
		Ar.SerializeIntPacked(MovementContextRevision);
		Ar.SerializeIntPacked(MovementContextGeneration);
		Ar << MovementContextHash;
		Ar << MovementParameterHash;
		if (MovementContextRevision == 0 || MovementContextGeneration == 0)
		{
			Ar.SetError();
		}
	}
	else if (Ar.IsLoading())
	{
		MovementRequest = 0;
		MovementContextRevision = 0;
		MovementContextGeneration = 0;
		MovementContextHash = 0;
		MovementParameterHash = 0;
	}

	bool bSuccess = true;
	RagdollInput.NetSerialize(Ar, PackageMap, bSuccess);
	return bBaseSuccess && bSuccess && !Ar.IsError();
}

FIMGCharacterNetworkMoveDataContainer::FIMGCharacterNetworkMoveDataContainer()
{
	NewMoveData = &Moves[0];
	PendingMoveData = &Moves[1];
	OldMoveData = &Moves[2];
}

void FIMGCharacterMoveResponseDataContainer::ServerFillResponseData(
	const UCharacterMovementComponent& CharacterMovement,
	const FClientAdjustment& PendingAdjustment)
{
	FCharacterMoveResponseDataContainer::ServerFillResponseData(CharacterMovement, PendingAdjustment);
	const UIMGCharacterMovementComponent& Movement = *CastChecked<const UIMGCharacterMovementComponent>(&CharacterMovement);
	const FIMGMovementRequestRuntime& RequestRuntime = Movement.MovementRequestRuntime;
	const FIMGRagdollMovementRuntime& RagdollRuntime = Movement.RagdollRuntime;

	bHasMovementContext = RequestRuntime.AdjustmentTimeStamp == PendingAdjustment.TimeStamp
		&& RequestRuntime.AdjustmentGeneration != 0
		&& RequestRuntime.AdjustmentContext.IsValid();
	MovementContext = bHasMovementContext ? RequestRuntime.AdjustmentContext : FMovementSimulationContext();
	MovementContextGeneration = bHasMovementContext ? RequestRuntime.AdjustmentGeneration : 0;

	RagdollState = Movement.GetRagdollMovementState();
	bHasRagdollState = RagdollState.Revision != 0;

	RagdollBodyReference = RagdollRuntime.ActiveBodyReference;
	bHasRagdollBodyReference = bHasRagdollState
		&& RagdollBodyReference.Sequence != 0
		&& RagdollBodyReference.Session == RagdollState.Session
		&& RagdollBodyReference.MovementRevision == RagdollState.Revision;

	bRagdollPlacementReflected = bHasRagdollState
		&& RagdollState.bHasRecovery
		&& (!PendingAdjustment.bAckGoodMove || !Movement.bIgnoreClientMovementErrorChecksAndCorrection)
		&& RagdollRuntime.AdjustmentTimeStamp == PendingAdjustment.TimeStamp
		&& RagdollRuntime.AdjustmentPlacementSession == RagdollState.Session;

	if (bHasRagdollState && (RagdollState.Phase != EIMGRagdollPhase::Inactive || RagdollState.bHasRecovery))
	{
		bHasRotation = true;
	}

	TEnumAsByte<EMovementMode> PendingMode;
	TEnumAsByte<EMovementMode> PendingGroundMode;
	uint8 PendingCustomMode = 0;
	Movement.UnpackNetworkMovementMode(PendingAdjustment.MovementMode, PendingMode, PendingCustomMode, PendingGroundMode);
	if (Movement.IsRagdollMovementMode()
		|| (PendingMode == MOVE_Custom && PendingCustomMode == static_cast<uint8>(EIMGCustomMovementMode::Ragdoll)))
	{
		bRootMotionMontageCorrection = false;
		bRootMotionSourceCorrection = false;
	}
}

bool FIMGCharacterMoveResponseDataContainer::Serialize(
	UCharacterMovementComponent& CharacterMovement,
	FArchive& Ar,
	UPackageMap* PackageMap)
{
	const bool bBaseSuccess = FCharacterMoveResponseDataContainer::Serialize(CharacterMovement, Ar, PackageMap);
	Ar.SerializeBits(&bHasMovementContext, 1);
	if (bHasMovementContext)
	{
		Ar.SerializeIntPacked(MovementContextGeneration);
		if (!MovementContext.Serialize(Ar) || MovementContextGeneration == 0)
		{
			Ar.SetError();
		}
	}
	else if (Ar.IsLoading())
	{
		MovementContextGeneration = 0;
		MovementContext = FMovementSimulationContext();
	}

	Ar.SerializeBits(&bHasRagdollState, 1);
	bool bSuccess = true;
	if (bHasRagdollState)
	{
		RagdollState.NetSerialize(Ar, PackageMap, bSuccess);
		if (RagdollState.bHasRecovery)
		{
			Ar.SerializeBits(&bRagdollPlacementReflected, 1);
		}
		else if (Ar.IsLoading())
		{
			bRagdollPlacementReflected = false;
		}
	}
	else if (Ar.IsLoading())
	{
		bRagdollPlacementReflected = false;
	}

	Ar.SerializeBits(&bHasRagdollBodyReference, 1);
	if (bHasRagdollBodyReference)
	{
		bool bReferenceSuccess = true;
		RagdollBodyReference.NetSerialize(Ar, PackageMap, bReferenceSuccess);
		bSuccess &= bReferenceSuccess;
	}
	else if (Ar.IsLoading())
	{
		RagdollBodyReference = FIMGRagdollBodyReference();
	}

	return bBaseSuccess && bSuccess && !Ar.IsError();
}

FNetworkPredictionData_Client* UIMGCharacterMovementComponent::GetPredictionData_Client() const
{
	if (!ClientPredictionData)
	{
		UIMGCharacterMovementComponent* MutableThis = const_cast<UIMGCharacterMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_IMG(*this);
	}
	return ClientPredictionData;
}

bool UIMGCharacterMovementComponent::RegisterMovementRequestProvider(
	UObject* Source,
	FMovementRequestCapture Capture,
	FMovementRequestApply Apply)
{
	if (!IsValid(Source) || Capture.GetUObject() != Source || Apply.GetUObject() != Source)
	{
		UE_LOG(LogIMGMovementNet, Error, TEXT("%s: Invalid movement request provider. Source=%s CaptureOwner=%s ApplyOwner=%s"),
			*GetPathName(), *GetPathNameSafe(Source), *GetPathNameSafe(Capture.GetUObject()), *GetPathNameSafe(Apply.GetUObject()));
		return false;
	}

	if (MovementRequestRuntime.Source.IsValid() && MovementRequestRuntime.Source.Get() != Source)
	{
		UE_LOG(LogIMGMovementNet, Error, TEXT("%s: Cannot register movement request provider %s because %s already owns the slot."),
			*GetPathName(), *GetPathNameSafe(Source), *GetPathNameSafe(MovementRequestRuntime.Source.Get()));
		return false;
	}

	if (IsMovementRequestProvider(Source))
	{
		return true;
	}

	if (CharacterOwner
		&& (CharacterOwner->GetLocalRole() == ROLE_AutonomousProxy || CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy)
		&& !ShouldUsePackedMovementRPCs())
	{
		UE_LOG(LogIMGMovementNet, Error, TEXT("%s: Movement request provider %s requires Packed Movement RPCs (p.NetUsePackedMovementRPCs)."),
			*GetPathName(), *GetPathNameSafe(Source));
		return false;
	}

	ResetMovementRequestHistory();
	MovementRequestRuntime.Source = Source;
	MovementRequestRuntime.CaptureDelegate = MoveTemp(Capture);
	MovementRequestRuntime.ApplyDelegate = MoveTemp(Apply);
	return true;
}

void UIMGCharacterMovementComponent::UnregisterMovementRequestProvider(const UObject* Source)
{
	if (MovementRequestRuntime.Source.IsValid() && MovementRequestRuntime.Source.Get() != Source)
	{
		return;
	}
	ResetMovementRequestHistory();
	MovementRequestRuntime.Source.Reset();
	MovementRequestRuntime.CaptureDelegate.Unbind();
	MovementRequestRuntime.ApplyDelegate.Unbind();
}

void UIMGCharacterMovementComponent::ResetMovementRequestHistory()
{
	MovementRequestRuntime.ResetHistory();
}

bool UIMGCharacterMovementComponent::HasMovementRequestProvider() const
{
	return MovementRequestRuntime.Source.IsValid()
		&& MovementRequestRuntime.CaptureDelegate.IsBound()
		&& MovementRequestRuntime.ApplyDelegate.IsBound();
}

bool UIMGCharacterMovementComponent::ApplyMovementRequest(const FVector& Input)
{
	if (!MovementRequestRuntime.bHasRequest || !HasMovementRequestProvider() || !HasValidData() || !IsRegistered() || !IsActive())
	{
		return false;
	}

	const uint32 Generation = MovementRequestRuntime.Generation;
	const bool bApplied = MovementRequestRuntime.Context.IsValid()
		&& MovementRequestRuntime.ApplyDelegate.Execute(
			MovementRequestRuntime.Request,
			MovementRequestRuntime.Context,
			Input,
			CharacterOwner->bClientUpdating);
	return bApplied && Generation == MovementRequestRuntime.Generation && HasMovementRequestProvider() && HasValidData();
}

bool UIMGCharacterMovementComponent::CaptureMovementRequestContext(bool bCaptureRequest)
{
	if (!HasMovementRequestProvider())
	{
		return false;
	}

	const uint32 Generation = MovementRequestRuntime.Generation;
	uint8 Request = 0;
	FMovementSimulationContext Context;
	if (!MovementRequestRuntime.CaptureDelegate.Execute(Request, Context)
		|| Generation != MovementRequestRuntime.Generation
		|| !HasMovementRequestProvider()
		|| !Context.IsValid()
		|| Context.ParameterRevisions.Num() != Context.Parameters.Num())
	{
		return false;
	}

	ResolveMovementContext(Context);
	MovementRequestRuntime.Context = MoveTemp(Context);
	if (bCaptureRequest)
	{
		MovementRequestRuntime.Request = Request;
	}
	return true;
}

void UIMGCharacterMovementComponent::ResolveMovementContext(FMovementSimulationContext& Context) const
{
	if (CharacterOwner
		&& CharacterOwner->GetLocalRole() == ROLE_AutonomousProxy
		&& MovementRequestRuntime.AuthorityContextGeneration == MovementRequestRuntime.Generation
		&& MovementRequestRuntime.AuthorityContext.IsValid()
		&& Context.Parameters.Num() == MovementRequestRuntime.AuthorityContext.Parameters.Num()
		&& Context.ParameterRevisions.Num() == Context.Parameters.Num()
		&& MovementRequestRuntime.AuthorityParameterRevisions.Num() == Context.Parameters.Num())
	{
		for (int32 Index = 0; Index < Context.Parameters.Num(); ++Index)
		{
			if (Context.ParameterRevisions[Index] == MovementRequestRuntime.AuthorityParameterRevisions[Index])
			{
				Context.Parameters[Index] = MovementRequestRuntime.AuthorityContext.Parameters[Index];
			}
		}
	}
}

void UIMGCharacterMovementComponent::ControlledCharacterMove(const FVector& InputVector, float DeltaSeconds)
{
	if (!HasValidData())
	{
		return;
	}

	TGuardValue<bool> InputScope(MovementRequestRuntime.bHasRequestInput, false);
	MovementRequestRuntime.bHasRequest = CaptureMovementRequestContext();
	if (MovementRequestRuntime.bHasRequest)
	{
		// Prepare input scaling here; PerformMovement finalizes the same rules after rewind/jump handling.
		ApplyMovementRequest(InputVector);
	}
	if (HasValidData())
	{
		Super::ControlledCharacterMove(InputVector, DeltaSeconds);
	}
}

void UIMGCharacterMovementComponent::PerformMovement(float DeltaSeconds)
{
	MovementRequestRuntime.bContextApplied = false;
	if (HasValidData() && CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy)
	{
		const FVector Input = MovementRequestRuntime.bHasRequestInput ? MovementRequestRuntime.RequestInput : Acceleration;
		// Evaluate against this simulation's velocity, after native combining/replay has restored it.
		MovementRequestRuntime.bContextApplied = ApplyMovementRequest(Input);
		if (MovementRequestRuntime.bContextApplied)
		{
			MovementRequestRuntime.ParameterHash = FCharacterMovementParameters::Capture(*this).GetParameterHash();
		}
		if (MovementRequestRuntime.bContextApplied && MovementRequestRuntime.bHasRequestInput)
		{
			Acceleration = ConstrainInputAcceleration(Input).GetSafeNormal()
				* (MovementRequestRuntime.InputMagnitude / 255.f) * GetMaxAcceleration();
			AnalogInputModifier = ComputeAnalogInputModifier();
		}
	}
	Super::PerformMovement(DeltaSeconds);
}

bool UIMGCharacterMovementComponent::ForcePositionUpdate(float DeltaTime)
{
	// Keep accepted intent/input, but allow a new authority buff to take effect at this simulation boundary.
	if (MovementRequestRuntime.bHasRequest)
	{
		MovementRequestRuntime.bHasRequest = CaptureMovementRequestContext(false);
	}
	TGuardValue<bool> InputScope(
		MovementRequestRuntime.bHasRequestInput,
		MovementRequestRuntime.bHasRequest && HasMovementRequestProvider());
	return Super::ForcePositionUpdate(DeltaTime);
}

float UIMGCharacterMovementComponent::ComputeAnalogInputModifier() const
{
	if (MovementRequestRuntime.bHasRequestInput && MovementRequestRuntime.bHasRequest && HasMovementRequestProvider())
	{
		return Acceleration.IsZero() ? 0.f : MovementRequestRuntime.InputMagnitude / 255.f;
	}
	if (CharacterOwner && (CharacterOwner->HasAuthority() || CharacterOwner->bClientUpdating))
	{
		if (const FCharacterNetworkMoveData* Data = GetCurrentNetworkMoveData())
		{
			// The current authority cap may differ from the cap used when this input was recorded.
			return Acceleration.IsZero() ? 0.f : static_cast<const FIMGCharacterNetworkMoveData*>(Data)->InputMagnitude / 255.f;
		}
	}
	if (CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_AutonomousProxy && ShouldUsePackedMovementRPCs())
	{
		return IMGMovementNet::QuantizeInputMagnitude(Acceleration, GetMaxAcceleration()) / 255.f;
	}
	return Super::ComputeAnalogInputModifier();
}

void UIMGCharacterMovementComponent::MoveAutonomous(
	float ClientTimeStamp,
	float DeltaTime,
	uint8 CompressedFlags,
	const FVector& NewAccel)
{
	const FIMGCharacterNetworkMoveData* RequestData = static_cast<const FIMGCharacterNetworkMoveData*>(GetCurrentNetworkMoveData());
	if (CharacterOwner && !CharacterOwner->bClientUpdating)
	{
		if (RequestData)
		{
			MovementRequestRuntime.bHasRequest = RequestData->bHasMovementRequest && CaptureMovementRequestContext(false);
			MovementRequestRuntime.Request = MovementRequestRuntime.bHasRequest ? RequestData->MovementRequest : 0;
		}
		else
		{
			// Without packed data, retain the last request instead of sampling live provider intent.
			MovementRequestRuntime.bHasRequest &= HasMovementRequestProvider();
		}
	}

	TGuardValue<bool> InputScope(MovementRequestRuntime.bHasRequestInput, MovementRequestRuntime.bHasRequest && RequestData != nullptr);
	const uint32 Generation = MovementRequestRuntime.Generation;
	MovementRequestRuntime.bContextApplied = false;
	MovementRequestRuntime.ContextTimeStamp = 0.f;
	MovementRequestRuntime.RequestInput = NewAccel;
	if (RequestData)
	{
		MovementRequestRuntime.InputMagnitude = RequestData->InputMagnitude;
	}

	if (RequestData && CharacterOwner && CharacterOwner->HasAuthority())
	{
		RagdollRuntime.InputMoveTimeStamp = ClientTimeStamp;
		RagdollMode.Input = RequestData->RagdollInput;
		if (ActiveRagdollState.Phase == EIMGRagdollPhase::Simulating
			&& RagdollMode.Input.Session == ActiveRagdollState.Session)
		{
			RagdollRuntime.RemoteInputTime = GetWorld()->GetTimeSeconds();
		}
	}

	Super::MoveAutonomous(ClientTimeStamp, DeltaTime, CompressedFlags, NewAccel);
	RagdollRuntime.InputMoveTimeStamp = -1.f;

	if (CharacterOwner
		&& CharacterOwner->HasAuthority()
		&& MovementRequestRuntime.bContextApplied
		&& Generation == MovementRequestRuntime.Generation
		&& HasMovementRequestProvider())
	{
		MovementRequestRuntime.ContextTimeStamp = ClientTimeStamp;
	}
}

bool UIMGCharacterMovementComponent::ClientUpdatePositionAfterServerUpdate()
{
	const TOptional<FVector> RagdollCorrectionOrigin = RagdollRuntime.PreCorrectionLocation;
	const uint16 CorrectionSession = RagdollRuntime.CorrectionSession;
	RagdollRuntime.ClearCorrection();

	const bool bFinishingMeshCorrection = AutonomousMeshSmoothing.PreCorrectionLocation.IsSet()
		&& HasPredictionData_Client()
		&& GetPredictionData_Client_Character()->bUpdatePosition;

	const uint32 Generation = MovementRequestRuntime.Generation;
	const uint8 SavedRequest = MovementRequestRuntime.Request;
	const bool bSavedHasRequest = MovementRequestRuntime.bHasRequest;
	const FVector SavedRequestInput = MovementRequestRuntime.RequestInput;
	const uint8 SavedInputMagnitude = MovementRequestRuntime.InputMagnitude;
	const bool bSavedHasRequestInput = MovementRequestRuntime.bHasRequestInput;
	const FMovementSimulationContext SavedContext = MovementRequestRuntime.Context;
	// Replay consumes historical parameters; the next live move keeps this frame's configuration.
	const FCharacterMovementParameters SavedParameters = FCharacterMovementParameters::Capture(*this);
	const float SavedBrakingFriction = BrakingFriction;
	const float SavedBrakingFrictionFactor = BrakingFrictionFactor;
	const bool bSavedSeparateBrakingFriction = bUseSeparateBrakingFriction;

	const bool bResult = Super::ClientUpdatePositionAfterServerUpdate();

	// A callback may have removed/replaced the provider during replay; don't undo its cleanup.
	if (Generation == MovementRequestRuntime.Generation)
	{
		MovementRequestRuntime.Request = SavedRequest;
		MovementRequestRuntime.bHasRequest = bSavedHasRequest;
		MovementRequestRuntime.RequestInput = SavedRequestInput;
		MovementRequestRuntime.InputMagnitude = SavedInputMagnitude;
		MovementRequestRuntime.bHasRequestInput = bSavedHasRequestInput;
		MovementRequestRuntime.Context = SavedContext;
		SavedParameters.Apply(*this, HasMovementRequestProvider());
	}
	BrakingFriction = SavedBrakingFriction;
	BrakingFrictionFactor = SavedBrakingFrictionFactor;
	bUseSeparateBrakingFriction = bSavedSeparateBrakingFriction;

	if (bFinishingMeshCorrection)
	{
		FinishAutonomousMeshCorrection();
	}

	if (RagdollCorrectionOrigin.IsSet()
		&& HasValidData()
		&& IsActive()
		&& IsRegistered()
		&& CharacterOwner->GetLocalRole() == ROLE_AutonomousProxy
		&& IsRagdollMovementMode()
		&& ActiveRagdollState.Phase == EIMGRagdollPhase::Simulating
		&& ActiveRagdollState.Session == CorrectionSession)
	{
		const FVector Delta = UpdatedComponent->GetComponentLocation() - RagdollCorrectionOrigin.GetValue();
		if (!Delta.IsNearlyZero())
		{
			RagdollCapsuleCorrection.Broadcast(CorrectionSession, Delta);
		}
	}

	return bResult;
}

void UIMGCharacterMovementComponent::HandleMoveResponse_MovementContext(
	const FIMGCharacterMoveResponseDataContainer& Response,
	const FSavedMove_Character& AckedMove)
{
	if (!Response.bHasMovementContext
		|| !HasMovementRequestProvider()
		|| Response.MovementContextGeneration != MovementRequestRuntime.Generation)
	{
		return;
	}

	const FSavedMove_IMG& IMGMove = static_cast<const FSavedMove_IMG&>(AckedMove);
	if (!IMGMove.bSavedHasMovementRequest
		|| IMGMove.SavedMovementRequestGeneration != MovementRequestRuntime.Generation
		|| IMGMove.SavedMovementContext.Revision != Response.MovementContext.Revision
		|| IMGMove.SavedMovementContext.Parameters.Num() != Response.MovementContext.Parameters.Num())
	{
		return;
	}

	// Rebase unchanged inputs independently: a landing or friction change must not resurrect stale speed.
	MovementRequestRuntime.AuthorityContext = Response.MovementContext;
	MovementRequestRuntime.AuthorityParameterRevisions = IMGMove.SavedMovementContext.ParameterRevisions;
	MovementRequestRuntime.AuthorityContextGeneration = MovementRequestRuntime.Generation;

	if (GetMovementNetDiagnosticsLevel() >= 1
		&& !IMGMove.SavedMovementContext.HasSameParameters(MovementRequestRuntime.AuthorityContext))
	{
		UE_LOG(LogIMGMovementNet, Log,
			TEXT("[MovementContext] Actor=%s Move=%.6f Generation=%u Revision=%u PredictedHash=%u AuthorityHash=%u"),
			*GetPathNameSafe(CharacterOwner.Get()),
			Response.ClientAdjustment.TimeStamp,
			MovementRequestRuntime.Generation,
			MovementRequestRuntime.AuthorityContext.Revision,
			IMGMove.SavedMovementContext.GetParameterHash(),
			MovementRequestRuntime.AuthorityContext.GetParameterHash());
	}
}

void UIMGCharacterMovementComponent::HandleMoveResponse_Ragdoll(
	const FIMGCharacterMoveResponseDataContainer& Response,
	bool bAcceptedAckAdvance,
	const FNetworkPredictionData_Client_Character*& ClientData)
{
	if (!HasValidData() || !bAcceptedAckAdvance)
	{
		return;
	}

	if (Response.bHasRagdollState
		&& Response.bRagdollPlacementReflected
		&& Response.RagdollState.bHasRecovery
		&& Response.RagdollState.Session == ActiveRagdollState.Session
		&& !IsRagdollMovementMode())
	{
		// This accepted ACK/correction was evaluated after the approved placement, not merely packed after it.
		RagdollRuntime.RecoveryAppliedSession = ActiveRagdollState.Session;
		RagdollRuntime.LocalPlacementSession = ActiveRagdollState.Session;
		OnRagdollMovementUpdated.Broadcast();
		ClientData = HasPredictionData_Client() ? GetPredictionData_Client_Character() : nullptr;
	}

	if (HasValidData() && Response.bHasRagdollBodyReference && ClientData && ClientData->LastAckedMove.IsValid())
	{
		ReceiveRagdollBodyReference(Response.RagdollBodyReference);
	}
}

void UIMGCharacterMovementComponent::ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& MoveResponse)
{
	const FVector PreResponseLocation = UpdatedComponent ? UpdatedComponent->GetComponentLocation() : FVector::ZeroVector;
	const uint16 PreResponseSession = ActiveRagdollState.Phase == EIMGRagdollPhase::Simulating ? ActiveRagdollState.Session : 0;
	const FIMGCharacterMoveResponseDataContainer& Response = static_cast<const FIMGCharacterMoveResponseDataContainer&>(MoveResponse);
	const FNetworkPredictionData_Client_Character* PreviousClientData = HasPredictionData_Client() ? GetPredictionData_Client_Character() : nullptr;
	const FSavedMovePtr PreviousAckedMove = PreviousClientData ? PreviousClientData->LastAckedMove : FSavedMovePtr();

	if (Response.bHasRagdollState)
	{
		ReceiveRagdollMovementState(Response.RagdollState);
	}

	const FNetworkPredictionData_Client_Character* CurrentClientData = HasPredictionData_Client() ? GetPredictionData_Client_Character() : nullptr;
	bool bDiscardCorrection = false;
	if (HasValidData()
		&& IsActive()
		&& Response.IsCorrection()
		&& !bClientIgnoreMovementCorrections
		&& CurrentClientData
		&& CurrentClientData->GetSavedMoveIndex(Response.ClientAdjustment.TimeStamp) != INDEX_NONE)
	{
		ClientCorrectionFilter.Broadcast(MoveResponse, bDiscardCorrection);
	}

	if (bDiscardCorrection)
	{
		// Preserve native ACK/buffer bookkeeping without applying the obsolete transform, mode or root motion.
		FCharacterMoveResponseDataContainer Ack;
		Ack.ClientAdjustment.bAckGoodMove = true;
		Ack.ClientAdjustment.TimeStamp = Response.ClientAdjustment.TimeStamp;
		Super::ClientHandleMoveResponse(Ack);
	}
	else
	{
		Super::ClientHandleMoveResponse(MoveResponse);
	}

	const FNetworkPredictionData_Client_Character* ClientData = HasPredictionData_Client() ? GetPredictionData_Client_Character() : nullptr;
	const bool bAckAdvanced = ClientData
		&& ClientData->LastAckedMove.IsValid()
		&& ClientData->LastAckedMove != PreviousAckedMove
		&& ClientData->LastAckedMove->TimeStamp == Response.ClientAdjustment.TimeStamp;
	const bool bAcceptedResponse = HasValidData()
		&& !bDiscardCorrection
		&& (!Response.IsCorrection() || !bClientIgnoreMovementCorrections)
		&& bAckAdvanced;

	if (bAcceptedResponse && Response.IsCorrection())
	{
		const FSavedMovePtr AcceptedMove = ClientData->LastAckedMove;
		if (PreResponseSession != 0
			&& PreResponseSession == ActiveRagdollState.Session
			&& IsRagdollMovementMode()
			&& ActiveRagdollState.Phase == EIMGRagdollPhase::Simulating
			&& !RagdollRuntime.PreCorrectionLocation.IsSet())
		{
			RagdollRuntime.PreCorrectionLocation = PreResponseLocation;
			RagdollRuntime.CorrectionSession = PreResponseSession;
		}
		ClientCorrectionApplied.Broadcast(MoveResponse, *AcceptedMove);
		ClientData = HasPredictionData_Client() ? GetPredictionData_Client_Character() : nullptr;
	}

	if (bAcceptedResponse && HasValidData() && ClientData && ClientData->LastAckedMove.IsValid())
	{
		HandleMoveResponse_MovementContext(Response, *ClientData->LastAckedMove);
	}

	if (GetMovementNetDiagnosticsLevel() >= 1 && Response.IsCorrection())
	{
		UE_LOG(LogIMGMovementNet, Log,
			TEXT("[Correction] Actor=%s Move=%.6f Discarded=%d AckAdvanced=%d ServerPackedMode=%u CurrentPackedMode=%u ServerLocRaw=%s RelativeLoc=%d ServerVelRaw=%s RelativeVel=%d MontageCorrection=%d SourceCorrection=%d SavedMoves=%d"),
			*GetPathNameSafe(CharacterOwner.Get()),
			Response.ClientAdjustment.TimeStamp,
			bDiscardCorrection,
			bAckAdvanced,
			Response.ClientAdjustment.MovementMode,
			PackNetworkMovementMode(),
			*Response.ClientAdjustment.NewLoc.ToCompactString(),
			Response.ClientAdjustment.bBaseRelativePosition,
			*Response.ClientAdjustment.NewVel.ToCompactString(),
			Response.ClientAdjustment.bBaseRelativeVelocity,
			Response.bRootMotionMontageCorrection,
			Response.bRootMotionSourceCorrection,
			ClientData ? ClientData->SavedMoves.Num() : 0);
	}

	HandleMoveResponse_Ragdoll(Response, bAcceptedResponse, ClientData);
}

void UIMGCharacterMovementComponent::ServerMoveHandleClientError(
	float ClientTimeStamp,
	float DeltaTime,
	const FVector& Accel,
	const FVector& RelativeClientLocation,
	FMovementBaseInterfaceData* ClientMovementBaseInterfaceData,
	FName ClientBaseBoneName,
	uint8 ClientMovementMode)
{
	const uint16 Revision = ActiveRagdollState.Revision;
	const uint16 PlacementSession = ActiveRagdollState.bHasRecovery ? ActiveRagdollState.Session : 0;
	const FIMGCharacterNetworkMoveData* MoveData = static_cast<const FIMGCharacterNetworkMoveData*>(GetCurrentNetworkMoveData());
	const bool bHasContext = MoveData
		&& MoveData->bHasMovementRequest
		&& MovementRequestRuntime.bContextApplied
		&& MovementRequestRuntime.ContextTimeStamp == ClientTimeStamp
		&& MovementRequestRuntime.Context.IsValid();

	if (bHasContext
		&& MoveData->MovementContextHash != MovementRequestRuntime.Context.GetParameterHash()
		&& MoveData->MovementParameterHash != MovementRequestRuntime.ParameterHash)
	{
		// Return a position/velocity baseline together with the actual rule inputs, even before position error grows.
		GetPredictionData_Server_Character()->bForceClientUpdate = true;
	}

	Super::ServerMoveHandleClientError(
		ClientTimeStamp, DeltaTime, Accel, RelativeClientLocation,
		ClientMovementBaseInterfaceData, ClientBaseBoneName, ClientMovementMode);

	if (HasPredictionData_Server() && GetPredictionData_Server_Character()->PendingAdjustment.TimeStamp == ClientTimeStamp)
	{
		RagdollRuntime.AdjustmentTimeStamp = ClientTimeStamp;
		RagdollRuntime.AdjustmentPlacementSession = ActiveRagdollState.Revision == Revision ? PlacementSession : 0;
		MovementRequestRuntime.AdjustmentTimeStamp = ClientTimeStamp;
		MovementRequestRuntime.AdjustmentGeneration = bHasContext ? MoveData->MovementContextGeneration : 0;
		MovementRequestRuntime.AdjustmentContext = bHasContext ? MovementRequestRuntime.Context : FMovementSimulationContext();
		if (bHasContext)
		{
			MovementRequestRuntime.AdjustmentContext.Revision = MoveData->MovementContextRevision;
		}
	}
}
