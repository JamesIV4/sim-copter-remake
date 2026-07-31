// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Game/SimCopterSessionSubsystem.h"
#include "GameFramework/GameModeBase.h"
#include "SimCopterMainMenuGameMode.generated.h"

class SWidget;
class USimCopterHangarArt;
enum class ESimCopterMainMenuItem : uint8;

// Which front-end screen is up. These are FUN_00449cb0's own screen numbers for the two the
// remake has - the main menu is state 4 and the career city select is state 5 - plus the two the
// remake substitutes for dialogs the original handed to Windows.
UENUM()
enum class ESimCopterFrontEndScreen : uint8
{
	None,
	MainMenu,       // state 4, page 0x7d2, main1.bmp
	CareerSelect,   // state 5, page 0x7d7, career.bmp
	UserCityPicker, // stands in for FUN_00406400's GetOpenFileName
	SavedGamePicker,// remake SaveGame slots, filtered to the original career/user split
	Message,        // MBox.bmp
};

// Game mode for the front-end map (`/Game/MainMenu`): no pawn, no city, just the shell.
//
// The original ran its shell screens with the simulation stopped and only then loaded a city, so
// the remake keeps them in their own tiny level and travels to the city level once the player has
// chosen. The choice is handed over in USimCopterSessionSubsystem, which outlives the travel.
//
// This class is the remake's FUN_00449cb0 / FUN_0044c710 pair: it owns the screen the shell is on
// and answers the menu's five items exactly as the original's handler does. Decode:
// Docs/scratchpad/mainmenu-DECODED.md.
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
	UFUNCTION(Exec)
	void SimLoadGame(const FString& SlotName);

private:
	// The original's artwork, shared by every front-end screen.
	UPROPERTY(Transient)
	TObjectPtr<USimCopterHangarArt> Art = nullptr;

	TSharedPtr<SWidget> ScreenWidget;
	ESimCopterFrontEndScreen Screen = ESimCopterFrontEndScreen::None;
	ESimCopterSessionKind PendingSaveKind = ESimCopterSessionKind::None;

	// SCHOOK: EnterState 0x00449cb0 - tear the current screen down and put the next one up.
	void EnterScreen(ESimCopterFrontEndScreen NewScreen);
	void CloseScreen();
	TSharedRef<SWidget> BuildScreen(ESimCopterFrontEndScreen NewScreen);

	// SCHOOK: MainMenuCommand 0x0044c710 - what each of the five items does.
	void HandleMainMenuItem(ESimCopterMainMenuItem Item);

	void HandleCareerCityChosen(int32 CareerCityIndex);
	void HandleUserCityChosen(const FString& CityFilePath);
	void HandleSavedGameChosen(const FString& SlotName);

	// Travels to the city level with whatever the shell wrote into the session subsystem.
	void StartPendingSession();
	void QuitGame();

	FString ResolveOriginalGameRoot() const;

	// Raised for the two items the remake cannot honour yet; the original's own demo build put the
	// same refusal in the same box (STRINGTABLE 653).
	FText PendingMessage;
};
