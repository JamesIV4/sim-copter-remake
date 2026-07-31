// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Missions/SimCopterMissionSystem.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class USimCopterSessionSubsystem;
class SVerticalBox;

// SimCopter's main menu. First pass: the original's item set with placeholder styling.
//
// The original panel (documented by the game's own help, help/English/37ref.htm, and drawn from
// main1.bmp..main5.bmp over the menusky.smk backdrop) has exactly five items:
//
//   New Career Game  - choose one of three cities, fly through it, start at the helipad
//   Open Career Game - resume a saved career
//   New User Game    - file dialog for any SimCity 2000 city; start at its airport
//   Open User Game   - resume a saved user city
//   Quit             - leave the game
//
// The two "Open" items need a save system, which the remake does not have yet, so they are shown
// disabled rather than faked. The city choice offered by New Career Game is the original's own
// first successor trio {City0, City1, City2} (FUN_00407f30). Everything below the divider is a
// first-pass extra with no original equivalent, marked as such in the panel.
class SSimCopterMainMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterMainMenu) {}
		SLATE_ARGUMENT(TWeakObjectPtr<USimCopterSessionSubsystem>, Session)
		// The chosen session has been written to the subsystem: travel to the city level.
		SLATE_EVENT(FSimpleDelegate, OnStartRequested)
		SLATE_EVENT(FSimpleDelegate, OnQuitRequested)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	TSharedPtr<SWidget> GetInitialFocusWidget() const { return InitialFocusWidget; }

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	// Which panel is showing. The original is the same two steps: main menu, then city choice.
	enum class EPanel : uint8
	{
		Root,
		CareerCity,
		UserCity,
	};

	TWeakObjectPtr<USimCopterSessionSubsystem> Session;
	FSimpleDelegate OnStartRequested;
	FSimpleDelegate OnQuitRequested;

	EPanel Panel = EPanel::Root;

	// The three cities New Career Game offers (FUN_00407f30's opening successor trio).
	static constexpr int32 NewCareerCityChoices[3] = { 0, 1, 2 };
	int32 CareerChoice = 0;

	TArray<FString> UserCityPaths;
	int32 UserCityChoice = 0;

	// Career tuning read straight from career.twk so the panel can show what each city will play.
	SimCopterMissions::FSimCopterMissionSystem CareerData;
	bool bCareerDataLoaded = false;

	// First-pass extras (no original equivalent).
	int32 MissionChoice = INDEX_NONE; // index into the mission catalog; INDEX_NONE = none
	bool bStartFirstMissionImmediately = true;

	FText StatusText;

	TSharedRef<SWidget> BuildRootPanel();
	TSharedRef<SWidget> BuildCareerCityPanel();
	TSharedRef<SWidget> BuildUserCityPanel();
	TSharedRef<SWidget> BuildMissionOptions();
	void ShowPanel(EPanel NewPanel);

	FText GetCareerCityLineText() const;
	FText GetCareerCityDetailText() const;
	FText GetUserCityLineText() const;
	FText GetMissionLineText() const;
	FText GetMissionDetailText() const;
	FText GetFirstMissionToggleText() const;
	FText GetStatusText() const { return StatusText; }

	// All handlers return Handled so a menu click never reaches the world behind it.
	FReply HandleNewCareerGame();
	FReply HandleNewUserGame();
	FReply HandleUnavailable(FText Reason);
	FReply HandleQuit();
	FReply HandleBack();
	FReply HandleCareerPrev();
	FReply HandleCareerNext();

	// The front-end screens each own a standalone sound object rather than a table slot
	// (career.wav / carsel.wav in FUN_00457c90), so these play by filename.
	static void PlayFrontEndSound(const TCHAR* WavName);
	FReply HandleUserPrev();
	FReply HandleUserNext();
	FReply HandleMissionPrev();
	FReply HandleMissionNext();
	FReply HandleToggleFirstMission();
	FReply HandleStartCareer();
	FReply HandleStartUserCity();

	void ApplyMissionOptionsToSession();
	void StepMission(int32 Delta);

	// The panel body is rebuilt when the step changes, so the whole widget is one vertical box.
	TSharedPtr<SVerticalBox> Body;
	TSharedPtr<SWidget> InitialFocusWidget;
};
