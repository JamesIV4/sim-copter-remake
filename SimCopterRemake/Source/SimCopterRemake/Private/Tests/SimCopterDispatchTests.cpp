// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ground/SimCopterDispatch.h"
#include "Ground/SimCopterBehaviorVM.h"

#include "Misc/AutomationTest.h"

// Automation for the decoded emergency-dispatch core
// (Docs/scratchpad/ghidra/emergency_dispatch_decode_20260725.md). Everything here runs on
// a synthetic tile grid, so the tests never need original game data.

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
using namespace SimCopterDispatch;

// A 128x128 XBLD grid the tests paint by hand. 0 = empty land.
struct FTestGrid
{
	TArray<uint8> Ids;

	FTestGrid() { Ids.SetNumZeroed(MapTiles * MapTiles); }

	void Set(int32 X, int32 Y, uint8 Id)
	{
		if (X >= 0 && X < MapTiles && Y >= 0 && Y < MapTiles)
		{
			Ids[Y * MapTiles + X] = Id;
		}
	}

	// Paint a straight road along a row.
	void SetRoadRow(int32 Y, int32 FromX, int32 ToX)
	{
		for (int32 X = FromX; X <= ToX; ++X)
		{
			Set(X, Y, 0x1d);
		}
	}

	// Paint a 3x3 station with its top-left corner at (X, Y), as the original scan expects.
	void SetStation(int32 X, int32 Y, uint8 Id)
	{
		for (int32 Dy = 0; Dy < 3; ++Dy)
		{
			for (int32 Dx = 0; Dx < 3; ++Dx)
			{
				Set(X + Dx, Y + Dy, Id);
			}
		}
	}

	int32 Get(int32 X, int32 Y) const
	{
		if (X < 0 || X >= MapTiles || Y < 0 || Y >= MapTiles)
		{
			return 0;
		}
		return Ids[Y * MapTiles + X];
	}
};

// Routes are "possible" between any two road tiles on the same painted row, which is all the
// selection tests need; the real graph search lives in the traffic actor.
class FTestDispatchWorld final : public ISimCopterDispatchWorld
{
public:
	explicit FTestDispatchWorld(const FTestGrid& InGrid, bool bInAllowRoutes = true)
		: Grid(InGrid)
		, bAllowRoutes(bInAllowRoutes)
	{
	}

	virtual int32 GetXbldTileId(int32 TileX, int32 TileY) const override { return Grid.Get(TileX, TileY); }

	virtual bool CanRouteBetween(const FIntPoint& From, const FIntPoint& To) const override
	{
		RouteQueries.Add(TPair<FIntPoint, FIntPoint>(From, To));
		return bAllowRoutes;
	}

	mutable TArray<TPair<FIntPoint, FIntPoint>> RouteQueries;

private:
	const FTestGrid& Grid;
	bool bAllowRoutes;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterDispatchTileRulesTest,
	"SimCopter.Dispatch.TileRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterDispatchTileRulesTest::RunTest(const FString& Parameters)
{
	// SCHOOK: AmbulanceOnScene 0x004b8f60
	// The on-scene ambulance deploys FUN_004bd980(0x0c, 5), and BHAV 272 later selects
	// FUN_004cac70 object class 10 to bring the patient back to the ambulance pool.
	TestEqual(TEXT("ambulance deploys behavior class 12"), AmbulanceMedicBehaviorClass, 0x0c);
	TestEqual(TEXT("ambulance deploys person state 5"), AmbulanceMedicPersonState, 5);
	TestEqual(TEXT("original object class 10 is ambulance"), EBhavObjectClass::Ambulance, 10);
	TestEqual(TEXT("original object class 11 is police car"), EBhavObjectClass::PoliceCar, 11);
	TestEqual(TEXT("original object class 12 is fire truck"), EBhavObjectClass::FireTruck, 12);

	// The three ranges FUN_004bc110 / FUN_004bc680 accept.
	TestTrue(TEXT("0x1d is road"), IsRoadTileId(0x1d));
	TestTrue(TEXT("0x2b is road"), IsRoadTileId(0x2b));
	TestTrue(TEXT("0x3f is road"), IsRoadTileId(0x3f));
	TestTrue(TEXT("0x46 is road"), IsRoadTileId(0x46));
	TestTrue(TEXT("0x51 is road"), IsRoadTileId(0x51));
	TestTrue(TEXT("0x59 is road"), IsRoadTileId(0x59));
	TestFalse(TEXT("0x1c is not road"), IsRoadTileId(0x1c));
	TestFalse(TEXT("0x2c is not road"), IsRoadTileId(0x2c));
	TestFalse(TEXT("0x47 is not road"), IsRoadTileId(0x47));
	TestFalse(TEXT("0x5a is not road"), IsRoadTileId(0x5a));

	// FUN_004bb900's intersection window, with its explicit 0x69 exclusion.
	TestTrue(TEXT("0x27 is an intersection"), IsIntersectionTileId(0x27));
	TestTrue(TEXT("0x2b is an intersection"), IsIntersectionTileId(0x2b));
	TestFalse(TEXT("0x26 is not an intersection"), IsIntersectionTileId(0x26));
	TestFalse(TEXT("0x69 is explicitly excluded"), IsIntersectionTileId(0x69));

	// Octile metric: larger + (smaller >> 1).
	TestEqual(TEXT("cost is symmetric on axis"), TileCost(FIntPoint(0, 0), FIntPoint(10, 0)), 10);
	TestEqual(TEXT("cost mixes the shorter leg at half weight"), TileCost(FIntPoint(0, 0), FIntPoint(10, 4)), 12);
	TestEqual(TEXT("cost is order independent"), TileCost(FIntPoint(4, 10), FIntPoint(0, 0)), 12);
	TestEqual(TEXT("zero distance"), TileCost(FIntPoint(7, 7), FIntPoint(7, 7)), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterDispatchStationScanTest,
	"SimCopter.Dispatch.StationScan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterDispatchStationScanTest::RunTest(const FString& Parameters)
{
	FTestGrid Grid;
	// Fire station occupying (10,10)..(12,12); a road two tiles below its centre.
	Grid.SetStation(10, 10, XbldFireStation);
	Grid.SetRoadRow(13, 0, 40);

	auto GetTileId = [&Grid](int32 X, int32 Y) { return Grid.Get(X, Y); };

	TArray<FStation> Stations;
	ScanStations(GetTileId, EService::FireTruck, Stations);

	// The 3x3 must yield exactly one record anchored on the centre, not nine corner hits.
	TestEqual(TEXT("one station per building"), Stations.Num(), 1);
	if (Stations.Num() != 1)
	{
		return false;
	}
	TestEqual(TEXT("station tile is the footprint centre"), Stations[0].Tile, FIntPoint(11, 11));
	TestEqual(TEXT("road access is the tile two south"), Stations[0].RoadTile, FIntPoint(11, 13));
	TestEqual(TEXT("no dispatches outstanding at startup"), Stations[0].Outstanding, 0);

	// A police station on the same map must not be picked up by the fire scan.
	Grid.SetStation(30, 30, XbldPoliceStation);
	Grid.SetRoadRow(33, 0, 40);
	ScanStations(GetTileId, EService::FireTruck, Stations);
	TestEqual(TEXT("fire scan ignores police stations"), Stations.Num(), 1);

	TArray<FStation> PoliceStations;
	ScanStations(GetTileId, EService::Police, PoliceStations);
	TestEqual(TEXT("police scan finds its own station"), PoliceStations.Num(), 1);

	// A station with no road within ring 4 is dropped entirely (FUN_004bc110 -> 0xff).
	FTestGrid Isolated;
	Isolated.SetStation(60, 60, XbldHospital);
	auto GetIsolatedId = [&Isolated](int32 X, int32 Y) { return Isolated.Get(X, Y); };
	TArray<FStation> Hospitals;
	ScanStations(GetIsolatedId, EService::Ambulance, Hospitals);
	TestEqual(TEXT("a station with no road access is skipped"), Hospitals.Num(), 0);

	// Ring 5 is out of range even though the tile is a road.
	Isolated.SetRoadRow(66, 0, 80);
	ScanStations(GetIsolatedId, EService::Ambulance, Hospitals);
	TestEqual(TEXT("road access stops at ring 4"), Hospitals.Num(), 0);

	Isolated.SetRoadRow(65, 0, 80);
	ScanStations(GetIsolatedId, EService::Ambulance, Hospitals);
	TestEqual(TEXT("road access is found at ring 4"), Hospitals.Num(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterDispatchTargetSnapTest,
	"SimCopter.Dispatch.TargetSnap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterDispatchTargetSnapTest::RunTest(const FString& Parameters)
{
	FTestGrid Grid;
	Grid.SetRoadRow(20, 0, 60);
	auto GetTileId = [&Grid](int32 X, int32 Y) { return Grid.Get(X, Y); };

	FIntPoint Snapped;
	TestTrue(TEXT("a road tile snaps to itself"), TryFindNearestRoadTile(GetTileId, FIntPoint(30, 20), TargetRoadSnapRadius, Snapped));
	TestEqual(TEXT("no movement when already on a road"), Snapped, FIntPoint(30, 20));

	TestTrue(TEXT("a nearby tile snaps onto the road"), TryFindNearestRoadTile(GetTileId, FIntPoint(30, 23), TargetRoadSnapRadius, Snapped));
	TestEqual(TEXT("snapped onto the painted row"), Snapped.Y, 20);

	TestFalse(
		TEXT("nothing snaps when the road is beyond the spiral"),
		TryFindNearestRoadTile(GetTileId, FIntPoint(30, 40), TargetRoadSnapRadius, Snapped));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterDispatchSelectionTest,
	"SimCopter.Dispatch.Selection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterDispatchSelectionTest::RunTest(const FString& Parameters)
{
	FTestGrid Grid;
	Grid.SetRoadRow(20, 0, 120);
	const FTestDispatchWorld World(Grid);

	TArray<FStation> Stations;
	{
		FStation Near;
		Near.Service = EService::Police;
		Near.Tile = FIntPoint(30, 21);
		Near.RoadTile = FIntPoint(30, 20);
		Stations.Add(Near);

		FStation Far;
		Far.Service = EService::Police;
		Far.Tile = FIntPoint(100, 21);
		Far.RoadTile = FIntPoint(100, 20);
		Stations.Add(Far);
	}

	TArray<FVehicleSlotView> Slots;
	Slots.SetNum(VehiclesPerService);

	// With every slot empty, the nearest station wins and takes an outstanding dispatch.
	FDispatchOutcome Outcome = Dispatch(World, Stations, Slots, FIntPoint(35, 20));
	TestEqual(TEXT("dispatched"), Outcome.Result, EDispatchResult::Dispatched);
	TestEqual(TEXT("the nearest station responds"), Outcome.StationIndex, 0);
	TestFalse(TEXT("a station launch is not a redirect"), Outcome.bRedirectedExistingVehicle);
	TestEqual(TEXT("the destination is the snapped road tile"), Outcome.DestinationTile, FIntPoint(35, 20));
	TestEqual(TEXT("the station now has one dispatch out"), Stations[0].Outstanding, 1);
	TestEqual(TEXT("the far station is untouched"), Stations[1].Outstanding, 0);

	// A busy station stops being a candidate; the second request falls to the far one.
	Outcome = Dispatch(World, Stations, Slots, FIntPoint(35, 20));
	TestEqual(TEXT("dispatched from the second station"), Outcome.Result, EDispatchResult::Dispatched);
	TestEqual(TEXT("busy stations are skipped"), Outcome.StationIndex, 1);

	// Both busy and no idle vehicle: no unit available.
	Outcome = Dispatch(World, Stations, Slots, FIntPoint(35, 20));
	TestEqual(TEXT("no unit available"), Outcome.Result, EDispatchResult::NoUnitAvailable);

	// An idle vehicle closer than either station wins over them, and does not consume a
	// station slot.
	Stations[0].Outstanding = 0;
	Slots[2].bSpawned = true;
	Slots[2].bIdle = true;
	Slots[2].Tile = FIntPoint(34, 20);
	Outcome = Dispatch(World, Stations, Slots, FIntPoint(35, 20));
	TestEqual(TEXT("dispatched"), Outcome.Result, EDispatchResult::Dispatched);
	TestTrue(TEXT("the idle vehicle is redirected"), Outcome.bRedirectedExistingVehicle);
	TestEqual(TEXT("the redirected slot is the idle one"), Outcome.SlotIndex, 2);
	TestEqual(TEXT("no station slot was consumed"), Stations[0].Outstanding, 0);

	// A spawned but busy vehicle is not a candidate.
	Slots[2].bIdle = false;
	Stations[0].Outstanding = 1;
	Stations[1].Outstanding = 1;
	Outcome = Dispatch(World, Stations, Slots, FIntPoint(35, 20));
	TestEqual(TEXT("a busy vehicle is not a candidate"), Outcome.Result, EDispatchResult::NoUnitAvailable);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterDispatchFailureTest,
	"SimCopter.Dispatch.Failures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterDispatchFailureTest::RunTest(const FString& Parameters)
{
	FTestGrid Grid;
	Grid.SetRoadRow(20, 0, 120);

	TArray<FStation> Stations;
	FStation Station;
	Station.Service = EService::FireTruck;
	Station.Tile = FIntPoint(30, 21);
	Station.RoadTile = FIntPoint(30, 20);
	Stations.Add(Station);

	TArray<FVehicleSlotView> Slots;
	Slots.SetNum(VehiclesPerService);

	// Off-map target: FUN_004be910 returns before the manager is touched, so no clip and no
	// station slot is consumed.
	{
		const FTestDispatchWorld World(Grid);
		const FDispatchOutcome Outcome = Dispatch(World, Stations, Slots, FIntPoint(200, 20));
		TestEqual(TEXT("off-map target is rejected"), Outcome.Result, EDispatchResult::InvalidTarget);
		TestEqual(TEXT("no station slot consumed"), Stations[0].Outstanding, 0);
	}

	// Target with no road within the radius-4 spiral.
	{
		const FTestDispatchWorld World(Grid);
		const FDispatchOutcome Outcome = Dispatch(World, Stations, Slots, FIntPoint(30, 60));
		TestEqual(TEXT("no road near the target"), Outcome.Result, EDispatchResult::CannotReach);
		TestEqual(TEXT("no station slot consumed"), Stations[0].Outstanding, 0);
		TestEqual(TEXT("routing was never asked"), World.RouteQueries.Num(), 0);
	}

	// Road is there but nothing can route to it.
	{
		const FTestDispatchWorld World(Grid, /*bAllowRoutes=*/false);
		const FDispatchOutcome Outcome = Dispatch(World, Stations, Slots, FIntPoint(40, 20));
		TestEqual(TEXT("unroutable target"), Outcome.Result, EDispatchResult::CannotReach);
		TestEqual(TEXT("no station slot consumed on a routing failure"), Stations[0].Outstanding, 0);
		TestTrue(TEXT("routing was consulted"), World.RouteQueries.Num() > 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterDispatchSpiralTest,
	"SimCopter.Dispatch.Spiral",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterDispatchSpiralTest::RunTest(const FString& Parameters)
{
	// The walker must cover every tile of the requested ring exactly once and stay inside
	// the ring's Chebyshev radius.
	const FIntPoint Origin(64, 64);
	FIntPoint Tile = Origin;
	FSpiralWalker Walker(ClearDispatchRadius);

	TSet<FIntPoint> Seen;
	Seen.Add(Origin);
	int32 MaxRing = 0;
	while (Walker.Step(Tile))
	{
		TestFalse(TEXT("the spiral never repeats a tile"), Seen.Contains(Tile));
		Seen.Add(Tile);
		MaxRing = FMath::Max(MaxRing, FMath::Max(FMath::Abs(Tile.X - Origin.X), FMath::Abs(Tile.Y - Origin.Y)));
	}

	TestTrue(TEXT("the radius-2 spiral reaches ring 2"), MaxRing >= ClearDispatchRadius);
	TestTrue(TEXT("the whole 3x3 around the origin is covered"), Seen.Num() >= 9);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
