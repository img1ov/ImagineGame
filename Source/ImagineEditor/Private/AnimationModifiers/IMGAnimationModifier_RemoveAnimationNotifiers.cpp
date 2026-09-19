#include "AnimationModifiers/IMGAnimationModifier_RemoveAnimationNotifiers.h"

#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGAnimationModifier_RemoveAnimationNotifiers)

namespace IMG::AnimationModifiers
{
	bool MatchesNotifierNameIgnoreCase(const FName ExistingName, const TArray<FName>& RequestedNames)
	{
		const FString ExistingString = ExistingName.ToString();
		return RequestedNames.ContainsByPredicate([&ExistingString](const FName RequestedName)
		{
			return !RequestedName.IsNone()
				&& ExistingString.Equals(RequestedName.ToString(), ESearchCase::IgnoreCase);
		});
	}

	template <typename TNotifier>
	bool MatchesNotifierClass(const TNotifier* Notifier, const TArray<TSubclassOf<TNotifier>>& RequestedClasses)
	{
		return Notifier != nullptr && RequestedClasses.ContainsByPredicate([Notifier](const TSubclassOf<TNotifier> RequestedClass)
		{
			return RequestedClass != nullptr && Notifier->IsA(RequestedClass);
		});
	}
}

void UIMGAnimationModifier_RemoveAnimationNotifiers::OnApply_Implementation(UAnimSequence* Animation)
{
	if (Animation == nullptr)
	{
		return;
	}

	auto ShouldRemoveNotifier = [this](const FAnimNotifyEvent& NotifyEvent)
	{
		return bRemoveAllNotifiers
			|| IMG::AnimationModifiers::MatchesNotifierClass(NotifyEvent.Notify.Get(), AnimationNotifyClasses)
			|| IMG::AnimationModifiers::MatchesNotifierClass(NotifyEvent.NotifyStateClass.Get(), AnimationNotifyStateClasses)
			|| IMG::AnimationModifiers::MatchesNotifierNameIgnoreCase(NotifyEvent.NotifyName, NotifierNames);
	};

	Animation->Modify();
	const int32 RemovedNotifierCount = Animation->Notifies.RemoveAll(ShouldRemoveNotifier);

	// Tracks are removed from back to front so the remaining notifier and sync-marker indices stay valid.
	TArray<int32> EmptyTrackIndices;
	for (int32 TrackIndex = 0; TrackIndex < Animation->AnimNotifyTracks.Num(); ++TrackIndex)
	{
		const bool bHasNotifier = Animation->Notifies.ContainsByPredicate([TrackIndex](const FAnimNotifyEvent& NotifyEvent)
		{
			return NotifyEvent.TrackIndex == TrackIndex;
		});

		if (!bHasNotifier)
		{
			EmptyTrackIndices.Add(TrackIndex);
		}
	}

	for (int32 EmptyTrackArrayIndex = EmptyTrackIndices.Num() - 1; EmptyTrackArrayIndex >= 0; --EmptyTrackArrayIndex)
	{
		const int32 TrackIndexToRemove = EmptyTrackIndices[EmptyTrackArrayIndex];

		for (FAnimNotifyEvent& NotifyEvent : Animation->Notifies)
		{
			if (NotifyEvent.TrackIndex > TrackIndexToRemove)
			{
				--NotifyEvent.TrackIndex;
			}
		}

		Animation->AuthoredSyncMarkers.RemoveAll([TrackIndexToRemove](const FAnimSyncMarker& SyncMarker)
		{
			return SyncMarker.TrackIndex == TrackIndexToRemove;
		});

		for (FAnimSyncMarker& SyncMarker : Animation->AuthoredSyncMarkers)
		{
			if (SyncMarker.TrackIndex > TrackIndexToRemove)
			{
				--SyncMarker.TrackIndex;
			}
		}

		Animation->AnimNotifyTracks.RemoveAt(TrackIndexToRemove);
	}

	if (RemovedNotifierCount > 0 || !EmptyTrackIndices.IsEmpty())
	{
		Animation->MarkPackageDirty();
		Animation->RefreshCacheData();
	}

	RemoveAfterApply(Animation);
}
