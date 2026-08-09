// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Formats/SimCopterOriginalGamePaths.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// The single search list every reader uses to find the player's original SimCopter install. The
// packaged build got this wrong three different ways before it was centralised, and the failures
// are all silent (a subsystem quietly has no data), so the rules that keep it honest are pinned
// here. Ground truth: Docs/memory/simcopter-packaged-build.md.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterOriginalGamePathsRootShapeTest,
	"SimCopter.OriginalGamePaths.RootShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterOriginalGamePathsRootShapeTest::RunTest(const FString& Parameters)
{
	// The whole point of `../SimCopter`: one relative path that is the repo root to a developer
	// and the automatically populated data folder beside the .exe in a package. If this ever stops
	// being ProjectDir's parent, packaged builds stop finding the data Build.cs staged.
	const FString PlayerRoot = SimCopterOriginalGame::GetPlayerRootDir();
	TestFalse(TEXT("Player root is not empty"), PlayerRoot.IsEmpty());
	TestTrue(TEXT("Player root is absolute"), !FPaths::IsRelative(PlayerRoot));
	TestEqual(TEXT("Player root is named SimCopter"), FPaths::GetCleanFilename(PlayerRoot), FString(TEXT("SimCopter")));

	FString ExpectedParent = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("..")));
	FPaths::NormalizeDirectoryName(ExpectedParent);
	FString ActualParent = FPaths::GetPath(PlayerRoot);
	FPaths::NormalizeDirectoryName(ActualParent);
	TestEqual(TEXT("Player root sits beside the project folder"), ActualParent, ExpectedParent);

	TArray<FString> Roots;
	SimCopterOriginalGame::GetSearchRoots(Roots);
	TestTrue(TEXT("There are search roots"), Roots.Num() > 0);
	TestEqual(TEXT("The bundled-data folder is searched first"), Roots[0], PlayerRoot);

	for (const FString& Root : Roots)
	{
		TestTrue(*FString::Printf(TEXT("Search root is absolute: %s"), *Root), !FPaths::IsRelative(Root));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterOriginalGamePathsFallbackNoteTest,
	"SimCopter.OriginalGamePaths.FallbackNote",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterOriginalGamePathsFallbackNoteTest::RunTest(const FString& Parameters)
{
	// Normal packages contain the required data and never need this. Keep a source copy in Content
	// for editor/developer recovery; a damaged package whose Content copy is also absent uses the
	// concise C++ fallback rather than failing to create any diagnostic note.
	const FString FallbackNote = FPaths::Combine(
		FPaths::ProjectContentDir(), TEXT("SimCopter"), SimCopterOriginalGame::PlaceholderFileName);

	TestTrue(
		*FString::Printf(TEXT("The fallback note source is at %s"), *FallbackNote),
		IFileManager::Get().FileExists(*FallbackNote));

	FString Contents;
	if (FFileHelper::LoadFileToString(Contents, *FallbackNote))
	{
		TestTrue(TEXT("The note says what folder it is about"), Contents.Contains(TEXT("SimCopter")));
		TestTrue(TEXT("The note is not a stub"), Contents.Len() > 200);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterOriginalGamePathsMarkerTest,
	"SimCopter.OriginalGamePaths.Markers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterOriginalGamePathsMarkerTest::RunTest(const FString& Parameters)
{
	// The trap this check exists for: a custom or damaged build can create the `SimCopter` folder
	// at first launch solely to hold its recovery note. A plain DirectoryExists test would resolve
	// to that empty folder and every reader would come up empty-handed with nothing in the log to
	// say why.
	const FString Scratch = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Tests/OriginalGamePaths"));
	IFileManager::Get().DeleteDirectory(*Scratch, /*RequireExists=*/false, /*Tree=*/true);

	const FString EmptyRoot = FPaths::Combine(Scratch, TEXT("Empty"));
	IFileManager::Get().MakeDirectory(*EmptyRoot, /*Tree=*/true);
	TestFalse(TEXT("An empty folder is not an install"), SimCopterOriginalGame::IsOriginalGameRoot(EmptyRoot));

	const FString NoteOnlyRoot = FPaths::Combine(Scratch, TEXT("NoteOnly"));
	IFileManager::Get().MakeDirectory(*NoteOnlyRoot, /*Tree=*/true);
	FFileHelper::SaveStringToFile(
		FString(TEXT("put the game here")),
		*FPaths::Combine(NoteOnlyRoot, SimCopterOriginalGame::PlaceholderFileName));
	TestFalse(TEXT("The placeholder note alone is not an install"),
		SimCopterOriginalGame::IsOriginalGameRoot(NoteOnlyRoot));

	// A shipped install always brings its data folders. Both spellings count: the CD installs
	// upper case, but a copy round-tripped through a case-sensitive filesystem often comes out
	// lower, and refusing those is a support ticket nobody can diagnose.
	const FString LowerRoot = FPaths::Combine(Scratch, TEXT("Lower"));
	IFileManager::Get().MakeDirectory(*FPaths::Combine(LowerRoot, TEXT("cities")), /*Tree=*/true);
	TestTrue(TEXT("A cities/ folder makes it an install"), SimCopterOriginalGame::IsOriginalGameRoot(LowerRoot));

	const FString UpperRoot = FPaths::Combine(Scratch, TEXT("Upper"));
	IFileManager::Get().MakeDirectory(*FPaths::Combine(UpperRoot, TEXT("GEO")), /*Tree=*/true);
	TestTrue(TEXT("An upper-case GEO/ folder counts too"), SimCopterOriginalGame::IsOriginalGameRoot(UpperRoot));

	TestFalse(TEXT("A folder that does not exist is not an install"),
		SimCopterOriginalGame::IsOriginalGameRoot(FPaths::Combine(Scratch, TEXT("Absent"))));
	TestFalse(TEXT("An empty path is not an install"), SimCopterOriginalGame::IsOriginalGameRoot(FString()));

	IFileManager::Get().DeleteDirectory(*Scratch, /*RequireExists=*/false, /*Tree=*/true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterOriginalGamePathsResolveTest,
	"SimCopter.OriginalGamePaths.Resolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterOriginalGamePathsResolveTest::RunTest(const FString& Parameters)
{
	// This machine has the developer checkout's Reference copy, so the readers' two entry points
	// have to agree with each other and land inside a resolved root. On a machine with no install
	// at all they are all empty, which is also consistent - so assert the relationship, not the
	// presence.
	const FString Root = SimCopterOriginalGame::ResolveRoot();
	const FString Cities = SimCopterOriginalGame::ResolveDirectory(TEXT("cities"));
	const FString Career = SimCopterOriginalGame::ResolveFile(TEXT("tweak/career.twk"));

	if (Root.IsEmpty())
	{
		AddInfo(TEXT("No original game install on this machine; resolution asserts are vacuous."));
		TestTrue(TEXT("No root means no cities dir"), Cities.IsEmpty());
		TestTrue(TEXT("No root means no career.twk"), Career.IsEmpty());
		return true;
	}

	TestTrue(TEXT("The resolved root passes its own marker check"),
		SimCopterOriginalGame::IsOriginalGameRoot(Root));

	TArray<FString> SearchRoots;
	SimCopterOriginalGame::GetSearchRoots(SearchRoots);

	if (!Cities.IsEmpty())
	{
		FString Parent = FPaths::GetPath(Cities);
		FPaths::NormalizeDirectoryName(Parent);
		TestTrue(TEXT("cities/ resolves under one of the search roots"), SearchRoots.Contains(Parent));
		TestTrue(TEXT("cities/ really exists"), IFileManager::Get().DirectoryExists(*Cities));
	}
	if (!Career.IsEmpty())
	{
		TestTrue(TEXT("career.twk really exists"), IFileManager::Get().FileExists(*Career));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
