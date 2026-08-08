// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Game/SimCopterCareerProgression.h"
#include "Misc/AutomationTest.h"
#include "UI/SSimCopterCareerSelect.h"
#include "UI/SSimCopterMainMenu.h"

// The front end's pure logic: the two decoded selection wheels and the career graph. The pages
// themselves need artwork and a viewport, so they are left to whoever is at the keyboard; these
// cover the parts that can be wrong silently. Ground truth: Docs/scratchpad/mainmenu-DECODED.md.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterMainMenuNavigationTest,
	"SimCopter.FrontEnd.MainMenuNavigation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterMainMenuNavigationTest::RunTest(const FString& Parameters)
{
	using namespace SimCopterMainMenuLayout;

	constexpr int32 Count = ItemCount;

	// Down / Page Down wrap past the end back to the first item.
	TestEqual(TEXT("Next from 0"), GetNavigationTarget(ENavigation::Next, 0, Count), 1);
	TestEqual(TEXT("Next from 3"), GetNavigationTarget(ENavigation::Next, 3, Count), 4);
	TestEqual(TEXT("Next wraps"), GetNavigationTarget(ENavigation::Next, 4, Count), 0);

	// Up wraps the other way.
	TestEqual(TEXT("Previous from 4"), GetNavigationTarget(ENavigation::Previous, 4, Count), 3);
	TestEqual(TEXT("Previous wraps"), GetNavigationTarget(ENavigation::Previous, 0, Count), Count - 1);

	// Page Up is deliberately not Up: FUN_0045f040 only takes its branch while the selection is
	// above the first item, so it stops at the top instead of wrapping.
	TestEqual(TEXT("Page Up from 2"), GetNavigationTarget(ENavigation::PreviousNoWrap, 2, Count), 1);
	TestEqual(TEXT("Page Up at the top does nothing"),
		GetNavigationTarget(ENavigation::PreviousNoWrap, 0, Count), (int32)INDEX_NONE);

	TestEqual(TEXT("Home"), GetNavigationTarget(ENavigation::First, 3, Count), 0);
	TestEqual(TEXT("End"), GetNavigationTarget(ENavigation::Last, 0, Count), Count - 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterMainMenuLayoutTest,
	"SimCopter.FrontEnd.MainMenuLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterMainMenuLayoutTest::RunTest(const FString& Parameters)
{
	using namespace SimCopterMainMenuLayout;

	// FUN_00411900's descriptor: x 116, first y 42, stride 64, five rows.
	TestEqual(TEXT("First item top"), GetItemTextRect(0).Top, 42.0f);
	TestEqual(TEXT("Last item top"), GetItemTextRect(4).Top, 42.0f + 64.0f * 4.0f);
	TestEqual(TEXT("Item left"), GetItemTextRect(2).Left, 116.0f);

	// FUN_0045fc60's hit test spans the panel, not the label's own box.
	TestEqual(TEXT("Hit row left"), GetItemHitRect(1).Left, 29.0f);
	TestEqual(TEXT("Hit row right"), GetItemHitRect(1).Right, 394.0f);
	TestEqual(TEXT("Hit row height is the font cell"), GetItemHitRect(1).Height(), 26.0f);

	// The lamp and key strips are contiguous down their columns: FUN_0045fe10's first row is
	// 56 px tall and the other four are 64, which is what makes them meet.
	for (int32 Index = 0; Index + 1 < ItemCount; ++Index)
	{
		const float LampHeight = LampSourceBottom[Index] - LampSourceTop[Index];
		TestEqual(
			FString::Printf(TEXT("Lamp row %d meets row %d"), Index, Index + 1),
			LampTop[Index] + LampHeight,
			LampTop[Index + 1]);

		const float KeyHeight = KeySourceBottom[Index] - KeySourceTop[Index];
		TestEqual(
			FString::Printf(TEXT("Key row %d meets row %d"), Index, Index + 1),
			KeyTop[Index] + KeyHeight,
			KeyTop[Index + 1]);
	}

	// Both source strips are exactly two columns of their own bitmap: main4.bmp is 120x312 and
	// main5.bmp 78x297, so the second column has to fit and the last row has to reach the bottom.
	TestEqual(TEXT("Lamp strip is two 60px columns"), LampColumnWidth * 2.0f, 120.0f);
	TestEqual(TEXT("Lamp strip ends at the bitmap's bottom"), LampSourceBottom[ItemCount - 1], 312.0f);
	TestEqual(TEXT("Key strip is two 39px columns"), KeyColumnWidth * 2.0f, 78.0f);
	TestEqual(TEXT("Key strip ends at the bitmap's bottom"), KeySourceBottom[ItemCount - 1], 297.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterMainMenuUpscaledArtTest,
	"SimCopter.FrontEnd.MainMenuUpscaledArt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterMainMenuUpscaledArtTest::RunTest(const FString& Parameters)
{
	const FString SlateDir = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Slate"));

	const FString BaseFile = FPaths::Combine(SlateDir, TEXT("MAIN1-upscaled-rows-off.png"));
	TestTrue(TEXT("MAIN1-upscaled-rows-off.png exists in Content/Slate"), FPaths::FileExists(BaseFile));

	for (int32 Row = 1; Row <= 5; ++Row)
	{
		const FString RowFile = FPaths::Combine(SlateDir, FString::Printf(TEXT("MAIN1-upscaled-row%d-on.png"), Row));
		TestTrue(FString::Printf(TEXT("MAIN1-upscaled-row%d-on.png exists in Content/Slate"), Row), FPaths::FileExists(RowFile));
	}

	const FString HoseTopFile = FPaths::Combine(SlateDir, TEXT("MAIN2-upscaled.png"));
	TestTrue(TEXT("MAIN2-upscaled.png exists in Content/Slate"), FPaths::FileExists(HoseTopFile));

	const FString HoseCornerFile = FPaths::Combine(SlateDir, TEXT("MAIN3-upscaled.png"));
	TestTrue(TEXT("MAIN3-upscaled.png exists in Content/Slate"), FPaths::FileExists(HoseCornerFile));

	const TCHAR* const NewUpscaledAssets[] = {
		TEXT("MENU4-upscaled.png"),
		TEXT("MSSN_BTN-upscaled.png"),
		TEXT("MAIN1-upscaled.png"),
		TEXT("RENDER-upscaled.png"),
		TEXT("CARSEL-upscaled.png"),
		TEXT("CAREER-upscaled.png"),
	};

	for (const TCHAR* AssetName : NewUpscaledAssets)
	{
		const FString AssetFile = FPaths::Combine(SlateDir, AssetName);
		TestTrue(FString::Printf(TEXT("%s exists in Content/Slate"), AssetName), FPaths::FileExists(AssetFile));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterMenuSkyLayoutTest,
	"SimCopter.FrontEnd.MenuSkyLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterMenuSkyLayoutTest::RunTest(const FString& Parameters)
{
	using namespace SimCopterMenuSkyLayout;

	// MENUSKY.SMK's actual SMK2 header and FFmpeg frame count. Keeping these together catches a
	// transcode or playback change that silently alters the original loop cadence.
	TestEqual(TEXT("movie width"), MovieWidth, 640.0f);
	TestEqual(TEXT("movie height"), MovieHeight, 480.0f);
	TestEqual(TEXT("movie has 201 frames"), FrameCount, 201);
	TestEqual(TEXT("each frame is 71 ms"), FrameDurationMilliseconds, 71);
	TestEqual(TEXT("the loop is 14.271 seconds"), FrameCount * FrameDurationMilliseconds, 14271);

	const FRect Original = GetCenteredMovieRect(640.0f, 480.0f);
	TestEqual(TEXT("4:3 movie starts at the left edge"), Original.Left, 0.0f);
	TestEqual(TEXT("4:3 movie starts at the top edge"), Original.Top, 0.0f);
	TestEqual(TEXT("4:3 movie fills its original screen"), Original.Right, 640.0f);
	TestEqual(TEXT("4:3 movie fills its original height"), Original.Bottom, 480.0f);

	const FRect Widescreen = GetCenteredMovieRect(1920.0f, 1080.0f);
	TestEqual(TEXT("16:9 preserves the original aspect"), Widescreen.Width() / Widescreen.Height(), 4.0f / 3.0f);
	TestEqual(TEXT("16:9 frame fills the viewport height"), Widescreen.Height(), 1080.0f);
	TestEqual(TEXT("16:9 leaves equal animated margins"), Widescreen.Left, 1920.0f - Widescreen.Right);
	TestTrue(TEXT("the extension crop has positive width"), GetExtensionTileWidth(1080.0f) > 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterCareerSelectNavigationTest,
	"SimCopter.FrontEnd.CareerSelectNavigation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterCareerSelectNavigationTest::RunTest(const FString& Parameters)
{
	using namespace SimCopterCareerSelectLayout;

	// FUN_00458a90's table, transcribed. Left and Right are a ring; Up and Down are not, and both
	// land on panel 0 from panel 2.
	TestEqual(TEXT("Left 0"), GetNavigationTarget(EPanelNavigation::Left, 0, 3), 2);
	TestEqual(TEXT("Left 1"), GetNavigationTarget(EPanelNavigation::Left, 1, 3), 0);
	TestEqual(TEXT("Left 2"), GetNavigationTarget(EPanelNavigation::Left, 2, 3), 1);

	TestEqual(TEXT("Right 0"), GetNavigationTarget(EPanelNavigation::Right, 0, 3), 1);
	TestEqual(TEXT("Right 1"), GetNavigationTarget(EPanelNavigation::Right, 1, 3), 2);
	TestEqual(TEXT("Right 2"), GetNavigationTarget(EPanelNavigation::Right, 2, 3), 0);

	TestEqual(TEXT("Up 0"), GetNavigationTarget(EPanelNavigation::Up, 0, 3), 2);
	TestEqual(TEXT("Up 1"), GetNavigationTarget(EPanelNavigation::Up, 1, 3), 0);
	TestEqual(TEXT("Up 2"), GetNavigationTarget(EPanelNavigation::Up, 2, 3), 0);

	TestEqual(TEXT("Down 0"), GetNavigationTarget(EPanelNavigation::Down, 0, 3), 2);
	TestEqual(TEXT("Down 1"), GetNavigationTarget(EPanelNavigation::Down, 1, 3), 2);
	TestEqual(TEXT("Down 2"), GetNavigationTarget(EPanelNavigation::Down, 2, 3), 0);

	// Two panels: every key toggles. One panel: nothing moves.
	TestEqual(TEXT("Two panels, left"), GetNavigationTarget(EPanelNavigation::Left, 0, 2), 1);
	TestEqual(TEXT("Two panels, down"), GetNavigationTarget(EPanelNavigation::Down, 1, 2), 0);
	TestEqual(TEXT("One panel is fixed"),
		GetNavigationTarget(EPanelNavigation::Right, 0, 1), (int32)INDEX_NONE);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterCareerSelectLayoutTest,
	"SimCopter.FrontEnd.CareerSelectLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterCareerSelectLayoutTest::RunTest(const FString& Parameters)
{
	using namespace SimCopterCareerSelectLayout;

	// FUN_00457c90's three panels exactly match the 200x108 CITY<N>_S.SMK movies; two are across
	// the top and one is below.
	for (int32 Panel = 0; Panel < PanelCount; ++Panel)
	{
		TestEqual(FString::Printf(TEXT("Panel %d width"), Panel), PanelRect[Panel].Width(), 200.0f);
		TestEqual(FString::Printf(TEXT("Panel %d height"), Panel), PanelRect[Panel].Height(), 108.0f);
	}
	TestEqual(TEXT("Panels 0 and 1 share a row"), PanelRect[0].Top, PanelRect[1].Top);
	TestEqual(TEXT("Panel 2 is under panel 0"), PanelRect[2].Left, PanelRect[0].Left);

	// Each panel's highlight rect must enclose its panel rect.
	for (int32 Panel = 0; Panel < PanelCount; ++Panel)
	{
		const FRect& HRect = HighlightPanelRect[Panel];
		TestTrue(FString::Printf(TEXT("Panel %d glow encloses the frame"), Panel),
			HRect.Left <= PanelRect[Panel].Left && HRect.Top <= PanelRect[Panel].Top
				&& HRect.Right >= PanelRect[Panel].Right && HRect.Bottom >= PanelRect[Panel].Bottom);
	}

	TestTrue(TEXT("The highlight rects fit carsel.bmp's width"),
		HighlightPanelRect[1].Right <= 557.0f);
	TestTrue(TEXT("The highlight rects fit carsel.bmp's height"),
		HighlightPanelRect[PanelCount - 1].Bottom <= 743.0f);

	TestEqual(TEXT("Readout font fits the lower-right wells"), ReadoutFontHeight, 16);
	TestEqual(TEXT("City readout is lowered in its well"), CityNameRect.Top, 239.0f);
	TestEqual(TEXT("Level readout is lowered in its well"), LevelNameRect.Top, 274.0f);
	TestEqual(TEXT("Readouts share a horizontal centre"),
		CityNameRect.Left + CityNameRect.Right, LevelNameRect.Left + LevelNameRect.Right);
	TestTrue(TEXT("Preview rounded corner leaves room for feather"),
		PreviewCornerRadius > PreviewFeatherWidth);
	TestEqual(TEXT("Preview feather is doubled"), PreviewFeatherWidth, 8.0f);
	TestEqual(TEXT("CARSEL hollow edge stays softly feathered"), HighlightHoleFeatherWidth, 4.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterCareerProgressionTest,
	"SimCopter.FrontEnd.CareerProgression",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterCareerProgressionTest::RunTest(const FString& Parameters)
{
	using namespace SimCopterCareerProgression;

	// FUN_00457c90's null-trio branch.
	TArray<int32> Choices;
	GetNewCareerChoices(Choices);
	TestEqual(TEXT("A new career offers three cities"), Choices.Num(), 3);
	TestEqual(TEXT("Choice 0"), Choices[0], 0);
	TestEqual(TEXT("Choice 1"), Choices[1], 1);
	TestEqual(TEXT("Choice 2"), Choices[2], 2);

	// Spot checks against FUN_00408370.
	TArray<int32> Successors;
	GetSuccessors(0, Successors);
	TestEqual(TEXT("City0 offers three"), Successors.Num(), 3);
	TestEqual(TEXT("City0 -> 3"), Successors[0], 3);
	TestEqual(TEXT("City0 -> 4"), Successors[1], 4);
	TestEqual(TEXT("City0 -> 1"), Successors[2], 1);

	GetSuccessors(18, Successors);
	TestEqual(TEXT("City18 narrows to two"), Successors.Num(), 2);
	GetSuccessors(27, Successors);
	TestEqual(TEXT("City27 narrows to one"), Successors.Num(), 1);
	TestEqual(TEXT("City27 -> 29"), Successors[0], 29);
	GetSuccessors(29, Successors);
	TestEqual(TEXT("City29 is the end of the ladder"), Successors.Num(), 0);

	TestEqual(TEXT("City0 is Level 1"), FString(GetLevelName(GetLevel(0))), FString(TEXT("Level 1")));
	TestEqual(TEXT("City29 is the Final Level"), FString(GetLevelName(GetLevel(29))), FString(TEXT("Final Level")));
	TestEqual(TEXT("City0's name"), FString(GetCityName(0)), FString(TEXT("Sea Cliff")));
	TestEqual(TEXT("City29's name"), FString(GetCityName(29)), FString(TEXT("Metropolis")));
	TestEqual(TEXT("City12's map"), GetMapBaseName(12), FString(TEXT("city12")));

	// The graph has to be walkable: every city below the top has somewhere to go, the top has
	// nowhere, and no edge ever goes backwards. It does NOT always climb - City0 offers City1,
	// which is on the same level - so the career can move sideways once before it moves up.
	for (int32 City = 0; City < CityCount; ++City)
	{
		const int32 Level = GetLevel(City);
		TestTrue(FString::Printf(TEXT("City %d has a valid level"), City), Level >= 0 && Level < LevelCount);

		GetSuccessors(City, Successors);
		if (Level == LevelCount - 1)
		{
			TestEqual(FString::Printf(TEXT("City %d ends the career"), City), Successors.Num(), 0);
			continue;
		}

		TestTrue(FString::Printf(TEXT("City %d has somewhere to go"), City), Successors.Num() > 0);
		for (const int32 Successor : Successors)
		{
			const int32 SuccessorLevel = GetLevel(Successor);
			TestTrue(
				FString::Printf(TEXT("City %d -> %d never goes backwards"), City, Successor),
				SuccessorLevel == Level || SuccessorLevel == Level + 1);
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
