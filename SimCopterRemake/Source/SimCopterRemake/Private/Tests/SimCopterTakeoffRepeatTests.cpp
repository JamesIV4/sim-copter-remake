// Copyright Epic Games, Inc. All Rights Reserved.
//
// Scratch reproduction for "the second takeoff takes a long time". Drives the pure flight model
// through take off -> climb -> descend -> land -> take off again and reports how many steps each
// spool needed, plus every Parked/Flying transition and touchdown edge (the CHOPSTAR/CHOPSTOP
// triggers) so an oscillation shows up as a count rather than a guess.

#if WITH_DEV_AUTOMATION_TESTS

#include "Flight/SimCopterFlightModel.h"
#include "Flight/SimCopterHelicopterPawn.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr float StepSeconds = 1.0f / 60.0f;

FSimCopterFlightEnvironment GroundAt(int32 Terrain, int32 Surface)
{
	FSimCopterFlightEnvironment Env;
	Env.TerrainHeight = Terrain;
	Env.SurfaceHeight = Surface;
	Env.bTerrainFlat = true;
	Env.bHostileSurface = false;
	return Env;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterTakeoffRepeatTest,
	"SimCopter.Flight.TakeoffRepeat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterTakeoffRepeatTest::RunTest(const FString& Parameters)
{
	FSimCopterFlightModel Model;
	Model.ResetOnSurface(0, 0, 0);
	const FSimCopterFlightEnvironment Env = GroundAt(0, 0);

	FSimCopterFlightInputs Up;
	Up.ClimbCommand = 1;
	FSimCopterFlightInputs Neutral;
	Neutral.ClimbCommand = 0;
	FSimCopterFlightInputs Down;
	Down.ClimbCommand = -1;
	FSimCopterFlightEvents Events;

	auto Spool = [&](const TCHAR* Label) -> int32
	{
		int32 Steps = 0;
		for (; Steps < 600 && Model.State != ESimCopterFlightState::Flying; ++Steps)
		{
			Model.Step(StepSeconds, Up, Env, Events);
		}
		AddInfo(FString::Printf(
			TEXT("%s: %d steps (%.2f s), rotor %.1f, altitude %.3f"),
			Label,
			Steps,
			Steps * StepSeconds,
			SimCopterFixed::ToFloat(Model.RotorSpeed),
			SimCopterFixed::ToFloat(Model.Altitude)));
		return Steps;
	};

	// 100/s from a standing start to the 300 gate: three seconds, whatever the step size.
	const int32 FirstSpool = Spool(TEXT("first takeoff"));
	const float FirstSpoolSeconds = FirstSpool * StepSeconds;
	TestTrue(
		*FString::Printf(TEXT("first takeoff is about three seconds (was %.2f s)"), FirstSpoolSeconds),
		FirstSpoolSeconds >= 2.8f && FirstSpoolSeconds <= 3.3f);

	// Climb well clear, then come back down and settle.
	for (int32 Step = 0; Step < 240; ++Step)
	{
		Model.Step(StepSeconds, Up, Env, Events);
	}
	AddInfo(FString::Printf(TEXT("after climb: altitude %.3f, rotor %.1f"),
		SimCopterFixed::ToFloat(Model.Altitude), SimCopterFixed::ToFloat(Model.RotorSpeed)));

	int32 DescendSteps = 0;
	for (; DescendSteps < 3000 && Model.State != ESimCopterFlightState::Parked; ++DescendSteps)
	{
		// Ease off near the deck so the landing limits are satisfied.
		const FSimCopterFlightInputs& In =
			Model.ClimbSpeed < -Model.Tuning.LandMaxYSpeed / 2 ? Neutral : Down;
		Model.Step(StepSeconds, In, Env, Events);
	}
	if (!TestEqual(TEXT("landed again"), int32(Model.State), int32(ESimCopterFlightState::Parked)))
	{
		return false;
	}
	AddInfo(FString::Printf(
		TEXT("landed after %d steps: rotor %.1f, altitude %.3f"),
		DescendSteps,
		SimCopterFixed::ToFloat(Model.RotorSpeed),
		SimCopterFixed::ToFloat(Model.Altitude)));

	// Sit parked with the collective down for a second, as a player would before pulling up.
	for (int32 Step = 0; Step < 60; ++Step)
	{
		Model.Step(StepSeconds, Neutral, Env, Events);
	}
	AddInfo(FString::Printf(TEXT("after a second parked: rotor %.1f"), SimCopterFixed::ToFloat(Model.RotorSpeed)));

	// Now the reported case. Count the Parked/Flying flips and touchdown edges while it happens:
	// every touchdown plays CHOPSTOP and every parked frame with the collective up plays CHOPSTAR,
	// so a flip count above one IS the back-to-back wind up/wind down.
	int32 SecondSpool = 0;
	int32 Flips = 0;
	int32 Touchdowns = 0;
	int32 Liftoffs = 0;
	ESimCopterFlightState Previous = Model.State;
	for (; SecondSpool < 900; ++SecondSpool)
	{
		Model.Step(StepSeconds, Up, Env, Events);
		if (Model.State != Previous)
		{
			++Flips;
			Previous = Model.State;
		}
		Touchdowns += Events.bTouchedDown ? 1 : 0;
		Liftoffs += Events.bLiftedOff ? 1 : 0;
		// Stop once it is properly away rather than at the first Flying frame.
		if (Model.State == ESimCopterFlightState::Flying && Model.AboveGround > SimCopterFixed::FromFloat(5.0f))
		{
			break;
		}
	}
	AddInfo(FString::Printf(
		TEXT("second takeoff: %d steps (%.2f s), %d state flips, %d touchdowns, %d liftoffs, rotor %.1f"),
		SecondSpool,
		SecondSpool * StepSeconds,
		Flips,
		Touchdowns,
		Liftoffs,
		SimCopterFixed::ToFloat(Model.RotorSpeed)));

	// A helicopter that has just landed still has most of its rotor speed - it decays at only
	// 50/s while parked - so the second takeoff is *faster* than the first, never slower. Any
	// extra lift-off or touchdown here is the Parked/Flying oscillation that would play CHOPSTAR
	// and CHOPSTOP over each other.
	TestEqual(TEXT("the second takeoff lifts off exactly once"), Liftoffs, 1);
	TestEqual(TEXT("the second takeoff does not touch back down"), Touchdowns, 0);
	TestEqual(TEXT("the second takeoff changes state exactly once"), Flips, 1);
	TestTrue(
		*FString::Printf(TEXT("the second takeoff is no slower than the first (was %.2f s)"), SecondSpool * StepSeconds),
		SecondSpool <= FirstSpool);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterDryRotorTest,
	"SimCopter.Flight.DryRotorSpoolsDown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterDryRotorTest::RunTest(const FString& Parameters)
{
	// FUN_00485f50's last statement writes the no-fuel override into the collective field itself
	// (`if (heli[0xcc] < 1) heli[3] = -1;`), so FUN_00487740 sees -1 too and winds the parked
	// rotor down. Deriving it privately inside the vertical step left the rotor step reading the
	// player's raw input, which froze the rotor instead - and a frozen rotor above the lift gate
	// keeps the blur disc spinning on a helicopter that has run dry.
	FSimCopterFlightModel Model;
	Model.ResetOnSurface(0, 0, 0);
	const FSimCopterFlightEnvironment Env = GroundAt(0, 0);

	FSimCopterFlightInputs Up;
	Up.ClimbCommand = 1;
	FSimCopterFlightEvents Events;

	Model.RotorSpeed = FSimCopterFlightModel::RotorLiftGate;
	Model.Fuel = 0;

	for (int32 Step = 0; Step < 60; ++Step)
	{
		Model.Step(StepSeconds, Up, Env, Events);
	}

	TestEqual(TEXT("a dry helicopter cannot take off"), int32(Model.State), int32(ESimCopterFlightState::Parked));
	TestTrue(
		*FString::Printf(TEXT("the dry rotor winds down (was %.1f)"), SimCopterFixed::ToFloat(Model.RotorSpeed)),
		Model.RotorSpeed < FSimCopterFlightModel::RotorLiftGate);

	// With fuel, the same held collective spools it up instead - the override is the only reason.
	FSimCopterFlightModel Fuelled;
	Fuelled.ResetOnSurface(0, 0, 0);
	const int32 StartRotor = Fuelled.RotorSpeed;
	for (int32 Step = 0; Step < 60; ++Step)
	{
		Fuelled.Step(StepSeconds, Up, Env, Events);
	}
	TestTrue(TEXT("a fuelled rotor spools up on the same input"), Fuelled.RotorSpeed > StartRotor);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterEngineHoldArbitrationTest,
	"SimCopter.Flight.EngineHoldArbitration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterEngineHoldArbitrationTest::RunTest(const FString& Parameters)
{
	using EHold = ASimCopterHelicopterPawn::EEngineHoldAction;

	TestEqual(TEXT("nothing held does nothing"),
		int32(ASimCopterHelicopterPawn::ResolveEngineHoldAction(false, false)), int32(EHold::None));
	TestEqual(TEXT("start alone starts"),
		int32(ASimCopterHelicopterPawn::ResolveEngineHoldAction(true, false)), int32(EHold::Start));
	TestEqual(TEXT("shutdown alone shuts down"),
		int32(ASimCopterHelicopterPawn::ResolveEngineHoldAction(false, true)), int32(EHold::Shutdown));

	// The one that mattered. Both live used to let the two hold timers take turns: start would
	// run out and set the engine running, shutdown would immediately run out and clear it, about
	// once a second forever. With the engine off BuildFlightInputs returns dead controls, so the
	// rotor sawtoothed instead of spooling and the collective looked like it did nothing - and
	// LastClimbCommand flapping 1/0 while parked is exactly what plays CHOPSTAR then CHOPSTOP,
	// back to back, which is how the bug announced itself.
	TestEqual(TEXT("start and shutdown together cancel rather than oscillate"),
		int32(ASimCopterHelicopterPawn::ResolveEngineHoldAction(true, true)), int32(EHold::None));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
