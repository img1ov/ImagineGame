

#include "Cosmetics/IMGControllerComponent_CharacterParts.h"
#include "Cosmetics/IMGCharacterPartTypes.h"
#include "Cosmetics/IMGPawnComponent_CharacterParts.h"
#include "GameFramework/CheatManagerDefines.h"
#include "Cosmetics/IMGCosmeticDeveloperSettings.h"
#include "GameFramework/Pawn.h"
#include "Components/GameFrameworkComponentManager.h"
#include "IMGLogChannels.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGControllerComponent_CharacterParts)

//////////////////////////////////////////////////////////////////////

UIMGControllerComponent_CharacterParts::UIMGControllerComponent_CharacterParts(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UIMGControllerComponent_CharacterParts::BeginPlay()
{
	Super::BeginPlay();

	// Listen for pawn possession changed events
	if (HasAuthority())
	{
		if (AController* OwningController = GetController<AController>())
		{
			OwningController->OnPossessedPawnChanged.AddDynamic(this, &ThisClass::OnPossessedPawnChanged);

			if (APawn* ControlledPawn = GetPawn<APawn>())
			{
				OnPossessedPawnChanged(nullptr, ControlledPawn);
			}
		}

		ApplyDeveloperSettings();
	}
}

void UIMGControllerComponent_CharacterParts::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	PawnExtensionHandle.Reset();
	if (AController* OwningController = GetController<AController>())
	{
		OwningController->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::OnPossessedPawnChanged);
	}
	RemoveAllCharacterParts();
	Super::EndPlay(EndPlayReason);
}

UIMGPawnComponent_CharacterParts* UIMGControllerComponent_CharacterParts::GetPawnCustomizer() const
{
	if (APawn* ControlledPawn = GetPawn<APawn>())
	{
		UIMGPawnComponent_CharacterParts* Customizer = ControlledPawn->FindComponentByClass<UIMGPawnComponent_CharacterParts>();
		// Preinstalled components can accept parts before BeginPlay; do not delay the normal possession path.
		return IsValid(Customizer) && Customizer->IsRegistered() && !Customizer->IsBeingDestroyed() ? Customizer : nullptr;
	}
	return nullptr;
}

void UIMGControllerComponent_CharacterParts::AddCharacterPart(const FIMGCharacterPart& NewPart)
{
	AddCharacterPartInternal(NewPart, ECharacterPartSource::Natural);
}

void UIMGControllerComponent_CharacterParts::AddCharacterPartInternal(const FIMGCharacterPart& NewPart, ECharacterPartSource Source)
{
	if (!HasAuthority())
	{
		return;
	}

	FIMGControllerCharacterPartEntry& NewEntry = CharacterParts.AddDefaulted_GetRef();
	NewEntry.Part = NewPart;
	NewEntry.Source = Source;

	ApplyCharacterPartsToPawn();
}

void UIMGControllerComponent_CharacterParts::RemoveCharacterPart(const FIMGCharacterPart& PartToRemove)
{
	for (auto EntryIt = CharacterParts.CreateIterator(); EntryIt; ++EntryIt)
	{
		if (FIMGCharacterPart::AreEquivalentParts(EntryIt->Part, PartToRemove))
		{
			if (UIMGPawnComponent_CharacterParts* PawnCustomizer = AppliedPawnCustomizer.Get())
			{
				PawnCustomizer->RemoveCharacterPart(EntryIt->Handle);
			}

			EntryIt.RemoveCurrent();
			break;
		}
	}
}

void UIMGControllerComponent_CharacterParts::RemoveAllCharacterParts()
{
	DetachFromPawnCustomizer();
	CharacterParts.Reset();
}

void UIMGControllerComponent_CharacterParts::OnPossessedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	PawnExtensionHandle.Reset();
	if (UIMGPawnComponent_CharacterParts* Customizer = AppliedPawnCustomizer.Get(); Customizer && Customizer->GetOwner() != NewPawn)
	{
		DetachFromPawnCustomizer();
	}
	bWarnedMissingPawnCustomizer = false;

	if (NewPawn)
	{
		if (UGameFrameworkComponentManager* Manager = UGameFrameworkComponentManager::GetForActor(NewPawn))
		{
			PawnExtensionHandle = Manager->AddExtensionHandler(NewPawn->GetClass(),
				UGameFrameworkComponentManager::FExtensionHandlerDelegate::CreateUObject(this, &ThisClass::HandlePawnExtension));
		}
	}
	ApplyCharacterPartsToPawn();
}

void UIMGControllerComponent_CharacterParts::DetachFromPawnCustomizer()
{
	UIMGPawnComponent_CharacterParts* Customizer = AppliedPawnCustomizer.Get();
	AppliedPawnCustomizer.Reset();
	for (FIMGControllerCharacterPartEntry& Entry : CharacterParts)
	{
		const FIMGCharacterPartHandle Handle = Entry.Handle;
		Entry.Handle.Reset();
		if (Customizer && Handle.IsValid())
		{
			Customizer->RemoveCharacterPart(Handle);
		}
	}
}

void UIMGControllerComponent_CharacterParts::ApplyCharacterPartsToPawn()
{
	if (!HasAuthority())
	{
		return;
	}

	UIMGPawnComponent_CharacterParts* Customizer = GetPawnCustomizer();
	if (AppliedPawnCustomizer.Get() != Customizer || !Customizer)
	{
		DetachFromPawnCustomizer();
		AppliedPawnCustomizer = Customizer;
	}

	if (!Customizer)
	{
		if (GetPawn<APawn>() && !bWarnedMissingPawnCustomizer && CharacterParts.ContainsByPredicate([](const FIMGControllerCharacterPartEntry& Entry)
			{ return Entry.Source != ECharacterPartSource::NaturalSuppressedViaCheat; }))
		{
			bWarnedMissingPawnCustomizer = true;
			UE_LOG(LogIMG, Warning, TEXT("Controller Parts %s: Pawn %s has no available PawnComponent_CharacterParts. Keeping requested parts and waiting for CharacterPartsReady; check the Pawn BP or Game Feature Add Components."),
				*GetPathName(), *GetPathNameSafe(GetPawn<APawn>()));
		}
		return;
	}

	bWarnedMissingPawnCustomizer = false;
	for (FIMGControllerCharacterPartEntry& Entry : CharacterParts)
	{
		if (!Entry.Handle.IsValid() && Entry.Source != ECharacterPartSource::NaturalSuppressedViaCheat)
		{
			Entry.Handle = Customizer->AddCharacterPart(Entry.Part);
		}
	}
}

void UIMGControllerComponent_CharacterParts::HandlePawnExtension(AActor* Actor, FName EventName)
{
	if (Actor != GetPawn<APawn>())
	{
		return;
	}
	if (EventName == UIMGPawnComponent_CharacterParts::NAME_CharacterPartsReady)
	{
		ApplyCharacterPartsToPawn();
	}
	else if (EventName == UIMGPawnComponent_CharacterParts::NAME_CharacterPartsRemoved)
	{
		// The component owns its teardown; its handles must not survive into a replacement.
		AppliedPawnCustomizer.Reset();
		for (FIMGControllerCharacterPartEntry& Entry : CharacterParts)
		{
			Entry.Handle.Reset();
		}
	}
}

void UIMGControllerComponent_CharacterParts::ApplyDeveloperSettings()
{
#if UE_WITH_CHEAT_MANAGER
	if (!HasAuthority())
	{
		return;
	}

	const UIMGCosmeticDeveloperSettings* Settings = GetDefault<UIMGCosmeticDeveloperSettings>();

	// Suppress or unsuppress natural parts if needed
	const bool bSuppressNaturalParts = (Settings->CheatMode == ECosmeticCheatMode::ReplaceParts) && (Settings->CheatCosmeticCharacterParts.Num() > 0);
	SetSuppressionOnNaturalParts(bSuppressNaturalParts);

	// Remove anything added by developer settings and re-add it
	UIMGPawnComponent_CharacterParts* PawnCustomizer = AppliedPawnCustomizer.Get();
	for (auto It = CharacterParts.CreateIterator(); It; ++It)
	{
		if (It->Source == ECharacterPartSource::AppliedViaDeveloperSettingsCheat)
		{
			if (PawnCustomizer != nullptr)
			{
				PawnCustomizer->RemoveCharacterPart(It->Handle);
			}
			It.RemoveCurrent();
		}
	}

	// Add new parts
	for (const FIMGCharacterPart& PartDesc : Settings->CheatCosmeticCharacterParts)
	{
		AddCharacterPartInternal(PartDesc, ECharacterPartSource::AppliedViaDeveloperSettingsCheat);
	}
#endif
}


void UIMGControllerComponent_CharacterParts::AddCheatPart(const FIMGCharacterPart& NewPart, bool bSuppressNaturalParts)
{
#if UE_WITH_CHEAT_MANAGER
	SetSuppressionOnNaturalParts(bSuppressNaturalParts);
	AddCharacterPartInternal(NewPart, ECharacterPartSource::AppliedViaCheatManager);
#endif
}

void UIMGControllerComponent_CharacterParts::ClearCheatParts()
{
#if UE_WITH_CHEAT_MANAGER
	UIMGPawnComponent_CharacterParts* PawnCustomizer = AppliedPawnCustomizer.Get();

	// Remove anything added by cheat manager cheats
	for (auto It = CharacterParts.CreateIterator(); It; ++It)
	{
		if (It->Source == ECharacterPartSource::AppliedViaCheatManager)
		{
			if (PawnCustomizer != nullptr)
			{
				PawnCustomizer->RemoveCharacterPart(It->Handle);
			}
			It.RemoveCurrent();
		}
	}

	ApplyDeveloperSettings();
#endif
}

void UIMGControllerComponent_CharacterParts::SetSuppressionOnNaturalParts(bool bSuppressed)
{
#if UE_WITH_CHEAT_MANAGER
	UIMGPawnComponent_CharacterParts* PawnCustomizer = AppliedPawnCustomizer.Get();

	for (FIMGControllerCharacterPartEntry& Entry : CharacterParts)
	{
		if ((Entry.Source == ECharacterPartSource::Natural) && bSuppressed)
		{
			// Suppress
			if (PawnCustomizer != nullptr)
			{
				PawnCustomizer->RemoveCharacterPart(Entry.Handle);
			}
			Entry.Handle.Reset();
			Entry.Source = ECharacterPartSource::NaturalSuppressedViaCheat;
		}
		else if ((Entry.Source == ECharacterPartSource::NaturalSuppressedViaCheat) && !bSuppressed)
		{
			// Unsuppress
			Entry.Source = ECharacterPartSource::Natural;
		}
	}
	ApplyCharacterPartsToPawn();
#endif
}

