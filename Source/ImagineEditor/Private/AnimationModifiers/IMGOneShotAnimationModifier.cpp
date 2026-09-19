#include "AnimationModifiers/IMGOneShotAnimationModifier.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AnimationModifiersAssetUserData.h"
#include "Containers/Ticker.h"
#include "UObject/StrongObjectPtr.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGOneShotAnimationModifier)

namespace IMG::AnimationModifiers
{
	struct FPendingRemoval
	{
		explicit FPendingRemoval(UIMGOneShotAnimationModifier* InSourceModifier)
			: SourceModifier(InSourceModifier)
		{
		}

		TStrongObjectPtr<UIMGOneShotAnimationModifier> SourceModifier;
		int32 PendingApplications = 0;
	};

	TMap<FGuid, TSharedPtr<FPendingRemoval>> PendingRemovals;

	UIMGOneShotAnimationModifier* FindSourceModifier(
		const UIMGOneShotAnimationModifier* AppliedModifier,
		UAnimSequence* Animation,
		const FGuid& InstanceId)
	{
		auto FindInAsset = [AppliedModifier, &InstanceId](IInterface_AssetUserData* Asset) -> UIMGOneShotAnimationModifier*
		{
			if (Asset == nullptr)
			{
				return nullptr;
			}

			const UAnimationModifiersAssetUserData* ModifierData = Asset->GetAssetUserData<UAnimationModifiersAssetUserData>();
			if (ModifierData == nullptr)
			{
				return nullptr;
			}

			for (UAnimationModifier* Modifier : ModifierData->GetAnimationModifierInstances())
			{
				UIMGOneShotAnimationModifier* Candidate = Cast<UIMGOneShotAnimationModifier>(Modifier);
				if (Candidate != nullptr
					&& Candidate->GetClass() == AppliedModifier->GetClass()
					&& Candidate->GetInstanceId() == InstanceId)
				{
					return Candidate;
				}
			}

			return nullptr;
		};

		if (UIMGOneShotAnimationModifier* SourceModifier = FindInAsset(Animation))
		{
			return SourceModifier;
		}

		return FindInAsset(Animation != nullptr ? Animation->GetSkeleton() : nullptr);
	}

	void RemoveSourceInstance(UIMGOneShotAnimationModifier* SourceModifier)
	{
		UAnimationModifiersAssetUserData* ModifierData = Cast<UAnimationModifiersAssetUserData>(SourceModifier->GetOuter());
		if (ModifierData == nullptr)
		{
			return;
		}

		TArray<UAnimationModifier*>& ModifierInstances =
			const_cast<TArray<UAnimationModifier*>&>(ModifierData->GetAnimationModifierInstances());
		if (!ModifierInstances.Contains(SourceModifier))
		{
			return;
		}

		ModifierData->Modify();
		if (UObject* OwningAsset = ModifierData->GetOuter())
		{
			OwningAsset->Modify();
			OwningAsset->MarkPackageDirty();
		}

		ModifierInstances.RemoveSingle(SourceModifier);
	}
}

void UIMGOneShotAnimationModifier::PostInitProperties()
{
	Super::PostInitProperties();

	if (!HasAnyFlags(RF_ClassDefaultObject) && !InstanceId.IsValid())
	{
		InstanceId = FGuid::NewGuid();
	}
}

void UIMGOneShotAnimationModifier::RemoveAfterApply(UAnimSequence* Animation)
{
	if (Animation == nullptr || !InstanceId.IsValid())
	{
		return;
	}

	using namespace IMG::AnimationModifiers;

	TSharedPtr<FPendingRemoval>& PendingRemoval = PendingRemovals.FindOrAdd(InstanceId);
	if (!PendingRemoval.IsValid())
	{
		UIMGOneShotAnimationModifier* SourceModifier = FindSourceModifier(this, Animation, InstanceId);
		if (SourceModifier == nullptr)
		{
			PendingRemovals.Remove(InstanceId);
			return;
		}

		PendingRemoval = MakeShared<FPendingRemoval>(SourceModifier);
		RemoveSourceInstance(SourceModifier);
	}

	++PendingRemoval->PendingApplications;

	const FGuid PendingId = InstanceId;
	const TWeakObjectPtr<UAnimSequence> WeakAnimation(Animation);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[PendingId, PendingRemoval, WeakAnimation](float)
		{
			UIMGOneShotAnimationModifier* SourceModifier = PendingRemoval->SourceModifier.Get();
			if (UAnimSequence* AppliedAnimation = WeakAnimation.Get();
				SourceModifier != nullptr && AppliedAnimation != nullptr && SourceModifier->CanRevert(AppliedAnimation))
			{
				// OnRevert is deliberately empty: this removes Unreal's applied record only.
				SourceModifier->RevertFromAnimationSequence(AppliedAnimation);
			}

			--PendingRemoval->PendingApplications;
			if (PendingRemoval->PendingApplications <= 0)
			{
				PendingRemovals.Remove(PendingId);
			}

			return false;
		}));
}

void UIMGOneShotAnimationModifier::OnRevert_Implementation(UAnimSequence* Animation)
{
}
