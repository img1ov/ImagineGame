#pragma once

#include "Containers/Ticker.h"
#include "DataSource/GameSettingDataSourceDynamic.h" // IWYU pragma: keep
#include "GameSettingRegistry.h"
#include "Settings/IMGSettingsLocal.h" // IWYU pragma: keep

#include "IMGGameSettingRegistry.generated.h"

class ULocalPlayer;
class UObject;

//--------------------------------------
// UIMGGameSettingRegistry
//--------------------------------------

class UGameSettingCollection;
class UIMGLocalPlayer;

DECLARE_LOG_CATEGORY_EXTERN(LogIMGGameSettingRegistry, Log, Log);

#define GET_SHARED_SETTINGS_FUNCTION_PATH(FunctionOrPropertyName)							\
	MakeShared<FGameSettingDataSourceDynamic>(TArray<FString>({								\
		GET_FUNCTION_NAME_STRING_CHECKED(UIMGLocalPlayer, GetSharedSettings),				\
		GET_FUNCTION_NAME_STRING_CHECKED(UIMGSettingsShared, FunctionOrPropertyName)		\
	}))

#define GET_LOCAL_SETTINGS_FUNCTION_PATH(FunctionOrPropertyName)							\
	MakeShared<FGameSettingDataSourceDynamic>(TArray<FString>({								\
		GET_FUNCTION_NAME_STRING_CHECKED(UIMGLocalPlayer, GetLocalSettings),				\
		GET_FUNCTION_NAME_STRING_CHECKED(UIMGSettingsLocal, FunctionOrPropertyName)		\
	}))

/**
 * 
 */
UCLASS()
class UIMGGameSettingRegistry : public UGameSettingRegistry
{
	GENERATED_BODY()

public:
	UIMGGameSettingRegistry();

	static UIMGGameSettingRegistry* Get(UIMGLocalPlayer* InLocalPlayer);
	
	virtual void SaveChanges() override;

protected:
	virtual void OnInitialize(ULocalPlayer* InLocalPlayer) override;
	
	
	virtual bool IsFinishedInitializing() const override;
	
	// TODO: GameSettingRegistry
	UGameSettingCollection* InitializeVideoSettings(UIMGLocalPlayer* InLocalPlayer) { return nullptr;}
	void InitializeVideoSettings_FrameRates(UGameSettingCollection* Screen, UIMGLocalPlayer* InLocalPlayer) {}
	void AddPerformanceStatPage(UGameSettingCollection* Screen, UIMGLocalPlayer* InLocalPlayer) {}

	UGameSettingCollection* InitializeAudioSettings(UIMGLocalPlayer* InLocalPlayer) { return nullptr;}
	UGameSettingCollection* InitializeGameplaySettings(UIMGLocalPlayer* InLocalPlayer) { return nullptr;}

	UGameSettingCollection* InitializeMouseAndKeyboardSettings(UIMGLocalPlayer* InLocalPlayer) { return nullptr;}
	UGameSettingCollection* InitializeGamepadSettings(UIMGLocalPlayer* InLocalPlayer) { return nullptr;}

	void AddDLCPage(UGameSettingCollection* Screen, UIMGLocalPlayer* InLocalPlayer) {}
	
	UPROPERTY()
	TObjectPtr<UGameSettingCollection> VideoSettings;

	UPROPERTY()
	TObjectPtr<UGameSettingCollection> AudioSettings;

	UPROPERTY()
	TObjectPtr<UGameSettingCollection> GameplaySettings;

	UPROPERTY()
	TObjectPtr<UGameSettingCollection> MouseAndKeyboardSettings;

	UPROPERTY()
	TObjectPtr<UGameSettingCollection> GamepadSettings;

	FTSTicker::FDelegateHandle DLCTickHandle;
};
