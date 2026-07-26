// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// One mission the player can ask for by hand. This is not an original screen - SimCopter only ever
// created missions through the scheduler - but every entry is a real type mask that
// FUN_004a6e60/FUN_004a92f0 can dispatch, listed in the scheduler's bucket order.
struct FSimCopterMissionCatalogEntry
{
	int32 TypeMask;
	// Short unambiguous name for a button or list row. GetTypeDisplayName is not enough on its
	// own: it answers by bit, so the three criminal masks and the two rescue masks all come back
	// with the same word.
	const TCHAR* Label;
	// The career.twk weight bucket whose FUN_004a6e60 branch rolls this mask.
	const TCHAR* Bucket;
	// The difficulty-tier gate the scheduler applies, plus anything the remake still lacks.
	const TCHAR* Note;
	// False when the type's ISimCopterMissionWorld hook is still a stub, so placement always fails.
	// Kept in the list because the mask, scoring and lifecycle are ported and the failure is the
	// honest current state.
	bool bWorldHookPorted;
};

// The catalog, shared by the main menu and the gameplay game mode's SimLoadMission command so both
// use the same indices.
TArrayView<const FSimCopterMissionCatalogEntry> GetSimCopterMissionCatalog();
