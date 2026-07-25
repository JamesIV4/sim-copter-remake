// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/SimCopterSpotlight.h"
#include "Ground/SimCopterInteraction.h"
#include "Misc/AutomationTest.h"

// Gates for the decoded spotlight target service and the shared interaction dispatch.
// Evidence: Docs/scratchpad/ghidra/heli_tools_models_decode_20260724.md sections 4 and 5.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterSpotlightMathTest,
	"SimCopter.Spotlight.MarchAndBands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterSpotlightMathTest::RunTest(const FString& Parameters)
{
	using namespace SimCopterSpotlight;

	// FUN_00489250: 16 steps of 32.0 units, clamped at 0x1ffffff to 511.0.
	TestEqual(TEXT("full march reaches 512 units"), MarchStep1616 * MaxMarchSteps, 0x2000000);
	TestEqual(TEXT("distance under the threshold passes through"), ClampMarchDistance(0x1000000), 0x1000000);
	TestEqual(TEXT("distance over the threshold clamps to 511.0"), ClampMarchDistance(0x2000000), MaxDistance1616);
	TestEqual(TEXT("threshold itself is not clamped"), ClampMarchDistance(0x1ffffff), 0x1ffffff);

	// Band thresholds are inclusive upper bounds: 128 -> 0, 129 -> 1, 256 -> 1, 384 -> 2, 385 -> 3.
	TestEqual(TEXT("zero distance is band 0"), SelectBand(0), 0);
	TestEqual(TEXT("128.0 is band 0"), SelectBand(Band0Max1616), 0);
	TestEqual(TEXT("just past 128.0 is band 1"), SelectBand(Band0Max1616 + 1), 1);
	TestEqual(TEXT("256.0 is band 1"), SelectBand(Band1Max1616), 1);
	TestEqual(TEXT("just past 256.0 is band 2"), SelectBand(Band1Max1616 + 1), 2);
	TestEqual(TEXT("384.0 is band 2"), SelectBand(Band2Max1616), 2);
	TestEqual(TEXT("just past 384.0 is band 3"), SelectBand(Band2Max1616 + 1), 3);
	TestEqual(TEXT("max distance is band 3"), SelectBand(MaxDistance1616), 3);

	// The megaphone refuses to broadcast in band 3 (FUN_0048a800's heli[0x150] < 3 gate).
	TestEqual(TEXT("megaphone allows bands 0..2"), MegaphoneMaxBand, 2);

	// Smoothing: (previous * 7 + raw) >> 3 while flying, raw while parked.
	TestEqual(TEXT("parked uses the raw distance"), SmoothDistance(0x100000, 0x800000, false), 0x800000);
	TestEqual(
		TEXT("flying folds the raw value in at 1/8"),
		SmoothDistance(0x800000, 0x800000, true),
		0x800000);
	TestEqual(
		TEXT("flying eases toward a nearer hit"),
		SmoothDistance(0x800000, 0x0, true),
		(0x800000 * 7) >> 3);

	// Cone scale: Div(distance, 512.0) * 10, floored at 0.3.
	TestEqual(TEXT("tiny distances hit the scale floor"), SelectNodeScale(0), MinNodeScale1616);
	TestEqual(TEXT("full range scales to 10.0"), SelectNodeScale(0x2000000), 10 * 0x10000);

	// Aim clamps to +/-500.0 tenth-degrees.
	TestEqual(TEXT("aim inside the clamp passes through"), ClampAim(0x100000), 0x100000);
	TestEqual(TEXT("aim clamps high"), ClampAim(AimClamp1616 + 1), AimClamp1616);
	TestEqual(TEXT("aim clamps low"), ClampAim(-AimClamp1616 - 1), -AimClamp1616);

	// One key frame moves the aim 4 degrees; 500.0 tenth-degrees is 50 degrees of travel.
	TestEqual(TEXT("aim step is 40.0 tenth-degrees"), AimStep1616, 40 * 0x10000);
	TestEqual(TEXT("aim clamp is 500.0 tenth-degrees"), AimClamp1616, 500 * 0x10000);
	TestEqual(TEXT("rest pose tilts 36 degrees down"), BasePitch1616, 360 * 0x10000);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterInteractionSpiralTest,
	"SimCopter.Interaction.SpiralScan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterInteractionSpiralTest::RunTest(const FString& Parameters)
{
	TArray<FIntPoint> Tiles;

	// FUN_0048ae70 mode 1 (spotlight): run length 3 -> 8 visited tiles, starting on the centre.
	SimCopterInteraction::BuildSpiralTiles(FIntPoint(10, 20), SimCopterInteraction::SpotlightRings, Tiles);
	TestEqual(TEXT("spotlight scan visits 8 tiles"), Tiles.Num(), 8);
	if (Tiles.Num() == 8)
	{
		const FIntPoint Expected[] = {
			{10, 20}, {10, 19}, {11, 19}, {11, 20}, {11, 21}, {10, 21}, {9, 21}, {9, 20},
		};
		for (int32 Index = 0; Index < 8; ++Index)
		{
			TestEqual(
				FString::Printf(TEXT("spotlight tile %d"), Index),
				Tiles[Index],
				Expected[Index]);
		}
	}

	// Mode 2 (megaphone): run length 5 -> 24 visited tiles.
	SimCopterInteraction::BuildSpiralTiles(FIntPoint(0, 0), SimCopterInteraction::MegaphoneRings, Tiles);
	TestEqual(TEXT("megaphone scan visits 24 tiles"), Tiles.Num(), 24);
	TestTrue(TEXT("megaphone scan starts on the centre tile"), Tiles.Num() > 0 && Tiles[0] == FIntPoint(0, 0));

	// The original never revisits a tile within one scan.
	TSet<FIntPoint> Unique(Tiles);
	TestEqual(TEXT("megaphone scan has no duplicate tiles"), Unique.Num(), Tiles.Num());

	// The walk stays inside the ring box it claims to cover.
	for (const FIntPoint& Tile : Tiles)
	{
		TestTrue(
			TEXT("megaphone tiles stay within +/-2 of the centre"),
			FMath::Abs(Tile.X) <= 2 && FMath::Abs(Tile.Y) <= 2);
	}

	// FUN_0048ae70 returns immediately for any mode other than 1 and 2.
	SimCopterInteraction::BuildSpiralTiles(FIntPoint(0, 0), 0, Tiles);
	TestEqual(TEXT("zero rings visits nothing"), Tiles.Num(), 0);
	TestEqual(
		TEXT("only modes 1 and 2 spiral"),
		SimCopterInteraction::GetSpiralRingsForMode(ESimCopterInteractionMode::TearGasCloud),
		0);
	TestEqual(
		TEXT("spotlight mode uses 3 rings"),
		SimCopterInteraction::GetSpiralRingsForMode(ESimCopterInteractionMode::Spotlight),
		SimCopterInteraction::SpotlightRings);
	TestEqual(
		TEXT("megaphone mode uses 5 rings"),
		SimCopterInteraction::GetSpiralRingsForMode(ESimCopterInteractionMode::Megaphone),
		SimCopterInteraction::MegaphoneRings);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterReactionTableTest,
	"SimCopter.Interaction.ReactionTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterReactionTableTest::RunTest(const FString& Parameters)
{
	using namespace SimCopterInteraction;

	// DAT_0058d728 as written by FUN_004c3010, cross-checked against the shipped people.df
	// BHAV names. These four are the entries the tool ports depend on.
	TestEqual(TEXT("mode 2 is BHAV 901 'Rxn: Megaphone'"), GetPersonReactionProgram(ESimCopterInteractionMode::Megaphone), 901);
	TestEqual(TEXT("mode 4 is BHAV 908 'Rxn: Water'"), GetPersonReactionProgram(ESimCopterInteractionMode::Water), 908);
	TestEqual(TEXT("mode 5 is BHAV 907 'Rxn: Teargas'"), GetPersonReactionProgram(ESimCopterInteractionMode::TearGasCloud), 907);
	TestEqual(TEXT("mode 3 is BHAV 915 'Rxn: Missile/bullet'"), GetPersonReactionProgram(ESimCopterInteractionMode::Missile), 915);
	TestEqual(TEXT("mode 7 is BHAV 915 'Rxn: Missile/bullet'"), GetPersonReactionProgram(ESimCopterInteractionMode::MachineGun), 915);
	TestEqual(TEXT("mode 14 is BHAV 910 'Rxn: Debris stuff hit'"), GetPersonReactionProgram(ESimCopterInteractionMode::TearGasCanister), 910);

	// Entries 17..19 are the 0xffff fill.
	TestEqual(TEXT("mode 17 has no reaction"), GetPersonReactionProgram(17), INDEX_NONE);
	TestEqual(TEXT("out-of-range mode has no reaction"), GetPersonReactionProgram(99), INDEX_NONE);
	TestEqual(TEXT("negative mode has no reaction"), GetPersonReactionProgram(-1), INDEX_NONE);

	// Mode 1 is the one entry FUN_004c1050 overrides.
	TestEqual(TEXT("spotlight uses BHAV 950, not the table"), SpotlightReactionProgram, 950);
	TestNotEqual(
		TEXT("the table's mode-1 entry is the rioter program"),
		GetPersonReactionProgram(ESimCopterInteractionMode::Spotlight),
		SpotlightReactionProgram);

	// Priority rules: 903/915/912/909 cannot be displaced by a lesser reaction.
	TestTrue(TEXT("915 is high priority"), IsHighPriorityReaction(915));
	TestTrue(TEXT("903 is high priority"), IsHighPriorityReaction(903));
	TestTrue(TEXT("912 is high priority"), IsHighPriorityReaction(912));
	TestTrue(TEXT("909 is high priority"), IsHighPriorityReaction(909));
	TestFalse(TEXT("901 is not high priority"), IsHighPriorityReaction(901));

	TestTrue(TEXT("any reaction starts when nothing is running"), ReactionCanInterrupt(901, INDEX_NONE));
	TestFalse(TEXT("a reaction cannot restart itself"), ReactionCanInterrupt(901, 901));
	TestTrue(TEXT("a missile hit interrupts a megaphone reaction"), ReactionCanInterrupt(915, 901));
	TestFalse(TEXT("a megaphone reaction cannot interrupt a missile hit"), ReactionCanInterrupt(901, 915));
	TestFalse(TEXT("one high-priority reaction does not displace another"), ReactionCanInterrupt(912, 915));
	TestFalse(TEXT("an absent reaction never interrupts"), ReactionCanInterrupt(INDEX_NONE, 901));

	return true;
}
