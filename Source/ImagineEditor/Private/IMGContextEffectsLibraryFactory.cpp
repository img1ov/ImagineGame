
#include "IMGContextEffectsLibraryFactory.h"

#include "Feedback/ContextEffects/IMGContextEffectsLibrary.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMGContextEffectsLibraryFactory)

class FFeedbackContext;
class UClass;
class UObject;

UIMGContextEffectsLibraryFactory::UIMGContextEffectsLibraryFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UIMGContextEffectsLibrary::StaticClass();

	bCreateNew = true;
	bEditorImport = false;
	bEditAfterNew = true;
}

UObject* UIMGContextEffectsLibraryFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UIMGContextEffectsLibrary* IMGContextEffectsLibrary = NewObject<UIMGContextEffectsLibrary>(InParent, Name, Flags);

	return IMGContextEffectsLibrary;
}
