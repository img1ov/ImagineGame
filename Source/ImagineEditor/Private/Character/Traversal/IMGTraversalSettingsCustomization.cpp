#include "Character/Traversal/IMGTraversalSettingsCustomization.h"

#include "CoreMinimal.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailGroup.h"
#include "IPropertyTypeCustomization.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "UObject/UnrealType.h"

namespace IMGTraversalSettingsEditor
{
namespace
{
const FName SettingsTypes[] = {TEXT("IMGTraversalInitiationSettings"), TEXT("IMGTraversalDetectorSettings"),
    TEXT("IMGTraversalExecutionSettings"), TEXT("IMGTraversalLandingSettings")};

class FSettingsCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShared<FSettingsCustomization>();
	}

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow,
	    IPropertyTypeCustomizationUtils& CustomizationUtils) override
	{
		HeaderRow.NameContent()[PropertyHandle->CreatePropertyNameWidget()];
		HeaderRow.ValueContent()[PropertyHandle->CreatePropertyValueWidget()];
	}

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder,
	    IPropertyTypeCustomizationUtils& CustomizationUtils) override
	{
		// Struct children do not create category nodes by default; keep native rows inside metadata-driven groups.
		TMap<FString, IDetailGroup*> Groups;
		uint32 ChildCount = 0;
		PropertyHandle->GetNumChildren(ChildCount);
		for (uint32 ChildIndex = 0; ChildIndex < ChildCount; ++ChildIndex)
		{
			const TSharedPtr<IPropertyHandle> Child = PropertyHandle->GetChildHandle(ChildIndex);
			if (!Child.IsValid() || !Child->GetProperty())
			{
				continue;
			}
			TArray<FString> CategoryParts;
			Child->GetProperty()->GetMetaData(TEXT("Category")).ParseIntoArray(CategoryParts, TEXT("|"), true);
			IDetailGroup* Group = nullptr;
			FString GroupPath;
			for (const FString& Part : CategoryParts)
			{
				if (!GroupPath.IsEmpty())
				{
					GroupPath += TEXT("|");
				}
				GroupPath += Part;
				IDetailGroup*& Existing = Groups.FindOrAdd(GroupPath);
				if (!Existing)
				{
					const FName GroupName(*GroupPath);
					const FText Label = FText::FromString(Part);
					Existing = Group ? &Group->AddGroup(GroupName, Label) : &ChildBuilder.AddGroup(GroupName, Label);
				}
				Group = Existing;
			}
			if (Group)
			{
				Group->AddPropertyRow(Child.ToSharedRef());
			}
			else
			{
				ChildBuilder.AddProperty(Child.ToSharedRef());
			}
		}
	}
};
}

void Startup()
{
	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	for (FName Type : SettingsTypes)
	{
		PropertyEditor.RegisterCustomPropertyTypeLayout(
		    Type, FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FSettingsCustomization::MakeInstance));
	}
	PropertyEditor.NotifyCustomizationModuleChanged();
}

void Shutdown()
{
	if (FPropertyEditorModule* PropertyEditor = FModuleManager::GetModulePtr<FPropertyEditorModule>(TEXT("PropertyEditor")))
	{
		for (FName Type : SettingsTypes)
		{
			PropertyEditor->UnregisterCustomPropertyTypeLayout(Type);
		}
		PropertyEditor->NotifyCustomizationModuleChanged();
	}
}
}
