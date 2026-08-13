// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Replay/SimCopterReplaySubsystem.h"
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
	SaveName,      // remake in-app replacement for the Win32 Save As dialog
	Message,       // MBox.bmp, one button
	Confirm,       // MBox.bmp, Yes/No
	SaveBeforeLeave,// MBox.bmp, STRINGTABLE 49 Yes/No
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

	/** Headless save equivalent. Empty overwrites the current named save; a name performs Save As. */
	UFUNCTION(Exec)
	void SimSaveGame(const FString& SaveName = FString());

	/** Console equivalent of Tab: raises or lowers the replay panel. */
	UFUNCTION(Exec)
	void SimReplay();

	/** Prints one line of everything the replay system believes. For bug reports. */
	UFUNCTION(Exec)
	void SimReplayStatus();

	/** Per-track detail: keys, bound stand-in, and clip position vs actual position at the playhead. */
	UFUNCTION(Exec)
	void SimReplayDump();

	/**
	 * Installs the replay bindings on an input component. Public and separable from
	 * `SetupInputComponent` so `SimCopter.Replay.InputDoesNotConsume` can assert the one rule that
	 * matters here without a world: anything sharing a key with gameplay must NOT consume.
	 */
	static void BindReplayInput(UInputComponent& Component, ASimCopterPlayerController& Owner);

	/** True for the two keys nothing else binds (Tab, H), which may therefore consume. */
	static bool IsReplayExclusiveAction(FName ActionName);

	bool IsSettingsOpen() const { return Screen != ESimCopterSettingsScreen::None; }
	bool IsReplayPanelOpen() const { return ReplayPanelWidget.IsValid(); }

	/**
	 * FUN_004346c0's `app+0xbc`: the pause is reference counted, because opening a sub-dialog
	 * pauses again on the way in and its handler resumes on the way out. Public because the replay
	 * subsystem pauses the world for the length of a review and has to share the same counter -
	 * opening Settings on top of a review and closing it again must not resume the world under it.
	 */
	void PushPause();
	void PopPause();

private:
	UPROPERTY(Transient)
	TObjectPtr<USimCopterHangarArt> Art;

	TSharedPtr<SWidget> ScreenWidget;
	TSharedPtr<SWidget> InitialFocusWidget;
	ESimCopterSettingsScreen Screen = ESimCopterSettingsScreen::None;

	FText PendingMessage;
	bool bLeaveAfterSaveAs = false;

	int32 PauseDepth = 0;

	// --- the replay panel (Tab) ---
	//
	// NOT a port: the original has no replay. The panel is docked to the bottom of the screen and
	// deliberately does not take the whole viewport the way the Settings screen does - a replay
	// tool that covers the picture cannot be used to frame a shot. See
	// Docs/memory/simcopter-replay-clips.md.

	TSharedPtr<SWidget> ReplayPanelWidget;
	/**
	 * A take keeps running with the panel lowered, so there has to be something on screen saying so.
	 * Small, top-right, and it obeys Hide HUD like everything else.
	 */
	TSharedPtr<SWidget> RecordingIndicatorWidget;
	/**
	 * Bound for the whole session, not just while the panel is up: the indicator has to appear and
	 * disappear with a take that outlives the panel, and the panel's own camera button changes the
	 * view without going through this controller.
	 */
	FDelegateHandle ReplayStateChangedHandle;
	/** True while the right mouse button is held with the free camera live: mouse look. */
	bool bFreeCameraLookHeld = false;
	/**
	 * The pawn whose input the free camera blocked, so it is the one that gets it back. Holding a
	 * bool instead would leave the old pawn deaf forever if the player got out of the helicopter
	 * while the free camera was live.
	 */
	TWeakObjectPtr<APawn> PawnWithSuppressedInput;

	class USimCopterReplaySubsystem* ResolveReplay() const;
	/** Null unless the panel is up AND the free camera is the selected view. */
	class ASimCopterReplayFreeCamera* ResolveActiveFreeCamera() const;
	void OpenReplayPanel();
	void CloseReplayPanel();
	/**
	 * Answers the subsystem's change broadcast: input suppression, the REC indicator, and which of
	 * the panel and the game viewport owns the keyboard.
	 */
	void HandleReplayStateChanged();
	/**
	 * The panel owns the keyboard only while a review is up (world paused, nothing to fly); the
	 * rest of the time the game viewport must hold it or no axis binding fires at all.
	 */
	void UpdateReplayKeyboardFocus();
	ESimCopterReplayState LastObservedReplayState = ESimCopterReplayState::Idle;
	void UpdateRecordingIndicator();
	void RemoveRecordingIndicator();

	/** Tab, and the SimReplay console command. */
	void ToggleReplayPanel();
	/** H, and the panel's own button. */
	void ToggleReplayHud();
	/** C while the panel is up - the pawn defers to this so one press moves one view. */
	void ReplayCycleCamera();

	// The review transport. These are controller bindings rather than Slate key handlers because
	// the panel never takes keyboard focus, and each refuses unless a clip is being reviewed - the
	// only time the world is paused and these keys are not doing their gameplay job.
	void ReplayTogglePlayPause();
	void ReplayStepBack();
	void ReplayStepForward();
	void ReplayGoToStart();
	void ReplayAddBookmark();
	/** Nudges the playhead by whole recorded frames, pausing playback first. */
	void ReplayStepFrames(int32 FrameDelta);

	// Free-camera input. These reuse the flight axes rather than adding a second set of bindings:
	// W/S, A/D and Space/LeftCtrl already mean forward, strafe and up/down to anyone playing, and
	// the free camera is only ever live while the pawn's own input is suppressed.
	void FreeCameraForward(float Value);
	void FreeCameraStrafe(float Value);
	void FreeCameraVertical(float Value);
	void FreeCameraLookYaw(float Value);
	void FreeCameraLookPitch(float Value);
	void FreeCameraFov(float Value);
	void FreeCameraLookPressed();
	void FreeCameraLookReleased();

	/**
	 * The free camera and the helicopter must never both answer the same key. Whenever the free
	 * camera goes live the pawn's input is blocked, and it is restored the moment it does not.
	 */
	void UpdateFreeCameraInputSuppression();

	void OpenSettings();
	void EnterScreen(ESimCopterSettingsScreen NewScreen);
	void CloseScreen();
	TSharedRef<SWidget> BuildScreen(ESimCopterSettingsScreen NewScreen);

	/** FUN_0044c9e0's switch, item for item. */
	void HandleSettingsItem(ESimCopterSettingsItem Item);

	/** Settings item 0 only exists in a user game, which is DAT_00518d50 == 1. */
	bool IsUserGame() const;

	void ShowMessage(const FText& Message);
	void OpenSaveNameDialog(bool bLeaveAfterSave);
	void SaveAsName(const FString& SaveName);
	bool SaveToCurrentSlot();

	/** First Yes on STRINGTABLE 11 advances to the original's STRINGTABLE 49 save prompt. */
	void ConfirmLeaveCity();
	void HandleSaveBeforeLeave();

	/** Settings item 6: back to the main menu after the two original confirmation steps. */
	void LeaveCity();

	class ASimCopterMissionSystemActor* ResolveMissionSystem() const;

	/** Restores the input mode and cursor the pawn wants once the screen goes away. */
	void RestoreGameInput();

	static FString ResolveOriginalGameRoot();
};
