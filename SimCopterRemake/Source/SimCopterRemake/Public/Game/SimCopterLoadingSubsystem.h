#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SimCopterLoadingSubsystem.generated.h"

class SWidget;

// Owns the original loader across map travel, until the startup camera is ready.
UCLASS()
class SIMCOPTERREMAKE_API USimCopterLoadingSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	void Show();
	void Finish();
	static void SetStage(const UObject* WorldContext, int32 Stage);
private:
	void BeforeMapLoad(const FString& MapName);
	void AfterMapLoad(UWorld* World);
	TSharedPtr<SWidget> LoadingWidget;
	bool bPlayingMovie = false;
	bool bInViewport = false;
};
