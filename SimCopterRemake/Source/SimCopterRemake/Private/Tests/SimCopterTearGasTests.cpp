// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/SimCopterTearGas.h"
#include "Flight/SimCopterWaterGameplay.h"
#include "Ground/SimCopterInteraction.h"
#include "Misc/AutomationTest.h"
#include "UI/SimCopterFlapLayout.h"

using namespace SimCopterTearGas;

namespace
{
constexpr int32 FixedOne = 0x10000;
constexpr int32 FrameStep1616 = SimCopterWaterGameplay::SimulationStep1616; // 0.05 s
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterTearGasConstantsTest,
	"SimCopter.TearGas.Constants",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterTearGasConstantsTest::RunTest(const FString& Parameters)
{
	// FUN_0048db20 / FUN_0048e0b0 / FUN_0048ed00 in raw 16.16.
	TestEqual(TEXT("pool is ten slots"), PoolSlots, 10);
	TestEqual(TEXT("fuse is 5.0 s"), FuseLife1616, 0x50000);
	TestEqual(TEXT("cloud is 30.0 s"), CloudLife1616, 0x1e0000);
	TestEqual(TEXT("trail cadence is 0.5 s"), TrailInterval1616, 0x8000);
	TestEqual(TEXT("cloud puff cadence is 0.3 s"), CloudPuffInterval1616, 0x4ccc);
	TestEqual(TEXT("launch bonus is 50.0 units/s"), LaunchSpeedBonus1616, 0x320000);
	TestEqual(TEXT("muzzle sits 3.0 units above the body"), LaunchHeight1616, 0x30000);
	TestEqual(TEXT("bounce keeps 0xc20c of the speed"), BounceDamping1616, 0xc20c);

	// The trap this whole port turns on: the gas cloud is interaction mode 5 (BHAV 907
	// "Rxn: Teargas"), and the canister's own physical hit is mode 0xe (BHAV 910 "Rxn: Debris
	// stuff hit"). Mode 7 is the Apache machine gun and has nothing to do with tear gas.
	TestEqual(
		TEXT("mode 5 is the tear gas reaction"),
		SimCopterInteraction::GetPersonReactionProgram(ESimCopterInteractionMode::TearGasCloud),
		907);
	TestEqual(
		TEXT("mode 0xe is the canister's debris hit"),
		SimCopterInteraction::GetPersonReactionProgram(ESimCopterInteractionMode::TearGasCanister),
		910);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterTearGasLaunchTest,
	"SimCopter.TearGas.Launch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterTearGasLaunchTest::RunTest(const FString& Parameters)
{
	// FUN_00484d20 passes heli[0x4e] + 0x320000, so a moving helicopter throws further than a
	// hovering one - the launcher has no aim of its own.
	const FCanisterState Hover = MakeLaunchState(FVector::ForwardVector, 0);
	TestEqual(TEXT("hover launch speed"), Hover.Speed1616, LaunchSpeedBonus1616);
	TestFalse(TEXT("a fresh canister has not burst"), Hover.bDetonated);
	TestEqual(TEXT("a fresh canister is on its fuse"), Hover.Life1616, FuseLife1616);
	TestEqual(TEXT("the first trail card is half a second out"), Hover.EffectTimer1616, TrailInterval1616);

	const FCanisterState Running = MakeLaunchState(FVector::ForwardVector, 100 * FixedOne);
	TestEqual(TEXT("forward speed is added"), Running.Speed1616, 100 * FixedOne + LaunchSpeedBonus1616);
	TestEqual(TEXT("direction x"), Running.Direction1616.X, FixedOne);
	TestEqual(TEXT("direction y"), Running.Direction1616.Y, 0);
	TestEqual(TEXT("direction z"), Running.Direction1616.Z, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterTearGasFlightTest,
	"SimCopter.TearGas.FuseAndCloud",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterTearGasFlightTest::RunTest(const FString& Parameters)
{
	// Both timers are countdowns against the frame delta, so the frame a timer fires on is
	// floor(timer / delta) + 1 and the frame a life expires on is floor((life - 1) / delta) + 1.
	// Neither divides evenly by the original's 0.05 s frame, which is why the cadences below are
	// 0.55 s and 0.35 s rather than the 0.5 s and 0.3 s the constants name.
	const auto StepsToExpire = [](const int32 Life1616) { return (Life1616 - 1) / FrameStep1616 + 1; };
	const auto StepsToFire = [](const int32 Timer1616) { return Timer1616 / FrameStep1616 + 1; };

	FCanisterState State = MakeLaunchState(FVector::ForwardVector, 0);

	// Phase 0: the fuse burns, dropping a smoke card every eleven frames.
	int32 Trails = 0;
	int32 Steps = 0;
	bool bBurst = false;
	bool bPoppedAndGassedTogether = false;
	while (Steps < 400)
	{
		const FCanisterFrame Frame = AdvanceCanisterFrame(State, FrameStep1616);
		++Steps;
		TestTrue(TEXT("the canister survives its fuse"), Frame.bAlive);
		if (Frame.bDetonatedThisFrame)
		{
			bBurst = true;
			// The burst zeroes the effect timer rather than reloading it, so the first gas puff
			// comes out on the same frame as the pop.
			bPoppedAndGassedTogether = Frame.bEmitCloudPuff && !Frame.bEmitTrail;
			break;
		}
		Trails += Frame.bEmitTrail ? 1 : 0;
		TestFalse(TEXT("no gas before the pop"), Frame.bEmitCloudPuff);
	}

	TestTrue(TEXT("the fuse runs out"), bBurst);
	TestTrue(TEXT("the pop gasses on its own frame"), bPoppedAndGassedTogether);
	TestEqual(TEXT("the fuse is 101 frames"), Steps, StepsToExpire(FuseLife1616));
	TestEqual(TEXT("trail cards over the fuse"), Trails, (Steps - 1) / StepsToFire(TrailInterval1616));
	TestTrue(TEXT("the slot is now a cloud"), State.bDetonated);
	TestEqual(TEXT("the cloud lasts thirty seconds"), State.Life1616, CloudLife1616);

	// Phase 1: gas every seventh frame until the thirty seconds are up.
	int32 Puffs = 1;
	int32 CloudSteps = 0;
	bool bExpired = false;
	while (CloudSteps < 2000)
	{
		const FCanisterFrame Frame = AdvanceCanisterFrame(State, FrameStep1616);
		++CloudSteps;
		if (!Frame.bAlive)
		{
			bExpired = true;
			break;
		}
		TestFalse(TEXT("a burst canister leaves no smoke trail"), Frame.bEmitTrail);
		Puffs += Frame.bEmitCloudPuff ? 1 : 0;
	}

	TestTrue(TEXT("the cloud eventually clears"), bExpired);
	TestEqual(TEXT("the cloud is 601 frames"), CloudSteps, StepsToExpire(CloudLife1616));
	TestEqual(
		TEXT("puffs over the cloud's life"),
		Puffs,
		1 + (CloudSteps - 1) / StepsToFire(CloudPuffInterval1616));
	// Eighty-six puffs of gas is what a single canister is worth; anything much lower means the
	// cadence or the cloud life has drifted.
	TestEqual(TEXT("a canister is worth 86 puffs"), Puffs, 86);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterTearGasMotionTest,
	"SimCopter.TearGas.DragGravityAndBounce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterTearGasMotionTest::RunTest(const FString& Parameters)
{
	// Drag is the debris pool's 0x28f per frame, taken before gravity is folded in.
	FCanisterState State;
	State.Direction1616 = FIntVector(FixedOne, 0, 0);
	State.Speed1616 = 100 * FixedOne;
	State.Life1616 = FuseLife1616;
	AdvanceCanisterFrame(State, 0);
	TestEqual(
		TEXT("one frame of drag"),
		State.Speed1616,
		100 * FixedOne - SimCopterWaterGameplay::FixedMul(Drag1616, 100 * FixedOne));

	// With a real delta the vertical component picks up 40 units/s^2 downwards.
	FCanisterState Falling;
	Falling.Direction1616 = FIntVector(FixedOne, 0, 0);
	Falling.Speed1616 = 100 * FixedOne;
	Falling.Life1616 = FuseLife1616;
	AdvanceCanisterFrame(Falling, FrameStep1616);
	TestTrue(TEXT("gravity tips the canister over"), Falling.Direction1616.Z < 0);

	// FUN_00490690 reflects rather than despawning, and FUN_0048ed00 damps what is left.
	FCanisterState Bouncing;
	Bouncing.Direction1616 = FIntVector(0, 0, -FixedOne);
	Bouncing.Speed1616 = 100 * FixedOne;
	const bool bLoud = ApplyBounce(Bouncing, FVector::UpVector);
	TestTrue(TEXT("a fast bounce is audible"), bLoud);
	TestTrue(TEXT("a bounce turns the canister around"), Bouncing.Direction1616.Z > 0);
	TestEqual(
		TEXT("a bounce keeps 0xc20c of the speed"),
		Bouncing.Speed1616,
		SimCopterWaterGameplay::FixedMul(BounceDamping1616, 100 * FixedOne));

	FCanisterState Settling;
	Settling.Direction1616 = FIntVector(0, 0, -FixedOne);
	Settling.Speed1616 = 10 * FixedOne;
	TestFalse(TEXT("a slow bounce is silent"), ApplyBounce(Settling, FVector::UpVector));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterTearGasCloudScatterTest,
	"SimCopter.TearGas.CloudScatter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterTearGasCloudScatterTest::RunTest(const FString& Parameters)
{
	// (0x14 - rand() % 0x28) * 0x10000: whole units in [-19, +20], never a fraction.
	TestEqual(TEXT("a zero draw offsets +20"), CloudOffsetAxis1616(0), 20 * FixedOne);
	TestEqual(TEXT("a draw of 20 offsets 0"), CloudOffsetAxis1616(20), 0);
	TestEqual(TEXT("the largest draw offsets -19"), CloudOffsetAxis1616(39), -19 * FixedOne);
	TestEqual(TEXT("the modulo wraps"), CloudOffsetAxis1616(40), 20 * FixedOne);

	for (int32 Draw = 0; Draw < 512; ++Draw)
	{
		const int32 Offset = CloudOffsetAxis1616(Draw);
		TestTrue(TEXT("the scatter stays inside twenty units"),
			Offset >= -19 * FixedOne && Offset <= 20 * FixedOne);
		TestEqual(TEXT("the scatter is a whole number of units"), Offset & 0xffff, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterTearGasCounterTest,
	"SimCopter.TearGas.FlapCounter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterTearGasCounterTest::RunTest(const FString& Parameters)
{
	using namespace SimCopterFlapLayout::CanisterCounter;

	// 0x00455790: x = 18 + 12 * (i % 5), y = 12 + 13 * (i / 5).
	TestEqual(TEXT("first lamp"), GetLampOrigin(0), FIntPoint(18, 12));
	TestEqual(TEXT("last of the top row"), GetLampOrigin(4), FIntPoint(66, 12));
	TestEqual(TEXT("first of the bottom row"), GetLampOrigin(5), FIntPoint(18, 25));
	TestEqual(TEXT("last lamp"), GetLampOrigin(9), FIntPoint(66, 25));

	// The sliver flapbtn1.bmp carries past its two octagons: full over empty, 4x4 each.
	TestEqual(TEXT("full lamp frame"), GetLampFullFrame(), FIntRect(34, 0, 38, 4));
	TestEqual(TEXT("empty lamp frame"), GetLampEmptyFrame(), FIntRect(34, 4, 38, 8));

	// The first `10 - rounds` lamps are the dark ones, so the row empties from the left.
	for (int32 Index = 0; Index < LampCount; ++Index)
	{
		TestFalse(TEXT("a full magazine lights every lamp"), IsLampEmpty(Index, 10));
		TestTrue(TEXT("an empty magazine darkens every lamp"), IsLampEmpty(Index, 0));
		TestEqual(
			TEXT("three rounds light the last three"),
			static_cast<int32>(IsLampEmpty(Index, 3)),
			static_cast<int32>(Index < 7));
	}
	TestTrue(TEXT("a negative count cannot light a lamp"), IsLampEmpty(9, -1));
	TestFalse(TEXT("an over-full count cannot darken one"), IsLampEmpty(0, 99));
	return true;
}
