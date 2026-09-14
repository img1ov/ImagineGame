#pragma once

#include "Engine/DeveloperSettings.h"

#include "IMGTextHotfixConfig.generated.h"

struct FPolyglotTextData;
struct FPropertyChangedEvent;

/** Allows individual localized text values to be replaced through config hotfixes. */
UCLASS(config=Game, defaultconfig)
class UIMGTextHotfixConfig : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UIMGTextHotfixConfig(const FObjectInitializer& ObjectInitializer);

	virtual void PostInitProperties() override;
	virtual void PostReloadConfig(FProperty* PropertyThatWasLoaded) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void ApplyTextReplacements() const;

	UPROPERTY(Config, EditAnywhere)
	TArray<FPolyglotTextData> TextReplacements;
};
