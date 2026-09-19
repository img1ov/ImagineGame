
#include "AssetTypeActions_IMGContextEffectsLibrary.h"

#include "Feedback/ContextEffects/IMGContextEffectsLibrary.h"

class UClass;

#define LOCTEXT_NAMESPACE "AssetTypeActions"

UClass* FAssetTypeActions_IMGContextEffectsLibrary::GetSupportedClass() const
{
	return UIMGContextEffectsLibrary::StaticClass();
}

#undef LOCTEXT_NAMESPACE
