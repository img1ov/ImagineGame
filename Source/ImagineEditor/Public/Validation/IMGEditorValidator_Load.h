
#pragma once

#include "Validation/IMGEditorValidator.h"

#include "IMGEditorValidator_Load.generated.h"

class FText;
class UObject;

UCLASS()
class UIMGEditorValidator_Load : public UIMGEditorValidator
{
	GENERATED_BODY()

public:
	UIMGEditorValidator_Load();

	virtual bool IsEnabled() const override;

	static bool GetLoadWarningsAndErrorsForPackage(const FString& PackageName, TArray<FString>& OutWarningsAndErrors);

protected:
	using Super::CanValidateAsset_Implementation; // -Woverloaded-virtual
	virtual bool CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InObject, FDataValidationContext& InContext) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& Context) override;
	
private:
	static TArray<FString> InMemoryReloadLogIgnoreList;
};
