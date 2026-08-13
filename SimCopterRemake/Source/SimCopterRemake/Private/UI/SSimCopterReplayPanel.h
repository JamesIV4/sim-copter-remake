// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Replay/SimCopterReplaySubsystem.h"
#include "UI/SSimCopterReplayTimeline.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
class SScrollBox;
class SVerticalBox;

/**
 * The replay panel - Tab.
 *
 * Docked to the bottom of the screen rather than filling it. It is a modal in the sense that it
 * owns the keyboard and the transport while it is up, but a replay tool that covers the picture is
 * useless: the whole point of the scrub bar is watching the city move while you drag it.
 *
 * The panel holds no state of its own beyond what is being typed and which list is expanded.
 * Everything else is read from `USimCopterReplaySubsystem` through attribute lambdas, so a clip
 * that ends, a take that hits its memory budget, or a scrub driven from a console command all show
 * up here without the panel being told.
 *
 * NOT a port: the original has no replay, so none of this reproduces a shipped dialog and it is
 * drawn in the developer-panel style rather than from the game's own bitmaps.
 */
class SSimCopterReplayPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCopterReplayPanel) {}
		SLATE_ARGUMENT(TWeakObjectPtr<USimCopterReplaySubsystem>, Replay)
		/**
		 * Lower the panel and hand the world back. The panel cannot do this itself - the widget is
		 * owned by the player controller, which is also what has to restore the input mode - so both
		 * the CLOSE button and the Tab key route out through here.
		 */
		SLATE_EVENT(FSimpleDelegate, OnRequestClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	~SSimCopterReplayPanel();

	/**
	 * NEVER. The panel is a mouse-only surface: clicks on its buttons, its slider and its scrub bar,
	 * and nothing else.
	 *
	 * Gameplay axis bindings only fire while the GAME VIEWPORT holds keyboard focus - the rule
	 * `ASimCopterHelicopterPawn::RestoreGameViewportFocus` exists for on the possession path. Slate
	 * hands focus to the first widget in the clicked path that accepts it, so a panel that accepts
	 * focus takes the keyboard away from the game on the first click and the helicopter simply stops
	 * answering the controls.
	 *
	 * Every keyboard shortcut the panel used to own therefore lives on `ASimCopterPlayerController`
	 * as an ordinary input binding instead, where it works without focus and without a Slate key
	 * handler. The only widget in here that may take focus is the clip name box, because typing
	 * requires it - and it hands the keyboard straight back the moment it is done.
	 */
	virtual bool SupportsKeyboardFocus() const override { return false; }

private:
	TWeakObjectPtr<USimCopterReplaySubsystem> Replay;
	FSimpleDelegate OnRequestClose;

	TSharedPtr<SEditableTextBox> ClipNameBox;
	TSharedPtr<SVerticalBox> ClipListBox;
	TSharedPtr<SScrollBox> EventListBox;
	TSharedPtr<SScrollBox> ClipListScrollBox;
	TSharedPtr<SSimCopterReplayTimeline> Timeline;

	/** The Load list is collapsed until asked for; it costs a directory read and a parse per clip. */
	bool bClipListExpanded = false;
	/** Playback is suspended for the length of a drag and resumed after it, if it was running. */
	bool bResumeAfterScrub = false;
	FText StatusMessage;

	FDelegateHandle StateChangedHandle;

	USimCopterReplaySubsystem* GetReplay() const { return Replay.Get(); }

	/** Hands the keyboard back to the game viewport. The panel must never be holding it. */
	void ReturnFocusToGame() const;

public:
	/**
	 * True only while the player has clicked into the clip name box and is typing. The one moment
	 * the keyboard legitimately belongs to this panel, and the one case the controller's
	 * focus-enforcement has to leave alone.
	 */
	bool IsTypingClipName() const;

private:

	TSharedRef<SWidget> BuildTransportRow();
	TSharedRef<SWidget> BuildViewRow();
	TSharedRef<SWidget> BuildClipRow();
	TSharedRef<SWidget> BuildEventList();

	void RebuildClipList();
	void RefreshEventList();
	void HandleReplayStateChanged();

	// Rebuilt from the clip whenever it changes and pushed into the bar, rather than read back
	// through an attribute every paint.
	void RebuildTimelineMarkers();

	FText GetStateText() const;
	FText GetTimecodeText() const;
	FText GetPlayPauseText() const;
	FText GetSpeedText() const;
	FText GetCameraText() const;
	FText GetSmoothText() const;
	FText GetHudText() const;
	FText GetStatusText() const;
	EVisibility GetClipListVisibility() const;

	FReply HandleRecord();
	FReply HandleStop();
	FReply HandleReset();
	/** Ends a review and lowers the panel, keeping the clip. Distinct from RESET, which discards it. */
	FReply HandleCloseClip();
	/** Re-enters a review of the clip already in memory. */
	FReply HandleReview();
	FReply HandlePlayPause();
	FReply HandleGoToStart();
	FReply HandleCycleCamera();
	FReply HandleToggleSmooth();
	FReply HandleToggleHud();
	FReply HandleSave();
	FReply HandleToggleClipList();
	FReply HandleLoadClip(FString FileName);
	FReply HandleDeleteClip(FString FileName);

	void HandleScrub(float Seconds);
	void HandleScrubBegin();
	void HandleScrubEnd();
	void HandleSpeedChanged(float Value);

	static FText FormatTimecode(float Seconds);
};
