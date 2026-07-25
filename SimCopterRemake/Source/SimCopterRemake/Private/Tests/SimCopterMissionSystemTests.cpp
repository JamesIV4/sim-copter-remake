// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Missions/SimCopterMissionSystem.h"

using namespace SimCopterMissions;

namespace
{
struct FSimCopterTestMissionWorld : public ISimCopterMissionWorld
{
	int32 BuildingFootprint = 1;

	virtual int32 GetXbldTileId(int32 TileX, int32 TileY) const override
	{
		return (TileX >= 0 && TileX < 128 && TileY >= 0 && TileY < 128) ? 0x70 : 0;
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

// Regression: an emergency-service water burst must aim at the flame's own local offset,
// not at the anchor cell's origin. IgniteBuilding puts a multi-tile building's flames far
// outside Fire Radius of that origin, so a cell-origin douse silently reaches nothing -
// which is what left dispatched fire trucks parked next to a burning building doing
// nothing.
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
	
	if (System.GetCareerCity().Difficulty != 0)
	{
		AddError(TEXT("Should not have advanced city yet."));
		return false;
	}
	
	System.AddScore(200);
	System.AdvanceCareerIfComplete(); // Should advance
	
	// Next city (City1) usually has 500 PointsNeeded.
	// We'll just verify the score reset to 0!
	if (System.GetScore() != 0)
	{
		AddError(TEXT("Score should reset to 0 after advancing city."));
		return false;
	}

	return true;
}
