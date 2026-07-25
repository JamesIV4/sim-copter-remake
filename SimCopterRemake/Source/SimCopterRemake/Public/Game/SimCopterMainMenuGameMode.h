// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SimCopterMainMenuGameMode.generated.h"

class SWidget;

// Game mode for the front-end map (`/Game/MainMenu`): no pawn, no city, just the main menu.
//
// The original ran its shell screens with the simulation stopped and only then loaded a city, so
// the remake keeps the menu in its own tiny level and travels to the city level once the player has
// chosen. The choice is handed over in USimCopterSessionSubsystem, which outlives the travel.
UCLASS()
class SIMCOPTERREMAKE_API ASimCopterMainMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASimCopterMainMenuGameMode();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// Console equivalents of the menu items, so the front end can also be driven headlessly.
	UFUNCTION(Exec)
	void SimNewCareer(int32 CareerCityIndex = 0);
	UFUNCTION(Exec)
	void SimNewUserGame(int32 CityIndex = 0);

private:
	TSharedPtr<SWidget> MainMenuWidget;

	void OpenMainMenu();
	void CloseMainMenu();
	// Travels to the city level with whatever the menu wrote into the session subsystem.
	void StartPendingSession();
	void QuitGame();
};
