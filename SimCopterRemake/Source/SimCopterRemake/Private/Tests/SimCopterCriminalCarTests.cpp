// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Ground/SimCopterCriminalCar.h"
#include "Misc/AutomationTest.h"

using namespace SimCopterCriminalCar;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterCriminalCarTargetTest,
	"SimCopter.Crime.PursuitTargetFilter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterCriminalCarTargetTest::RunTest(const FString& Parameters)
{
	// FUN_0049dab0: obj[0x14] == 0x11e || (obj[5] & 8).
	TestTrue(TEXT("The criminal car is a target by id alone"), IsPursuitTarget(CriminalCarMessageId, false));
	TestTrue(TEXT("Anything fleeing is a target whatever it is"), IsPursuitTarget(0x11d, true));
	TestFalse(TEXT("An ordinary car parked up is not"), IsPursuitTarget(0x11c, false));

	// FUN_004b9e40: the officer's state depends on what the car was doing.
	TestEqual(TEXT("No target deploys the plain officer"), GetOfficerPersonState(false, false), 8);
	TestEqual(TEXT("A stopped non-fleeing target still deploys the plain officer"),
		GetOfficerPersonState(true, false), 8);
	TestEqual(TEXT("A fleeing target deploys the 0xe officer"), GetOfficerPersonState(true, true), 0xe);

	// FUN_0049b000: octile - the larger axis plus half the smaller.
	TestEqual(TEXT("Same tile"), GetTileStepDistance(FIntPoint(10, 10), FIntPoint(10, 10)), 0);
	TestEqual(TEXT("Straight run of 4"), GetTileStepDistance(FIntPoint(10, 10), FIntPoint(14, 10)), 4);
	TestEqual(TEXT("Diagonal 4,4 is 4 + 2"), GetTileStepDistance(FIntPoint(10, 10), FIntPoint(14, 14)), 6);
	TestEqual(TEXT("3,1 is 3 + 0"), GetTileStepDistance(FIntPoint(10, 10), FIntPoint(13, 11)), 3);
	TestEqual(TEXT("The metric is symmetric"),
		GetTileStepDistance(FIntPoint(14, 11), FIntPoint(10, 10)),
		GetTileStepDistance(FIntPoint(10, 10), FIntPoint(14, 11)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterCriminalCarSpotlightTest,
	"SimCopter.Crime.SpotlightMark",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterCriminalCarSpotlightTest::RunTest(const FString& Parameters)
{
	// FUN_004a01f0: +2 a tick, and it stops climbing at 10.
	int32 Mark = 0;
	for (int32 Tick = 0; Tick < 4; ++Tick)
	{
		Mark = AccumulateSpotlightMark(Mark, /*bLit=*/true, /*bSpotlightActive=*/true);
	}
	TestEqual(TEXT("Four lit ticks give 8"), Mark, 8);

	Mark = AccumulateSpotlightMark(Mark, true, true);
	TestEqual(TEXT("The fifth reaches the cap"), Mark, SpotlightMarkMax);
	Mark = AccumulateSpotlightMark(Mark, true, true);
	TestEqual(TEXT("...and it does not climb past it"), Mark, SpotlightMarkMax);

	// In the cone, out of the cone: the counter holds rather than decaying.
	TestEqual(TEXT("A tick out of the cone holds the mark"), AccumulateSpotlightMark(6, false, true), 6);

	// The light going out wipes it - the original's DAT_00503aa0 == 3 branch.
	TestEqual(TEXT("Switching the light off clears the mark"), AccumulateSpotlightMark(SpotlightMarkMax, true, false), 0);
	TestEqual(TEXT("...even for a car that was not lit anyway"), AccumulateSpotlightMark(4, false, false), 0);

	// The cone's ground radius widens with the beam's length.
	TestEqual(TEXT("Band 0 marks within 48 units"), GetSpotlightMarkRadiusOriginalUnits(0), 48.0f);
	TestEqual(TEXT("Band 1 within 72"), GetSpotlightMarkRadiusOriginalUnits(1), 72.0f);
	TestEqual(TEXT("Band 2 within 96"), GetSpotlightMarkRadiusOriginalUnits(2), 96.0f);
	TestEqual(TEXT("Band 3 is too far to mark anything"), GetSpotlightMarkRadiusOriginalUnits(3), 0.0f);
	TestEqual(TEXT("A missing band marks nothing"), GetSpotlightMarkRadiusOriginalUnits(INDEX_NONE), 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterCriminalCarSpeedTest,
	"SimCopter.Crime.SpeederSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterCriminalCarSpeedTest::RunTest(const FString& Parameters)
{
	constexpr float Tolerance = 0.001f;

	// FUN_0049d980. A car that is not fleeing is untouched.
	TestEqual(TEXT("An ordinary car keeps its speed"), GetFleeingSpeedMultiplier(false, 0, 1), 1.0f, Tolerance);
	TestEqual(TEXT("...even one that has somehow been marked"),
		GetFleeingSpeedMultiplier(false, SpotlightMarkMax, 0), 1.0f, Tolerance);

	// An unmarked speeder runs flat out.
	TestEqual(TEXT("An unmarked speeder runs at 1.75x"), GetFleeingSpeedMultiplier(true, 0, 1), 1.75f, Tolerance);

	// Marked, the beam drags it down - and the tighter the band the more it drags.
	const float Band0 = GetFleeingSpeedMultiplier(true, 2, 0);
	const float Band1 = GetFleeingSpeedMultiplier(true, 2, 1);
	const float Band2 = GetFleeingSpeedMultiplier(true, 2, 2);
	TestEqual(TEXT("Band 0 slows it to 1.054x"), Band0, 1.0542f, 0.001f);
	TestEqual(TEXT("Band 1 to 1.316x"), Band1, 1.3158f, 0.001f);
	TestEqual(TEXT("Band 2 to 1.522x"), Band2, 1.5217f, 0.001f);
	TestTrue(TEXT("A tighter beam always slows it more"), Band0 < Band1 && Band1 < Band2);
	TestTrue(TEXT("Any marked band beats running free"), Band2 < 1.75f);

	// Band 3 is out of the table, so a car marked at that range is not actually slowed.
	TestEqual(TEXT("Band 3 leaves the speeder at full speed"),
		GetFleeingSpeedMultiplier(true, SpotlightMarkMax, 3), 1.75f, Tolerance);

	// The relationship the whole chase depends on: even the fastest speeder running flat out has
	// to stay under the helicopter's own airspeed ceiling, or the player can never follow one and
	// the spotlight step is unreachable. The remake had this inverted while ambient cars ran at
	// 115 units/s instead of the authored 36..47.
	const float FastestSpeeder =
		static_cast<float>(RoadSpeedMaxUnitsPerSecond) * GetFleeingSpeedMultiplier(true, 0, INDEX_NONE);
	TestTrue(
		FString::Printf(TEXT("The fastest speeder (%.1f u/s) stays under the helicopter (%.1f u/s)"),
			FastestSpeeder, HelicopterTopSpeedUnitsPerSecond),
		FastestSpeeder < HelicopterTopSpeedUnitsPerSecond);

	// ...and once it is lit, the slowest marked speeder drops to roughly ordinary traffic speed,
	// which is what lets a police car close on it.
	const float SlowestMarked =
		static_cast<float>(RoadSpeedMinUnitsPerSecond) * GetFleeingSpeedMultiplier(true, 2, 0);
	TestTrue(
		FString::Printf(TEXT("A tightly lit speeder (%.1f u/s) is near ordinary traffic speed"), SlowestMarked),
		SlowestMarked < static_cast<float>(RoadSpeedMaxUnitsPerSecond));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterCriminalCarStopOrderTest,
	"SimCopter.Crime.PullOverOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterCriminalCarStopOrderTest::RunTest(const FString& Parameters)
{
	// FUN_004b89a0's truth table. This is the heart of the pursuit: a police order only lands on
	// a car the player has already lit up.
	TestTrue(TEXT("A marked car pulls over for the police"),
		AcceptsStopOrder(EState::Cruising, 2, PoliceCarMessageId, false));
	TestFalse(TEXT("An unmarked cruising car ignores the police"),
		AcceptsStopOrder(EState::Cruising, 0, PoliceCarMessageId, false));

	// The police override beats the flags a self-stop would respect.
	TestTrue(TEXT("A marked car pulls over even while already decelerating"),
		AcceptsStopOrder(EState::Cruising, SpotlightMarkMax, PoliceCarMessageId, true));

	// Two states refuse outright, marked or not: it has already been taken, or it is leaving.
	TestFalse(TEXT("An arrested car ignores the order"),
		AcceptsStopOrder(EState::Arrested, SpotlightMarkMax, PoliceCarMessageId, false));
	TestFalse(TEXT("A leaving car ignores the order"),
		AcceptsStopOrder(EState::Leaving, SpotlightMarkMax, PoliceCarMessageId, false));

	// A non-police caller falls through to the plain state test, which only two states pass.
	TestTrue(TEXT("An idling car stops for anyone"),
		AcceptsStopOrder(EState::Idling, 0, 0, false));
	TestTrue(TEXT("A car already pulling over accepts a repeat"),
		AcceptsStopOrder(EState::Stopping, 0, 0, false));
	TestFalse(TEXT("A cruising car does not stop for a non-police caller"),
		AcceptsStopOrder(EState::Cruising, 0, 0, false));
	TestFalse(TEXT("A fleeing car does not stop for a non-police caller"),
		AcceptsStopOrder(EState::Fleeing, 0, 0, false));
	TestFalse(TEXT("...and a car already stopping is not re-ordered"),
		AcceptsStopOrder(EState::Idling, 0, 0, true));

	// An unmarked fleeing car is exactly the case the player has to solve with the searchlight:
	// the police can reach it and still not be able to stop it.
	TestFalse(TEXT("An unmarked fleeing car cannot be stopped at all"),
		AcceptsStopOrder(EState::Fleeing, 0, PoliceCarMessageId, false));
	TestTrue(TEXT("...but one tick of searchlight is enough to make it stoppable"),
		AcceptsStopOrder(EState::Fleeing, SpotlightMarkStep, PoliceCarMessageId, false));

	return true;
}
