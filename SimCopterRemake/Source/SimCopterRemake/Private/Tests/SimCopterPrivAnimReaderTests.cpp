// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Formats/SimCopterPrivAnimReader.h"
#include "Ground/SimCopterPopulationFigure.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

// Validates the C++ privanim.df parser against ground truth established by the decompilation
// pass (Docs/OriginalGameFileFormats.md "Exact Container Spec"), the Python reference extractor
// (Tools/privanim_extract.py, 437/437 chunks byte-exact), and the live-game memory oracle
// (Tools/privanim_live_oracle.py). Skips cleanly when the original game files are absent.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterPrivAnimReferenceTest,
	"SimCopter.Formats.PrivAnim.Reference",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterPrivAnimReferenceTest::RunTest(const FString& Parameters)
{
	const FString RootPath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reference/SimCopterOriginalGame")));
	const FString PrivAnimPath = FSimCopterPrivAnimReader::ResolvePrivAnimPath(RootPath);
	if (PrivAnimPath.IsEmpty())
	{
		AddInfo(TEXT("Original privanim.df not present; skipping reference validation."));
		return true;
	}

	FPrivAnimModel Model;
	FString Error;
	if (!TestTrue(TEXT("Parses privanim.df"), FSimCopterPrivAnimReader::LoadFromFile(PrivAnimPath, Model, Error)))
	{
		AddError(Error);
		return false;
	}

	// 21 named figures, including the easter eggs.
	TestEqual(TEXT("Figure count"), Model.Figures.Num(), 21);
	TestTrue(TEXT("Has Elvis"), Model.FindFigureIndex(TEXT("Elvis")) != INDEX_NONE);
	TestTrue(TEXT("Has Nessie"), Model.FindFigureIndex(TEXT("Nessie")) != INDEX_NONE);

	const int32 PilotIndex = Model.FindFigureIndex(TEXT("pilot"));
	if (!TestTrue(TEXT("Has pilot"), PilotIndex != INDEX_NONE))
	{
		return false;
	}
	const FPrivAnimFigure& Pilot = Model.Figures[PilotIndex];
	TestEqual(TEXT("pilot part count"), Pilot.Parts.Num(), 75);
	TestEqual(TEXT("pilot clip count"), Pilot.ClipIndexByMnemonic.Num(), 18);
	TestEqual(TEXT("pilot skeleton root"), Pilot.Parts[0].Name, FString(TEXT("New ")));
	TestEqual(TEXT("pilot root has no parent"), Pilot.Parts[0].ParentIndex, (int32)INDEX_NONE);
	TestEqual(TEXT("pilot part1 parent is root"), Pilot.Parts[1].ParentIndex, 0);

	// Walk clip: 8 frames; first segment of frame 0 is the spine top (0,0,13)->(0,0,3)
	// (byte-validated against both the file and the live game).
	const FPrivAnimClip* PilotWalk = Model.FindClip(Pilot, TEXT("1Wal"));
	if (TestNotNull(TEXT("pilot 1Wal clip"), PilotWalk))
	{
		TestEqual(TEXT("pilot walk clip name"), PilotWalk->Name, FString(TEXT("101!")));
		TestEqual(TEXT("pilot walk frames"), PilotWalk->FrameCount, 8);
		TestEqual(TEXT("pilot walk parts"), PilotWalk->PartCount, 75);
		const FPrivAnimSegment& First = PilotWalk->Segment(0, 0);
		TestEqual(TEXT("seg A.Z"), (int32)First.A.Z, 13);
		TestEqual(TEXT("seg B.Z"), (int32)First.B.Z, 3);
		TestEqual(TEXT("seg A.X"), (int32)First.A.X, 0);
	}

	// 2woman's NoMo clip is "412!" with 3 frames x 51 parts (the live-oracle case).
	const int32 WomanIndex = Model.FindFigureIndex(TEXT("2woman"));
	if (TestTrue(TEXT("Has 2woman"), WomanIndex != INDEX_NONE))
	{
		const FPrivAnimFigure& Woman = Model.Figures[WomanIndex];
		const FPrivAnimClip* NoMo = Model.FindClip(Woman, TEXT("NoMo"));
		if (TestNotNull(TEXT("2woman NoMo clip"), NoMo))
		{
			TestEqual(TEXT("2woman NoMo name"), NoMo->Name, FString(TEXT("412!")));
			TestEqual(TEXT("2woman NoMo frames"), NoMo->FrameCount, 3);
			TestEqual(TEXT("2woman NoMo parts"), NoMo->PartCount, 51);
		}
	}

	// Shared figure-building data (palette + head images) loads for the same root.
	FString SharedError;
	const TSharedPtr<FSimCopterPrivAnimShared> Shared = FSimCopterPopulationFigure::GetShared(RootPath, SharedError);
	if (TestTrue(TEXT("Shared figure data loads"), Shared.IsValid()))
	{
		TestEqual(TEXT("Palette size"), Shared->Palette.Num(), 256);
		TestTrue(TEXT("Head images decoded"), Shared->HeadImages.Num() > 0);
	}
	else
	{
		AddError(SharedError);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
