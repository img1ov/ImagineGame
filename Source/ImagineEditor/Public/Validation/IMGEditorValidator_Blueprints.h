
#pragma once

#include "Validation/IMGEditorValidator.h"

#include "IMGEditorValidator_Blueprints.generated.h"

class FText;
class UObject;

UCLASS()
class UIMGEditorValidator_Blueprints : public UIMGEditorValidator
{
	GENERATED_BODY()

public:
	UIMGEditorValidator_Blueprints();

protected:
	using Super::CanValidateAsset_Implementation; // -Woverloaded-virtual
	virtual bool CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InObject, FDataValidationContext& InContext) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& Context) override;
};
