
#include "Inventory/IMGInventoryItemInstance.h"

#include "Inventory/IMGInventoryItemDefinition.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "Messages/IMGVerbMessage.h"
#include "NativeGameplayTags.h"
#include "Net/UnrealNetwork.h"

#include "Iris/ReplicationSystem/ReplicationFragmentUtil.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGInventoryItemInstance)

class FLifetimeProperty;

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_IMG_Inventory_Message_ItemStatsChanged, "IMG.Inventory.Message.ItemStatsChanged");

UIMGInventoryItemInstance::UIMGInventoryItemInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UIMGInventoryItemInstance::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	
	DOREPLIFETIME(ThisClass, StatTags);
	DOREPLIFETIME(ThisClass, ItemDef);
}

void UIMGInventoryItemInstance::RegisterReplicationFragments(UE::Net::FFragmentRegistrationContext& Context,
	UE::Net::EFragmentRegistrationFlags RegistrationFlags)
{
	using namespace UE::Net;
	
	// Build descriptors and allocate PropertyReplicationFragments for this object
	FReplicationFragmentUtil::CreateAndRegisterFragmentsForObject(this, Context, RegistrationFlags);
}

void UIMGInventoryItemInstance::AddStatTagStack(FGameplayTag Tag, int32 StackCount)
{
	const AActor* OwnerActor = GetTypedOuter<AActor>();
	if (!OwnerActor || !OwnerActor->HasAuthority() || StackCount < 1 || !Tag.IsValid())
	{
		return;
	}
	const int32 OldCount = StatTags.GetStackCount(Tag);
	StatTags.AddStack(Tag, StackCount);
	if (StatTags.GetStackCount(Tag) != OldCount)
	{
		BroadcastStatTagsChanged();
	}
}

void UIMGInventoryItemInstance::RemoveStatTagStack(FGameplayTag Tag, int32 StackCount)
{
	const AActor* OwnerActor = GetTypedOuter<AActor>();
	if (!OwnerActor || !OwnerActor->HasAuthority() || StackCount < 1 || !Tag.IsValid())
	{
		return;
	}
	const int32 OldCount = StatTags.GetStackCount(Tag);
	StatTags.RemoveStack(Tag, StackCount);
	if (StatTags.GetStackCount(Tag) != OldCount)
	{
		BroadcastStatTagsChanged();
	}
}

void UIMGInventoryItemInstance::OnRep_StatTags()
{
	BroadcastStatTagsChanged();
}

void UIMGInventoryItemInstance::BroadcastStatTagsChanged()
{
	if (AActor* OwnerActor = GetTypedOuter<AActor>(); OwnerActor && OwnerActor->GetWorld())
	{
		FIMGVerbMessage Message;
		Message.Verb = TAG_IMG_Inventory_Message_ItemStatsChanged;
		Message.Instigator = OwnerActor;
		Message.Target = this;
		UGameplayMessageSubsystem::Get(OwnerActor).BroadcastMessage(Message.Verb, Message);
	}
}

int32 UIMGInventoryItemInstance::GetStatTagStackCount(FGameplayTag Tag) const
{
	return StatTags.GetStackCount(Tag);
}

bool UIMGInventoryItemInstance::HasStatTag(FGameplayTag Tag) const
{
	return StatTags.ContainsTag(Tag);
}

void UIMGInventoryItemInstance::SetItemDef(TSubclassOf<UIMGInventoryItemDefinition> InDef)
{
	ItemDef = InDef;
}

const UIMGInventoryItemFragment* UIMGInventoryItemInstance::FindFragmentByClass(
	TSubclassOf<UIMGInventoryItemFragment> FragmentClass) const
{
	if ((ItemDef != nullptr) && (FragmentClass != nullptr))
	{
		return GetDefault<UIMGInventoryItemDefinition>(ItemDef)->FindFragmentByClass(FragmentClass);
	}

	return nullptr;
}
