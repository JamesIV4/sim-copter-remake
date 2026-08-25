// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Game/SimCopterKeyboardFocus.h"
#include "GameFramework/InputSettings.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterKeyboardFocusGuardTest,
	"SimCopter.Input.KeyboardFocusGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterKeyboardFocusGuardTest::RunTest(const FString& Parameters)
{
	using namespace SimCopterKeyboardFocus;

	// The reported fault: a cockpit widget has taken the keyboard while the player is flying. Every
	// gameplay axis and action binding is dead until it is taken back.
	FFocusState Stolen;
	Stolen.bGameplayWantsInput = true;
	Stolen.bGameViewportHasFocus = false;
	Stolen.bFocusIsReclaimable = true;
	TestTrue(TEXT("A cockpit widget holding the keyboard while flying is repaired"),
		ShouldRestoreGameViewportFocus(Stolen));

	// The editor is not the game's to fight: clicking a Details panel during PIE is how a developer
	// takes the keyboard off a running game, and it has to keep working.
	FFocusState EditorPanel = Stolen;
	EditorPanel.bFocusIsReclaimable = false;
	TestFalse(TEXT("Focus the game has no claim on is left alone"),
		ShouldRestoreGameViewportFocus(EditorPanel));

	// The ordinary case, which is every frame: nothing to do, and no focus call to make. Setting
	// focus to the widget that already has it is a no-op in Slate, but asking for it every frame is
	// how a guard turns into a fight with whatever else is setting focus.
	FFocusState Healthy = Stolen;
	Healthy.bGameViewportHasFocus = true;
	TestFalse(TEXT("The viewport already holding focus needs no repair"),
		ShouldRestoreGameViewportFocus(Healthy));

	// UIOnly screens own the keyboard outright: the Settings pages, the hangar shell, the front end.
	// FInputModeUIOnly is exactly what makes the viewport client ignore input, so one flag covers
	// all of them - and taking focus back from a menu the player is reading would break it.
	FFocusState UiScreen = Stolen;
	UiScreen.bGameplayWantsInput = false;
	TestFalse(TEXT("A UIOnly screen keeps the keyboard"),
		ShouldRestoreGameViewportFocus(UiScreen));

	// The replay panel's clip-name box is the one in-game widget that legitimately types, and it
	// gives the keyboard back itself. Stealing it back mid-word would eat the letter.
	FFocusState Typing = Stolen;
	Typing.bTextEntryActive = true;
	TestFalse(TEXT("Text entry keeps the keyboard while it is being typed into"),
		ShouldRestoreGameViewportFocus(Typing));

	// A summoned dropdown (the megaphone menu) may hold the keyboard for as long as it is up;
	// pulling focus out from under an open menu dismisses it.
	FFocusState MenuUp = Stolen;
	MenuUp.bMenuVisible = true;
	TestFalse(TEXT("An open menu keeps the keyboard"),
		ShouldRestoreGameViewportFocus(MenuUp));

	// A default-constructed state must not ask for a repair: the guard runs from a Slate
	// pre-processor tick, which is alive before there is a world to fly in.
	TestFalse(TEXT("Nothing running asks for nothing"),
		ShouldRestoreGameViewportFocus(FFocusState()));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterHeldKeysSurviveUiFocusTest,
	"SimCopter.Input.HeldKeysSurviveUiFocus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterHeldKeysSurviveUiFocusTest::RunTest(const FString& Parameters)
{
	// The other half of the fix, and it is one line of DefaultInput.ini.
	//
	// UGameViewportClient::LostFocus releases every key the player is holding whenever the game
	// viewport loses Slate focus, and it makes no distinction between an Alt-Tab and a click on an
	// in-game panel. APlayerController already draws that distinction - SetInputMode(GameAndUI)
	// clears bShouldFlushInputWhenViewportFocusChanges precisely so raising in-game UI does not
	// reset the player's inputs - but this project-wide flag is an OR over that decision, so while
	// it is true the cockpit cannot opt out. Alt-Tab is covered explicitly instead, by
	// ASimCopterPlayerController::HandleApplicationActivationChanged.
	//
	// A saved Input.ini in the user's directory can override DefaultInput.ini, so this asserts what
	// the engine actually resolved rather than what the file says.
	const UInputSettings* Settings = GetDefault<UInputSettings>();
	if (!TestNotNull(TEXT("Input settings resolve"), Settings))
	{
		return false;
	}

	TestFalse(
		TEXT("Losing viewport focus must not flush the keys the player is holding"),
		Settings->bShouldFlushPressedKeysOnViewportFocusLost != 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
