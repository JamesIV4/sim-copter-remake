// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/SimCopterKeyboardFocus.h"

namespace SimCopterKeyboardFocus
{
bool ShouldRestoreGameViewportFocus(const FFocusState& State)
{
	// A UIOnly screen is the one thing allowed to take the keyboard outright, and it is the only
	// state in which the pawn is not expecting keys at all.
	if (!State.bGameplayWantsInput)
	{
		return false;
	}

	if (State.bGameViewportHasFocus)
	{
		return false;
	}

	// Both of these are widgets that hold the keyboard on purpose and hand it back themselves.
	// Taking it from them would eat the letter being typed, or close the dropdown being read.
	if (State.bTextEntryActive || State.bMenuVisible)
	{
		return false;
	}

	// Only the cockpit's own furniture - and the window a dismissed dropdown drops focus onto - is
	// ours to take the keyboard back from.
	return State.bFocusIsReclaimable;
}
}
