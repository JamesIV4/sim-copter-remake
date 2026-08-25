// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Who owns the keyboard while the city is running.
 *
 * NOT a port - the original is a single-window Win32 app with one input queue and nothing to lose
 * focus to. This is a remake rule, and it exists because of how Slate and the game viewport fit
 * together:
 *
 *  - Widgets added with AddViewportWidgetContent are DESCENDANTS of SViewport (the game layer
 *    manager is the viewport's content). A click walks that path leaf-to-root looking for the first
 *    widget whose SupportsKeyboardFocus says yes. If no cockpit widget does, the walk reaches
 *    SViewport - which already holds focus - and FSlateApplication::SetUserFocus returns early.
 *    Nothing moves, and the flight keys carry on.
 *  - The moment ONE widget in that path accepts focus, the game viewport loses it. Gameplay axis
 *    and action bindings only fire while the viewport holds focus, so every flight key goes dead;
 *    and on the way out UGameViewportClient::LostFocus releases whatever the player was holding.
 *    That pair is exactly the reported fault: "clicking a button stops W from flying forward".
 *
 * So the cockpit's rule is: no HUD widget may accept keyboard focus (SButton needs an explicit
 * IsFocusable(false); SCompoundWidget/SLeafWidget already refuse by default), and this predicate is
 * the backstop that puts the keyboard back where it belongs if one ever does. It is a free function
 * over plain flags so the truth table can be tested without a world, a viewport or Slate:
 * SimCopter.Input.KeyboardFocusGuard.
 */
namespace SimCopterKeyboardFocus
{
/** Everything the decision needs, gathered by ASimCopterPlayerController. */
struct SIMCOPTERREMAKE_API FFocusState
{
	/**
	 * False whenever a UIOnly screen is up - the Settings pages, the hangar shell, the front end -
	 * because that is precisely what FInputModeUIOnly does to the viewport client. Those screens
	 * own the keyboard legitimately and must keep it.
	 */
	bool bGameplayWantsInput = false;

	/** True while the game viewport widget already holds the keyboard: nothing to do. */
	bool bGameViewportHasFocus = false;

	/**
	 * True when whatever holds the keyboard is the game's to take back. Three things are:
	 *
	 *  - a widget inside the game viewport's own subtree, which is where every cockpit panel lives;
	 *  - the game viewport's own top-level window, which is where a DROPDOWN leaves it. Slate hosts
	 *    an in-window menu in the WINDOW's popup layer, not the viewport's, so dismissing one walks
	 *    focus up to the window - past the viewport entirely;
	 *  - nobody at all, outside the editor.
	 *
	 * Focus that has gone somewhere else belongs to the editor: clicking a Details panel during PIE
	 * is exactly how a developer takes the keyboard off a running game, and a guard that fought
	 * that would make the editor unusable. A packaged game has nowhere else for it to go.
	 */
	bool bFocusIsReclaimable = false;

	/**
	 * A summoned menu (the megaphone dropdown) may hold the keyboard for as long as it is up. The
	 * remake's own dropdowns are pushed with bFocusMenu=false and so never take it, but taking it
	 * back from one that did would dismiss it under the player's cursor.
	 */
	bool bMenuVisible = false;

	/**
	 * The replay panel's clip-name box: the one in-game widget that legitimately types. It gives
	 * the keyboard back itself the moment it is done.
	 */
	bool bTextEntryActive = false;
};

/** True when the keyboard has wandered off the game viewport and should be taken back this frame. */
SIMCOPTERREMAKE_API bool ShouldRestoreGameViewportFocus(const FFocusState& State);
}
