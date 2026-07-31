// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SimCopterPlayerController.generated.h"

class SWidget;
class USimCopterHangarArt;
enum class ESimCopterSettingsItem : uint8;

/** Which of the Settings screen's pages is up. */
UENUM()
enum class ESimCopterSettingsScreen : uint8
{
	None,
	Menu,          // playmenu.bmp, control 0x7d3
	CitySettings,  // cityset.bmp,  0x7d8
	Graphics,      // render.bmp,   0x7d5
	Sound,         // sound.bmp,    0x7d6
	Controls,      // input.bmp,    0x7d4
	Message,       // MBox.bmp, one button
	Confirm,       // MBox.bmp, Yes/No
};

/**
 * SCHOOK: SettingsMenuRouter 0x0044c9e0 (app vtable +0x88)
 *
 * The city level's player controller, which owns the in-game Settings screen: it raises the page
 * the way app command 0x3f does, pauses the sim first through the same reference-counted pause
 * FUN_004346c0 implements, and routes the eight items exactly as FUN_0044c9e0 does.
 *
 * Decode with citations: Docs/scratchpad/settings-DECODED.md.
 */
UCLASS()
class SIMCOPTERREMAKE_API ASimCopterPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ASimCopterPlayerController();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;

	/** Console equivalent of the key, so the screen can be reached without a binding. */
	UFUNCTION(Exec)
	void SimSettings();

	bool IsSettingsOpen() const { return Screen != ESimCopterSettingsScreen::None; }

private:
	UPROPERTY(Transient)
	TObjectPtr<USimCopterHangarArt> Art;

	TSharedPtr<SWidget> ScreenWidget;
	ESimCopterSettingsScreen Screen = ESimCopterSettingsScreen::None;

	FText PendingMessage;

	/**
	 * FUN_004346c0's `app+0xbc`: the pause is reference counted, because opening a sub-dialog
	 * pauses again on the way in and its handler resumes on the way out.
	 */
	int32 PauseDepth = 0;

	void PushPause();
	void PopPause();

	void OpenSettings();
	void EnterScreen(ESimCopterSettingsScreen NewScreen);
	void CloseScreen();
	TSharedRef<SWidget> BuildScreen(ESimCopterSettingsScreen NewScreen);

	/** FUN_0044c9e0's switch, item for item. */
	void HandleSettingsItem(ESimCopterSettingsItem Item);

	/** Settings item 0 only exists in a user game, which is DAT_00518d50 == 1. */
	bool IsUserGame() const;

	void ShowMessage(const FText& Message);

	/** Settings item 6: back to the main menu, after the confirm on STRINGTABLE 11. */
	void LeaveCity();

	class ASimCopterMissionSystemActor* ResolveMissionSystem() const;

	/** Restores the input mode and cursor the pawn wants once the screen goes away. */
	void RestoreGameInput();

	static FString ResolveOriginalGameRoot();
};
