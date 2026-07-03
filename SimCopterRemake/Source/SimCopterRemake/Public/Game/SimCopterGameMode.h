// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SimCopterGameMode.generated.h"

class ASimCopterTrafficSystemActor;

UCLASS()
class SIMCOPTERREMAKE_API ASimCopterGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASimCopterGameMode();

	virtual void BeginPlay() override;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "SimCopter|Population")
	bool bSpawnTrafficSystem = true;

	UPROPERTY(EditDefaultsOnly, Category = "SimCopter|Missions")
	bool bSpawnMissionSystem = true;

	UPROPERTY(EditDefaultsOnly, Category = "SimCopter|Missions")
	TSubclassOf<class ASimCopterMissionSystemActor> MissionSystemClass;

	UPROPERTY(EditDefaultsOnly, Category = "SimCopter|Population")
	TSubclassOf<ASimCopterTrafficSystemActor> TrafficSystemClass;
};
