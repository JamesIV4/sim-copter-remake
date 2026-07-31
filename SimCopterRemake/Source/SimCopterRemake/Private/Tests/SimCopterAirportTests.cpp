// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/SimCopterAirport.h"
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

namespace
{
// A 128x128 XZON/XBLD pair a test can stamp an airport zone into.
struct FTestCityMap
{
	TArray<uint8> Zone;
	TArray<uint8> Xbld;

	FTestCityMap()
	{
		Zone.SetNumZeroed(SimCopterAirport::MapSize * SimCopterAirport::MapSize);
		Xbld.SetNumZeroed(SimCopterAirport::MapSize * SimCopterAirport::MapSize);
	}

	void ZoneBlock(int32 OriginX, int32 OriginY, int32 Span, uint8 ZoneByte)
	{
		for (int32 X = OriginX; X < OriginX + Span; ++X)
		{
			for (int32 Y = OriginY; Y < OriginY + Span; ++Y)
			{
				if (X >= 0 && X < SimCopterAirport::MapSize && Y >= 0 && Y < SimCopterAirport::MapSize)
				{
					Zone[Y * SimCopterAirport::MapSize + X] = ZoneByte;
				}
			}
		}
	}

	int32 ReadZone(int32 X, int32 Y) const
	{
		const bool bInMap = X >= 0 && X < SimCopterAirport::MapSize && Y >= 0 && Y < SimCopterAirport::MapSize;
		return bInMap ? int32(Zone[Y * SimCopterAirport::MapSize + X]) : 0;
	}

	int32 ReadXbld(int32 X, int32 Y) const
	{
		const bool bInMap = X >= 0 && X < SimCopterAirport::MapSize && Y >= 0 && Y < SimCopterAirport::MapSize;
		return bInMap ? int32(Xbld[Y * SimCopterAirport::MapSize + X]) : 0;
	}

	FIntPoint FindOrigin() const
	{
		auto GetZone = [this](int32 X, int32 Y) { return ReadZone(X, Y); };
		auto GetXbld = [this](int32 X, int32 Y) { return ReadXbld(X, Y); };
		return SimCopterAirport::FindAirportOrigin(GetZone, GetXbld);
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterAirportSearchTest, "SimCopter.City.AirportSearch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterAirportSearchTest::RunTest(const FString& Parameters)
{
	// No airport zone anywhere: FUN_0047c0c0 builds one just past the map's far corner.
	{
		FTestCityMap Map;
		const FIntPoint Origin = Map.FindOrigin();
		TestEqual(TEXT("An airport-less city falls back past the corner"), Origin, FIntPoint(128, 128));
		TestTrue(TEXT("...and reports itself as the fallback"), SimCopterAirport::IsFallbackAirportOrigin(Origin));
	}

	// One airport zone: the block's own origin wins.
	{
		FTestCityMap Map;
		Map.ZoneBlock(40, 60, 4, 0x08);
		const FIntPoint Origin = Map.FindOrigin();
		TestEqual(TEXT("A 4x4 airport zone is found at its origin"), Origin, FIntPoint(40, 60));
		TestFalse(TEXT("...and is not the fallback"), SimCopterAirport::IsFallbackAirportOrigin(Origin));
	}

	// Only the low nibble is the zone type; the high nibble carries the zone's own flags.
	{
		FTestCityMap Map;
		Map.ZoneBlock(10, 10, 4, 0x58);
		TestEqual(TEXT("The high nibble of XZON is ignored"), Map.FindOrigin(), FIntPoint(10, 10));
	}

	// A 3x3 patch is too small to hold the 4x4 block and is skipped entirely.
	{
		FTestCityMap Map;
		Map.ZoneBlock(20, 20, 3, 0x08);
		TestEqual(TEXT("A 3x3 airport zone does not validate"), Map.FindOrigin(), FIntPoint(128, 128));
	}

	// A 5x5 patch validates at its own origin, not at the second candidate inside it.
	{
		FTestCityMap Map;
		Map.ZoneBlock(20, 20, 5, 0x08);
		TestEqual(TEXT("A 5x5 zone takes the first validating window"), Map.FindOrigin(), FIntPoint(20, 20));
	}

	// The sweep is x-outer/y-inner, so of two airports the lower X wins even when the other has
	// a much lower Y. This is the ordering that decides which airport a multi-airport city
	// starts at, so it is worth pinning.
	{
		FTestCityMap Map;
		Map.ZoneBlock(30, 5, 4, 0x08);
		Map.ZoneBlock(12, 90, 4, 0x08);
		TestEqual(TEXT("The x-outer sweep picks the lower column"), Map.FindOrigin(), FIntPoint(12, 90));
	}

	// A block that would run off the map's edge cannot validate.
	{
		FTestCityMap Map;
		Map.ZoneBlock(125, 60, 4, 0x08);
		TestEqual(TEXT("A block clipped by the map edge is rejected"), Map.FindOrigin(), FIntPoint(128, 128));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterAirportPadTableTest, "SimCopter.City.AirportPadTable", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterAirportPadTableTest::RunTest(const FString& Parameters)
{
	const FIntPoint Origin(40, 60);

	// Pad 0 is the one city entry parks the first helicopter on and points the camera at.
	TestEqual(TEXT("Pad 0 is (origin + 2, origin + 3)"), SimCopterAirport::GetPadTile(Origin, 0), FIntPoint(42, 63));
	TestEqual(TEXT("Pad 11 is (origin + 2, origin)"), SimCopterAirport::GetPadTile(Origin, 11), FIntPoint(42, 60));
	TestEqual(TEXT("The terminal is (origin + 1, origin + 1)"), SimCopterAirport::GetTerminalTile(Origin), FIntPoint(41, 61));

	// The twelve pads are the 4x4 block's perimeter: all distinct, all on the edge, and none of
	// them the 2x2 terminal in the middle.
	TSet<FIntPoint> Seen;
	for (int32 PadIndex = 0; PadIndex < SimCopterAirport::PadCount; ++PadIndex)
	{
		const FIntPoint Pad = SimCopterAirport::GetPadTile(Origin, PadIndex);
		const int32 DeltaX = Pad.X - Origin.X;
		const int32 DeltaY = Pad.Y - Origin.Y;

		TestTrue(FString::Printf(TEXT("Pad %d is inside the block"), PadIndex),
			DeltaX >= 0 && DeltaX < 4 && DeltaY >= 0 && DeltaY < 4);
		TestTrue(FString::Printf(TEXT("Pad %d is on the block's edge"), PadIndex),
			DeltaX == 0 || DeltaX == 3 || DeltaY == 0 || DeltaY == 3);
		TestFalse(FString::Printf(TEXT("Pad %d was already listed"), PadIndex), Seen.Contains(Pad));
		Seen.Add(Pad);
	}
	TestEqual(TEXT("Twelve distinct pads"), Seen.Num(), SimCopterAirport::PadCount);

	TestEqual(TEXT("An out-of-range pad index has no tile"),
		SimCopterAirport::GetPadTile(Origin, SimCopterAirport::PadCount), FIntPoint(INDEX_NONE, INDEX_NONE));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterAirportFreePadTest, "SimCopter.City.AirportFreePad", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterAirportFreePadTest::RunTest(const FString& Parameters)
{
	auto Never = [](int32) { return false; };

	// An empty apron: FUN_0048b000 returns on the first pad with no occupants at all.
	TestEqual(TEXT("An empty airport hands out pad 0"),
		SimCopterAirport::FindFreePadIndex(Never, Never), 0);

	// Pads 0 and 1 occupied but not blocking: the empty list at pad 2 still ends the search.
	{
		auto HasOccupants = [](int32 PadIndex) { return PadIndex < 2; };
		TestEqual(TEXT("The first empty pad wins outright"),
			SimCopterAirport::FindFreePadIndex(HasOccupants, Never), 2);
	}

	// Every pad has something on it, and only pads 3 and 7 are blocked. With no empty list to
	// short-circuit on, the original keeps walking and the *last* unblocked pad wins.
	{
		auto Always = [](int32) { return true; };
		auto IsBlocked = [](int32 PadIndex) { return PadIndex == 3 || PadIndex == 7; };
		TestEqual(TEXT("With no empty pad the last unblocked one wins"),
			SimCopterAirport::FindFreePadIndex(Always, IsBlocked), SimCopterAirport::PadCount - 1);
	}

	// Full apron.
	{
		auto Always = [](int32) { return true; };
		TestEqual(TEXT("A full airport has no free pad"),
			SimCopterAirport::FindFreePadIndex(Always, Always), int32(INDEX_NONE));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterAirportFootprintTest, "SimCopter.City.AirportBlockFootprintQuirk", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterAirportFootprintTest::RunTest(const FString& Parameters)
{
	// FUN_004829f0 hands FUN_004e4f80 a signed char, so the block-edge test can only see a
	// footprint of 2 on the two sub-0x70 ranges. Everything from 0x80 up reads back as 1
	// however big the building actually is.
	TestEqual(TEXT("0x49 is a 2x2"), SimCopterAirport::GetSignedXbldFootprintSize(0x49), 2);
	TestEqual(TEXT("0x61 is a 2x2"), SimCopterAirport::GetSignedXbldFootprintSize(0x61), 2);
	TestEqual(TEXT("0x6b is a 2x2"), SimCopterAirport::GetSignedXbldFootprintSize(0x6b), 2);
	TestEqual(TEXT("0x6c is a 1x1"), SimCopterAirport::GetSignedXbldFootprintSize(0x6c), 1);
	TestEqual(TEXT("0x8c reads back as 1 despite being a real 2x2"),
		SimCopterAirport::GetSignedXbldFootprintSize(0x8c), 1);
	TestEqual(TEXT("0xff reads back as 1"), SimCopterAirport::GetSignedXbldFootprintSize(0xff), 1);

	// A 2x2 at the block's far column with no matching tile behind it splits across the edge,
	// and the block is refused.
	{
		FTestCityMap Map;
		Map.ZoneBlock(40, 60, 8, 0x08);
		Map.Xbld[60 * SimCopterAirport::MapSize + 43] = 0x61;
		auto GetZone = [&Map](int32 X, int32 Y) { return Map.ReadZone(X, Y); };
		auto GetXbld = [&Map](int32 X, int32 Y) { return Map.ReadXbld(X, Y); };
		TestFalse(TEXT("A 2x2 straddling the far column refuses the block"),
			SimCopterAirport::IsAirportBlockAt(GetZone, GetXbld, 40, 60));
		// The sweep moves on and settles on the next window that does validate.
		TestEqual(TEXT("The sweep steps past the refused block"), Map.FindOrigin(), FIntPoint(40, 61));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterAirportStampTest, "SimCopter.City.AirportBlockStamp", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterAirportStampTest::RunTest(const FString& Parameters)
{
	const FIntPoint Origin(40, 60);

	// FUN_004829f0 leaves the terminal on the middle 2x2 and a pad on each of the twelve
	// perimeter tiles - the same twelve GetPadTile publishes.
	TestEqual(TEXT("The block's own corner is a pad"),
		SimCopterAirport::GetStampedXbldId(Origin, 40, 60), SimCopterAirport::PadXbldId);
	TestEqual(TEXT("The terminal's top-left is the terminal"),
		SimCopterAirport::GetStampedXbldId(Origin, 41, 61), SimCopterAirport::TerminalXbldId);
	TestEqual(TEXT("The terminal's bottom-right is the terminal"),
		SimCopterAirport::GetStampedXbldId(Origin, 42, 62), SimCopterAirport::TerminalXbldId);
	TestEqual(TEXT("A tile outside the block is untouched"),
		SimCopterAirport::GetStampedXbldId(Origin, 44, 60), int32(INDEX_NONE));

	int32 TerminalTiles = 0;
	int32 PadTiles = 0;
	for (int32 DeltaY = 0; DeltaY < SimCopterAirport::BlockSpan; ++DeltaY)
	{
		for (int32 DeltaX = 0; DeltaX < SimCopterAirport::BlockSpan; ++DeltaX)
		{
			const int32 Stamped = SimCopterAirport::GetStampedXbldId(Origin, Origin.X + DeltaX, Origin.Y + DeltaY);
			TerminalTiles += Stamped == SimCopterAirport::TerminalXbldId ? 1 : 0;
			PadTiles += Stamped == SimCopterAirport::PadXbldId ? 1 : 0;
		}
	}
	TestEqual(TEXT("Four terminal tiles"), TerminalTiles, 4);
	TestEqual(TEXT("Twelve pad tiles"), PadTiles, SimCopterAirport::PadCount);

	// Every published pad lands on a pad tile, never on the terminal - which is the bug this
	// stamp fixes: without it the pads keep the SimCity 2000 airport standing on them.
	for (int32 PadIndex = 0; PadIndex < SimCopterAirport::PadCount; ++PadIndex)
	{
		const FIntPoint Pad = SimCopterAirport::GetPadTile(Origin, PadIndex);
		TestEqual(
			*FString::Printf(TEXT("Pad %d is stamped as a helipad"), PadIndex),
			SimCopterAirport::GetStampedXbldId(Origin, Pad.X, Pad.Y),
			SimCopterAirport::PadXbldId);
	}

	// Normalize XZON beside the stamped ids so downstream SC2-grid consumers do not inherit the
	// demolished airport buildings' footprint corners. City mesh ownership itself comes from XBLD.
	TestEqual(TEXT("Every pad is marked as a 1x1"),
		SimCopterAirport::GetStampedZoneHighNibble(Origin, 40, 60), 0xf0);
	TestEqual(TEXT("The terminal's top-left carries the anchor corner"),
		SimCopterAirport::GetStampedZoneHighNibble(Origin, 41, 61), 0x80);
	TestEqual(TEXT("...top-right the width mark"),
		SimCopterAirport::GetStampedZoneHighNibble(Origin, 42, 61), 0x40);
	TestEqual(TEXT("...bottom-left the height mark"),
		SimCopterAirport::GetStampedZoneHighNibble(Origin, 41, 62), 0x10);
	TestEqual(TEXT("...bottom-right the remaining corner"),
		SimCopterAirport::GetStampedZoneHighNibble(Origin, 42, 62), 0x20);
	TestEqual(TEXT("A tile outside the block keeps its own marks"),
		SimCopterAirport::GetStampedZoneHighNibble(Origin, 39, 60), int32(INDEX_NONE));

	for (int32 PadIndex = 0; PadIndex < SimCopterAirport::PadCount; ++PadIndex)
	{
		const FIntPoint Pad = SimCopterAirport::GetPadTile(Origin, PadIndex);
		TestEqual(
			*FString::Printf(TEXT("Pad %d measures as a 1x1"), PadIndex),
			SimCopterAirport::GetStampedZoneHighNibble(Origin, Pad.X, Pad.Y),
			0xf0);
	}

	// FUN_004829f0's height-map write: read the terminal's own corner, then level the 5x5 patch
	// spanning the block to it.
	{
		constexpr int32 GridSize = SimCopterAirport::MapSize + 1;
		TArray<int16> Corners;
		Corners.SetNumZeroed(GridSize * GridSize);
		for (int32 GridY = 0; GridY < GridSize; ++GridY)
		{
			for (int32 GridX = 0; GridX < GridSize; ++GridX)
			{
				Corners[GridY * GridSize + GridX] = static_cast<int16>(GridX + GridY);
			}
		}

		SimCopterAirport::FlattenBlockCorners(Corners, GridSize, Origin);

		const int16 Expected = static_cast<int16>((Origin.X + 1) + (Origin.Y + 1));
		int32 Levelled = 0;
		for (int32 GridY = Origin.Y; GridY <= Origin.Y + SimCopterAirport::BlockSpan; ++GridY)
		{
			for (int32 GridX = Origin.X; GridX <= Origin.X + SimCopterAirport::BlockSpan; ++GridX)
			{
				Levelled += Corners[GridY * GridSize + GridX] == Expected ? 1 : 0;
			}
		}
		TestEqual(TEXT("The whole 5x5 corner patch is levelled"), Levelled, 25);
		TestEqual(TEXT("...to the terminal tile's own corner sample"),
			int32(Corners[Origin.Y * GridSize + Origin.X]), int32(Expected));
		TestNotEqual(TEXT("The corner just outside the patch is left alone"),
			int32(Corners[Origin.Y * GridSize + Origin.X - 1]), int32(Expected));
	}

	// The fallback block sits off the map; FUN_004829f0 skips every XBLD and height write for it.
	{
		constexpr int32 GridSize = SimCopterAirport::MapSize + 1;
		TArray<int16> Corners;
		Corners.SetNumZeroed(GridSize * GridSize);
		Corners[0] = 7;
		SimCopterAirport::FlattenBlockCorners(Corners, GridSize, FIntPoint(128, 128));
		TestEqual(TEXT("The fallback block levels nothing"), int32(Corners[0]), 7);
	}

	return true;
}
