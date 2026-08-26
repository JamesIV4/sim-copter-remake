// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/SimCopterAirport.h"
#include "City/SimCopterHangar.h"
#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Flight/SimCopterHelicopterRegistry.h"
#include "Formats/SimCopterOriginalGamePaths.h"
#include "Game/SimCopterCareerSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Missions/SimCopterMissionSystem.h"
#include "UI/SimCopterHangarArt.h"
#include "UI/SimCopterHangarShop.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterHangarPlacementTest,
	"SimCopter.City.HangarPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterHangarPlacementTest::RunTest(const FString& Parameters)
{
	using namespace SimCopterHangarPlacement;

	const FIntPoint Origin(40, 60);

	// The hangar takes the airport's terminal plot - the middle 2x2 the airport port already
	// resolves - so the plot maths has to agree with SimCopterAirport, not invent its own.
	{
		TestEqual(TEXT("The plot origin is the airport's terminal tile"),
			GetPlotOriginTile(Origin),
			SimCopterAirport::GetTerminalTile(Origin));
		TestEqual(TEXT("...which is one tile in from the block origin"),
			GetPlotOriginTile(Origin),
			FIntPoint(Origin.X + 1, Origin.Y + 1));

		const FVector2D Centre = GetPlotCentreTile(Origin);
		TestEqual(TEXT("The plot centre sits between the 2x2's tiles in X"), Centre.X, double(Origin.X) + 1.5);
		TestEqual(TEXT("...and in Y"), Centre.Y, double(Origin.Y) + 1.5);
	}

	// The four tiles the plot covers are exactly the ones FUN_004829f0 stamps the terminal id on,
	// and none of them is a helipad - the hangar must never stand on a pad.
	{
		TArray<FIntPoint> PlotTiles;
		GetPlotTiles(Origin, PlotTiles);
		TestEqual(TEXT("The plot is four tiles"), PlotTiles.Num(), 4);

		for (const FIntPoint& Tile : PlotTiles)
		{
			TestEqual(
				*FString::Printf(TEXT("Tile (%d, %d) carries the terminal id"), Tile.X, Tile.Y),
				SimCopterAirport::GetStampedXbldId(Origin, Tile.X, Tile.Y),
				SimCopterAirport::TerminalXbldId);
		}

		for (int32 PadIndex = 0; PadIndex < SimCopterAirport::PadCount; ++PadIndex)
		{
			const FIntPoint PadTile = SimCopterAirport::GetPadTile(Origin, PadIndex);
			TestFalse(
				*FString::Printf(TEXT("Pad %d is not under the hangar"), PadIndex),
				PlotTiles.Contains(PadTile));
		}
	}

	// The footprint has to fit inside that 2x2 or it would overhang onto the pad ring.
	{
		TestTrue(TEXT("The hangar is no wider than its plot"), WidthTiles <= 2.0f);
		TestTrue(TEXT("The hangar is no deeper than its plot"), DepthTiles <= 2.0f);
		TestTrue(TEXT("The doorway fits inside the front wall"), DoorWidthTiles < WidthTiles);
		TestTrue(TEXT("The doorway fits under the eaves"), DoorHeightTiles < EavesHeightTiles);
		TestTrue(TEXT("The ridge is above the eaves"), ApexHeightTiles > EavesHeightTiles);
		TestTrue(TEXT("The floor is lifted clear of the apron"), FloorLiftCm > 0.0f);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterHangarShopSellGateTest,
	"SimCopter.UI.HangarShopSellGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterHangarShopSellGateTest::RunTest(const FString& Parameters)
{
	// The subsystem insists on a GameInstance outer, so hang it off a throwaway one rather than
	// the editor's - none of the shop paths touch the instance itself.
	UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
	USimCopterCareerSubsystem* Career = NewObject<USimCopterCareerSubsystem>(GameInstance);
	Career->EnsurePricesLoaded(SimCopterOriginalGame::ResolveRoot());

	SimCopterHangarShop::FContext Shop;
	Shop.Career = Career;

	const int32 SchweizerType = USimCopterCareerSubsystem::StartingHelicopterTypeIndex;
	if (Career->GetHelicopterPrice(SchweizerType) <= 0)
	{
		// The reference install is optional on a machine; without heli.twk there is nothing to
		// gate against, and SimCopterOriginalGamePathsTests already covers the resolution rules.
		AddWarning(TEXT("heli.twk prices unavailable; the sell-gate cases were skipped."));
		return true;
	}

	Career->BeginCareer();

	// GetCheapestHelicopterPrice is the floor over the eight catalog rows.
	{
		int32 ExpectedCheapest = 0;
		for (int32 Row = 0; Row < SimCopterHangarLayout::CatalogTabCount; ++Row)
		{
			const int32 Price =
				Career->GetHelicopterPrice(SimCopterHangarLayout::GetTypeIndexForCatalogRow(Row));
			if (Price > 0 && (ExpectedCheapest == 0 || Price < ExpectedCheapest))
			{
				ExpectedCheapest = Price;
			}
		}
		TestEqual(TEXT("The cheapest price is the row minimum"),
			SimCopterHangarShop::GetCheapestHelicopterPrice(Shop), ExpectedCheapest);
		TestTrue(TEXT("The starting Schweizer is the cheapest chopper"),
			Career->GetHelicopterPrice(SchweizerType) == ExpectedCheapest);
	}

	// A fresh career owns only the starting Schweizer, undepreciated - its trade-in is the full
	// price, which covers the cheapest chopper, so the starting airframe can be sold.
	{
		const SimCopterHangarShop::FRowState Row0 = SimCopterHangarShop::GetHelicopterRowState(Shop, 0);
		TestTrue(TEXT("Row 0 is the owned starting chopper"), Row0.bOwned);
		TestTrue(TEXT("The starting helicopter can be sold"), Row0.bCanSell);
		TestTrue(TEXT("A sellable row carries no reason"), Row0.Reason.IsEmpty());
	}

	// Depreciation floors the trade-in at half price - below any chopper in the catalog - so
	// with no cash on top the sale of the last airframe has to be refused.
	{
		Career->AddHelicopterDepreciation(SchweizerType, Career->GetHelicopterPrice(SchweizerType));
		const SimCopterHangarShop::FRowState Depreciated = SimCopterHangarShop::GetHelicopterRowState(Shop, 0);
		TestFalse(TEXT("A sale that leaves too little to buy back in is refused"), Depreciated.bCanSell);
		TestFalse(TEXT("The refusal explains itself"), Depreciated.Reason.IsEmpty());
	}

	// Owning a second airframe lifts the money guard: whatever happens, there is one left to fly.
	{
		Career->SetHelicopterOwned(SimCopterHangarLayout::GetTypeIndexForCatalogRow(1), true);
		const SimCopterHangarShop::FRowState TwoOwned = SimCopterHangarShop::GetHelicopterRowState(Shop, 0);
		TestTrue(TEXT("One of two airframes sells regardless of funds"), TwoOwned.bCanSell);

		Career->SetHelicopterOwned(SimCopterHangarLayout::GetTypeIndexForCatalogRow(1), false);
		const SimCopterHangarShop::FRowState BackToOne = SimCopterHangarShop::GetHelicopterRowState(Shop, 0);
		TestFalse(TEXT("The guard returns once the last airframe is at stake again"), BackToOne.bCanSell);
	}

	return true;
}
