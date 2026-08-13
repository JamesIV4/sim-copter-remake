// Copyright Epic Games, Inc. All Rights Reserved.

#include "Formats/SimCopterPeopleCityRules.h"
#include "Ground/SimCopterBehaviorVM.h"
#include "Ground/SimCopterGroundAgent.h"

#include "Misc/AutomationTest.h"

// FUN_004c9470's per-cell rules, and the guard that makes them survivable.
//
// The move core brackets its whole tile-class block in
//     if (person+0x12a != newCellX || person+0x12c != newCellY)
// so a step that stays inside the cell it started in is written through untested. That guard was
// missing here, and with it missing "the cell I am standing on is not one my row allows" is a
// permanent freeze rather than a boundary: a walk tick moves a few centimetres and a tile is four
// metres, so every one of the eight facings resolves to the same cell and every one is refused.
//
// That is what a Robber/Arsonist/Mugger standing perfectly still on the pavement was. The mission
// placer puts an on-foot criminal wherever there is room near its mission building, and "room"
// includes bare ground, a tree and a park - none of which any row admits.

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
// The city's people grid: 64 original units to a tile, 400 cm to a tile.
constexpr float TileSizeCm = 400.0f;
constexpr float CmPerOriginalUnit = TileSizeCm / 64.0f;

// One behaviour tick's displacement, FUN_004ca7d0 -> the move core: octant * (+0x164) / 12.
float StepDistanceCm(const int32 MoveSpeed)
{
	return float(MoveSpeed) / 12.0f * CmPerOriginalUnit;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterPedestrianCellRuleGateTest,
	"SimCopter.People.MoveCoreCellRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterPedestrianCellRuleGateTest::RunTest(const FString& Parameters)
{
	using FAgent = ASimCopterGroundAgent;
	using FRules = FSimCopterPeopleCityRules;
	using FVM = FSimCopterBehaviorVM;

	// The arithmetic the guard exists for. Every speed the shipped programs assign moves a walker a
	// small fraction of a tile, so a step almost never changes cell: Walk-30 is 10, Run-10 is 25,
	// and the fastest thing any program sets is well under a tile.
	for (const int32 MoveSpeed : { 6, 8, 10, 12, 16, 20, 25 })
	{
		TestTrue(
			*FString::Printf(TEXT("Move speed %d steps far less than a tile"), MoveSpeed),
			StepDistanceCm(MoveSpeed) < TileSizeCm * 0.1f);
	}
	TestEqual(TEXT("Walk-30's step, in centimetres"), StepDistanceCm(10), 6.25f * 10.0f / 12.0f);

	// The guard itself: same cell, no rules; any change of cell, rules.
	TestFalse(TEXT("A step that stays in its cell is not tested"),
		FAgent::ArePedestrianCellRulesEvaluated(64, 40, 64, 40));
	TestTrue(TEXT("A step across the east boundary is"),
		FAgent::ArePedestrianCellRulesEvaluated(64, 40, 65, 40));
	TestTrue(TEXT("...and across the south one"),
		FAgent::ArePedestrianCellRulesEvaluated(64, 40, 64, 41));
	TestTrue(TEXT("...and diagonally"),
		FAgent::ArePedestrianCellRulesEvaluated(64, 40, 65, 41));
	// Off the map answers INDEX_NONE, which differs from any real cell, so the rules run and the
	// step is refused rather than silently allowed.
	TestTrue(TEXT("An unresolved cell is treated as a crossing"),
		FAgent::ArePedestrianCellRulesEvaluated(64, 40, INDEX_NONE, INDEX_NONE));

	// THE FREEZE, spelled out. A criminal is person state 10-12, behaviour class 9, and neither the
	// per-state rows nor the ambient rows admit the classes the mission placer can leave one on:
	// bare ground is 2 (XBLD 0x00), a tree is 3 (0x06-0x0C), the small park is 5 (0x0D).
	TestEqual(TEXT("Bare ground is people class 2"), FRules::GetTileClassForBuildingId(0x00), 2);
	TestEqual(TEXT("A tree tile is people class 3"), FRules::GetTileClassForBuildingId(0x08), 3);
	TestEqual(TEXT("The small park is people class 5"), FRules::GetTileClassForBuildingId(0x0D), 5);
	for (const int32 StrandedClass : { 2, 3, 5 })
	{
		TestFalse(
			*FString::Printf(TEXT("The criminal's row refuses class %d"), StrandedClass),
			FVM::GetAllowedTileClasses(9).Contains(StrandedClass));
		TestFalse(
			*FString::Printf(TEXT("...and so does the ambient row (class %d)"), StrandedClass),
			FRules::GetAmbientStateTileClasses(9).Contains(StrandedClass));
	}
	// So had the rows been consulted per step, all eight facings would be refused on any of those
	// cells - which is the whole of the reported bug. They are only consulted on a crossing now.
	TestFalse(TEXT("Standing on bare ground no longer consults the rows at all"),
		FAgent::ArePedestrianCellRulesEvaluated(12, 12, 12, 12));

	// The building tiles a criminal's mission is placed on are all classes their row does allow, so
	// nothing above is a claim that the rows are wrong - only that they are not this walker's.
	for (const int32 BuildingId : { 0x70, 0x8C, 0xA0, 0xB5, 0xC0, 0xD4 })
	{
		TestTrue(
			*FString::Printf(TEXT("Mission building 0x%02x is walkable for a criminal"), BuildingId),
			FVM::GetAllowedTileClasses(9).Contains(
				FRules::GetTileClassForBuildingId(uint8(BuildingId))));
	}

	// FUN_004c9cc0's water escape is 0x140000 - twenty original units, a bridge deck, not a kerb.
	TestEqual(TEXT("The water-crossing deck clearance is 20 original units"),
		FAgent::WaterCrossingDeckClearanceOriginalUnits, 20.0f);
	TestTrue(TEXT("...which is far above any kerb"),
		FAgent::WaterCrossingDeckClearanceOriginalUnits * CmPerOriginalUnit > 100.0f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
