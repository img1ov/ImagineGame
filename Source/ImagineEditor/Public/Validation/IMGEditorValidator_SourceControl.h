
#pragma once

#include "Validation/IMGEditorValidator.h"

#include "IMGEditorValidator_SourceControl.generated.h"

class FText;
class UObject;

UCLASS()
class UIMGEditorValidator_SourceControl : public UIMGEditorValidator
{
	GENERATED_BODY()

public:
	UIMGEditorValidator_SourceControl();

protected:
	using Super::CanValidateAsset_Implementation; // -Woverloaded-virtual
	virtual bool CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InObject, FDataValidationContext& InContext) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& Context) override;
};
