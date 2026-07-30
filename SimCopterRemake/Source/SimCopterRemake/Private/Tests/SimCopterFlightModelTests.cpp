// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Flight/SimCopterFlightModel.h"
#include "Misc/AutomationTest.h"

// Behavioural tests for the decompiled flight model (SimCopter.exe
// FUN_00484d20 and its sub-steps). These validate the invariants decoded from
// the executable rather than any remake-specific tuning; the constants come
// from Docs/scratchpad/ghidra/out_heli_*.txt.

namespace
{
constexpr float StepSeconds = 1.0f / 20.0f; // the original's effective frame cap

FSimCopterFlightEnvironment FlatGroundAt(int32 Height)
{
	FSimCopterFlightEnvironment Env;
	Env.TerrainHeight = Height;
	Env.SurfaceHeight = Height;
	Env.bTerrainFlat = true;
	Env.bHostileSurface = false;
	return Env;
}

// Spools the rotor and lifts off, returning the number of steps used.
int32 SpoolAndLiftOff(FSimCopterFlightModel& Model, const FSimCopterFlightEnvironment& Env, int32 MaxSteps = 200)
{
	FSimCopterFlightInputs Inputs;
	Inputs.ClimbCommand = 1;
	FSimCopterFlightEvents Events;
	for (int32 StepIndex = 0; StepIndex < MaxSteps; ++StepIndex)
	{
		Model.Step(StepSeconds, Inputs, Env, Events);
		if (Model.State == ESimCopterFlightState::Flying)
		{
			return StepIndex + 1;
		}
	}
	return MaxSteps;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterFlightFixedMathTest,
	"SimCopter.Flight.FixedMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterFlightFixedMathTest::RunTest(const FString& Parameters)
{
	using namespace SimCopterFixed;

	// 16.16 multiply/divide round-trips (FUN_0046c49d / FUN_0046c4bf).
	TestEqual(TEXT("2.5 * 4.0"), Mul(FromFloat(2.5f), FromFloat(4.0f)), FromFloat(10.0f));
	TestEqual(TEXT("10.0 / 4.0"), Div(FromFloat(10.0f), FromFloat(4.0f)), FromFloat(2.5f));
	TestEqual(TEXT("div by zero returns numerator"), Div(1234, 0), 1234);

	// Tenth-degree sine table (FUN_0046c4dc): 900.0 = 90 degrees.
	int32 Sin = 0;
	int32 Cos = 0;
	SinCos(FromFloat(900.0f), Sin, Cos);
	TestEqual(TEXT("sin(90deg)"), Sin, One);
	TestTrue(TEXT("cos(90deg) ~ 0"), FMath::Abs(Cos) <= 8);

	SinCos(FromFloat(1800.0f), Sin, Cos);
	TestTrue(TEXT("sin(180deg) ~ 0"), FMath::Abs(Sin) <= 8);
	TestEqual(TEXT("cos(180deg)"), Cos, -One);

	// Wrap at the original full turn of 3600.0 tenth-degrees.
	TestEqual(TEXT("wrap negative"), WrapAngle(FromFloat(-100.0f)), FromFloat(3500.0f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterFlightSpoolGateTest,
	"SimCopter.Flight.RotorSpoolGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterFlightSpoolGateTest::RunTest(const FString& Parameters)
{
	// FUN_00487160: holding the collective spools the rotor at 100/s; there is
	// no lift and no takeoff until it passes 300 (about three seconds).
	FSimCopterFlightModel Model;
	Model.ResetOnSurface(0, 0, 0);
	const FSimCopterFlightEnvironment Env = FlatGroundAt(0);

	FSimCopterFlightInputs Inputs;
	Inputs.ClimbCommand = 1;
	FSimCopterFlightEvents Events;

	// After one second the rotor is spooling but the helicopter has not moved.
	const int32 StartAltitude = Model.Altitude;
	for (int32 StepIndex = 0; StepIndex < 20; ++StepIndex)
	{
		Model.Step(StepSeconds, Inputs, Env, Events);
	}
	TestTrue(TEXT("rotor spooling after 1s"), Model.RotorSpeed > 0);
	TestTrue(TEXT("still below lift gate"), Model.RotorSpeed < FSimCopterFlightModel::RotorLiftGate);
	TestEqual(TEXT("no altitude gain while spooling"), Model.Altitude, StartAltitude);
	TestEqual(TEXT("still parked"), static_cast<int32>(Model.State), static_cast<int32>(ESimCopterFlightState::Parked));
	TestFalse(TEXT("no blur disc while spooling"), Model.bRotorBlurDisc);

	// Keep holding: lift-off happens shortly after the gate at ~3 seconds.
	int32 LiftSteps = 0;
	for (int32 StepIndex = 0; StepIndex < 200 && Model.State != ESimCopterFlightState::Flying; ++StepIndex)
	{
		Model.Step(StepSeconds, Inputs, Env, Events);
		++LiftSteps;
	}
	TestEqual(TEXT("lifted off"), static_cast<int32>(Model.State), static_cast<int32>(ESimCopterFlightState::Flying));
	TestTrue(TEXT("gate passed near 3s"), LiftSteps >= 30 && LiftSteps <= 60);
	TestTrue(TEXT("blur disc on at lift RPM"), Model.bRotorBlurDisc);

	// Climb continues and altitude rises once flying.
	for (int32 StepIndex = 0; StepIndex < 40; ++StepIndex)
	{
		Model.Step(StepSeconds, Inputs, Env, Events);
	}
	TestTrue(TEXT("climbing"), Model.Altitude > StartAltitude);
	TestTrue(
		TEXT("climb capped at 4x ClimbRate x load"),
		Model.ClimbSpeed <= SimCopterFixed::Mul(Model.Tuning.ClimbRate * 4, Model.LoadFactor));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterFlightPitchSpeedTest,
	"SimCopter.Flight.PitchDrivesSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterFlightPitchSpeedTest::RunTest(const FString& Parameters)
{
	// FUN_00485f50/FUN_00486a30/FUN_00486e90: the pitch key ramps the pitch
	// target, the target is clamped to MaxPitch, and the forward speed chases
	// the smoothed pitch (tenth-degrees double as units/s of airspeed).
	FSimCopterFlightModel Model;
	Model.ResetOnSurface(0, 0, 0);
	const FSimCopterFlightEnvironment Ground = FlatGroundAt(0);
	SpoolAndLiftOff(Model, Ground);

	// Climb away from the ground-proximity pitch bonus band, then push forward.
	FSimCopterFlightInputs Inputs;
	Inputs.ClimbCommand = 1;
	FSimCopterFlightEvents Events;
	for (int32 StepIndex = 0; StepIndex < 200; ++StepIndex)
	{
		Model.Step(StepSeconds, Inputs, Ground, Events);
	}

	Inputs.ClimbCommand = 0;
	Inputs.bPitchForwardKey = true;
	const int32 StartZ = Model.PosZ;
	for (int32 StepIndex = 0; StepIndex < 400; ++StepIndex)
	{
		Model.Step(StepSeconds, Inputs, Ground, Events);
	}

	// Turbulence adds +-2 tenth-degrees of noise on a healthy airframe, so the
	// clamp is validated with a small margin.
	TestTrue(
		TEXT("pitch target clamped near MaxPitch"),
		Model.PitchTarget <= Model.Tuning.MaxPitch + SimCopterFixed::FromFloat(4.0f));
	TestTrue(
		TEXT("speed approaches the smoothed pitch"),
		FMath::Abs(Model.ForwardSpeed - Model.PitchSmoothed) < SimCopterFixed::FromFloat(24.0f));
	TestTrue(TEXT("moved forward (+Z at heading 0)"), Model.PosZ > StartZ);
	TestTrue(TEXT("heading unchanged"), Model.Heading < SimCopterFixed::FromFloat(25.0f) || Model.Heading > SimCopterFixed::FromFloat(3575.0f));

	// Releasing the key decays the pitch target toward level.
	Inputs.bPitchForwardKey = false;
	const int32 HeldPitch = Model.PitchTarget;
	for (int32 StepIndex = 0; StepIndex < 40; ++StepIndex)
	{
		Model.Step(StepSeconds, Inputs, Ground, Events);
	}
	TestTrue(TEXT("pitch decays without input"), Model.PitchTarget < HeldPitch / 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterFlightTurnTest,
	"SimCopter.Flight.CoordinatedTurn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterFlightTurnTest::RunTest(const FString& Parameters)
{
	// FUN_00485f50 + FUN_00486a30: the turn key ramps bank and yaw rate
	// together; the yaw rate clamps at MaxYawRate and integrates into the
	// heading at x15 tenth-degrees per second.
	FSimCopterFlightModel Model;
	Model.ResetOnSurface(0, 0, 0);
	const FSimCopterFlightEnvironment Ground = FlatGroundAt(0);
	SpoolAndLiftOff(Model, Ground);

	FSimCopterFlightInputs Inputs;
	Inputs.ClimbCommand = 1;
	FSimCopterFlightEvents Events;
	for (int32 StepIndex = 0; StepIndex < 100; ++StepIndex)
	{
		Model.Step(StepSeconds, Inputs, Ground, Events);
	}

	Inputs.ClimbCommand = 0;
	Inputs.bTurnRightKey = true;
	for (int32 StepIndex = 0; StepIndex < 200; ++StepIndex)
	{
		Model.Step(StepSeconds, Inputs, Ground, Events);
	}

	TestTrue(
		TEXT("yaw rate clamped at MaxYawRate"),
		Model.YawRateTarget <= Model.Tuning.MaxYawRate + SimCopterFixed::FromFloat(4.0f));
	TestTrue(TEXT("bank leans into the turn (negative)"), Model.BankTarget < 0);
	TestTrue(
		TEXT("bank clamped at MaxBank"),
		Model.BankTarget >= -(Model.Tuning.MaxBank + SimCopterFixed::FromFloat(4.0f)));
	TestTrue(TEXT("heading advanced clockwise"), Model.Heading > SimCopterFixed::FromFloat(100.0f));

	// Full-rate turn covers MaxYawRate * 15 tenth-degrees per second.
	const int32 HeadingBefore = Model.Heading;
	Model.Step(StepSeconds, Inputs, Ground, Events);
	const int32 HeadingStep = SimCopterFixed::WrapAngle(Model.Heading - HeadingBefore);
	const int32 ExpectedStep = SimCopterFixed::Mul(Model.YawRateSmoothed, SimCopterFixed::Mul(SimCopterFixed::FromFloat(StepSeconds), 0xf0000));
	TestTrue(TEXT("heading step matches yaw x15 x dt"), FMath::Abs(HeadingStep - ExpectedStep) <= SimCopterFixed::FromFloat(1.0f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterFlightCeilingTest,
	"SimCopter.Flight.AltitudeCeiling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterFlightCeilingTest::RunTest(const FString& Parameters)
{
	// FUN_00487160: above 800 units over the terrain the collective loses
	// authority and the helicopter sinks at MaxDescentRate.
	FSimCopterFlightModel Model;
	Model.ResetOnSurface(0, 0, 0);
	const FSimCopterFlightEnvironment Ground = FlatGroundAt(0);
	SpoolAndLiftOff(Model, Ground);

	// Cheat the altitude straight past the ceiling.
	Model.Altitude = FSimCopterFlightModel::CeilingAboveTerrain + SimCopterFixed::FromFloat(50.0f);

	FSimCopterFlightInputs Inputs;
	Inputs.ClimbCommand = 1;
	FSimCopterFlightEvents Events;
	const int32 StartAltitude = Model.Altitude;
	for (int32 StepIndex = 0; StepIndex < 20; ++StepIndex)
	{
		Model.Step(StepSeconds, Inputs, Ground, Events);
	}
	TestTrue(TEXT("sinks above the ceiling despite full collective"), Model.Altitude < StartAltitude);
	TestEqual(TEXT("descends at MaxDescentRate"), Model.ClimbSpeed, -Model.Tuning.MaxDescentRate);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterFlightLandingTest,
	"SimCopter.Flight.LandingRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterFlightLandingTest::RunTest(const FString& Parameters)
{
	// FUN_00487160: a gentle, level descent onto flat terrain parks the
	// helicopter with the settle offset of 1.2 units; the same descent onto
	// hostile (water/wilderness) terrain bounces with a splash instead.
	FSimCopterFlightModel Model;
	Model.ResetOnSurface(0, 0, 0);
	const FSimCopterFlightEnvironment Ground = FlatGroundAt(0);
	SpoolAndLiftOff(Model, Ground);

	FSimCopterFlightInputs Inputs;
	Inputs.ClimbCommand = 1;
	FSimCopterFlightEvents Events;
	for (int32 StepIndex = 0; StepIndex < 100; ++StepIndex)
	{
		Model.Step(StepSeconds, Inputs, Ground, Events);
	}
	TestTrue(TEXT("gained height"), Model.Altitude > SimCopterFixed::FromFloat(10.0f));

	// Descend gently until touchdown.
	Inputs.ClimbCommand = -1;
	bool bLanded = false;
	for (int32 StepIndex = 0; StepIndex < 600 && !bLanded; ++StepIndex)
	{
		// Feather the collective so the descent stays inside LandMaxYSpeed.
		Inputs.ClimbCommand = Model.ClimbSpeed < -Model.Tuning.LandMaxYSpeed / 2 ? 0 : -1;
		Model.Step(StepSeconds, Inputs, Ground, Events);
		bLanded |= Events.bTouchedDown;
	}
	TestTrue(TEXT("touched down"), bLanded);
	TestEqual(TEXT("parked"), static_cast<int32>(Model.State), static_cast<int32>(ESimCopterFlightState::Parked));
	TestEqual(TEXT("settled at surface + 1.2 units"), Model.Altitude, Ground.TerrainHeight + 0x13333);
	TestEqual(TEXT("no damage on a good landing"), Model.HitPoints, Model.Tuning.MaxDamage);

	// Water: same profile must splash-bounce, not land.
	FSimCopterFlightModel WaterModel;
	WaterModel.ResetOnSurface(0, 0, 0);
	FSimCopterFlightEnvironment Water = FlatGroundAt(0);
	Water.bHostileSurface = true;
	Water.bTerrainFlat = false;
	SpoolAndLiftOff(WaterModel, FlatGroundAt(0));
	FSimCopterFlightInputs WaterInputs;
	WaterInputs.ClimbCommand = 1;
	for (int32 StepIndex = 0; StepIndex < 60; ++StepIndex)
	{
		WaterModel.Step(StepSeconds, WaterInputs, Water, Events);
	}
	WaterInputs.ClimbCommand = -1;
	bool bSplashed = false;
	for (int32 StepIndex = 0; StepIndex < 600 && !bSplashed; ++StepIndex)
	{
		WaterModel.Step(StepSeconds, WaterInputs, Water, Events);
		bSplashed |= Events.bSplashBounce;
	}
	TestTrue(TEXT("splash bounce on water"), bSplashed);
	TestTrue(TEXT("still flying (no water landing)"), WaterModel.State == ESimCopterFlightState::Flying);
	TestTrue(TEXT("splash cost hit points"), WaterModel.HitPoints < WaterModel.Tuning.MaxDamage);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterFlightDamagedLandingTest,
	"SimCopter.Flight.DamagedLanding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterFlightDamagedLandingTest::RunTest(const FString& Parameters)
{
	// Regression guard for the inverted surface push. FUN_00487160 pushes a
	// helicopter back up at 4x ClimbRate when it reaches the ground with the
	// collective neutral only if `param_1[0x53] == 0` - the terrain flat flag
	// CLEAR, i.e. ground it cannot land on. Testing the flag the other way up
	// shoves it off flat, landable ground instead, and since the push happens
	// every frame the helicopter reaches the surface, the settle never completes.
	//
	// The profile is how a player actually lands: hold descend, release at a
	// chosen height, coast the rest. Feathering the collective against
	// LandMaxYSpeed hides the bug, because the frames where the descend key is
	// down skip the neutral branch entirely.
	auto LandFromHeight = [](int32 HitPoints, float ReleaseHeightUnits)
	{
		FSimCopterFlightModel Model;
		Model.ResetOnSurface(0, 0, 0);
		const FSimCopterFlightEnvironment Ground = FlatGroundAt(0);
		SpoolAndLiftOff(Model, Ground);

		FSimCopterFlightInputs Inputs;
		Inputs.ClimbCommand = 1;
		FSimCopterFlightEvents Events;
		for (int32 StepIndex = 0; StepIndex < 240; ++StepIndex)
		{
			Model.Step(StepSeconds, Inputs, Ground, Events);
		}

		// Damage after the climb so the shake only shapes the descent.
		Model.HitPoints = HitPoints;

		const int32 ReleaseHeight = SimCopterFixed::FromFloat(ReleaseHeightUnits);
		for (int32 StepIndex = 0; StepIndex < 800; ++StepIndex)
		{
			Inputs.ClimbCommand = Model.AboveGround > ReleaseHeight ? -1 : 0;
			Model.Step(StepSeconds, Inputs, Ground, Events);
			if (Events.bTouchedDown)
			{
				return true;
			}
		}
		return false;
	};

	FSimCopterFlightModel Reference;
	const int32 MaxDamage = Reference.Tuning.MaxDamage;

	// 6.0 units is inside the band the inverted push used to make unlandable at
	// every damage level; 2.0 and 10.0 bracket it.
	const float ReleaseHeights[] = {2.0f, 6.0f, 10.0f};
	for (float ReleaseHeight : ReleaseHeights)
	{
		TestTrue(
			*FString::Printf(TEXT("healthy helicopter lands released at %.0f units"), ReleaseHeight),
			LandFromHeight(MaxDamage, ReleaseHeight));

		// FUN_00489800 amplitude becomes (MaxDamage - hp) / 20 instead of 3, so these
		// two shake several times harder all the way down.
		TestTrue(
			*FString::Printf(TEXT("half-damaged helicopter lands released at %.0f units"), ReleaseHeight),
			LandFromHeight(MaxDamage / 2, ReleaseHeight));

		TestTrue(
			*FString::Printf(TEXT("near-destroyed helicopter lands released at %.0f units"), ReleaseHeight),
			LandFromHeight(MaxDamage / 10, ReleaseHeight));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterFlightTurbulenceTest,
	"SimCopter.Flight.TurbulenceAndDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterFlightTurbulenceTest::RunTest(const FString& Parameters)
{
	// FUN_00489800: a healthy airframe jitters within +-2 tenth-degrees; heavy
	// damage widens the shake; hovering in the fire band burns hit points.
	FSimCopterFlightModel Model;
	Model.ResetOnSurface(0, 0, 0);
	const FSimCopterFlightEnvironment Ground = FlatGroundAt(0);
	SpoolAndLiftOff(Model, Ground);

	FSimCopterFlightInputs Inputs;
	FSimCopterFlightEvents Events;
	int32 MaxHealthyShake = 0;
	for (int32 StepIndex = 0; StepIndex < 100; ++StepIndex)
	{
		Model.Step(StepSeconds, Inputs, Ground, Events);
		MaxHealthyShake = FMath::Max(MaxHealthyShake, FMath::Abs(Model.TurbPitch));
	}
	TestTrue(TEXT("healthy shake within amplitude 3"), MaxHealthyShake <= SimCopterFixed::FromFloat(3.0f));

	// Half-damaged: amplitude includes (MaxDamage - hp)/20.
	Model.HitPoints = Model.Tuning.MaxDamage / 2;
	int32 MaxDamagedShake = 0;
	for (int32 StepIndex = 0; StepIndex < 100; ++StepIndex)
	{
		Model.Step(StepSeconds, Inputs, Ground, Events);
		MaxDamagedShake = FMath::Max(MaxDamagedShake, FMath::Abs(Model.TurbPitch));
	}
	TestTrue(TEXT("damaged airframe shakes harder"), MaxDamagedShake > MaxHealthyShake);

	// Fire band: hit points drain and the wreck eventually starts dying.
	FSimCopterFlightEnvironment Fire = Ground;
	Fire.FireHeightDelta = SimCopterFixed::FromFloat(20.0f); // inside [-48, 61.1]
	const int32 HpBefore = Model.HitPoints;
	Model.Step(StepSeconds, Inputs, Fire, Events);
	TestTrue(TEXT("fire band burns hit points"), Model.HitPoints < HpBefore);

	Model.HitPoints = -1;
	Model.Step(StepSeconds, Inputs, Ground, Events);
	TestTrue(TEXT("negative hit points start the death spiral"), Events.bStartedDying || Model.State == ESimCopterFlightState::Dying);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterFlightEasyModelTest,
	"SimCopter.Flight.EasyModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterFlightEasyModelTest::RunTest(const FString& Parameters)
{
	// The DAT_00503aa0 != 0 branches of FUN_00485f50, FUN_00486a30 and
	// FUN_00486e90. Two models are flown side by side from the same state with
	// the same inputs; only bEasyFlightModel differs.
	auto MakeAirborne = [](bool bEasy)
	{
		FSimCopterFlightModel Model;
		Model.bEasyFlightModel = bEasy;
		Model.ResetOnSurface(0, 0, 0);
		const FSimCopterFlightEnvironment Ground = FlatGroundAt(0);
		SpoolAndLiftOff(Model, Ground);

		// Climb well clear of the 150-unit ground-proximity pitch bonus band, so
		// the clamp comparison below is the plain MaxPitch one.
		FSimCopterFlightInputs Inputs;
		Inputs.ClimbCommand = 1;
		FSimCopterFlightEvents Events;
		for (int32 StepIndex = 0; StepIndex < 200; ++StepIndex)
		{
			Model.Step(StepSeconds, Inputs, Ground, Events);
		}
		return Model;
	};

	const FSimCopterFlightEnvironment Ground = FlatGroundAt(0);
	FSimCopterFlightModel Standard = MakeAirborne(false);
	FSimCopterFlightModel Easy = MakeAirborne(true);

	// The two models share their tuning, so the divergences below are the code's.
	TestEqual(TEXT("same MaxPitch"), Easy.Tuning.MaxPitch, Standard.Tuning.MaxPitch);

	// FUN_00485f50: one frame of the pitch key ramps the easy model half as far.
	FSimCopterFlightInputs Pitch;
	Pitch.bPitchForwardKey = true;
	FSimCopterFlightEvents Events;
	Standard.PitchTarget = 0;
	Easy.PitchTarget = 0;
	Standard.Step(StepSeconds, Pitch, Ground, Events);
	Easy.Step(StepSeconds, Pitch, Ground, Events);
	// Turbulence rides on top of the ramp, so compare with its +-2 tenth-degree band.
	TestTrue(
		TEXT("easy pitch key ramps at half rate"),
		FMath::Abs(Easy.PitchTarget * 2 - Standard.PitchTarget) < SimCopterFixed::FromFloat(6.0f));

	// Hold the key to the clamp: FUN_00486a30 halves the pitch limit.
	for (int32 StepIndex = 0; StepIndex < 600; ++StepIndex)
	{
		Standard.Step(StepSeconds, Pitch, Ground, Events);
		Easy.Step(StepSeconds, Pitch, Ground, Events);
	}
	TestTrue(
		TEXT("standard reaches MaxPitch"),
		Standard.PitchTarget > Standard.Tuning.MaxPitch - SimCopterFixed::FromFloat(4.0f));
	TestTrue(
		TEXT("easy clamps at half MaxPitch"),
		Easy.PitchTarget < (Easy.Tuning.MaxPitch >> 1) + SimCopterFixed::FromFloat(4.0f));

	// FUN_00486e90: half the pitch but double the speed per degree, so the two
	// models end up cruising at about the same airspeed at full deflection.
	TestTrue(
		TEXT("easy top speed matches standard despite half the pitch"),
		FMath::Abs(Easy.ForwardSpeed - Standard.ForwardSpeed) < SimCopterFixed::FromFloat(24.0f));

	// Releasing everything: FUN_00485f50's easy pitch decay is (1 - dt) rather
	// than (1 - 2*dt), so the trimmed nose attitude persists across more frames.
	const FSimCopterFlightInputs Neutral;
	const int32 StandardHeldPitch = Standard.PitchTarget;
	const int32 EasyHeldPitch = Easy.PitchTarget;
	for (int32 StepIndex = 0; StepIndex < 10; ++StepIndex)
	{
		Standard.Step(StepSeconds, Neutral, Ground, Events);
		Easy.Step(StepSeconds, Neutral, Ground, Events);
	}
	TestTrue(
		TEXT("easy keeps more of its pitch trim"),
		SimCopterFixed::Div(Easy.PitchTarget, EasyHeldPitch) >
			SimCopterFixed::Div(Standard.PitchTarget, StandardHeldPitch));

	// FUN_00486e90's other easy branch: closing on a *lower* speed target uses
	// >> 4 instead of >> 5. Measured with the nose pinned level so the target is
	// the same (zero) for both models and only the shift differs - in normal
	// flight the easy model's doubled target masks this.
	FSimCopterFlightModel StandardCoast = MakeAirborne(false);
	FSimCopterFlightModel EasyCoast = MakeAirborne(true);
	const int32 CoastSpeed = SimCopterFixed::FromFloat(320.0f);
	StandardCoast.PitchTarget = StandardCoast.PitchSmoothed = 0;
	EasyCoast.PitchTarget = EasyCoast.PitchSmoothed = 0;
	StandardCoast.ForwardSpeed = CoastSpeed;
	EasyCoast.ForwardSpeed = CoastSpeed;
	StandardCoast.Step(StepSeconds, Neutral, Ground, Events);
	EasyCoast.Step(StepSeconds, Neutral, Ground, Events);
	const int32 StandardBleed = CoastSpeed - StandardCoast.ForwardSpeed;
	const int32 EasyBleed = CoastSpeed - EasyCoast.ForwardSpeed;
	// Not exactly 2x any more: the chase is referenced to SpeedChaseFramePeriod, so a
	// step compounds several of the original's frames, and compounding a doubled
	// per-frame fraction does not double the result. 1/16 against 1/32 still lands
	// just under twice as much shed per step.
	const float BleedRatio = static_cast<float>(EasyBleed) / static_cast<float>(FMath::Max(StandardBleed, 1));
	TestTrue(
		*FString::Printf(TEXT("easy bleeds speed roughly twice as fast (x%.2f)"), BleedRatio),
		BleedRatio > 1.7f && BleedRatio < 2.1f);

	// The slide ramp re-reads Ctrl6 unhalved in FUN_00485f50 - the trap.
	FSimCopterFlightModel StandardSlide = MakeAirborne(false);
	FSimCopterFlightModel EasySlide = MakeAirborne(true);
	FSimCopterFlightInputs Slide;
	Slide.bSlideLeftKey = true;
	StandardSlide.SlideTarget = 0;
	EasySlide.SlideTarget = 0;
	StandardSlide.Step(StepSeconds, Slide, Ground, Events);
	EasySlide.Step(StepSeconds, Slide, Ground, Events);
	TestTrue(
		TEXT("slide ramp is not halved by the easy model"),
		FMath::Abs(EasySlide.SlideTarget - StandardSlide.SlideTarget) < SimCopterFixed::FromFloat(6.0f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterFlightTurbulenceFrameRateTest,
	"SimCopter.Flight.TurbulenceFrameRate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterFlightTurbulenceFrameRateTest::RunTest(const FString& Parameters)
{
	// FUN_00489800 pushes one sample and injects one average per rendered frame,
	// which makes the shake a function of the frame rate (excursion ~ 1/sqrt(dt)).
	// The remake pins the ring to a 20 Hz clock and scales the injection by the
	// substep's share of it, so the same damage shakes the same amount whether the
	// pawn is substepping at 20 or 60 Hz. Before that, a near-dead airframe peaked
	// at 140 tenth-degrees of pitch at 60 Hz against 77 at 20 Hz.
	auto PeakShake = [](float Dt, int32 HitPoints, int32& OutPeakPitch, int32& OutPeakSlide)
	{
		FSimCopterFlightModel Model;
		Model.ResetOnSurface(0, 0, 0);
		const FSimCopterFlightEnvironment Ground = FlatGroundAt(0);

		FSimCopterFlightInputs Inputs;
		Inputs.ClimbCommand = 1;
		FSimCopterFlightEvents Events;
		const int32 StepsPerSecond = FMath::RoundToInt(1.0f / Dt);
		for (int32 StepIndex = 0; StepIndex < StepsPerSecond * 10; ++StepIndex)
		{
			Model.Step(Dt, Inputs, Ground, Events);
		}

		// Hands off, hovering: everything the attitude does from here is shake.
		Model.HitPoints = HitPoints;
		const FSimCopterFlightInputs Neutral;
		OutPeakPitch = 0;
		OutPeakSlide = 0;
		for (int32 StepIndex = 0; StepIndex < StepsPerSecond * 30; ++StepIndex)
		{
			Model.Step(Dt, Neutral, Ground, Events);
			OutPeakPitch = FMath::Max(OutPeakPitch, FMath::Abs(Model.PitchTarget));
			OutPeakSlide = FMath::Max(OutPeakSlide, FMath::Abs(Model.SlideTarget));
		}
	};

	FSimCopterFlightModel Reference;
	const int32 HitPointCases[] = {Reference.Tuning.MaxDamage / 2, Reference.Tuning.MaxDamage / 10};
	for (int32 HitPoints : HitPointCases)
	{
		int32 SlowPitch = 0;
		int32 SlowSlide = 0;
		int32 FastPitch = 0;
		int32 FastSlide = 0;
		PeakShake(1.0f / 20.0f, HitPoints, SlowPitch, SlowSlide);
		PeakShake(1.0f / 60.0f, HitPoints, FastPitch, FastSlide);

		// The two rates draw different random sequences, so this is a check that the
		// amplitude is the same size, not that the noise matches sample for sample.
		TestTrue(
			*FString::Printf(TEXT("hp %d: pitch shake within 40%% across 20/60 Hz (%.1f vs %.1f)"),
				HitPoints,
				SimCopterFixed::ToFloat(SlowPitch),
				SimCopterFixed::ToFloat(FastPitch)),
			FMath::Abs(FastPitch - SlowPitch) < FMath::Max(SlowPitch, FastPitch) * 2 / 5);
		TestTrue(
			*FString::Printf(TEXT("hp %d: slide shake within 40%% across 20/60 Hz (%.1f vs %.1f)"),
				HitPoints,
				SimCopterFixed::ToFloat(SlowSlide),
				SimCopterFixed::ToFloat(FastSlide)),
			FMath::Abs(FastSlide - SlowSlide) < FMath::Max(SlowSlide, FastSlide) * 2 / 5);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterFlightFrameRateIndependenceTest,
	"SimCopter.Flight.FrameRateIndependence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterFlightFrameRateIndependenceTest::RunTest(const FString& Parameters)
{
	// Every rule the original wrote per frame with no delta term - the neutral
	// collective decay, the forward-speed chase, the attitude EMA, the rotor step -
	// is converted against OriginalFrameSeconds, so the same stick script has to
	// produce the same flight at any substep rate. The pawn substeps at up to 60 Hz
	// and shorter than that whenever the display runs faster, so 144 and 240 are
	// the rates that actually matter, not just 60.
	struct FOutcome
	{
		int32 Altitude = 0;
		int32 ForwardSpeed = 0;
		int32 PitchSmoothed = 0;
		int32 Heading = 0;
	};

	auto FlyScript = [](float Dt)
	{
		FSimCopterFlightModel Model;
		Model.ResetOnSurface(0, 0, 0);
		const FSimCopterFlightEnvironment Ground = FlatGroundAt(0);
		const int32 Rate = FMath::RoundToInt(1.0f / Dt);

		FSimCopterFlightInputs Inputs;
		FSimCopterFlightEvents Events;

		auto Hold = [&](float Seconds)
		{
			const int32 Steps = FMath::RoundToInt(Seconds * Rate);
			for (int32 StepIndex = 0; StepIndex < Steps; ++StepIndex)
			{
				Model.Step(Dt, Inputs, Ground, Events);
			}
		};

		// Spool and climb, accelerate, coast, turn, then release everything and let
		// the collective decay arrest the descent - one leg per converted rule.
		Inputs.ClimbCommand = 1;
		Hold(10.0f);
		Inputs.bPitchForwardKey = true;
		Hold(6.0f);
		Inputs.bPitchForwardKey = false;
		Inputs.ClimbCommand = 0;
		Hold(4.0f);
		Inputs.bTurnRightKey = true;
		Hold(4.0f);
		Inputs.bTurnRightKey = false;
		Inputs.ClimbCommand = -1;
		Hold(3.0f);
		Inputs.ClimbCommand = 0;
		Hold(3.0f);

		FOutcome Outcome;
		Outcome.Altitude = Model.Altitude;
		Outcome.ForwardSpeed = Model.ForwardSpeed;
		Outcome.PitchSmoothed = Model.PitchSmoothed;
		Outcome.Heading = Model.Heading;
		return Outcome;
	};

	// 20 Hz is the original's own frame, so it is the reference every other rate
	// has to match.
	const FOutcome Reference = FlyScript(1.0f / 20.0f);
	const int32 Rates[] = {30, 60, 144, 240};
	for (int32 Rate : Rates)
	{
		const FOutcome Actual = FlyScript(1.0f / Rate);

		auto Close = [this, Rate](const TCHAR* What, int32 Value, int32 Expected, float ToleranceUnits)
		{
			TestTrue(
				*FString::Printf(
					TEXT("%d Hz %s within %.1f of the 20 Hz reference (%.1f vs %.1f)"),
					Rate,
					What,
					ToleranceUnits,
					SimCopterFixed::ToFloat(Value),
					SimCopterFixed::ToFloat(Expected)),
				FMath::Abs(Value - Expected) <= SimCopterFixed::FromFloat(ToleranceUnits));
		};

		// Turbulence draws a different random sequence at each rate - the same size
		// of shake, not the same samples - so these compare the flight, not the noise.
		// Measured spread over 30 s of scripted flight is well inside these: altitude
		// 177.2-177.7 units, speed and pitch identical to 0.1, heading 22 tenth-deg
		// (2 degrees) after a full-rate turn.
		Close(TEXT("altitude"), Actual.Altitude, Reference.Altitude, 2.0f);
		Close(TEXT("forward speed"), Actual.ForwardSpeed, Reference.ForwardSpeed, 2.0f);
		Close(TEXT("smoothed pitch"), Actual.PitchSmoothed, Reference.PitchSmoothed, 2.0f);
		Close(TEXT("heading"), Actual.Heading, Reference.Heading, 40.0f);
	}

	// The rotor's strobe step is the one rule that is per-frame *by design*, so it is
	// checked as an angular rate rather than a phase: 39.1 degrees per 0.05 s frame
	// is 782 deg/s, and the blade has to sweep that per second at any substep.
	auto BladeSweepPerSecond = [](float Dt)
	{
		FSimCopterFlightModel Model;
		Model.ResetOnSurface(0, 0, 0);
		Model.State = ESimCopterFlightState::Flying;
		Model.Altitude = SimCopterFixed::FromFloat(200.0f);
		Model.RotorSpeed = FSimCopterFlightModel::RotorTopSpeed; // 360, past the 250 strobe gate
		const FSimCopterFlightEnvironment Ground = FlatGroundAt(0);
		const FSimCopterFlightInputs Inputs;
		FSimCopterFlightEvents Events;

		// A second of blade travel is several full turns, which overflows 16.16, so
		// the running total is int64 and reported in whole degrees.
		int64 Swept = 0;
		int32 Previous = Model.MainRotorAngle;
		const int32 Steps = FMath::RoundToInt(1.0f / Dt);
		for (int32 StepIndex = 0; StepIndex < Steps; ++StepIndex)
		{
			Model.Step(Dt, Inputs, Ground, Events);
			int32 Delta = Model.MainRotorAngle - Previous;
			if (Delta < 0)
			{
				Delta += SimCopterFixed::FullTurnTenthDeg;
			}
			Swept += Delta;
			Previous = Model.MainRotorAngle;
		}
		return Swept / (65536 * 10); // 16.16 tenth-degrees -> whole degrees
	};

	const int64 ReferenceSweep = BladeSweepPerSecond(1.0f / 20.0f);
	for (int32 Rate : Rates)
	{
		const int64 Sweep = BladeSweepPerSecond(1.0f / Rate);
		TestTrue(
			*FString::Printf(
				TEXT("%d Hz blade sweeps the 20 Hz rate within 5%% (%lld vs %lld deg/s)"),
				Rate,
				Sweep,
				ReferenceSweep),
			FMath::Abs(Sweep - ReferenceSweep) < FMath::Abs(ReferenceSweep) / 20);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterFlightRotorSpoolDownTest,
	"SimCopter.Flight.RotorSpoolDown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterFlightRotorSpoolDownTest::RunTest(const FString& Parameters)
{
	// FUN_00487740: parked with the collective idle the rotor decays at 50/s
	// and the blur disc drops out below the lift gate.
	FSimCopterFlightModel Model;
	Model.ResetOnSurface(0, 0, 0);
	Model.RotorSpeed = FSimCopterFlightModel::RotorTopSpeed;
	Model.bRotorBlurDisc = true;

	const FSimCopterFlightEnvironment Ground = FlatGroundAt(0);
	FSimCopterFlightInputs Inputs;
	FSimCopterFlightEvents Events;
	for (int32 StepIndex = 0; StepIndex < 40; ++StepIndex) // 2 seconds
	{
		Model.Step(StepSeconds, Inputs, Ground, Events);
	}
	const int32 ExpectedSpeed = FSimCopterFlightModel::RotorTopSpeed - SimCopterFixed::FromFloat(100.0f);
	TestTrue(TEXT("spooled down ~100 over 2s"), FMath::Abs(Model.RotorSpeed - ExpectedSpeed) < SimCopterFixed::FromFloat(6.0f));

	for (int32 StepIndex = 0; StepIndex < 200; ++StepIndex)
	{
		Model.Step(StepSeconds, Inputs, Ground, Events);
	}
	TestEqual(TEXT("rotor fully stopped"), Model.RotorSpeed, 0);
	TestFalse(TEXT("blur disc off"), Model.bRotorBlurDisc);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

