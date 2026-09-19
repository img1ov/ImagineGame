
#pragma once

#include "Validation/IMGEditorValidator.h"

#include "IMGEditorValidator_MaterialFunctions.generated.h"

class FText;
class UObject;

UCLASS()
class UIMGEditorValidator_MaterialFunctions : public UIMGEditorValidator
{
	GENERATED_BODY()

public:
	UIMGEditorValidator_MaterialFunctions();

protected:
	using Super::CanValidateAsset_Implementation; // -Woverloaded-virtual
	virtual bool CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InObject, FDataValidationContext& InContext) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& Context) override;
};
