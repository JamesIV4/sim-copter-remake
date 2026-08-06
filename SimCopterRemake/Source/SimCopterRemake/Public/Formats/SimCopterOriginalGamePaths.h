// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

// Where the player's copy of the original SimCopter install lives.
//
// The remake ships no original game data, so every reader has to find the user's install at
// runtime. That search used to be copy-pasted into a dozen call sites with slightly different
// candidate lists (one of them hard-coded a developer's S:\ drive), which meant a packaged build
// silently found some subsystems' data and not others. This is the single list.
//
// The candidates are all relative to FPaths::ProjectDir(), which is the repo's `SimCopterRemake/`
// folder in a developer checkout and the staged `<Package>/SimCopterRemake/` folder in a shipped
// build. `../SimCopter` therefore means the repo root in the first case and the folder next to
// SimCopterRemake.exe in the second - one path that reads the same to both audiences.
namespace SimCopterOriginalGame
{
	// The folder the player is told to fill, relative to ProjectDir. Absolute form is
	// GetPlayerRootDir().
	extern SIMCOPTERREMAKE_API const TCHAR* const PlayerRootRelativePath;

	// The note dropped into an empty PlayerRootDir so the folder explains itself.
	extern SIMCOPTERREMAKE_API const TCHAR* const PlaceholderFileName;

	// `<Package>/SimCopter`, absolute and normalized. Exists whether or not the player has filled
	// it in - this is the answer to "where do I put the files", not "where are they".
	SIMCOPTERREMAKE_API FString GetPlayerRootDir();

	// Every candidate root, in priority order, absolute and normalized.
	SIMCOPTERREMAKE_API void GetSearchRoots(TArray<FString>& OutRoots);

	// True when Dir looks like an original install rather than an empty folder someone made. The
	// distinction matters: the shipped `SimCopter` folder EXISTS from the first launch, so a
	// plain DirectoryExists check would resolve to it and every reader would come up empty.
	SIMCOPTERREMAKE_API bool IsOriginalGameRoot(const FString& Dir);

	// First candidate that passes IsOriginalGameRoot, or empty.
	SIMCOPTERREMAKE_API FString ResolveRoot();

	// First candidate the predicate accepts, or empty. For readers that need one specific file to
	// be present (privanim.dat, say) rather than just a plausible-looking root.
	SIMCOPTERREMAKE_API FString ResolveRootBy(TFunctionRef<bool(const FString& /*Root*/)> Predicate);

	// `<root>/RelativePath` for the first root where that subdirectory exists, or empty.
	SIMCOPTERREMAKE_API FString ResolveDirectory(const TCHAR* RelativePath);

	// `<root>/RelativePath` for the first root where that file exists, or empty.
	SIMCOPTERREMAKE_API FString ResolveFile(const TCHAR* RelativePath);

	// Creates GetPlayerRootDir() and writes PlaceholderFileName into it when the original game
	// data has not been found. Safe to call repeatedly: it never overwrites an existing note and
	// does nothing at all once a real install resolves.
	SIMCOPTERREMAKE_API void EnsurePlayerRootFolder();

	// The one sentence shown to the player when data is missing. Names the absolute folder, so it
	// is useful in a shipped build instead of quoting a source-tree path they do not have.
	SIMCOPTERREMAKE_API FText GetMissingDataHint();
}
