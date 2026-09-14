#include "Hotfix/IMGRuntimeOptions.h"

#include "UObject/Class.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGRuntimeOptions)

UIMGRuntimeOptions::UIMGRuntimeOptions()
{
	OptionCommandPrefix = TEXT("ro");
}

UIMGRuntimeOptions* UIMGRuntimeOptions::GetRuntimeOptions()
{
	return GetMutableDefault<UIMGRuntimeOptions>();
}

const UIMGRuntimeOptions& UIMGRuntimeOptions::Get()
{
	return *GetDefault<UIMGRuntimeOptions>();
}
