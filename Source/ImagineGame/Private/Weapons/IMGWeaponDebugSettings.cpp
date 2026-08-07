#include "Weapons/IMGWeaponDebugSettings.h"
#include "Misc/App.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGWeaponDebugSettings)

UIMGWeaponDebugSettings::UIMGWeaponDebugSettings()
{
}

FName UIMGWeaponDebugSettings::GetCategoryName() const
{
	return FApp::GetProjectName();
}

