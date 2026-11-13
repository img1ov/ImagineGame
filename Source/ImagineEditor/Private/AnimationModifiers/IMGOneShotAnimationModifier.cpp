#include "AnimationModifiers/IMGOneShotAnimationModifier.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AnimationModifiersAssetUserData.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "UObject/StrongObjectPtr.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGOneShotAnimationModifier)

namespace IMG::AnimationModifiers
{
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

	UIMGOneShotAnimationModifier* SourceModifier = FindSourceModifier(this, Animation, InstanceId);
	if (SourceModifier == nullptr)
	{
		return;
	}

	const TWeakObjectPtr<UAnimSequence> WeakAnimation(Animation);
	const TWeakObjectPtr<UIMGOneShotAnimationModifier> WeakAppliedModifier(this);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[StrongSourceModifier = TStrongObjectPtr<UIMGOneShotAnimationModifier>(SourceModifier),
			WeakAnimation, WeakAppliedModifier](float)
		{
			// A modal apply dialog can tick the editor while a modifier list is still being traversed.
			if (UE::Anim::FApplyModifiersScope::IsScopePending()
				|| (GEditor != nullptr && GEditor->Trans != nullptr && GEditor->Trans->IsActive()))
			{
				return true;
			}

			UIMGOneShotAnimationModifier* SourceModifier = StrongSourceModifier.Get();
			if (UAnimSequence* AppliedAnimation = WeakAnimation.Get();
				IsValid(SourceModifier) && AppliedAnimation != nullptr && WeakAppliedModifier.IsValid()
				&& SourceModifier->CanRevert(AppliedAnimation))
			{
				UE::Anim::FApplyModifiersScope Scope;
				RemoveSourceInstance(SourceModifier);
				// OnRevert is deliberately empty: this removes Unreal's applied record only.
				SourceModifier->RevertFromAnimationSequence(AppliedAnimation);
			}

			return false;
		}));
}

void UIMGOneShotAnimationModifier::OnRevert_Implementation(UAnimSequence* Animation)
{
}
