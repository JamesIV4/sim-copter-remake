// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/SimCopterHelicopterPawn.h"
#include "Ground/SimCopterAmbientVehicles.h"
#include "Ground/SimCopterGroundAgent.h"
#include "Ground/SimCopterInteraction.h"

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
	FSimCopterPassengerDoorOffsetTest,
	"SimCopter.Interaction.PassengerDoorOffset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterPassengerDoorOffsetTest::RunTest(const FString& Parameters)
{
	using FHeli = ASimCopterHelicopterPawn;
	const FBox Box = MakeTestAirframeBox();
	const FVector2D Fallback(-35.0, 175.0);

	const FVector2D RightDoor = FHeli::ComputePassengerDoorOffsetCm(Box, 0, 40.0f, Fallback);
	TestEqual(TEXT("First seat exits level with the airframe center"), RightDoor.X, 0.0, 0.01);
	TestEqual(TEXT("Right exit is one body clearance beyond the skin"), RightDoor.Y, 70.0, 0.01);

	const FVector2D LeftDoor = FHeli::ComputePassengerDoorOffsetCm(Box, 1, 40.0f, Fallback);
	TestEqual(TEXT("Opposite seat exits at the same fore-aft point"), LeftDoor.X, 0.0, 0.01);
	TestEqual(TEXT("Left exit mirrors against the other skin"), LeftDoor.Y, -70.0, 0.01);

	const FVector2D SecondRow = FHeli::ComputePassengerDoorOffsetCm(Box, 2, 40.0f, Fallback);
	TestEqual(TEXT("Later survivors shift by one compact seat row"), SecondRow.X, -32.0, 0.01);
	TestEqual(TEXT("Later survivors remain against the same airframe side"), SecondRow.Y, 70.0, 0.01);

	const FVector2D HeadlessFallback = FHeli::ComputePassengerDoorOffsetCm(
		FBox(ForceInit), 0, 40.0f, Fallback);
	TestEqual(TEXT("A headless frame retains the authored fallback"), HeadlessFallback, Fallback);
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

	// ...but a circle is the wrong shape for a car, and that is what stranded the ambulance medic.
	// FUN_004c8f70 overlaps the object's own BOX. The remake's vehicle capsule is 33.75 cm, while
	// the body it stands for is ~70 cm long: approaching the nose, a walker could be standing in
	// the bodywork with the circle still reporting a positive gap, so BHAV 262's return walk never
	// arrived and the handoff at BHAV 275 was never reached.
	// 90 cm long, 31 cm wide - the proportions of a remake car, whose length runs well past the
	// 33.75 cm capsule the traffic separation uses.
	const FBox CarBody(FVector(-45.0, -15.6, -13.0), FVector(45.0, 15.6, 13.0));
	const FVector CarOrigin(0.0, 0.0, 0.0);
	const FTransform CarFrame(FRotator::ZeroRotator, CarOrigin);

	// The blind spot: the circle reaches 33.75 + 8 = 41.75 cm from the centre, but the bumper is
	// 45 cm out. A walker standing 50 cm along the nose is 3 cm deep in the bodywork and the
	// circle still calls it clear.
	const FVector AtTheBumper(50.0, 0.0, 0.0);
	TestTrue(
		TEXT("The circle rule leaves a walker at the bumper measuring a positive gap"),
		FAgent::ComputeContactGapCm(CarOrigin, AtTheBumper, BodyRadiusCm, CarRadiusCm) > 0.0f);
	TestTrue(
		TEXT("The box rule has that same walker touching the bodywork"),
		FAgent::ComputeBodyGapCm(CarBody, CarFrame, AtTheBumper) - BodyRadiusCm <= 0.0f);

	// The box is not a licence to reach further everywhere: off the flank it is much tighter than
	// the circle, which is the whole point of using the body's real shape.
	TestEqual(
		TEXT("A point inside the bodywork has no gap"),
		FAgent::ComputeBodyGapCm(CarBody, CarFrame, FVector(10.0, 5.0, 0.0)),
		0.0f);
	TestTrue(
		TEXT("Well off the flank is not contact"),
		FAgent::ComputeBodyGapCm(CarBody, CarFrame, FVector(0.0, 60.0, 0.0)) - BodyRadiusCm > 0.0f);

	// It turns with the vehicle: yaw 90 degrees and the point that was inside the bumper is now
	// off a flank, 50 cm out from a body only 15.6 cm wide.
	const FTransform TurnedCar(FRotator(0.0f, 90.0f, 0.0f), CarOrigin);
	TestEqual(
		TEXT("The body box yaws with the vehicle"),
		FAgent::ComputeBodyGapCm(CarBody, TurnedCar, AtTheBumper),
		50.0f - 15.6f,
		0.01f);

	// Height inside the body's own span is ignored - the caller owns the vertical gate, exactly as
	// for the airframe.
	TestEqual(
		TEXT("The body gap is measured across the deck only"),
		FAgent::ComputeBodyGapCm(CarBody, CarFrame, FVector(10.0, 0.0, 200.0)),
		0.0f);

	// A vehicle with no mesh built yet must not report a false gap.
	TestEqual(
		TEXT("An invalid body box answers zero rather than a garbage distance"),
		FAgent::ComputeBodyGapCm(FBox(ForceInit), CarFrame, FVector(5000.0, 0.0, 0.0)),
		0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterUfoHitCountTest,
	"SimCopter.Ambient.UfoHitCount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterUfoHitCountTest::RunTest(const FString& Parameters)
{
	using FAmbient = ASimCopterAmbientVehiclesActor;

	// FUN_004b3ba0's retirement test is `if (9 < *(int *)(iVar1 + 0x50))`, so the TENTH missile is
	// the one that downs it - nine hits leave it flying. Before 2026-08-06 nothing incremented that
	// counter at all (and the meshes were on NoCollision, so nothing could even reach it), which is
	// why the UFO could not be shot down.
	TestFalse(TEXT("A fresh UFO is not downed"), FAmbient::IsPlaneDownedByHitCount(0));
	TestFalse(TEXT("One missile does not down it"), FAmbient::IsPlaneDownedByHitCount(1));
	TestFalse(TEXT("Nine missiles leave it flying"), FAmbient::IsPlaneDownedByHitCount(9));
	TestTrue(TEXT("The tenth missile downs it"), FAmbient::IsPlaneDownedByHitCount(10));
	TestTrue(TEXT("Past ten stays downed"), FAmbient::IsPlaneDownedByHitCount(11));

	// The two modes FUN_004b3ba0 has arms for are the Apache's, and only those. Anything else -
	// the bucket, tear gas, the megaphone - falls out of its switch without touching an aircraft.
	TestEqual(TEXT("Missile is interaction mode 3"), int32(ESimCopterInteractionMode::Missile), 3);
	TestEqual(TEXT("Machine gun is interaction mode 7"), int32(ESimCopterInteractionMode::MachineGun), 7);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterEmergencyCrewStateTest,
	"SimCopter.Dispatch.EmergencyCrewStates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterEmergencyCrewStateTest::RunTest(const FString& Parameters)
{
	using FAgent = ASimCopterGroundAgent;

	// Who counts as crew decides whose despawn may be refused. UpdateOriginalBehavior refuses one
	// for an "unresolved mission person" so a decoded program cannot erase a mission dependency -
	// but a worker is not a dependency, it is the thing resolving one. An ambulance medic carries
	// the medevac's event id and never sets bMissionResolutionReported (that flag belongs to
	// passengers), so before this split it was refused its op-40 despawn at BHAV 269, restarted
	// into BHAV 801, and - already attached to the ambulance with its movement suspended - could
	// neither walk nor finish. That was the reported ambulance loop.
	TestTrue(TEXT("State 5 is the ambulance medic"), FAgent::IsEmergencyCrewPersonState(5));
	TestTrue(TEXT("State 7 is police"), FAgent::IsEmergencyCrewPersonState(7));
	TestTrue(TEXT("State 8 is the foot cop"), FAgent::IsEmergencyCrewPersonState(8));
	TestTrue(TEXT("State 0xe is the speeder cop"), FAgent::IsEmergencyCrewPersonState(0xe));

	// Every state FUN_004ccf50 case 1 scores as a delivered passenger must stay out of it, or a
	// casualty could be despawned out from under an open mission.
	for (const int32 PassengerState : { 1, 2, 4, 6, 0x13 })
	{
		TestFalse(
			FString::Printf(TEXT("Passenger state %d is not crew"), PassengerState),
			FAgent::IsEmergencyCrewPersonState(PassengerState));
	}
	TestFalse(TEXT("An ordinary civilian is not crew"), FAgent::IsEmergencyCrewPersonState(0));

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
