// Copyright Epic Games, Inc. All Rights Reserved.

#include "Formats/SimCopterOriginalGamePaths.h"

#include "HAL/FileManager.h"
#include "Internationalization/Internationalization.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "SimCopterOriginalGamePaths"

namespace SimCopterOriginalGame
{
namespace
{
FString MakeAbsolute(const FString& Relative)
{
	FString Absolute = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), Relative));
	FPaths::NormalizeDirectoryName(Absolute);
	return Absolute;
}

// A shipped install always has these; an empty folder the player just made has none of them. The
// spellings are the ones seen in the wild: the CD installs upper case, but a copy made through a
// case-sensitive filesystem or an archiver often comes out lower.
const TCHAR* const RootMarkers[] =
{
	TEXT("cities"), TEXT("CITIES"),
	TEXT("bmp"), TEXT("BMP"),
	TEXT("geo"), TEXT("GEO"),
	TEXT("tweak"), TEXT("TWEAK"),
};

// The shipped note. DefaultGame.ini stages this folder as NonUFS and then remaps it to the
// package root, so in a packaged build the file is already sitting beside the executable before
// the game has ever run - which is the point of it. This copy is the source for the editor, and
// the repair path for a player who deleted theirs.
const TCHAR* const PlaceholderSourceRelativePath = TEXT("SimCopter");

// Only if the shipped note has gone missing too. Kept terse deliberately: the file on disk is the
// one people read, and two long texts drift apart.
const TCHAR* const PlaceholderFallbackText =
	TEXT("Put the contents of your original SimCopter (1996) install in THIS folder\r\n")
	TEXT("- bmp\\, cities\\, geo\\, sound\\, tweak\\ and the rest, not the folder itself.\r\n")
	TEXT("\r\n")
	TEXT("SimCopter Remake ships none of the original game's data and cannot load a\r\n")
	TEXT("city without it.\r\n");
}

const TCHAR* const PlayerRootRelativePath = TEXT("../SimCopter");
const TCHAR* const PlaceholderFileName = TEXT("PLACE ORIGINAL in SimCopter FOLDER.txt");

FString GetPlayerRootDir()
{
	return MakeAbsolute(PlayerRootRelativePath);
}

void GetSearchRoots(TArray<FString>& OutRoots)
{
	OutRoots.Reset();

	// The folder the player is actually told about wins, so dropping an install in there
	// overrides whatever a developer checkout happens to have lying around.
	OutRoots.Add(GetPlayerRootDir());
	OutRoots.Add(MakeAbsolute(TEXT("SimCopter")));

	// Bundled beside the cooked content, for a build that is allowed to ship the data.
	FString Bundled = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("OriginalGame"));
	FPaths::NormalizeDirectoryName(Bundled);
	OutRoots.Add(MoveTemp(Bundled));

	// The developer checkout's gitignored copy, and the two nestings a staged or installed build
	// has historically been unpacked into.
	OutRoots.Add(MakeAbsolute(TEXT("Reference/SimCopterOriginalGame")));
	OutRoots.Add(MakeAbsolute(TEXT("../Reference/SimCopterOriginalGame")));
	OutRoots.Add(MakeAbsolute(TEXT("../../Reference/SimCopterOriginalGame")));
}

bool IsOriginalGameRoot(const FString& Dir)
{
	if (Dir.IsEmpty() || !IFileManager::Get().DirectoryExists(*Dir))
	{
		return false;
	}

	for (const TCHAR* Marker : RootMarkers)
	{
		if (IFileManager::Get().DirectoryExists(*FPaths::Combine(Dir, Marker)))
		{
			return true;
		}
	}

	return false;
}

FString ResolveRootBy(TFunctionRef<bool(const FString&)> Predicate)
{
	TArray<FString> Roots;
	GetSearchRoots(Roots);

	for (const FString& Root : Roots)
	{
		if (Predicate(Root))
		{
			return Root;
		}
	}

	return FString();
}

FString ResolveRoot()
{
	return ResolveRootBy([](const FString& Root) { return IsOriginalGameRoot(Root); });
}

FString ResolveDirectory(const TCHAR* RelativePath)
{
	FString Resolved;
	ResolveRootBy([RelativePath, &Resolved](const FString& Root)
	{
		FString Candidate = FPaths::Combine(Root, RelativePath);
		FPaths::NormalizeDirectoryName(Candidate);
		if (IFileManager::Get().DirectoryExists(*Candidate))
		{
			Resolved = MoveTemp(Candidate);
			return true;
		}
		return false;
	});
	return Resolved;
}

FString ResolveFile(const TCHAR* RelativePath)
{
	FString Resolved;
	ResolveRootBy([RelativePath, &Resolved](const FString& Root)
	{
		FString Candidate = FPaths::Combine(Root, RelativePath);
		FPaths::NormalizeFilename(Candidate);
		if (IFileManager::Get().FileExists(*Candidate))
		{
			Resolved = MoveTemp(Candidate);
			return true;
		}
		return false;
	});
	return Resolved;
}

void EnsurePlayerRootFolder()
{
	if (!ResolveRoot().IsEmpty())
	{
		return;
	}

	const FString RootDir = GetPlayerRootDir();
	if (!IFileManager::Get().DirectoryExists(*RootDir) &&
		!IFileManager::Get().MakeDirectory(*RootDir, /*Tree=*/true))
	{
		// A read-only install location is a legitimate outcome; the message box still names the
		// folder, so the player is not left without an instruction.
		return;
	}

	const FString NotePath = FPaths::Combine(RootDir, PlaceholderFileName);
	if (IFileManager::Get().FileExists(*NotePath))
	{
		return;
	}

	// Prefer the shipped file over a string literal so there is exactly one copy of this text to
	// keep current.
	const FString SourcePath = FPaths::Combine(
		FPaths::ProjectContentDir(), PlaceholderSourceRelativePath, PlaceholderFileName);
	if (IFileManager::Get().Copy(*NotePath, *SourcePath) == COPY_OK)
	{
		return;
	}

	FFileHelper::SaveStringToFile(FString(PlaceholderFallbackText), *NotePath);
}

FText GetMissingDataHint()
{
	return FText::Format(
		LOCTEXT(
			"MissingOriginalGameData",
			"The original SimCopter game files have to be in place.\nPut them in:\n{0}"),
		FText::FromString(FPaths::ConvertRelativePathToFull(GetPlayerRootDir())));
}
}

#undef LOCTEXT_NAMESPACE
