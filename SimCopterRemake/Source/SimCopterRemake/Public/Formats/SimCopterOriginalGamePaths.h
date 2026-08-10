// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

// Where the original SimCopter runtime data lives.
//
// Packaged Game targets stage the required data trees from Reference/SimCopterOriginalGame to
// <Package>/SimCopter. Every reader still resolves through this one list so developer checkouts,
// custom builds and the packaged layout behave identically. The search used to be copy-pasted
// into a dozen call sites with slightly different candidates (one hard-coded a developer's S:\
// drive), which made packaged subsystems fail independently and silently.
//
// The candidates are all relative to FPaths::ProjectDir(), which is the repo's `SimCopterRemake/`
// folder in a developer checkout and the staged `<Package>/SimCopterRemake/` folder in a shipped
// build. `../SimCopter` therefore means the repo root in the first case and the folder next to
// SimCopterRemake.exe in the second - one path that reads the same to both audiences.
namespace SimCopterOriginalGame
{
	// The package's bundled-data folder (and optional developer override), relative to ProjectDir.
	// Absolute form is GetPlayerRootDir().
	extern SIMCOPTERREMAKE_API const TCHAR* const PlayerRootRelativePath;

	// The note dropped into an empty PlayerRootDir so the folder explains itself.
	extern SIMCOPTERREMAKE_API const TCHAR* const PlaceholderFileName;

	// `<Package>/SimCopter`, absolute and normalized. Packaging creates and fills this directory;
	// a custom/incomplete build may not have it.
	SIMCOPTERREMAKE_API FString GetPlayerRootDir();

	// Every candidate root, in priority order, absolute and normalized.
	SIMCOPTERREMAKE_API void GetSearchRoots(TArray<FString>& OutRoots);

	// True when Dir looks like an original install rather than an empty folder someone made. The
	// distinction matters for incomplete/custom builds: EnsurePlayerRootFolder may create an empty
	// `SimCopter` folder containing only a diagnostic note.
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

	// Creates GetPlayerRootDir() and writes PlaceholderFileName into it when bundled/runtime data
	// has not been found. This is a recovery hint for custom or damaged packages, not the normal
	// distribution path. Safe to call repeatedly and inert once a real data root resolves.
	SIMCOPTERREMAKE_API void EnsurePlayerRootFolder();

	// The one sentence shown to the player when data is missing. Names the absolute folder, so it
	// is useful in a shipped build instead of quoting a source-tree path they do not have.
	SIMCOPTERREMAKE_API FText GetMissingDataHint();
}
