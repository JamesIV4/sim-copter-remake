// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Ground/SimCopterTrafficJam.h"
#include "Misc/AutomationTest.h"

using namespace SimCopterTrafficJam;

namespace
{
// The remake's tile and vehicle capsule, which is what the queue's numbers land on in the game:
// ASimCopterTrafficSystemActor::TileSize 400 over 64 original units, and the 135 cm vehicle capsule
// through SimCopterGroundAgent's 0.25 PopulationWorldScale.
constexpr float CmPerOriginalUnit = 400.0f / 64.0f;
constexpr float VehicleRadiusCm = 135.0f * 0.25f;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterTrafficJamBlockerTest,
	"SimCopter.Traffic.JamBlockerDirection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterTrafficJamBlockerTest::RunTest(const FString& Parameters)
{
	// FUN_0049ee30 scales the blocking radius by (1 + dot) / 2.
	TestEqual(TEXT("A car on my heading imposes its whole radius"), GetHeadingBlockScale(1.0f), 1.0f);
	TestEqual(TEXT("One crossing perpendicular imposes half of it"), GetHeadingBlockScale(0.0f), 0.5f);
	TestEqual(TEXT("One coming the other way imposes none"), GetHeadingBlockScale(-1.0f), 0.0f);
	TestEqual(TEXT("The scale never goes negative"), GetHeadingBlockScale(-4.0f), 0.0f);

	// ...and the forward probe additionally wants dot >= 0x8000 + 0x2666 * veh[0x127].
	TestTrue(TEXT("Dead ahead, same way: a blocker"), IsSameDirectionBlocker(1.0f, 0));
	TestTrue(TEXT("Exactly 60 degrees off is still a blocker"), IsSameDirectionBlocker(0.5f, 0));
	TestFalse(TEXT("Just outside the cone is not"), IsSameDirectionBlocker(0.49f, 0));
	TestFalse(TEXT("Crossing traffic is not"), IsSameDirectionBlocker(0.0f, 0));

	// This is the property the whole two-way street rests on: a queue down one side must never take
	// the cars coming the other way as blockers, or one jam stops the entire road.
	TestFalse(TEXT("Oncoming traffic is never a blocker"), IsSameDirectionBlocker(-1.0f, 0));

	// veh+0x127: the longer a car has been yielding the stricter it gets about what counts.
	TestTrue(TEXT("Ticket 0 accepts a 60 degree blocker"), IsSameDirectionBlocker(0.5f, 0));
	TestFalse(TEXT("Ticket 1 no longer does"), IsSameDirectionBlocker(0.5f, 1));
	TestTrue(TEXT("Ticket 1 wants 0.65"), IsSameDirectionBlocker(0.65f, 1));
	TestTrue(TEXT("A negative ticket is treated as none"), IsSameDirectionBlocker(0.5f, -3));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterTrafficJamSpacingTest,
	"SimCopter.Traffic.JamQueueSpacing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterTrafficJamSpacingTest::RunTest(const FString& Parameters)
{
	// FUN_0049ee30's probe is pos + heading * (radius + 5 units), tested against the leader's own
	// radius, so the resting centre-to-centre gap is both radii plus five units.
	const float HoldCm = GetQueueHoldDistanceCm(VehicleRadiusCm, VehicleRadiusCm, CmPerOriginalUnit);
	TestEqual(TEXT("Two stock cars hold both radii plus five units apart"), HoldCm, 33.75f + 33.75f + 31.25f);

	// The rendered car body is ~70 cm long (a 100 cm cube at the vehicle proxy's 2.8 x 0.25 scale),
	// so the original's own spacing is already wide enough that a queue does not visibly interpenetrate.
	TestTrue(TEXT("...which clears the rendered body length"), HoldCm > 70.0f);

	TestEqual(
		TEXT("A zero unit scale degrades to bumper to bumper rather than negative"),
		GetQueueHoldDistanceCm(VehicleRadiusCm, VehicleRadiusCm, 0.0f),
		67.5f);

	// The speed ramp: stopped at the hold distance, stopped inside it, moving outside it.
	constexpr float SlowCm = 300.0f;
	TestEqual(TEXT("At the hold distance the car is stopped"), GetQueueSpeedScale(HoldCm, HoldCm, SlowCm), 0.0f);
	TestEqual(TEXT("Inside it too - never a creep"), GetQueueSpeedScale(HoldCm * 0.5f, HoldCm, SlowCm), 0.0f);
	TestEqual(TEXT("Nose to nose is still a full stop"), GetQueueSpeedScale(0.0f, HoldCm, SlowCm), 0.0f);
	TestEqual(TEXT("Beyond the band the car runs at full speed"), GetQueueSpeedScale(HoldCm + SlowCm, HoldCm, SlowCm), 1.0f);
	TestEqual(TEXT("...and stays there further out"), GetQueueSpeedScale(HoldCm + SlowCm * 4.0f, HoldCm, SlowCm), 1.0f);

	// Monotonic across the band, and biased low: a car that only starts braking at the end of the
	// band coasts into the back of the queue, which is how the pile-up looked.
	float Previous = -1.0f;
	for (int32 Step = 0; Step <= 10; ++Step)
	{
		const float Distance = HoldCm + SlowCm * (static_cast<float>(Step) / 10.0f);
		const float Scale = GetQueueSpeedScale(Distance, HoldCm, SlowCm);
		TestTrue(TEXT("The approach ramp never falls as the gap opens"), Scale >= Previous);
		Previous = Scale;
	}
	TestTrue(
		TEXT("Halfway through the band the car is already well under half speed"),
		GetQueueSpeedScale(HoldCm + SlowCm * 0.5f, HoldCm, SlowCm) < 0.3f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterTrafficJamHornTest,
	"SimCopter.Traffic.JamHornThresholds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterTrafficJamHornTest::RunTest(const FString& Parameters)
{
	// veh+0xaf in 16.16 seconds against DAT_005039a0's frame delta: 0x50000 and 0x140001.
	TestEqual(TEXT("The horn opens at five seconds held"), HornHeldSeconds, 5.0f);
	TestEqual(TEXT("The reroute / jam roll waits twenty"), RerouteHeldSeconds, 20.0f);

	// FUN_0049be50's two rolls: rand() & 0xf on the jam flag's own branch, rand() & 0x3f otherwise.
	TestEqual(TEXT("A jammed car honks one update in sixteen"), JammedHornOneIn, 16);
	TestEqual(TEXT("A merely held one, one in sixty-four"), HeldHornOneIn, 64);
	TestTrue(TEXT("The jammed car is the noisier of the two"), JammedHornOneIn < HeldHornOneIn);

	return true;
}
