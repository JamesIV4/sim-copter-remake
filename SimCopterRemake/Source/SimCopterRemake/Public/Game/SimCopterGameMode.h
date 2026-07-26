// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SimCopterGameMode.generated.h"

class ASimCopterTrafficSystemActor;
class ASimCopterMissionSystemActor;
class ASimCopterHangar;

// Game mode for the city level. Opens the session the main menu asked for
// (USimCopterSessionSubsystem) once the city and mission actors exist.
UCLASS()
class SIMCOPTERREMAKE_API ASimCopterGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASimCopterGameMode();

	virtual void BeginPlay() override;

	// The Settings panel's "Leave City" (help/English/38ref.htm): back to the main menu.
	UFUNCTION(Exec)
	void SimMainMenu();

	// Session commands, so a city entered directly (PIE, -game with no menu) can still be put into
	// any session without clicking. CareerCityIndex supplies the tuning record.
	UFUNCTION(Exec)
	void SimFreeRoam(int32 CareerCityIndex = 0);
	UFUNCTION(Exec)
	void SimCityJobs(int32 CareerCityIndex = 0);
	// MissionIndex is the main menu's mission list order; -1 lists it to the log.
	UFUNCTION(Exec)
	void SimLoadMission(int32 MissionIndex = -1, int32 CareerCityIndex = 0);

protected:
	UPROPERTY(EditDefaultsOnly, Category = "SimCopter|Population")
	bool bSpawnTrafficSystem = true;

	UPROPERTY(EditDefaultsOnly, Category = "SimCopter|Missions")
	bool bSpawnMissionSystem = true;

	UPROPERTY(EditDefaultsOnly, Category = "SimCopter|Missions")
	TSubclassOf<class ASimCopterMissionSystemActor> MissionSystemClass;

	UPROPERTY(EditDefaultsOnly, Category = "SimCopter|Population")
	TSubclassOf<ASimCopterTrafficSystemActor> TrafficSystemClass;

	// The player's hangar. It is stood beside the airport with its doors turned to face wherever
	// the session puts the player, so it is the first building they see on foot.
	UPROPERTY(EditDefaultsOnly, Category = "SimCopter|Hangar")
	bool bSpawnHangar = true;

	UPROPERTY(EditDefaultsOnly, Category = "SimCopter|Hangar")
	TSubclassOf<ASimCopterHangar> HangarClass;

private:
	TWeakObjectPtr<ASimCopterMissionSystemActor> MissionSystemActor;

	// Caches (and, when needed, finds) the map's mission system actor; logs and returns null when
	// the map has none.
	ASimCopterMissionSystemActor* ResolveMissionSystemActor();

	// Applies the main menu's choice: the city's scheduled jobs, plus any mission the menu asked to
	// start straight away. Does nothing when the level was entered without going through the menu,
	// which leaves the mission actor's own default session in charge.
	void ApplyPendingSession();

	// SCHOOK: SessionPlaceHelicopters 0x0047a240
	// Puts every helicopter in the level on a free airport helipad and stands the player next to
	// the one they will fly, the way city entry does. Runs a tick after BeginPlay because the
	// traffic system builds the city grid - and with it the airport - in its own BeginPlay,
	// which the engine dispatches after the game mode's.
	void PlaceSessionOnAirportPads();

	// Stands the hangar beside the airport, doors toward PlayerStandLocation. Called from
	// PlaceSessionOnAirportPads once the pads are known.
	void PlaceHangar(ASimCopterTrafficSystemActor* Traffic, const FVector& PlayerStandLocation);
};
