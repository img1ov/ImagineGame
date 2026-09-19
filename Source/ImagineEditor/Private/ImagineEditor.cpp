
#include "ImagineEditor.h"

#include "AbilitySystemGlobals.h"
#include "DataValidationModule.h"
#include "Development/IMGDeveloperSettings.h"
#include "Editor/UnrealEdEngine.h"
#include "Engine/GameInstance.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ImagineEditorStyle.h"
#include "GameModes/IMGExperienceManager.h"
#include "GameplayAbilitiesEditorModule.h"
#include "GameplayCueInterface.h"
#include "GameplayCueNotify_Burst.h"
#include "GameplayCueNotify_BurstLatent.h"
#include "GameplayCueNotify_Looping.h"
#include "AssetTypeActions_IMGContextEffectsLibrary.h"
#include "ToolMenu.h"
#include "ToolMenus.h"
#include "UObject/UObjectIterator.h"
#include "UnrealEdGlobals.h"
#include "Validation/IMGEditorValidator.h"

class SWidget;

#define LOCTEXT_NAMESPACE "ImagineEditor"

DEFINE_LOG_CATEGORY(LogImagineEditor);

// This function tells the GameplayCue editor what classes to expose when creating new notifies.
static void GetGameplayCueDefaultClasses(TArray<UClass*>& Classes)
{
	Classes.Empty();
	Classes.Add(UGameplayCueNotify_Burst::StaticClass());
	Classes.Add(AGameplayCueNotify_BurstLatent::StaticClass());
	Classes.Add(AGameplayCueNotify_Looping::StaticClass());
}

// This function tells the GameplayCue editor what classes to search for GameplayCue events.
static void GetGameplayCueInterfaceClasses(TArray<UClass*>& Classes)
{
	Classes.Empty();

	for (UClass* Class : TObjectRange<UClass>())
	{
		if (Class->IsChildOf<AActor>() && Class->ImplementsInterface(UGameplayCueInterface::StaticClass()))
		{
			Classes.Add(Class);
		}
	}
}

// This function tells the GameplayCue editor where to create the GameplayCue notifies based on their tag.
static FString GetGameplayCuePath(FString GameplayCueTag)
{
	FString Path = FString(TEXT("/Game"));

	//@TODO: Try plugins (e.g., GameplayCue.ShooterGame.Foo should be in ShooterCore or something)

	// Default path to the first entry in the UAbilitySystemGlobals::GameplayCueNotifyPaths.
	if (IGameplayAbilitiesModule::IsAvailable())
	{
		IGameplayAbilitiesModule& GameplayAbilitiesModule = IGameplayAbilitiesModule::Get();

		if (GameplayAbilitiesModule.IsAbilitySystemGlobalsAvailable())
		{
			UAbilitySystemGlobals* AbilitySystemGlobals = GameplayAbilitiesModule.GetAbilitySystemGlobals();
			check(AbilitySystemGlobals);

			TArray<FString> GetGameplayCueNotifyPaths = AbilitySystemGlobals->GetGameplayCueNotifyPaths();

			if (GetGameplayCueNotifyPaths.Num() > 0)
			{
				Path = GetGameplayCueNotifyPaths[0];
			}
		}
	}

	GameplayCueTag.RemoveFromStart(TEXT("GameplayCue."));

	FString NewDefaultPathName = FString::Printf(TEXT("%s/GCN_%s"), *Path, *GameplayCueTag);

	return NewDefaultPathName;
}

static bool HasPlayWorld()
{
	return GEditor->PlayWorld != nullptr;
}

static bool HasNoPlayWorld()
{
	return !HasPlayWorld();
}

static bool HasPlayWorldAndRunning()
{
	return HasPlayWorld() && !GUnrealEd->PlayWorld->bDebugPauseExecution;
}

static void OpenCommonMap_Clicked(const FString MapPath)
{
	if (ensure(MapPath.Len()))
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(MapPath);
	}
}

static bool CanShowCommonMaps()
{
	return HasNoPlayWorld() && !GetDefault<UIMGDeveloperSettings>()->CommonEditorMaps.IsEmpty();
}

static TSharedRef<SWidget> GetCommonMapsDropdown()
{
	FMenuBuilder MenuBuilder(true, nullptr);
	
	for (const FSoftObjectPath& Path : GetDefault<UIMGDeveloperSettings>()->CommonEditorMaps)
	{
		if (!Path.IsValid())
		{
			continue;
		}
		
		const FText DisplayName = FText::FromString(Path.GetAssetName());
		MenuBuilder.AddMenuEntry(
			DisplayName,
			LOCTEXT("CommonPathDescription", "Opens this map in the editor"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateStatic(&OpenCommonMap_Clicked, Path.ToString()),
				FCanExecuteAction::CreateStatic(&HasNoPlayWorld),
				FIsActionChecked(),
				FIsActionButtonVisible::CreateStatic(&HasNoPlayWorld)
			)
		);
	}

	return MenuBuilder.MakeWidget();
}

static void CheckGameContent_Clicked()
{
	UIMGEditorValidator::ValidateCheckedOutContent(/*bInteractive=*/true, EDataValidationUsecase::Manual);
}

static void RegisterGameEditorMenus()
{
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
	FToolMenuSection& Section = Menu->AddSection("PlayGameExtensions", TAttribute<FText>(), FToolMenuInsert("Play", EToolMenuInsertType::After));

	// Uncomment this to add a custom toolbar that is displayed during PIE
	// Useful for making easy access to changing game state artificially, adding cheats, etc
	// FToolMenuEntry BlueprintEntry = FToolMenuEntry::InitComboButton(
	// 	"OpenGameMenu",
	// 	FUIAction(
	// 		FExecuteAction(),
	// 		FCanExecuteAction::CreateStatic(&HasPlayWorld),
	// 		FIsActionChecked(),
	// 		FIsActionButtonVisible::CreateStatic(&HasPlayWorld)),
	// 	FOnGetContent::CreateStatic(&YourCustomMenu),
	// 	LOCTEXT("GameOptions_Label", "Game Options"),
	// 	LOCTEXT("GameOptions_ToolTip", "Game Options"),
	// 	FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.OpenLevelBlueprint")
	// );
	// BlueprintEntry.StyleNameOverride = "CalloutToolbar";
	// Section.AddEntry(BlueprintEntry);

	FToolMenuEntry CheckContentEntry = FToolMenuEntry::InitToolBarButton(
		"CheckContent",
		FUIAction(
			FExecuteAction::CreateStatic(&CheckGameContent_Clicked),
			FCanExecuteAction::CreateStatic(&HasNoPlayWorld),
			FIsActionChecked(),
			FIsActionButtonVisible::CreateStatic(&HasNoPlayWorld)),
		LOCTEXT("CheckContentButton", "Check Content"),
		LOCTEXT("CheckContentDescription", "Runs the Content Validation job on all checked out assets to look for warnings and errors"),
		FSlateIcon(FImagineEditorStyle::GetStyleSetName(), "ImagineEditor.CheckContent")
	);
	CheckContentEntry.StyleNameOverride = "CalloutToolbar";
	Section.AddEntry(CheckContentEntry);

	FToolMenuEntry CommonMapEntry = FToolMenuEntry::InitComboButton(
		"CommonMapOptions",
		FUIAction(
			FExecuteAction(),
			FCanExecuteAction::CreateStatic(&HasNoPlayWorld),
			FIsActionChecked(),
			FIsActionButtonVisible::CreateStatic(&CanShowCommonMaps)),
		FOnGetContent::CreateStatic(&GetCommonMapsDropdown),
		LOCTEXT("CommonMaps_Label", "Common Maps"),
		LOCTEXT("CommonMaps_ToolTip", "Some commonly desired maps while using the editor"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Level")
	);
	CommonMapEntry.StyleNameOverride = "CalloutToolbar";
	Section.AddEntry(CommonMapEntry);
}

/**
 * FImagineEditorModule
 */
class FImagineEditorModule : public FDefaultGameModuleImpl
{
	typedef FImagineEditorModule ThisClass;

	virtual void StartupModule() override
	{
		FImagineEditorStyle::Initialize();

		if (!IsRunningGame())
		{
			FModuleManager::Get().OnModulesChanged().AddRaw(this, &FImagineEditorModule::ModulesChangedCallback);

			BindGameplayAbilitiesEditorDelegates();

			if (FSlateApplication::IsInitialized())
			{
				ToolMenusHandle = UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateStatic(&RegisterGameEditorMenus));
			}

			FEditorDelegates::BeginPIE.AddRaw(this, &ThisClass::OnBeginPIE);
			FEditorDelegates::EndPIE.AddRaw(this, &ThisClass::OnEndPIE);
		}

		// Register the Context Effects Library asset type actions.
		{
			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
			TSharedRef<FAssetTypeActions_IMGContextEffectsLibrary> AssetAction = MakeShared<FAssetTypeActions_IMGContextEffectsLibrary>();
			IMGContextEffectsLibraryAssetAction = AssetAction;
			AssetTools.RegisterAssetTypeActions(AssetAction);
		}
	}

	void OnBeginPIE(bool bIsSimulating)
	{
		UIMGExperienceManager* ExperienceManager = GEngine->GetEngineSubsystem<UIMGExperienceManager>();
		check(ExperienceManager);
		ExperienceManager->OnPlayInEditorBegun();
	}

	void OnEndPIE(bool bIsSimulating)
	{
	}

	virtual void ShutdownModule() override
	{
		// Unregister the Context Effects Library asset type actions.
		{
			FAssetToolsModule* AssetToolsModule = FModuleManager::GetModulePtr<FAssetToolsModule>("AssetTools");
			TSharedPtr<IAssetTypeActions> AssetAction = IMGContextEffectsLibraryAssetAction.Pin();
			if (AssetToolsModule && AssetAction)
			{
				AssetToolsModule->Get().UnregisterAssetTypeActions(AssetAction.ToSharedRef());
			}
		}

		FEditorDelegates::BeginPIE.RemoveAll(this);
		FEditorDelegates::EndPIE.RemoveAll(this);

		// Undo UToolMenus
		if (UObjectInitialized() && ToolMenusHandle.IsValid())
		{
			UToolMenus::UnRegisterStartupCallback(ToolMenusHandle);
		}

		UnbindGameplayAbilitiesEditorDelegates();
		FModuleManager::Get().OnModulesChanged().RemoveAll(this);
		FImagineEditorStyle::Shutdown();
	}

protected:

	static void BindGameplayAbilitiesEditorDelegates()
	{
		IGameplayAbilitiesEditorModule& GameplayAbilitiesEditorModule = IGameplayAbilitiesEditorModule::Get();

		GameplayAbilitiesEditorModule.GetGameplayCueNotifyClassesDelegate().BindStatic(&GetGameplayCueDefaultClasses);
		GameplayAbilitiesEditorModule.GetGameplayCueInterfaceClassesDelegate().BindStatic(&GetGameplayCueInterfaceClasses);
		GameplayAbilitiesEditorModule.GetGameplayCueNotifyPathDelegate().BindStatic(&GetGameplayCuePath);
	}

	static void UnbindGameplayAbilitiesEditorDelegates()
	{
		if (IGameplayAbilitiesEditorModule::IsAvailable())
		{
			IGameplayAbilitiesEditorModule& GameplayAbilitiesEditorModule = IGameplayAbilitiesEditorModule::Get();
			GameplayAbilitiesEditorModule.GetGameplayCueNotifyClassesDelegate().Unbind();
			GameplayAbilitiesEditorModule.GetGameplayCueInterfaceClassesDelegate().Unbind();
			GameplayAbilitiesEditorModule.GetGameplayCueNotifyPathDelegate().Unbind();
		}
	}

	void ModulesChangedCallback(FName ModuleThatChanged, EModuleChangeReason ReasonForChange)
	{
		if ((ReasonForChange == EModuleChangeReason::ModuleLoaded) && (ModuleThatChanged.ToString() == TEXT("GameplayAbilitiesEditor")))
		{
			BindGameplayAbilitiesEditorDelegates();
		}
	}

private:
	TWeakPtr<IAssetTypeActions> IMGContextEffectsLibraryAssetAction;
	FDelegateHandle ToolMenusHandle;
};

IMPLEMENT_MODULE(FImagineEditorModule, ImagineEditor);

#undef LOCTEXT_NAMESPACE
