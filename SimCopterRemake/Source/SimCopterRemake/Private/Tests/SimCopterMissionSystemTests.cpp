// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Ground/SimCopterTrafficSystemActor.h"
#include "Missions/SimCopterMissionSystem.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

using namespace SimCopterMissions;

namespace
{
struct FSimCopterTestMissionWorld : public ISimCopterMissionWorld
{
	int32 BuildingFootprint = 1;
	// The id every in-bounds tile reports. 0x70 is an unoccupied building in the real XBLD property
	// table; tests that need occupants (property bit 2) set this to one of the 39 ids that carry it.
	int32 TileXbldId = 0x70;

	virtual int32 GetXbldTileId(int32 TileX, int32 TileY) const override
	{
		return (TileX >= 0 && TileX < 128 && TileY >= 0 && TileY < 128) ? TileXbldId : 0;
	}

	virtual int32 GetBuildingFootprintSize(int32 TileX, int32 TileY) const override
	{
		return BuildingFootprint;
	}

	virtual bool GetCameraTile(int32& OutTileX, int32& OutTileY) const override
	{
		OutTileX = 64;
		OutTileY = 64;
		return true;
	}

	virtual bool GetPlayerTile(int32& OutTileX, int32& OutTileY) const override
	{
		OutTileX = 64;
		OutTileY = 64;
		return true;
	}

	virtual bool TrySpawnMissionPerson(int32 SpawnMode, int32 PersonState, int32 TileX, int32 TileY, int32 EventId) override
	{
		return true;
	}

	virtual bool TryStartCarFire(int32 EventId, int32& OutTileX, int32& OutTileY) override
	{
		OutTileX = 21;
		OutTileY = 22;
		return true;
	}

	bool bSpeederCarPlaced = false;

	virtual bool TryActivateSpeederCar(int32 EventId, int32 TileX, int32 TileY) override
	{
		bSpeederCarPlaced = true;
		return true;
	}
};

// A city that is water except where bAnyBuildings puts a building, and that records every tile
// a mission person is actually spawned on.
struct FSimCopterCrimeTestWorld : public FSimCopterTestMissionWorld
{
	bool bAnyBuildings = true;
	mutable TArray<FIntPoint> SpawnedTiles;

	virtual int32 GetXbldTileId(int32 TileX, int32 TileY) const override
	{
		if (!bAnyBuildings || TileX < 0 || TileX >= 128 || TileY < 0 || TileY >= 128)
		{
			return 0;
		}
		// Buildings on the even tiles only, so an unfiltered pick would land off one about
		// three quarters of the time.
		return ((TileX % 2) == 0 && (TileY % 2) == 0) ? 0x80 : 0;
	}

	virtual bool TrySpawnMissionPerson(int32 SpawnMode, int32 PersonState, int32 TileX, int32 TileY, int32 EventId) override
	{
		SpawnedTiles.Add(FIntPoint(TileX, TileY));
		return true;
	}
};

struct FSimCopterTrafficJamTestWorld : public FSimCopterTestMissionWorld
{
	bool bJamStarted = false;
	TArray<FSimCopterMissionUiMessage> UiMessages;

	virtual bool TryStartTrafficJam(int32 EventId, int32& OutTileX, int32& OutTileY) override
	{
		bJamStarted = true;
		OutTileX = 61;
		OutTileY = 62;
		return true;
	}

	virtual void OnUiMessage(const FSimCopterMissionUiMessage& Message) override
	{
		UiMessages.Add(Message);
	}
};

FString ResolveCareerTweakPath()
{
	TArray<FString, TInlineAllocator<3>> Candidates;
	Candidates.Add(FPaths::ProjectContentDir() / TEXT("OriginalGame/tweak/career.twk"));
	Candidates.Add(FPaths::Combine(FPaths::ProjectDir(), TEXT("Reference/SimCopterOriginalGame/tweak/career.twk")));
	Candidates.Add(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reference/SimCopterOriginalGame/tweak/career.twk")));

	for (FString Candidate : Candidates)
	{
		Candidate = FPaths::ConvertRelativePathToFull(Candidate);
		FPaths::NormalizeFilename(Candidate);
		if (FPaths::FileExists(Candidate))
		{
			return Candidate;
		}
	}

	return Candidates.Last();
}

int32 CountActiveMissionsOfType(const FSimCopterMissionSystem& System, int32 TypeMask)
{
	int32 Count = 0;
	for (const FSimCopterMissionRecord& Record : System.GetRecords())
	{
		if (Record.bActive && (Record.TypeMask & TypeMask) != 0)
		{
			Count++;
		}
	}
	return Count;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterMissionSystemPRNGTest, "SimCopter.Missions.PRNGParity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterMissionSystemPRNGTest::RunTest(const FString& Parameters)
{
	FSimCopterMsvcRand Rng;
	Rng.Seed(1);

	// MSVC rand() with seed 1 produces: 41, 18467, 6334, 26500, 19169
	int32 Expected[] = { 41, 18467, 6334, 26500, 19169 };

	for (int32 i = 0; i < 5; ++i)
	{
		int32 Val = Rng.Rand();
		if (Val != Expected[i])
		{
			AddError(FString::Printf(TEXT("PRNG mismatch at step %d: Expected %d, Got %d"), i, Expected[i], Val));
			return false;
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterMissionSystemWeightTableTest, "SimCopter.Missions.WeightTableParity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterMissionSystemWeightTableTest::RunTest(const FString& Parameters)
{
	FSimCopterCareerCity City;
	City.Weights[0] = 100.0f; // Fire
	City.Weights[1] = 50.0f;  // Crime
	City.Weights[2] = 200.0f; // Rescue
	City.Weights[3] = 0.0f;   // Riot
	City.Weights[4] = 0.0f;   // Traffic
	City.Weights[5] = 150.0f; // MedEvac
	City.Weights[6] = 0.0f;   // Transport

	FSimCopterMissionSystem System;
	System.Initialize(nullptr, 1);
	System.SetCareerCity(City);

	// Fire: 100/500 = 20% -> 20
	// Crime: 50/500 = 10% -> 30
	// Rescue: 200/500 = 40% -> 70
	// Riot: 0 -> 70
	// Traffic: 0 -> 70
	// MedEvac: 150/500 = 30% -> 100
	// Transport: 0 -> 100

	const int32* Weights = System.GetCumulativeWeightTable();

	if (Weights[1] != 20 || Weights[2] != 30 || Weights[3] != 70 || Weights[4] != 70 || Weights[5] != 70 || Weights[6] != 100 || Weights[7] != 100)
	{
		AddError(FString::Printf(TEXT("Weight table mismatch: %d %d %d %d %d %d %d"), Weights[1], Weights[2], Weights[3], Weights[4], Weights[5], Weights[6], Weights[7]));
		return false;
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterMissionSystemSchedulerTest, "SimCopter.Missions.SchedulerParity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterMissionSystemSchedulerTest::RunTest(const FString& Parameters)
{
	FSimCopterCareerCity City;
	// Equal weights so we can test the mask dispatch
	for (int32 i=0; i<7; ++i) City.Weights[i] = 10.0f;

	FSimCopterMissionSystem System;
	System.Initialize(nullptr, 1);
	System.SetCareerCity(City);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterMissionSystemMarkerCoordinateTest, "SimCopter.Missions.MarkerCoordinates", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterMissionSystemMarkerCoordinateTest::RunTest(const FString& Parameters)
{
	FSimCopterTestMissionWorld World;
	FSimCopterMissionSystem System;
	System.Initialize(&World, 1);

	const int32 TransportEventId = System.CreateEventAt(10, 20, TYPE_Transport);
	TestTrue(TEXT("Transport mission should be created"), TransportEventId != INDEX_NONE);
	const FSimCopterMissionRecord* TransportRecord = System.FindRecord(TransportEventId);
	TestNotNull(TEXT("Transport record should exist"), TransportRecord);
	if (TransportRecord != nullptr)
	{
		TestTrue(TEXT("Transport destination X should be set"), TransportRecord->SecondaryX >= 0 && TransportRecord->SecondaryX < 128);
		TestTrue(TEXT("Transport destination Y should be set"), TransportRecord->SecondaryY >= 0 && TransportRecord->SecondaryY < 128);
		TestFalse(TEXT("Transport destination should differ from pickup"), TransportRecord->SecondaryX == TransportRecord->TileX && TransportRecord->SecondaryY == TransportRecord->TileY);
	}

	const int32 CarFireEventId = System.CreateEventAt(4, 5, TYPE_CarFireEvent);
	TestTrue(TEXT("Car fire mission should be created"), CarFireEventId != INDEX_NONE);
	const FSimCopterMissionRecord* CarFireRecord = System.FindRecord(CarFireEventId);
	TestNotNull(TEXT("Car fire record should exist"), CarFireRecord);
	if (CarFireRecord != nullptr)
	{
		TestEqual(TEXT("Car fire should use hook tile X"), CarFireRecord->TileX, 21);
		TestEqual(TEXT("Car fire should use hook tile Y"), CarFireRecord->TileY, 22);
		TestEqual(TEXT("Car fire should require one car outcome"), CarFireRecord->CarsCrashed, 1);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterMissionSystemFireDouseTest, "SimCopter.Missions.FireDouse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterMissionSystemFireDouseTest::RunTest(const FString& Parameters)
{
	FSimCopterTestMissionWorld World;
	FSimCopterMissionSystem System;
	System.Initialize(&World, 1);

	const int32 FireId = System.CreateEventAt(30, 40, TYPE_BuildingFire);
	TestTrue(TEXT("Building fire should be created"), FireId != INDEX_NONE);

	const int32 InitialFlames = System.GetActiveFlameCount();
	TestTrue(TEXT("Igniting a building should spawn flames"), InitialFlames > 0);

	// Water landing away from the fire does nothing.
	TestEqual(TEXT("Dousing an empty tile reports no flames in range"), System.DouseAtTile(0, 0), 0);
	TestEqual(TEXT("Flames unchanged after dousing empty tile"), System.GetActiveFlameCount(), InitialFlames);

	// The first douse over the fire tile should report the flames in range.
	TestTrue(TEXT("Dousing the fire tile reports flames in range"), System.DouseAtTile(30, 40) >= InitialFlames);

	// Sustained water extinguishes every flame and credits them as doused (not expired).
	int32 Guard = 0;
	while (System.GetActiveFlameCount() > 0 && Guard++ < 500)
	{
		System.DouseAtTile(30, 40);
	}
	TestEqual(TEXT("Sustained water extinguishes all flames"), System.GetActiveFlameCount(), 0);

	const FSimCopterMissionRecord* Record = System.FindRecord(FireId);
	TestNotNull(TEXT("Fire record should still exist"), Record);
	if (Record != nullptr)
	{
		TestTrue(TEXT("Doused flames should be credited to the mission"), Record->FlamesDoused >= InitialFlames);
		TestEqual(TEXT("No doused flame should be counted as burned out"), Record->FlamesExpired, 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterMissionSystemFootprintFireDouseTest,
	"SimCopter.Missions.FireDouseAcrossFootprint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterMissionSystemFootprintFireDouseTest::RunTest(const FString& Parameters)
{
	FSimCopterTestMissionWorld World;
	World.BuildingFootprint = 3;
	FSimCopterMissionSystem System;
	System.Initialize(&World, 1);

	const int32 FireId = System.CreateEventAt(30, 40, TYPE_BuildingFire);
	TestTrue(TEXT("Large-building fire should be created"), FireId != INDEX_NONE);

	int32 OuterFlameIndex = INDEX_NONE;
	const TArray<FSimCopterFlame>& Flames = System.GetFlames();
	for (int32 Index = 0; Index < Flames.Num(); ++Index)
	{
		if (Flames[Index].bActive &&
			Flames[Index].PosX == 0x500000 &&
			Flames[Index].PosZ == 0x400000)
		{
			OuterFlameIndex = Index;
			break;
		}
	}
	TestTrue(TEXT("Three-tile footprint should create its guaranteed outer flame"), OuterFlameIndex != INDEX_NONE);
	if (OuterFlameIndex == INDEX_NONE)
	{
		return false;
	}

	const int32 InitialHealth = Flames[OuterFlameIndex].DouseHealth1616;
	// The visible point is +80 source X and +64 source Z from anchor tile (30, 40).
	// That same world location belongs to tile (29, 41), at local (+16, 0).
	const int32 FlamesHit =
		System.DouseAtLocalOffset(29, 41, 0x100000, 0, 1);
	TestTrue(TEXT("Water landing on an outer visible flame reaches its anchor-tile record"), FlamesHit >= 1);

	const FSimCopterFlame& OuterFlameAfter = System.GetFlames()[OuterFlameIndex];
	TestTrue(
		TEXT("The outer visible flame takes douse damage"),
		!OuterFlameAfter.bActive || OuterFlameAfter.DouseHealth1616 < InitialHealth);
	return true;
}

// Regression: water has to land on a flame's own local offset to hurt it, not on the anchor
// cell's origin. IgniteBuilding puts a multi-tile building's flames far outside Fire Radius
// of that origin, so a cell-origin douse silently reaches nothing - which is why a fire truck
// aims at the flame position FUN_004b9b10 computes rather than at the tile centre.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterMissionSystemServiceFireSuppressionTest,
	"SimCopter.Missions.ServiceFireSuppressionUsesFlameOffset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterMissionSystemServiceFireSuppressionTest::RunTest(const FString& Parameters)
{
	for (const int32 Footprint : { 2, 3, 4 })
	{
		FSimCopterTestMissionWorld World;
		World.BuildingFootprint = Footprint;
		FSimCopterMissionSystem System;
		System.Initialize(&World, 1);

		const int32 FireId = System.CreateEventAt(30, 40, TYPE_BuildingFire);
		TestTrue(*FString::Printf(TEXT("Footprint %d fire should be created"), Footprint), FireId != INDEX_NONE);

		int32 FlameIndex = INDEX_NONE;
		const TArray<FSimCopterFlame>& Flames = System.GetFlames();
		for (int32 Index = 0; Index < Flames.Num(); ++Index)
		{
			if (Flames[Index].bActive)
			{
				FlameIndex = Index;
				break;
			}
		}
		TestTrue(*FString::Printf(TEXT("Footprint %d should have a flame"), Footprint), FlameIndex != INDEX_NONE);
		if (FlameIndex == INDEX_NONE)
		{
			continue;
		}

		const FSimCopterFlame Flame = Flames[FlameIndex];

		// The defect: aiming at the cell origin reaches nothing on a multi-tile building.
		TestEqual(
			*FString::Printf(TEXT("Footprint %d: a cell-origin douse reaches no flame"), Footprint),
			System.DouseAtTile(Flame.TileX, Flame.TileY),
			0);

		// The fix: aiming at the flame's own offset does reach it.
		const int32 Hit = System.DouseAtLocalOffset(Flame.TileX, Flame.TileY, Flame.PosX, Flame.PosZ, 0x10000);
		TestTrue(
			*FString::Printf(TEXT("Footprint %d: a flame-offset douse reaches at least that flame"), Footprint),
			Hit >= 1);
	}

	// A 1x1 building keeps its flames close enough that both forms work; this is why the
	// bug never showed up on the smallest buildings.
	{
		FSimCopterTestMissionWorld World;
		World.BuildingFootprint = 1;
		FSimCopterMissionSystem System;
		System.Initialize(&World, 1);
		TestTrue(TEXT("Single-tile fire should be created"), System.CreateEventAt(30, 40, TYPE_BuildingFire) != INDEX_NONE);
		TestTrue(TEXT("Footprint 1: a cell-origin douse still reaches the flame"), System.DouseAtTile(30, 40) >= 1);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterMissionSystemFireLifecycleTest, "SimCopter.Missions.FireLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterMissionSystemFireLifecycleTest::RunTest(const FString& Parameters)
{
	// FUN_004a48e0 / FUN_004a4ac0: a flame burns for the full Fire Parms "TimeToLive
	// (secs)" - 190.3s at difficulty tier 1 - and a fire only ends when every flame is
	// gone. The old port used a flat 32.0s countdown, so fires vanished on their own.
	FSimCopterTestMissionWorld World;
	FSimCopterMissionSystem System;
	System.Initialize(&World, 1);
	// Isolate the fire: no career weights means the scheduler never adds missions.
	FSimCopterCareerCity City;
	City.Difficulty = 0;
	System.SetCareerCity(City);

	const int32 FireId = System.CreateEventAt(30, 40, TYPE_BuildingFire);
	TestTrue(TEXT("Building fire should be created"), FireId != INDEX_NONE);

	const TArray<FSimCopterFlame>& Flames = System.GetFlames();
	int32 FirstFlame = INDEX_NONE;
	for (int32 i = 0; i < Flames.Num(); ++i)
	{
		if (Flames[i].bActive)
		{
			FirstFlame = i;
			break;
		}
	}
	TestTrue(TEXT("Ignition should spawn at least one flame"), FirstFlame != INDEX_NONE);
	if (FirstFlame != INDEX_NONE)
	{
		TestEqual(
			TEXT("A new flame gets the full Fire Parms TimeToLive"),
			Flames[FirstFlame].BurnCountdown,
			System.Tuning.FireTimeToLive);
		TestEqual(
			TEXT("A new flame gets tier*0x14 + Douse Points of douse health"),
			Flames[FirstFlame].DouseHealth1616,
			System.GetDifficultyTier() * 0x14 + System.Tuning.FireDousePoints);
	}

	// Two minutes of simulation is well past the old 32-second countdown but short of
	// the decoded 190.3s burn, so the fire must still be alight.
	for (int32 Step = 0; Step < 120 * 30; ++Step)
	{
		System.Tick(1.0f / 30.0f);
	}
	TestTrue(TEXT("The fire is still burning two minutes in"), System.GetActiveFlameCount() > 0);

	const FSimCopterMissionRecord* Record = System.FindRecord(FireId);
	TestNotNull(TEXT("Fire record should still exist"), Record);
	if (Record != nullptr)
	{
		TestTrue(TEXT("The fire mission has not completed itself"), Record->bActive);
		TestTrue(
			TEXT("Some flames are still outstanding"),
			Record->FlamesDoused + Record->FlamesExpired < Record->FlamesCreated);
	}

	// Burning out is a loss: the last flame of a building posts EVT_CellBurnedOut, never
	// the EVT_ObjectCaughtFire "Bldg Saved" award that only water pays.
	int32 Guard = 0;
	while (System.GetActiveFlameCount() > 0 && Guard++ < 60 * 60 * 30)
	{
		System.Tick(1.0f / 30.0f);
	}
	TestEqual(TEXT("The fire eventually burns itself out"), System.GetActiveFlameCount(), 0);

	Record = System.FindRecord(FireId);
	if (Record != nullptr)
	{
		TestTrue(TEXT("Burned-out flames are credited as expired"), Record->FlamesExpired > 0);
		TestTrue(TEXT("A fire nobody fought burns a building cell out"), Record->CellsBurnedOut > 0);
		TestEqual(TEXT("Nothing was saved from a fire nobody fought"), Record->ObjectsCaughtFire, 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterMissionSystemTransportSchedulerTimerTest, "SimCopter.Missions.TransportSchedulerTimer", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterMissionSystemTransportSchedulerTimerTest::RunTest(const FString& Parameters)
{
	FSimCopterCareerCity City;
	City.Difficulty = 0;
	for (int32 i = 0; i < 7; ++i)
	{
		City.Weights[i] = 0.0f;
	}
	City.Weights[6] = 100.0f;

	FSimCopterTestMissionWorld World;
	FSimCopterMissionSystem System;
	System.Initialize(&World, 1);
	System.SetCareerCity(City);

	for (int32 Frame = 0; Frame < 1000 && CountActiveMissionsOfType(System, TYPE_Transport) == 0; ++Frame)
	{
		System.Tick(1.0f / 60.0f);
	}

	TestEqual(TEXT("The scheduler should create exactly one transport after the first timer trip"), CountActiveMissionsOfType(System, TYPE_Transport), 1);
	TestEqual(TEXT("Scheduled transport should count against active missions"), System.GetActiveMissionCount(), 1);
	TestEqual(TEXT("Scheduled transport should not count as background"), System.GetBackgroundMissionCount(), 0);

	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		System.Tick(1.0f / 60.0f);
	}

	TestEqual(TEXT("Transport creation should re-arm the scheduler instead of spawning every frame"), CountActiveMissionsOfType(System, TYPE_Transport), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterEconomyCareerParsingTest, "SimCopter.Economy.CareerParsing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterEconomyCareerParsingTest::RunTest(const FString& Parameters)
{
	FSimCopterMissionSystem System;
	System.Initialize(nullptr, 1);
	
	const FString CareerPath = ResolveCareerTweakPath();
	
	if (!System.LoadCareerData(CareerPath))
	{
		AddError(TEXT("Failed to load career data"));
		return false;
	}
	
	const FSimCopterCareerCity& City0 = System.GetCareerCity();
	if (City0.Difficulty != 0)
	{
		AddError(FString::Printf(TEXT("City0 Difficulty should be 0, got %d"), City0.Difficulty));
		return false;
	}
	
	if (City0.PointsNeeded != 400)
	{
		AddError(FString::Printf(TEXT("City0 PointsNeeded should be 400, got %d"), City0.PointsNeeded));
		return false;
	}

	return true;
}

// The debug main menu's free-roam session: a city whose seven weights sum to zero. FUN_004a6d20
// writes an all-zero cumulative table for it, so FUN_004a6e60 can never pick a bucket no matter
// how long the countdown runs.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterMissionZeroWeightCityTest, "SimCopter.Missions.ZeroWeightCityNeverSpawns", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterMissionZeroWeightCityTest::RunTest(const FString& Parameters)
{
	FSimCopterCareerCity City;
	City.Difficulty = 0;
	for (int32 Index = 0; Index < 7; ++Index)
	{
		City.Weights[Index] = 0.0f;
	}

	FSimCopterTestMissionWorld World;
	FSimCopterMissionSystem System;
	System.Initialize(&World, 1);
	System.SetCareerCity(City);

	const int32* Cumulative = System.GetCumulativeWeightTable();
	for (int32 Bucket = 1; Bucket <= 7; ++Bucket)
	{
		TestEqual(TEXT("A zero-weight city produces an empty cumulative table"), Cumulative[Bucket], 0);
	}

	// Well past the 180s initial countdown and several easy intervals.
	for (int32 Frame = 0; Frame < 60 * 600; ++Frame)
	{
		System.Tick(1.0f / 60.0f);
	}

	TestEqual(TEXT("Free roam must not schedule any mission"), System.GetActiveMissionCount(), 0);
	TestEqual(TEXT("Free roam must not schedule any background mission"), System.GetBackgroundMissionCount(), 0);

	// A mission asked for by hand still loads, which is what the menu's "load mission" does.
	const int32 EventId = System.CreateEventOfType(TYPE_Transport);
	TestTrue(TEXT("An explicitly created mission is unaffected by the zero weights"), EventId != -1);
	TestEqual(TEXT("The explicit mission is the only active one"), System.GetActiveMissionCount(), 1);

	return true;
}

// The rescue masks are composites of the victim bit 0x10, so the name selector has to match on
// every bit of them or a bare train crash reads as a train rescue.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterMissionTypeNameTest, "SimCopter.Missions.TypeDisplayNames", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterMissionTypeNameTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("0x1 is a building fire"), FString(FSimCopterMissionSystem::GetTypeDisplayName(TYPE_BuildingFire)), FString(TEXT("Building Fire")));
	TestEqual(TEXT("0x4 is a plane crash"), FString(FSimCopterMissionSystem::GetTypeDisplayName(TYPE_PlaneCrash)), FString(TEXT("Plane Crash")));
	TestEqual(TEXT("0x100 is a train crash, not a train rescue"), FString(FSimCopterMissionSystem::GetTypeDisplayName(TYPE_TrainCrash)), FString(TEXT("Train Crash")));
	TestEqual(TEXT("0x110 is a train rescue"), FString(FSimCopterMissionSystem::GetTypeDisplayName(TYPE_TrainRescue)), FString(TEXT("Train Rescue")));
	TestEqual(TEXT("0x90 is a boat rescue"), FString(FSimCopterMissionSystem::GetTypeDisplayName(TYPE_BoatRescue)), FString(TEXT("Boat Rescue")));
	TestEqual(TEXT("0x80010 is a fire rescue"), FString(FSimCopterMissionSystem::GetTypeDisplayName(TYPE_FireRescue)), FString(TEXT("Fire Rescue")));
	TestEqual(TEXT("0x408 is a car fire"), FString(FSimCopterMissionSystem::GetTypeDisplayName(TYPE_CarFireEvent)), FString(TEXT("Car Fire")));
	TestEqual(TEXT("0x800 is a traffic jam"), FString(FSimCopterMissionSystem::GetTypeDisplayName(TYPE_TrafficJam)), FString(TEXT("Traffic Jam")));
	TestEqual(TEXT("0x20 is a medevac"), FString(FSimCopterMissionSystem::GetTypeDisplayName(TYPE_Medevac)), FString(TEXT("MedEvac")));
	TestEqual(TEXT("0x40 is a transport"), FString(FSimCopterMissionSystem::GetTypeDisplayName(TYPE_Transport)), FString(TEXT("Transport")));
	TestEqual(TEXT("0x1000 is a riot"), FString(FSimCopterMissionSystem::GetTypeDisplayName(TYPE_Riot)), FString(TEXT("Riot")));
	TestEqual(TEXT("0x100000 is the UFO"), FString(FSimCopterMissionSystem::GetTypeDisplayName(TYPE_Ufo)), FString(TEXT("UFO")));

	// A fire that has picked up the debris bit is still a fire (the promotion FUN_004a89c0 case 7
	// does to a running 0x1 mission).
	TestEqual(TEXT("0x9 is still a building fire"), FString(FSimCopterMissionSystem::GetTypeDisplayName(TYPE_BuildingFire | TYPE_Debris)), FString(TEXT("Building Fire")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterMissionRuntimeSaveRoundTripTest,
	"SimCopter.Missions.RuntimeSaveRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterMissionRuntimeSaveRoundTripTest::RunTest(const FString& Parameters)
{
	FSimCopterTestMissionWorld World;
	FSimCopterMissionSystem Source;
	Source.Initialize(&World, 1);
	FSimCopterCareerCity City;
	City.Difficulty = 2;
	City.Weights[3] = 100.0f;
	City.PointsNeeded = 1234;
	Source.RestoreSessionState(321, 4567, City);
	Source.GetRand().Seed(0x13579bdu);

	const int32 RiotEventId = Source.CreateEventAt(44, 55, TYPE_Riot);
	if (!TestTrue(TEXT("Riot fixture was created"), RiotEventId != INDEX_NONE))
	{
		return false;
	}
	Source.PostEvent(EVT_RioterDispersed, RiotEventId, 1);
	const int32 SavedScore = Source.GetScore();
	const int32 SavedCash = Source.GetCash();

	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes, true);
	if (!TestTrue(TEXT("Mission runtime state writes"), Source.SerializeRuntimeState(Writer)))
	{
		return false;
	}
	Writer.Close();
	TestTrue(TEXT("Mission runtime blob is non-empty"), !Bytes.IsEmpty());

	FSimCopterMissionSystem Restored;
	Restored.Initialize(&World, 1);
	FMemoryReader Reader(Bytes, true);
	if (!TestTrue(TEXT("Mission runtime state reads"), Restored.SerializeRuntimeState(Reader)))
	{
		return false;
	}
	Reader.Close();

	TestEqual(TEXT("Score resumes"), Restored.GetScore(), SavedScore);
	TestEqual(TEXT("Cash resumes"), Restored.GetCash(), SavedCash);
	TestEqual(TEXT("Difficulty resumes"), Restored.GetDifficultyTier(), 3);
	const FSimCopterMissionRecord* Record = Restored.FindRecord(RiotEventId);
	if (!TestNotNull(TEXT("Active riot record resumes"), Record))
	{
		return false;
	}
	TestTrue(TEXT("Riot remains active after loading"), Record->bActive);
	TestEqual(TEXT("Riot tile X resumes"), Record->TileX, 44);
	TestEqual(TEXT("Riot tile Y resumes"), Record->TileY, 55);
	TestEqual(TEXT("Riot progress resumes"), Record->RiotersDispersed, 1);
	TestEqual(TEXT("Mission PRNG resumes at the exact next value"), Restored.GetRand().Rand(), Source.GetRand().Rand());
	TestEqual(TEXT("Original riot spawn agitation is seven"), RioterSpawnAgitation, 7);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterOriginalPickupMessageTest,
	"SimCopter.Missions.OriginalPickupMessage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterOriginalPickupMessageTest::RunTest(const FString& Parameters)
{
	FSimCopterTrafficJamTestWorld World;
	FSimCopterMissionSystem System;
	System.Initialize(&World, 1);

	const int32 EventId = System.CreateEventOfType(TYPE_Transport);
	if (EventId == INDEX_NONE)
	{
		AddError(TEXT("Could not create the transport fixture"));
		return false;
	}

	World.UiMessages.Reset();
	System.PostEvent(EVT_VictimPickedUp, EventId, 1);

	const FSimCopterMissionUiMessage* PickupMessage = World.UiMessages.FindByPredicate(
		[EventId](const FSimCopterMissionUiMessage& Message)
		{
			return Message.EventId == EventId && Message.Kind == 9;
		});
	if (!TestNotNull(TEXT("Picking up a transport Sim posts the cash/update message"), PickupMessage))
	{
		return false;
	}

	// FUN_004aa150 case 0x13 selects STRINGTABLE 0x3aa: retail text "Sim Picked Up!".
	TestEqual(TEXT("Pickup uses the original string-resource id"), PickupMessage->TextId, 0x3aa);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterTrafficJamCarClearTest,
	"SimCopter.Missions.TrafficJamCarClearCompletes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterTrafficJamCarClearTest::RunTest(const FString& Parameters)
{
	FSimCopterTrafficJamTestWorld World;
	FSimCopterMissionSystem System;
	System.Initialize(&World, 1);

	const int32 EventId = System.CreateEventOfType(TYPE_TrafficJam);
	if (EventId == INDEX_NONE)
	{
		AddError(TEXT("Could not create the traffic-jam fixture"));
		return false;
	}
	TestTrue(TEXT("The world marks an initial jammed car"), World.bJamStarted);

	const FSimCopterMissionRecord* Record = System.FindRecord(EventId);
	if (!TestNotNull(TEXT("The jam has a mission record"), Record))
	{
		return false;
	}
	// FUN_0049fca0 -> FUN_0049fe30 posts EVT_JamCarAdded for the initial 0x200 car.
	TestEqual(TEXT("The initial jammed car is counted"), Record->JamCarCount, 1);

	// FUN_0049d7e0 handles megaphone message 0 per car and posts EVT_CarCleared (0x1b).
	System.PostEvent(EVT_CarCleared, EventId, 1);
	System.Tick(1.0f / 30.0f);
	const FSimCopterMissionRecord* AfterClear = System.FindRecord(EventId);
	TestTrue(TEXT("Clearing every counted car resolves the traffic-jam mission"),
		AfterClear == nullptr || !AfterClear->bActive);
	return true;
}

// FUN_00408210 (enter city) + FUN_00407f30/FUN_004080c0 (open session).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterMissionSessionStartTest, "SimCopter.Missions.SessionStart", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterMissionSessionStartTest::RunTest(const FString& Parameters)
{
	FSimCopterMissionSystem System;
	System.Initialize(nullptr, 1);

	if (!System.LoadCareerData(ResolveCareerTweakPath()))
	{
		AddError(TEXT("Failed to load career data"));
		return false;
	}

	TestEqual(TEXT("career.twk supplies 30 cities"), System.GetCareerCityCount(), 30);

	// City25 is difficulty 3 (tier 4) in the shipped table.
	if (!System.SelectCareerCity(25))
	{
		AddError(TEXT("SelectCareerCity(25) failed"));
		return false;
	}

	TestEqual(TEXT("Selecting a city records the index"), System.GetCareerCityIndex(), 25);
	TestEqual(TEXT("City25 is difficulty 3"), System.GetCareerCity().Difficulty, 3);
	TestEqual(TEXT("Difficulty tier is difficulty + 1"), System.GetDifficultyTier(), 4);

	System.AddScore(500);
	System.SelectCareerCity(0);
	TestEqual(TEXT("Entering a city clears the city score"), System.GetScore(), 0);
	TestEqual(TEXT("Entering a city adopts its tier"), System.GetDifficultyTier(), 1);

	System.AddScore(120);
	System.AddCash(50);
	System.BeginSession();
	TestEqual(TEXT("A new session starts at $1000"), System.GetCash(), FSimCopterMissionSystem::SessionStartingCash);
	TestEqual(TEXT("A new session starts at 0 points"), System.GetScore(), 0);

	TestFalse(TEXT("Out-of-range cities are rejected"), System.SelectCareerCity(30));
	TestTrue(TEXT("The career city list is addressable"), System.GetCareerCityByIndex(29) != nullptr);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterMissionsXbldPropertyTableTest, "SimCopter.Missions.XbldPropertyTable", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterMissionsXbldPropertyTableTest::RunTest(const FString& Parameters)
{
	// DAT_00504848, extracted from SimCopter.exe's .data (see GetXbldPropertyFlags). These are
	// counts and spot values straight out of the blob in
	// Docs/scratchpad/agent-sessions/2026-08-05-mission-authenticity/xbld_property_table.bin -
	// enough that a bad edit to the literal array cannot pass unnoticed.
	int32 Solid = 0;      // bit 0
	int32 Buildings = 0;  // bit 1
	int32 Occupied = 0;   // bit 2
	for (int32 Id = 0; Id <= 0xff; ++Id)
	{
		const uint8 Flags = FSimCopterMissionSystem::GetXbldPropertyFlags(Id);
		if (Flags & 0x01) ++Solid;
		if (Flags & 0x02) ++Buildings;
		if (Flags & 0x04) ++Occupied;
	}
	TestEqual(TEXT("117 ids are solid (bit 0)"), Solid, 117);
	TestEqual(TEXT("57 ids are buildings (bit 1)"), Buildings, 57);
	TestEqual(TEXT("39 ids have occupants (bit 2)"), Occupied, 39);

	// Occupancy almost implies building: 38 of the 39 occupied ids also carry bit 1. The single
	// exception is 0xfd, whose byte is 0x05 - solid and occupied, with the building bit clear. That
	// is what the table says, so it is asserted rather than tidied away; anything else appearing
	// here means the array has been corrupted.
	for (int32 Id = 0; Id <= 0xff; ++Id)
	{
		const uint8 Flags = FSimCopterMissionSystem::GetXbldPropertyFlags(Id);
		if ((Flags & 0x04) != 0 && (Flags & 0x02) == 0 && Id != 0xfd)
		{
			AddError(FString::Printf(TEXT("id 0x%02x has occupants but is not a building"), Id));
		}
	}
	TestEqual(TEXT("0xfd is the lone solid+occupied non-building"), int32(FSimCopterMissionSystem::GetXbldPropertyFlags(0xfd)), 0x05);

	// The three the fire-rescue placer excludes by hand really do carry the bit; the old stand-in
	// wrongly denied it to them, which is what made that exclusion look redundant.
	TestTrue(TEXT("0xd1 (hospital) is occupied"), (FSimCopterMissionSystem::GetXbldPropertyFlags(0xd1) & 0x04) != 0);
	TestTrue(TEXT("0xd2 is occupied"), (FSimCopterMissionSystem::GetXbldPropertyFlags(0xd2) & 0x04) != 0);
	TestTrue(TEXT("0xd3 is occupied"), (FSimCopterMissionSystem::GetXbldPropertyFlags(0xd3) & 0x04) != 0);

	// Nothing below 0x81 has occupants - the fact that makes the original's signed-char read in
	// the scheduled fire-rescue placer unable to ever succeed.
	for (int32 Id = 0; Id < 0x81; ++Id)
	{
		if ((FSimCopterMissionSystem::GetXbldPropertyFlags(Id) & 0x04) != 0)
		{
			AddError(FString::Printf(TEXT("id 0x%02x below 0x81 unexpectedly has occupants"), Id));
		}
	}

	// Roads (0x1d..0x2b) are not buildings and carry no occupants.
	TestEqual(TEXT("a road tile has no properties"), int32(FSimCopterMissionSystem::GetXbldPropertyFlags(0x20) & 0x06), 0);
	// Out of range answers null, as FUN_0049a4d0 does.
	TestEqual(TEXT("negative ids answer 0"), int32(FSimCopterMissionSystem::GetXbldPropertyFlags(-1)), 0);
	TestEqual(TEXT("ids past 0xff answer 0"), int32(FSimCopterMissionSystem::GetXbldPropertyFlags(0x100)), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterMissionsBuildingFireDifficultyTest, "SimCopter.Missions.BuildingFireDifficulty", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterMissionsBuildingFireDifficultyTest::RunTest(const FString& Parameters)
{
	// FUN_004a92f0's param_1 == 1 filter: which buildings a scheduled fire may start in, by tier.
	// The rolls make individual calls non-deterministic, so assert the shape over many samples -
	// what matters is that tier 1 is size-1 only and that the bigger sizes open up as tiers rise.
	FSimCopterTestMissionWorld World;

	// 0x90 carries property bit 2 (occupants); 0x70 is a building without it. Both arms of the
	// filter are real now that the table is extracted rather than stood in for.
	constexpr int32 OccupiedId = 0x90;
	constexpr int32 EmptyId = 0x70;

	auto AcceptRate = [&World](int32 Tier, int32 Footprint, int32 XbldId) -> float
	{
		World.BuildingFootprint = Footprint;
		World.TileXbldId = XbldId;
		FSimCopterMissionSystem System;
		System.Initialize(&World, 1);
		FSimCopterCareerCity City;
		for (int32 i = 0; i < 7; ++i) City.Weights[i] = 10.0f;
		City.Difficulty = Tier - 1;
		System.SetCareerCity(City);

		int32 Accepted = 0;
		constexpr int32 Samples = 400;
		for (int32 i = 0; i < Samples; ++i)
		{
			if (System.IsBuildingFireTargetAllowedByDifficulty(30, 30))
			{
				++Accepted;
			}
		}
		return static_cast<float>(Accepted) / static_cast<float>(Samples);
	};

	// Tier 1 takes 1x1 buildings and nothing else - every fire in an easy city is a shack.
	TestEqual(TEXT("Tier 1 always accepts a 1x1"), AcceptRate(1, 1, OccupiedId), 1.0f);
	TestEqual(TEXT("Tier 1 never accepts a 2x2"), AcceptRate(1, 2, OccupiedId), 0.0f);
	TestEqual(TEXT("Tier 1 never accepts a 4x4"), AcceptRate(1, 4, OccupiedId), 0.0f);

	// Tier 2's size-1 arm is the one-in-three roll...
	const float Tier2Small = AcceptRate(2, 1, OccupiedId);
	TestTrue(TEXT("Tier 2 takes a 1x1 about a third of the time"), Tier2Small > 0.2f && Tier2Small < 0.5f);
	// ...and its other arm wants size 2-3 with NO occupants. This is the arm the old stand-in made
	// unreachable, because it claimed every building had people in it.
	TestTrue(TEXT("Tier 2 takes an empty 2x2 most of the time"), AcceptRate(2, 2, EmptyId) > 0.5f);
	TestTrue(TEXT("Tier 2 rejects an occupied 2x2 except via the 1-in-3"), AcceptRate(2, 2, OccupiedId) < 0.1f);

	// Tiers 3 and 4 invert that: they want the big OCCUPIED buildings tier 1 refused outright.
	TestTrue(TEXT("Tier 3 accepts an occupied 4x4"), AcceptRate(3, 4, OccupiedId) > 0.5f);
	TestTrue(TEXT("Tier 4 accepts an occupied 4x4"), AcceptRate(4, 4, OccupiedId) > 0.5f);
	TestTrue(TEXT("Tier 4 mostly rejects an empty 4x4"), AcceptRate(4, 4, EmptyId) < 0.35f);
	// ...and tier 4 no longer wants the smallest ones except through its one-in-seven wildcard.
	TestTrue(TEXT("Tier 4 rarely settles for a 1x1"), AcceptRate(4, 1, OccupiedId) < 0.35f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterEconomyScoreProgressionTest, "SimCopter.Economy.ScoreProgression", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterEconomyScoreProgressionTest::RunTest(const FString& Parameters)
{
	FSimCopterMissionSystem System;
	System.Initialize(nullptr, 1);
	
	const FString CareerPath = ResolveCareerTweakPath();
	
	if (!System.LoadCareerData(CareerPath))
	{
		AddError(TEXT("Failed to load career data"));
		return false;
	}
	
	// Default City0 requires 400 points.
	System.AddScore(200);
	System.AdvanceCareerIfComplete(); // Should do nothing
	
	if (System.IsLevelComplete())
	{
		AddError(TEXT("Should not be level complete at 200 points."));
		return false;
	}

	if (System.GetScore() != 200)
	{
		AddError(TEXT("Score should remain 200."));
		return false;
	}
	
	System.AddScore(200);
	System.AdvanceCareerIfComplete(); // Should trigger level complete state
	
	if (!System.IsLevelComplete())
	{
		AddError(TEXT("Should be level complete at 400 points."));
		return false;
	}

	// Score must NOT reset to 0 mid-gameplay upon reaching PointsNeeded
	if (System.GetScore() != 400)
	{
		AddError(TEXT("Score should remain intact (400) upon reaching level completion points."));
		return false;
	}

	// Score resets only after level transition finishes and AdvanceCareerCity is called
	System.AdvanceCareerCity();
	if (System.GetScore() != 0)
	{
		AddError(TEXT("Score should reset to 0 after advancing city."));
		return false;
	}

	if (System.IsLevelComplete())
	{
		AddError(TEXT("Level complete state should clear for new city."));
		return false;
	}

	return true;
}

// The user-visible symptom this guards: a criminal appearing out in the ocean, where nothing
// on the police side can reach. FUN_004a92f0 sends 0x200/0x2000/0x20000 through LAB_004a95ff,
// whose only candidate tiles are ones carrying a mission building.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterCrimePlacementTest, "SimCopter.Missions.CrimePlacementNeedsBuilding", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterCrimePlacementTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("An ordinary building is a candidate"), FSimCopterMissionSystem::IsMissionBuildingTile(0x70));
	TestTrue(TEXT("The last building id is a candidate"), FSimCopterMissionSystem::IsMissionBuildingTile(0xdb));
	TestFalse(TEXT("Open water is not"), FSimCopterMissionSystem::IsMissionBuildingTile(0x00));
	TestFalse(TEXT("Roads are not"), FSimCopterMissionSystem::IsMissionBuildingTile(0x3d));
	TestFalse(TEXT("The tile below the range is not"), FSimCopterMissionSystem::IsMissionBuildingTile(0x6f));
	TestFalse(TEXT("0xdc is past the range"), FSimCopterMissionSystem::IsMissionBuildingTile(0xdc));
	for (int32 Excluded = 0xd1; Excluded <= 0xd3; ++Excluded)
	{
		TestFalse(FString::Printf(TEXT("0x%x is excluded"), Excluded), FSimCopterMissionSystem::IsMissionBuildingTile(Excluded));
	}

	// A city that is nothing but water: every one of the five tries has to be refused, and no
	// criminal may reach the world.
	FSimCopterCrimeTestWorld Ocean;
	Ocean.bAnyBuildings = false;
	FSimCopterMissionSystem OceanSystem;
	OceanSystem.Initialize(&Ocean, 1);
	for (const int32 CrimeMask : { int32(TYPE_CriminalA), int32(TYPE_SpeederEvent), int32(TYPE_CriminalC) })
	{
		TestEqual(
			FString::Printf(TEXT("Crime 0x%x is not placed in a city with no buildings"), CrimeMask),
			OceanSystem.CreateEventOfType(CrimeMask),
			-1);
	}
	TestEqual(TEXT("No criminal was spawned into the water"), Ocean.SpawnedTiles.Num(), 0);

	// The same city with buildings on the even tiles: every criminal that does get placed must
	// have landed on one of them.
	FSimCopterCrimeTestWorld City;
	City.bAnyBuildings = true;
	FSimCopterMissionSystem CitySystem;
	CitySystem.Initialize(&City, 1);
	for (int32 Attempt = 0; Attempt < 40; ++Attempt)
	{
		CitySystem.CreateEventOfType(TYPE_CriminalA);
	}
	TestTrue(TEXT("Criminals were placed at all"), City.SpawnedTiles.Num() > 0);
	for (const FIntPoint& Tile : City.SpawnedTiles)
	{
		TestTrue(
			FString::Printf(TEXT("Criminal at (%d, %d) is on a mission building"), Tile.X, Tile.Y),
			FSimCopterMissionSystem::IsMissionBuildingTile(City.GetXbldTileId(Tile.X, Tile.Y)));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterSpeederCarRecordTest, "SimCopter.Missions.SpeederCarStaysOpen", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterSpeederCarRecordTest::RunTest(const FString& Parameters)
{
	// FUN_004a7a10's 0x4000 branch writes 1 to the record's +0x94 once the car is placed. Miss it
	// and the crime completion test reads `caught + casualties < 0` as already satisfied, so the
	// mission resolves on its very first update and the player never sees the car.
	FSimCopterCrimeTestWorld World;
	World.bAnyBuildings = true;
	FSimCopterMissionSystem System;
	System.Initialize(&World, 1);

	const int32 EventId = System.CreateEventOfType(TYPE_CriminalCar);
	if (EventId == -1)
	{
		AddError(TEXT("The speeder car mission was not created at all"));
		return false;
	}
	TestTrue(TEXT("The world was asked to place a speeder car"), World.bSpeederCarPlaced);

	const FSimCopterMissionRecord* Record = System.FindRecord(EventId);
	if (Record == nullptr)
	{
		AddError(TEXT("No record for the speeder car mission"));
		return false;
	}
	TestEqual(TEXT("The record wants one criminal caught"), Record->TargetCount, 1);
	TestEqual(TEXT("...and starts with none"), Record->CriminalsCaught, 0);

	// Run the system for a while: an uncaught speeder must keep its record open.
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		System.Tick(1.0f / 30.0f);
	}
	const FSimCopterMissionRecord* AfterUpdates = System.FindRecord(EventId);
	TestTrue(TEXT("The mission is still open four seconds later"),
		AfterUpdates != nullptr && AfterUpdates->bActive);

	// FUN_004b8c90 posts EVT_CriminalCaught as the car is taken away. That is what takes
	// CriminalsCaught to TargetCount and completes the mission - the earlier port posted
	// EVT_SetCategory(CAT_ExpireSilently) instead, which is FUN_004b8b60's *failure* branch and
	// makes the update loop skip the completion test, so nothing was ever paid out.
	const int32 ScoreBefore = System.GetScore();
	System.PostEvent(EVT_CriminalCaught, EventId, 1);
	System.Tick(1.0f / 30.0f);

	const FSimCopterMissionRecord* AfterCatch = System.FindRecord(EventId);
	TestTrue(TEXT("Catching the driver closes the mission"),
		AfterCatch == nullptr || !AfterCatch->bActive);
	TestTrue(TEXT("...and it pays out"), System.GetScore() > ScoreBefore);

	// The failure branch must not pay: CAT_ExpireSilently retires the record instead.
	FSimCopterCrimeTestWorld QuietWorld;
	FSimCopterMissionSystem QuietSystem;
	QuietSystem.Initialize(&QuietWorld, 1);
	const int32 QuietEvent = QuietSystem.CreateEventOfType(TYPE_CriminalCar);
	if (QuietEvent != -1)
	{
		const int32 QuietScoreBefore = QuietSystem.GetScore();
		QuietSystem.PostEvent(EVT_SetCategory, QuietEvent, CAT_ExpireSilently);
		for (int32 Frame = 0; Frame < 30; ++Frame)
		{
			QuietSystem.Tick(1.0f / 30.0f);
		}
		TestEqual(TEXT("A retired speeder pays nothing"), QuietSystem.GetScore(), QuietScoreBefore);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterMedevacCasualtyRewardTest,
	"SimCopter.Missions.MedevacCasualtyHasNoDeliveryReward",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterMedevacCasualtyRewardTest::RunTest(const FString& Parameters)
{
	FSimCopterTestMissionWorld World;
	FSimCopterMissionSystem System;
	System.Initialize(&World, 1);

	const int32 EventId = System.CreateEventOfType(TYPE_Medevac);
	if (EventId == INDEX_NONE)
	{
		AddError(TEXT("Could not create the medevac fixture"));
		return false;
	}

	System.PostEvent(EVT_VictimPickedUp, EventId, 1);
	System.PostEvent(EVT_PersonDied, EventId, 1);

	const FSimCopterMissionRecord* BeforeCompletion = System.FindRecord(EventId);
	if (BeforeCompletion == nullptr)
	{
		AddError(TEXT("The medevac record vanished before lifecycle completion"));
		return false;
	}
	TestEqual(TEXT("A deceased patient is a casualty"), BeforeCompletion->Casualties, 1);
	TestEqual(TEXT("Death is not a medevac delivery"), BeforeCompletion->MedevacDelivered, 0);

	const int32 CashBeforeCompletion = System.GetCash();
	const int32 ScoreBeforeCompletion = System.GetScore();
	for (int32 Frame = 0; Frame < 4; ++Frame)
	{
		System.Tick(1.0f / 30.0f);
	}

	const FSimCopterMissionRecord* RetiredRecord = nullptr;
	for (const FSimCopterMissionRecord& Record : System.GetRecords())
	{
		if (Record.EventId == EventId)
		{
			RetiredRecord = &Record;
			break;
		}
	}
	TestTrue(TEXT("The casualty completes the scoring record"),
		RetiredRecord != nullptr && !RetiredRecord->bActive);
	TestTrue(TEXT("The retired record still has no delivered patient"),
		RetiredRecord != nullptr && RetiredRecord->MedevacDelivered == 0);
	TestEqual(TEXT("A casualty completion adds no cash"), System.GetCash(), CashBeforeCompletion);
	TestEqual(TEXT("A casualty completion adds no score"), System.GetScore(), ScoreBeforeCompletion);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterTransportBuildingSpawnTest,
	"SimCopter.Missions.TransportPassengerSpawnOutsideBuildings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterTransportBuildingSpawnTest::RunTest(const FString& Parameters)
{
	FSimCopterCrimeTestWorld City;
	City.bAnyBuildings = true;
	FSimCopterMissionSystem System;
	System.Initialize(&City, 1);

	const int32 TransportId = System.CreateEventOfType(TYPE_Transport);
	TestTrue(TEXT("Transport mission created"), TransportId != INDEX_NONE);

	const FSimCopterMissionRecord* Record = System.FindRecord(TransportId);
	TestNotNull(TEXT("Transport record exists"), Record);
	if (Record != nullptr)
	{
		TestTrue(
			TEXT("Transport passenger pickup tile is on a valid mission building tile"),
			FSimCopterMissionSystem::IsMissionBuildingTile(City.GetXbldTileId(Record->TileX, Record->TileY)));
	}

	return true;
}

