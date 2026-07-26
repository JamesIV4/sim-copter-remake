// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/SimCopterAirport.h"
#include "City/SimCopterHangar.h"
#include "CoreMinimal.h"
#include "Flight/SimCopterHelicopterRegistry.h"
#include "Misc/AutomationTest.h"
#include "Missions/SimCopterMissionSystem.h"
#include "UI/SimCopterHangarArt.h"
#include "UI/SimCopterHangarShop.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterHangarPlacementTest,
	"SimCopter.City.HangarPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterHangarPlacementTest::RunTest(const FString& Parameters)
{
	using namespace SimCopterHangarPlacement;

	const FIntPoint Origin(40, 60);
	constexpr float BlockCentre = (SimCopterAirport::BlockSpan - 1) * 0.5f;

	// Each side anchor sits StandoffTiles clear of the block's own edge, square on the other axis.
	{
		const double Centre = Origin.X + BlockCentre;
		const double NearEdge = Origin.Y - 0.5 - StandoffTiles;
		const double FarEdge = Origin.Y + (SimCopterAirport::BlockSpan - 1) + 0.5 + StandoffTiles;

		const FVector2D North = GetSideAnchorTile(Origin, ESide::North);
		TestEqual(TEXT("The north anchor is centred on the block"), North.X, Centre);
		TestEqual(TEXT("...and stands off the near edge"), North.Y, NearEdge);

		const FVector2D South = GetSideAnchorTile(Origin, ESide::South);
		TestEqual(TEXT("The south anchor is centred on the block"), South.X, Centre);
		TestEqual(TEXT("...and stands off the far edge"), South.Y, FarEdge);

		const FVector2D West = GetSideAnchorTile(Origin, ESide::West);
		TestEqual(TEXT("The west anchor stands off the near column"), West.X, double(Origin.X) - 0.5 - StandoffTiles);
		TestEqual(TEXT("...and is centred on the block"), West.Y, double(Origin.Y) + BlockCentre);

		const FVector2D East = GetSideAnchorTile(Origin, ESide::East);
		TestEqual(TEXT("The east anchor stands off the far column"), East.X, double(Origin.X) + (SimCopterAirport::BlockSpan - 1) + 0.5 + StandoffTiles);
		TestEqual(TEXT("...and is centred on the block"), East.Y, double(Origin.Y) + BlockCentre);
	}

	// The building goes on the side the player is nearest, so the doors face them.
	{
		TestEqual(TEXT("A point below the block picks the south side"),
			static_cast<int32>(GetNearestSide(Origin, FVector2D(Origin.X + 1.5f, Origin.Y + 9.0f))),
			static_cast<int32>(ESide::South));
		TestEqual(TEXT("A point above the block picks the north side"),
			static_cast<int32>(GetNearestSide(Origin, FVector2D(Origin.X + 1.5f, Origin.Y - 9.0f))),
			static_cast<int32>(ESide::North));
		TestEqual(TEXT("A point left of the block picks the west side"),
			static_cast<int32>(GetNearestSide(Origin, FVector2D(Origin.X - 9.0f, Origin.Y + 1.5f))),
			static_cast<int32>(ESide::West));
		TestEqual(TEXT("A point right of the block picks the east side"),
			static_cast<int32>(GetNearestSide(Origin, FVector2D(Origin.X + 9.0f, Origin.Y + 1.5f))),
			static_cast<int32>(ESide::East));
	}

	// Pad 0 is the block's (2, 3) tile - the far row - and the session stands the player beside
	// it, so the hangar has to land on the south side of a block found at the origin above.
	{
		const FIntPoint Pad0 = SimCopterAirport::GetPadTile(Origin, 0);
		const FVector2D PlayerTile(Pad0.X - 0.75f, Pad0.Y);
		TestEqual(TEXT("The session's start beside pad 0 puts the hangar south of the block"),
			static_cast<int32>(GetNearestSide(Origin, PlayerTile)),
			static_cast<int32>(ESide::South));
	}

	// The doorway is the actor's +X, and the yaw is snapped so the building stays grid-square.
	{
		const FVector From(0.0f, 0.0f, 0.0f);
		TestEqual(TEXT("A target on +X needs no turn"), GetSnappedFacingYawDegrees(From, FVector(1000.0f, 0.0f, 0.0f)), 0.0f);
		TestEqual(TEXT("A target on +Y turns a quarter"), GetSnappedFacingYawDegrees(From, FVector(0.0f, 1000.0f, 0.0f)), 90.0f);
		TestEqual(TEXT("A target on -Y turns the other way"), GetSnappedFacingYawDegrees(From, FVector(0.0f, -1000.0f, 0.0f)), -90.0f);
		TestEqual(TEXT("A target behind turns about"), FMath::Abs(GetSnappedFacingYawDegrees(From, FVector(-1000.0f, 0.0f, 0.0f))), 180.0f);

		// 30 degrees off axis still snaps square rather than skewing the building.
		TestEqual(TEXT("An off-axis target snaps to the nearest quarter turn"),
			GetSnappedFacingYawDegrees(From, FVector(1000.0f, 577.0f, 0.0f)),
			0.0f);
		TestEqual(TEXT("...and past 45 degrees it snaps the other way"),
			GetSnappedFacingYawDegrees(From, FVector(577.0f, 1000.0f, 0.0f)),
			90.0f);

		// A degenerate target must not produce a NaN rotation.
		TestEqual(TEXT("A target on top of the hangar leaves it alone"), GetSnappedFacingYawDegrees(From, From), 0.0f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterHangarCatalogTest,
	"SimCopter.UI.HangarCatalog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterHangarCatalogTest::RunTest(const FString& Parameters)
{
	// FUN_0042d840's helicopter permutation {4, 0, 1, 8, 3, 5, 6, 7}: eight rows, and the Apache
	// (runtime type 2) is on none of them.
	{
		const int32 Expected[SimCopterHangarLayout::CatalogTabCount] = { 4, 0, 1, 8, 3, 5, 6, 7 };
		for (int32 Row = 0; Row < SimCopterHangarLayout::CatalogTabCount; ++Row)
		{
			TestEqual(
				*FString::Printf(TEXT("Catalog row %d is runtime type %d"), Row, Expected[Row]),
				SimCopterHangarLayout::GetTypeIndexForCatalogRow(Row),
				Expected[Row]);
			TestEqual(
				*FString::Printf(TEXT("...and type %d is back on row %d"), Expected[Row], Row),
				SimCopterHangarLayout::GetCatalogRowForTypeIndex(Expected[Row]),
				Row);
		}

		TestEqual(TEXT("The Apache is not in the shop"), SimCopterHangarLayout::GetCatalogRowForTypeIndex(2), int32(INDEX_NONE));
		TestEqual(TEXT("There is no row 8"), SimCopterHangarLayout::GetTypeIndexForCatalogRow(8), int32(INDEX_NONE));
		TestEqual(TEXT("There is no row -1"), SimCopterHangarLayout::GetTypeIndexForCatalogRow(INDEX_NONE), int32(INDEX_NONE));
	}

	// FUN_0042d840's equipment permutation {0, 1, 3, 4, 2}: the upgrades page reads down the left
	// column then down the right, which is bucket, megaphone, gas, cannon, harness.
	{
		const ESimCopterHelicopterTool Expected[SimCopterHangarShop::UpgradeRowCount] = {
			ESimCopterHelicopterTool::WaterBucket,
			ESimCopterHelicopterTool::Megaphone,
			ESimCopterHelicopterTool::TearGas,
			ESimCopterHelicopterTool::WaterCannon,
			ESimCopterHelicopterTool::RescueHarness,
		};
		const int32 ExpectedIndices[SimCopterHangarShop::UpgradeRowCount] = { 0, 1, 3, 4, 2 };

		for (int32 Row = 0; Row < SimCopterHangarShop::UpgradeRowCount; ++Row)
		{
			TestEqual(
				*FString::Printf(TEXT("Upgrade row %d maps to equipment index %d"), Row, ExpectedIndices[Row]),
				SimCopterHangarShop::GetEquipmentIndexForUpgradeRow(Row),
				ExpectedIndices[Row]);
			TestEqual(
				*FString::Printf(TEXT("Upgrade row %d is the %s"), Row, SimCopterHelicopterRegistry::GetToolDisplayName(Expected[Row])),
				static_cast<int32>(SimCopterHangarShop::GetToolForUpgradeRow(Row)),
				static_cast<int32>(Expected[Row]));
			TestTrue(
				*FString::Printf(TEXT("Upgrade row %d has the original's blurb"), Row),
				FCString::Strlen(SimCopterHangarShop::GetUpgradeDescription(Row)) > 40);
		}
	}

	// The inventory's five columns are strings 410..414, which is also the order FUN_004077f0
	// stamps the owned-equipment icons in.
	{
		const ESimCopterHelicopterTool Expected[SimCopterHangarShop::InventoryColumnCount] = {
			ESimCopterHelicopterTool::RescueHarness,
			ESimCopterHelicopterTool::WaterBucket,
			ESimCopterHelicopterTool::WaterCannon,
			ESimCopterHelicopterTool::Megaphone,
			ESimCopterHelicopterTool::TearGas,
		};
		const int32 ExpectedBits[SimCopterHangarShop::InventoryColumnCount] = { 0x04, 0x01, 0x10, 0x02, 0x08 };

		for (int32 Column = 0; Column < SimCopterHangarShop::InventoryColumnCount; ++Column)
		{
			const ESimCopterHelicopterTool Tool = SimCopterHangarShop::GetToolForInventoryColumn(Column);
			TestEqual(
				*FString::Printf(TEXT("Inventory column %d is the right tool"), Column),
				static_cast<int32>(Tool),
				static_cast<int32>(Expected[Column]));
			TestEqual(
				*FString::Printf(TEXT("Inventory column %d reads career bit 0x%02x"), Column, ExpectedBits[Column]),
				SimCopterHelicopterRegistry::GetToolCareerBit(Tool),
				ExpectedBits[Column]);
		}
	}

	// The catalog copy is indexed by row, not by runtime type: row 0 is the Schweizer 300, whose
	// History is the Hughes 300 line (string 460).
	{
		TestTrue(TEXT("Row 0's history is the Schweizer's"),
			FString(SimCopterHangarShop::GetCatalogHistory(0)).Contains(TEXT("Hughes 300")));
		TestTrue(TEXT("Row 4's description is the Bell 212's fourteen seats"),
			FString(SimCopterHangarShop::GetCatalogDescription(4)).Contains(TEXT("14 passengers")));
		TestEqual(TEXT("Row 0's name is the shop's spelling"),
			FString(SimCopterHangarShop::GetModelDisplayName(SimCopterHangarLayout::GetTypeIndexForCatalogRow(0))),
			FString(TEXT("Schweizer 300")));
	}

	// The eight tab hit boxes are in order, do not overlap, and cover the strip the page prints.
	{
		for (int32 Tab = 0; Tab < SimCopterHangarLayout::CatalogTabCount; ++Tab)
		{
			TestTrue(
				*FString::Printf(TEXT("Tab %d has width"), Tab),
				SimCopterHangarLayout::CatalogTabRight[Tab] > SimCopterHangarLayout::CatalogTabLeft[Tab]);
			if (Tab > 0)
			{
				TestTrue(
					*FString::Printf(TEXT("Tab %d starts after tab %d ends"), Tab, Tab - 1),
					SimCopterHangarLayout::CatalogTabLeft[Tab] > SimCopterHangarLayout::CatalogTabRight[Tab - 1]);
			}
			TestTrue(
				*FString::Printf(TEXT("Tab %d is inside the strip"), Tab),
				SimCopterHangarLayout::CatalogTabLeft[Tab] >= SimCopterHangarLayout::CatalogTabStripX &&
				SimCopterHangarLayout::CatalogTabRight[Tab] <=
					SimCopterHangarLayout::CatalogTabStripX + SimCopterHangarLayout::CatalogTabStripWidth);
		}
	}

	// Mission type names come from string 570 + n, and a compound rescue mask must not resolve to
	// the single bit it contains.
	{
		using namespace SimCopterMissions;
		TestEqual(TEXT("A building fire is a Fire"),
			FString(SimCopterHangarShop::GetMissionTypeLogName(TYPE_BuildingFire)), FString(TEXT("Fire")));
		TestEqual(TEXT("A boat rescue is not just a rescue"),
			FString(SimCopterHangarShop::GetMissionTypeLogName(TYPE_BoatRescue)), FString(TEXT("Boat Rescue")));
		TestEqual(TEXT("A train rescue is not a train crash"),
			FString(SimCopterHangarShop::GetMissionTypeLogName(TYPE_TrainRescue)), FString(TEXT("Train Rescue")));
		TestEqual(TEXT("An unnamed mask falls back to Unknown"),
			FString(SimCopterHangarShop::GetMissionTypeLogName(0)), FString(TEXT("Unknown")));
	}

	return true;
}
