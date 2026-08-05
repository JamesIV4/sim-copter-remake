// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/SimCopterHelicopterPawn.h"
#include "Ground/SimCopterGroundAgent.h"

#include "Misc/AutomationTest.h"

// "Am I at the helicopter?" for the two callers that ask it: the avatar boarding on foot, and a
// walker whose behaviour program sent it to an object (FUN_004ca940 -> FUN_004c9300 ->
// FUN_004c9470 -> FUN_004c9000 -> FUN_004c8f70). Both used to answer with a bubble around the
// helicopter's actor origin, or with "we share a tile"; a tile is 400 cm and the collision capsule
// is a 190 cm sphere, so both were far larger than the aircraft. Everything here is pure geometry.

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
// Stand-in fuselage: roughly a remake airframe's proportions, expressed in ModelPivot's frame with
// the skids at the bottom of the collision capsule the way CommitHelicopterModel leaves it. The
// exact numbers are not claimed to be any one model's - the rule under test is the shape of the
// test, not the mesh.
FBox MakeTestAirframeBox()
{
	return FBox(FVector(-125.0, -30.0, -95.0), FVector(125.0, 30.0, -15.0));
}

// Original units are 1/64 of a 400 cm tile.
constexpr float CmPerOriginalUnit = 400.0f / 64.0f;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterAirframeGapTest,
	"SimCopter.Interaction.AirframeGap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterAirframeGapTest::RunTest(const FString& Parameters)
{
	using FHeli = ASimCopterHelicopterPawn;

	const FBox Box = MakeTestAirframeBox();
	const FVector Origin(1000.0, 2000.0, 500.0);
	const FTransform Frame(FRotator::ZeroRotator, Origin);

	// Inside the fuselage is zero gap, in both measurement modes.
	TestEqual(
		TEXT("A point inside the airframe has no gap"),
		FHeli::ComputeAirframeGapCm(Box, Frame, Origin + FVector(0.0, 0.0, -50.0), false),
		0.0f);

	// Straight off the right flank: the gap is measured from the skin, not from the origin.
	TestEqual(
		TEXT("Gap off the flank is measured from the fuselage side"),
		FHeli::ComputeAirframeGapCm(Box, Frame, Origin + FVector(0.0, 100.0, -50.0), false),
		70.0f,
		0.01f);

	// Off the nose, along the long axis.
	TestEqual(
		TEXT("Gap off the nose is measured from the nose"),
		FHeli::ComputeAirframeGapCm(Box, Frame, Origin + FVector(200.0, 0.0, -50.0), false),
		75.0f,
		0.01f);

	// The box turns with the airframe: yaw 90 degrees and the same world point that was 75 cm off
	// the nose is now off the flank.
	const FTransform Turned(FRotator(0.0f, 90.0f, 0.0f), Origin);
	TestEqual(
		TEXT("The airframe box yaws with the aircraft"),
		FHeli::ComputeAirframeGapCm(Box, Turned, Origin + FVector(200.0, 0.0, -50.0), false),
		170.0f,
		0.01f);

	// This is the regression that mattered on foot. The old auto-enter test was "within 145 cm of
	// the actor origin", which reaches a metre past the flank of an airframe 60 cm wide - the
	// player boarded from beside the aircraft, having never touched it.
	const FVector BesideTheFlank = Origin + FVector(0.0, 120.0, -55.0);
	TestTrue(
		TEXT("The old 145 cm origin bubble accepted a point well clear of the airframe"),
		FVector::Dist(BesideTheFlank, Origin) <= 145.0f);
	TestTrue(
		TEXT("The airframe gap rejects that same point"),
		FHeli::ComputeAirframeGapCm(Box, Frame, BesideTheFlank, false) > 60.0f);

	// Horizontal-only measures across the deck, for a walker standing on the same surface as the
	// skids whose own vertical gate is applied separately. It must not answer "at the aircraft" for
	// somebody standing under one in the air, which is why the enter path uses the 3D form.
	const FVector Underneath = Origin + FVector(0.0, 0.0, -400.0);
	TestEqual(
		TEXT("Horizontal-only ignores height inside the box's own span"),
		FHeli::ComputeAirframeGapCm(Box, Frame, Underneath, true),
		0.0f);
	TestTrue(
		TEXT("The 3D form keeps a body 300 cm below the skids clear of the airframe"),
		FHeli::ComputeAirframeGapCm(Box, Frame, Underneath, false) > 300.0f);

	// A degenerate box (no fuselage geometry built yet) must not report a false gap.
	TestEqual(
		TEXT("An invalid box answers zero rather than a garbage distance"),
		FHeli::ComputeAirframeGapCm(FBox(ForceInit), Frame, Origin + FVector(5000.0, 0.0, 0.0), false),
		0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterSelectionContactTest,
	"SimCopter.Behavior.VM.SelectionContact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterSelectionContactTest::RunTest(const FString& Parameters)
{
	using FAgent = ASimCopterGroundAgent;

	// Both bodies are pedestrians: 32 cm authored, 0.25x population scale.
	const float BodyRadiusCm = 32.0f * 0.25f;
	const FVector Me(0.0, 0.0, 0.0);

	TestTrue(
		TEXT("Bodies whose capsules overlap are in contact"),
		FAgent::ComputeContactGapCm(Me, FVector(15.0, 0.0, 0.0), BodyRadiusCm, BodyRadiusCm) <= 0.0f);

	TestTrue(
		TEXT("Touching exactly at the sum of the two radii still counts"),
		FAgent::ComputeContactGapCm(Me, FVector(16.0, 0.0, 0.0), BodyRadiusCm, BodyRadiusCm) <= 0.0f);

	TestTrue(
		TEXT("Half a metre apart is not contact"),
		FAgent::ComputeContactGapCm(Me, FVector(50.0, 0.0, 0.0), BodyRadiusCm, BodyRadiusCm) > 0.0f);

	// The rule this replaced. Sharing a tile put the two bodies anywhere inside 400 cm - and up to
	// ~283 cm apart on the diagonal - which is how a paramedic came to lift a casualty out of a
	// helicopter cabin from across the helipad.
	const FVector FarCornerOfTheSameTile(283.0, 0.0, 0.0);
	TestTrue(
		TEXT("Opposite corners of one tile are not contact"),
		FAgent::ComputeContactGapCm(Me, FarCornerOfTheSameTile, BodyRadiusCm, BodyRadiusCm) > 0.0f);

	// Vertical separation is deliberately not part of this gap: StepTowardSelectedObject applies
	// the original's own 5-unit feet-to-doorsill gate (FUN_004ca940's 0x50000) instead.
	TestTrue(
		TEXT("The contact gap is measured across the deck only"),
		FAgent::ComputeContactGapCm(Me, FVector(10.0, 0.0, 5.0 * CmPerOriginalUnit), BodyRadiusCm, BodyRadiusCm) <= 0.0f);

	// A car (135 cm authored radius, same scale) is reachable from further out because its own
	// body is bigger, exactly as the original's per-object +0x10 radius makes it.
	const float CarRadiusCm = 135.0f * 0.25f;
	TestTrue(
		TEXT("A vehicle's own extent widens contact with it"),
		FAgent::ComputeContactGapCm(Me, FVector(40.0, 0.0, 0.0), BodyRadiusCm, CarRadiusCm) <= 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterMedevacHandoffGateTest,
	"SimCopter.Dispatch.MedevacHandoffGates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterMedevacHandoffGateTest::RunTest(const FString& Parameters)
{
	using FAgent = ASimCopterGroundAgent;

	// The two remake-only gates that stop a paramedic taking a casualty out of a cabin it is
	// nowhere near, and stop the roof crew setting off after an aircraft that is merely in the
	// neighbourhood. Defaults from ASimCopterGroundAgent.
	const float BodyRadiusCm = 32.0f * 0.25f;
	const float ReachCm = 25.0f;
	const float MaxVerticalCm = 150.0f;

	// Standing against the fuselage on the same deck.
	TestTrue(
		TEXT("Touching the airframe on the same deck can hand the patient over"),
		FAgent::IsWithinHandoffReach(0.0f, BodyRadiusCm, ReachCm, 500.0f, 500.0f, MaxVerticalCm));

	TestTrue(
		TEXT("A hand's reach off the skin still counts"),
		FAgent::IsWithinHandoffReach(30.0f, BodyRadiusCm, ReachCm, 500.0f, 500.0f, MaxVerticalCm));

	// Across the pad. This is the reported bug: a medic several metres away emptying the cabin.
	TestFalse(
		TEXT("A metre off the airframe cannot reach into the cabin"),
		FAgent::IsWithinHandoffReach(100.0f, BodyRadiusCm, ReachCm, 500.0f, 500.0f, MaxVerticalCm));
	TestFalse(
		TEXT("Several tiles away certainly cannot"),
		FAgent::IsWithinHandoffReach(1200.0f, BodyRadiusCm, ReachCm, 500.0f, 500.0f, MaxVerticalCm));

	// Vertically a window, not contact: unloading a helicopter still hovering just off the pad is
	// deliberately allowed.
	TestTrue(
		TEXT("A low hover over the pad can still be unloaded"),
		FAgent::IsWithinHandoffReach(0.0f, BodyRadiusCm, ReachCm, 620.0f, 500.0f, MaxVerticalCm));
	TestFalse(
		TEXT("An aircraft at altitude cannot be unloaded from the ground beneath it"),
		FAgent::IsWithinHandoffReach(0.0f, BodyRadiusCm, ReachCm, 1100.0f, 500.0f, MaxVerticalCm));

	// Aggro: a 3x3 hospital is 1200 cm across, so its post half extent is 600.
	const FVector PostCenter(10000.0, 20000.0, 900.0);
	const float PostHalfExtentCm = 600.0f;
	const float MarginCm = 120.0f;

	TestTrue(
		TEXT("An aircraft over the middle of the roof is noticed"),
		FAgent::IsWithinRoofPostAggro(PostCenter, PostCenter, PostHalfExtentCm, MarginCm));
	TestTrue(
		TEXT("Parked with the skids just past the parapet is still noticed"),
		FAgent::IsWithinRoofPostAggro(
			PostCenter + FVector(680.0, 0.0, 0.0), PostCenter, PostHalfExtentCm, MarginCm));
	TestFalse(
		TEXT("Two tiles clear of the building is not noticed"),
		FAgent::IsWithinRoofPostAggro(
			PostCenter + FVector(800.0, 0.0, 0.0), PostCenter, PostHalfExtentCm, MarginCm));
	// The old behaviour: the mission layer started the handoff at 1500 cm - nearly four tiles - and
	// the crew set off from there, which is as far as the post's containment would ever let them get.
	TestFalse(
		TEXT("The old 1500 cm trigger distance is outside the aggro square"),
		FAgent::IsWithinRoofPostAggro(
			PostCenter + FVector(1500.0, 0.0, 0.0), PostCenter, PostHalfExtentCm, MarginCm));
	// Height is not part of it: the crew should walk out while the aircraft is still descending.
	TestTrue(
		TEXT("Aggro ignores altitude"),
		FAgent::IsWithinRoofPostAggro(
			PostCenter + FVector(0.0, 0.0, 3000.0), PostCenter, PostHalfExtentCm, MarginCm));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
