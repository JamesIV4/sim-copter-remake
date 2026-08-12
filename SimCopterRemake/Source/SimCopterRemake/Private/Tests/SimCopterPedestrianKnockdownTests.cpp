// Copyright Epic Games, Inc. All Rights Reserved.

#include "Formats/SimCopterPeopleCityRules.h"
#include "Formats/SimCopterPrivAnimReader.h"
#include "Ground/SimCopterGroundAgent.h"
#include "Ground/SimCopterPopulationFigure.h"

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

// Cars send people flying.
//
// DIVERGENCE, whole-cloth: the original has no vehicle-vs-person collision at all. FUN_0049ee30's
// blocking probe only considers other vehicles and the person move core only considers other
// people, so in the shipped game a car drives through a crowd and nobody notices. Nothing here is
// ported from anything, and there is no FUN_004xxxxx to check it against - which is exactly why the
// rules it *does* have are pinned down here instead.
//
// The rules: the whole strike scales with the car's speed and nothing else; a bounce never adds
// energy; a tumble that stops lays the body flat rather than leaving it stood on its head; and the
// recovery is settle -> lie -> get up, with water diverting to the swim.

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
// The traffic system's cruising speed, at PopulationWorldScale.
constexpr float CruiseSpeedCmPerSec = 259.0f;

// The shipped gains.
constexpr float ForwardGain = 2.4f;
constexpr float LateralGain = 0.55f;
constexpr float VerticalGain = 1.35f;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterKnockdownLaunchTest,
	"SimCopter.Knockdown.Launch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterKnockdownLaunchTest::RunTest(const FString& Parameters)
{
	using FAgent = ASimCopterGroundAgent;

	// A car heading down +X, with the pedestrian caught slightly to its left (-Y is the car's own
	// right, so +Y is its left here).
	const FVector CarVelocity(CruiseSpeedCmPerSec, 0.0f, 0.0f);
	const FVector OffsetToLeft(20.0f, 12.0f, 0.0f);
	const FVector Launch =
		FAgent::ComputeVehicleKnockdownLaunchVelocity(CarVelocity, OffsetToLeft, ForwardGain, LateralGain, VerticalGain);

	TestTrue(TEXT("They leave in the direction the car was going"), Launch.X > 0.0f);
	TestTrue(TEXT("...faster than the car itself, because the hit is exaggerated"),
		Launch.X > CruiseSpeedCmPerSec);
	TestTrue(TEXT("...and upward, or it is a shove rather than a launch"), Launch.Z > 0.0f);
	TestTrue(TEXT("They are thrown off the side of the car they were standing on"), Launch.Y > 0.0f);

	// The same hit from the other side of the bonnet throws them the other way.
	const FVector LaunchFromRight = FAgent::ComputeVehicleKnockdownLaunchVelocity(
		CarVelocity, FVector(20.0f, -12.0f, 0.0f), ForwardGain, LateralGain, VerticalGain);
	TestTrue(TEXT("Clipped on the other wing, they go the other way"), LaunchFromRight.Y < 0.0f);
	TestEqual(TEXT("...with the same forward throw"), float(LaunchFromRight.X), float(Launch.X), 0.01f);

	// PROPORTIONAL TO THE SPEED OF THE CAR, which is the whole brief. Every component scales
	// together, so half the speed is half the distance and half the height - not some of each.
	const FVector HalfSpeedLaunch = FAgent::ComputeVehicleKnockdownLaunchVelocity(
		CarVelocity * 0.5f, OffsetToLeft, ForwardGain, LateralGain, VerticalGain);
	TestTrue(TEXT("Half the car speed is half the launch, in every axis"),
		HalfSpeedLaunch.Equals(Launch * 0.5f, 0.01f));

	// A car that is not moving has nothing to give.
	TestTrue(TEXT("A stationary car launches nobody"),
		FAgent::ComputeVehicleKnockdownLaunchVelocity(
			FVector::ZeroVector, OffsetToLeft, ForwardGain, LateralGain, VerticalGain).IsNearlyZero());

	// The car's own vertical motion (a ramp, a bridge approach) is not part of the throw: only the
	// speed it is travelling over the ground is.
	const FVector ClimbingCar(CruiseSpeedCmPerSec, 0.0f, 400.0f);
	TestTrue(TEXT("A car on a ramp throws them exactly as hard as one on the flat"),
		FAgent::ComputeVehicleKnockdownLaunchVelocity(
			ClimbingCar, OffsetToLeft, ForwardGain, LateralGain, VerticalGain).Equals(Launch, 0.01f));

	// Dead centre still picks a side rather than sliding straight up the road.
	const FVector HeadOn = FAgent::ComputeVehicleKnockdownLaunchVelocity(
		CarVelocity, FVector(20.0f, 0.0f, 0.0f), ForwardGain, LateralGain, VerticalGain);
	TestTrue(TEXT("A head-on hit still spins them off to one side"), FMath::Abs(HeadOn.Y) > 0.0f);

	// The debug panel's two knobs, which are folded into the gains at the call site. POWER scales
	// the whole launch, so the arc keeps its shape and only its size changes...
	constexpr float PowerScale = 2.0f;
	const FVector Powered = FAgent::ComputeVehicleKnockdownLaunchVelocity(
		CarVelocity, OffsetToLeft,
		ForwardGain * PowerScale, LateralGain * PowerScale, VerticalGain * PowerScale);
	TestTrue(TEXT("POWER scales the whole launch, arc shape and all"),
		Powered.Equals(Launch * PowerScale, 0.01f));

	// ...while UP is the launch ANGLE, so it moves the vertical alone and leaves the ground track
	// exactly where it was.
	constexpr float UpScale = 2.0f;
	const FVector Steeper = FAgent::ComputeVehicleKnockdownLaunchVelocity(
		CarVelocity, OffsetToLeft, ForwardGain, LateralGain, VerticalGain * UpScale);
	TestEqual(TEXT("UP leaves the forward throw alone"), float(Steeper.X), float(Launch.X), 0.01f);
	TestEqual(TEXT("...and the sideways throw"), float(Steeper.Y), float(Launch.Y), 0.01f);
	TestEqual(TEXT("...and multiplies only the height"), float(Steeper.Z), float(Launch.Z) * UpScale, 0.01f);

	// Zero UP is a flat shove with no air under it - the bottom of the panel's range, and it has to
	// still be a throw rather than nothing at all.
	const FVector FlatThrow = FAgent::ComputeVehicleKnockdownLaunchVelocity(
		CarVelocity, OffsetToLeft, ForwardGain, LateralGain, 0.0f);
	TestEqual(TEXT("UP at zero takes all the air out of it"), float(FlatThrow.Z), 0.0f, 0.01f);
	TestTrue(TEXT("...and still throws them down the road"), FlatThrow.X > 0.0f);

	// And the shipped numbers put them somewhere worth watching: with the pedestrian gravity of
	// 980 cm/s^2, a cruising car throws a body the better part of a 400 cm tile.
	const float AirtimeSeconds = 2.0f * Launch.Z / 980.0f;
	const float RangeCm = static_cast<float>(FVector(Launch.X, Launch.Y, 0.0f).Size()) * AirtimeSeconds;
	TestTrue(TEXT("A cruising car throws them at least a tile"), RangeCm >= 400.0f);
	TestTrue(TEXT("...and not clean across the district"), RangeCm <= 400.0f * 6.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterKnockdownBounceTest,
	"SimCopter.Knockdown.Bounce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterKnockdownBounceTest::RunTest(const FString& Parameters)
{
	using FAgent = ASimCopterGroundAgent;

	constexpr float Restitution = 0.34f;
	constexpr float Friction = 0.42f;

	// Coming down onto the road at a shallow angle: they hop, and slower than they arrived.
	const FVector Incoming(600.0f, 0.0f, -400.0f);
	const FVector Bounced = FAgent::ComputeKnockdownBounceVelocity(Incoming, FVector::UpVector, Restitution, Friction);
	TestTrue(TEXT("A body that hits the ground comes back up"), Bounced.Z > 0.0f);
	TestEqual(TEXT("...at the restitution fraction of the speed it arrived with"),
		float(Bounced.Z), float(-Incoming.Z) * Restitution, 0.01f);
	TestTrue(TEXT("...still travelling forwards"), Bounced.X > 0.0f);
	TestTrue(TEXT("...but scrubbed by the friction of the contact"), Bounced.X < Incoming.X);
	TestTrue(TEXT("A bounce never adds energy"), Bounced.Size() < Incoming.Size());

	// Into a wall: the horizontal component reverses instead.
	const FVector IntoWall(600.0f, 0.0f, 100.0f);
	const FVector OffWall = FAgent::ComputeKnockdownBounceVelocity(
		IntoWall, FVector(-1.0f, 0.0f, 0.0f), Restitution, Friction);
	TestTrue(TEXT("A body that hits a building comes back off it"), OffWall.X < 0.0f);
	TestTrue(TEXT("...and keeps rising, because a wall does not stop a climb"), OffWall.Z > 0.0f);

	// A contact whose normal they are already leaving must be ignored, or a graze along a kerb
	// flings them back into it every frame they touch it.
	const FVector Leaving(0.0f, 0.0f, 300.0f);
	TestTrue(TEXT("A body already leaving a surface is left alone"),
		FAgent::ComputeKnockdownBounceVelocity(Leaving, FVector::UpVector, Restitution, Friction)
			.Equals(Leaving, 0.001f));

	// A degenerate normal (a hit result with nothing in it) must not annihilate the tumble.
	TestTrue(TEXT("A degenerate surface normal changes nothing"),
		FAgent::ComputeKnockdownBounceVelocity(Incoming, FVector::ZeroVector, Restitution, Friction)
			.Equals(Incoming, 0.001f));

	// Repeated bounces have to converge, or a body pings down the street forever.
	FVector Velocity = Incoming;
	for (int32 Bounce = 0; Bounce < 8; ++Bounce)
	{
		Velocity = FAgent::ComputeKnockdownBounceVelocity(
			FVector(Velocity.X, Velocity.Y, -FMath::Abs(Velocity.Z)), FVector::UpVector, Restitution, Friction);
	}
	TestTrue(TEXT("Eight bounces bring them under the rest threshold"), Velocity.Size() < 45.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterKnockdownRestPoseTest,
	"SimCopter.Knockdown.RestPose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterKnockdownRestPoseTest::RunTest(const FString& Parameters)
{
	using FAgent = ASimCopterGroundAgent;

	// However far the tumble got, the body ends up flat. Odd multiples of 90 are lying down; 0 is
	// standing and 180 is standing on their head, and neither is a thing to land in.
	const float SpinAngles[] = { 0.0f, 20.0f, 89.0f, 91.0f, 170.0f, 180.0f, 260.0f, 450.0f, -35.0f, -200.0f, -720.0f };
	for (const float SpinDegrees : SpinAngles)
	{
		const float RestDegrees = FAgent::ComputeKnockdownRestSpinDegrees(SpinDegrees);
		const float FromFlat = FMath::Abs(FMath::Fmod(FMath::Abs(RestDegrees) - 90.0f, 180.0f));
		TestTrue(
			*FString::Printf(TEXT("A tumble stopped at %.0f degrees lands flat"), SpinDegrees),
			FMath::IsNearlyZero(FromFlat, 0.01f) || FMath::IsNearlyEqual(FromFlat, 180.0f, 0.01f));
		TestTrue(
			*FString::Printf(TEXT("...without turning more than a quarter to get there (%.0f)"), SpinDegrees),
			FMath::Abs(RestDegrees - SpinDegrees) <= 90.0f + KINDA_SMALL_NUMBER);
	}

	// THE AXIS MUST BE HORIZONTAL, and this is the assertion that keeps it that way. The rest angle
	// above is a quarter turn about it, so any Z component is yaw - which spins the body on the spot
	// rather than laying it down, and leaves the sprawl standing at an angle. It shipped that way
	// once, with the jitter drawing up to 0.35 of Z against a unit axis.
	const FVector Launches[] = {
		FVector(600.0f, 0.0f, 300.0f),
		FVector(0.0f, -450.0f, 200.0f),
		FVector(-300.0f, 220.0f, 900.0f),
		FVector(0.0f, 0.0f, 500.0f),      // straight up: no flight direction to be across
		FVector::ZeroVector,
	};
	for (const FVector& Launch : Launches)
	{
		for (const float Jitter : { -0.3f, 0.0f, 0.3f })
		{
			const FVector Axis = FAgent::ComputeKnockdownTumbleAxis(Launch, Jitter, -Jitter);
			TestEqual(TEXT("The tumble axis is horizontal"), float(Axis.Z), 0.0f, KINDA_SMALL_NUMBER);
			TestTrue(TEXT("...and is a real direction"), Axis.IsNormalized());
		}
	}

	// Across the flight, not along it: a body goes over the bonnet, it does not corkscrew.
	const FVector AcrossX = FAgent::ComputeKnockdownTumbleAxis(FVector(600.0f, 0.0f, 300.0f), 0.0f, 0.0f);
	TestTrue(TEXT("The tumble axis lies across the flight"), FMath::Abs(AcrossX.Y) > 0.9f);

	// It is the NEAREST flat, so a body stopped anywhere in a turn tips the short way onto the
	// deck - the least motion from where the tumble actually left it.
	TestEqual(TEXT("A body barely past upright tips forward onto its side"),
		FAgent::ComputeKnockdownRestSpinDegrees(20.0f), 90.0f);
	TestEqual(TEXT("One nearly all the way over falls back onto the same side"),
		FAgent::ComputeKnockdownRestSpinDegrees(170.0f), 90.0f);
	TestEqual(TEXT("...and one just past that carries on onto the other"),
		FAgent::ComputeKnockdownRestSpinDegrees(190.0f), 270.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterKnockdownRecoveryTest,
	"SimCopter.Knockdown.Recovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterKnockdownRecoveryTest::RunTest(const FString& Parameters)
{
	using FAgent = ASimCopterGroundAgent;
	using EPhase = ESimCopterKnockdownPhase;

	// The shipped beats.
	constexpr float SettleSeconds = 1.0f;
	constexpr float ProneSeconds = 3.0f;

	// A second sprawled where they landed...
	TestEqual(TEXT("A body that has just stopped stays sprawled"),
		int32(FAgent::AdvanceKnockdownRecoveryPhase(EPhase::Settle, 0.9f, SettleSeconds, ProneSeconds, false)),
		int32(EPhase::Settle));
	TestEqual(TEXT("...then takes the lying pose at a second"),
		int32(FAgent::AdvanceKnockdownRecoveryPhase(EPhase::Settle, 1.0f, SettleSeconds, ProneSeconds, false)),
		int32(EPhase::Prone));

	// ...three seconds lying in it, then up.
	TestEqual(TEXT("They hold the lying pose the whole three seconds"),
		int32(FAgent::AdvanceKnockdownRecoveryPhase(EPhase::Prone, 2.9f, SettleSeconds, ProneSeconds, false)),
		int32(EPhase::Prone));
	TestEqual(TEXT("...and then get up and walk off"),
		int32(FAgent::AdvanceKnockdownRecoveryPhase(EPhase::Prone, 3.0f, SettleSeconds, ProneSeconds, false)),
		int32(EPhase::None));

	// In the water there is no lying pose to take: the authored clip is drawn on the ground and
	// would be half under the surface, so a swimmer goes straight to making for the shore.
	TestEqual(TEXT("A body that came down in the sea swims instead of lying down"),
		int32(FAgent::AdvanceKnockdownRecoveryPhase(EPhase::Settle, 1.0f, SettleSeconds, ProneSeconds, true)),
		int32(EPhase::WadeToShore));
	TestEqual(TEXT("...and one who was already prone when the tide reached them does too"),
		int32(FAgent::AdvanceKnockdownRecoveryPhase(EPhase::Prone, 3.0f, SettleSeconds, ProneSeconds, true)),
		int32(EPhase::WadeToShore));

	// The clock owns none of the other phases; the flight ends on a surface and the wade ends on
	// dry land, neither of which is a timer.
	TestEqual(TEXT("A tumble in the air is not ended by the recovery clock"),
		int32(FAgent::AdvanceKnockdownRecoveryPhase(EPhase::Airborne, 99.0f, SettleSeconds, ProneSeconds, false)),
		int32(EPhase::Airborne));
	TestEqual(TEXT("Neither is a swim"),
		int32(FAgent::AdvanceKnockdownRecoveryPhase(EPhase::WadeToShore, 99.0f, SettleSeconds, ProneSeconds, true)),
		int32(EPhase::WadeToShore));
	TestEqual(TEXT("Nor is somebody walking about"),
		int32(FAgent::AdvanceKnockdownRecoveryPhase(EPhase::None, 99.0f, SettleSeconds, ProneSeconds, false)),
		int32(EPhase::None));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterKnockdownContactTest,
	"SimCopter.Knockdown.Contact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterKnockdownContactTest::RunTest(const FString& Parameters)
{
	using FAgent = ASimCopterGroundAgent;

	// A car's rendered body at PopulationWorldScale: roughly 90 cm long, 50 wide, in its own frame
	// with the nose at +X. The traffic capsule is a 33.75 cm radius, which is narrower than the car
	// is long - measuring the body is what stops a person standing in the bonnet reading as clear.
	const FBox CarBody(FVector(-45.0, -25.0, -20.0), FVector(45.0, 25.0, 20.0));
	const FTransform CarFrame(FRotator::ZeroRotator, FVector::ZeroVector);
	constexpr float PersonRadiusCm = 8.0f;

	TestTrue(TEXT("Somebody standing on the bonnet is a hit"),
		FAgent::ComputeBodyGapCm(CarBody, CarFrame, FVector(50.0f, 0.0f, 0.0f)) <= PersonRadiusCm);
	TestTrue(TEXT("Somebody a body-length off the nose is not"),
		FAgent::ComputeBodyGapCm(CarBody, CarFrame, FVector(140.0f, 0.0f, 0.0f)) > PersonRadiusCm);
	TestTrue(TEXT("Somebody brushing the wing is a hit"),
		FAgent::ComputeBodyGapCm(CarBody, CarFrame, FVector(0.0f, 30.0f, 0.0f)) <= PersonRadiusCm);
	TestTrue(TEXT("Somebody on the pavement beside it is not"),
		FAgent::ComputeBodyGapCm(CarBody, CarFrame, FVector(0.0f, 70.0f, 0.0f)) > PersonRadiusCm);

	// The near miss the frame sweep exists for. At any ordinary frame rate a cruising car covers a
	// few centimetres against a body over a metre long, so nobody can be stepped over - but one
	// long hitch is enough to move the whole car past somebody between two overlap tests, and that
	// is the frame where a pedestrian would otherwise be driven clean through untouched.
	//
	// Sweeping the box backwards along the travel is the same thing as sweeping the query point
	// forwards along it, which is what the samples in UpdatePedestrianVehicleImpacts are.
	const FVector JustBehind(-100.0f, 0.0f, 0.0f);
	const FVector HitchTravel(150.0f, 0.0f, 0.0f);
	TestTrue(TEXT("After a hitch the car has already left them behind"),
		FAgent::ComputeBodyGapCm(CarBody, CarFrame, JustBehind) > PersonRadiusCm);
	float SweptGapCm = TNumericLimits<float>::Max();
	for (int32 SampleIndex = 0; SampleIndex <= 2; ++SampleIndex)
	{
		SweptGapCm = FMath::Min(
			SweptGapCm,
			FAgent::ComputeBodyGapCm(CarBody, CarFrame, JustBehind + HitchTravel * (0.5f * float(SampleIndex))));
	}
	TestTrue(TEXT("...and are still caught once the frame's travel is swept"),
		SweptGapCm <= PersonRadiusCm);

	// The gap is horizontal only (the caller owns the vertical), so a car on a bridge deck must not
	// be allowed to reach the quay below it by this test alone.
	TestTrue(TEXT("The body gap ignores height, which is why the caller gates it"),
		FAgent::ComputeBodyGapCm(CarBody, CarFrame, FVector(0.0f, 0.0f, 900.0f)) <= PersonRadiusCm);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterRoadJaywalkTest,
	"SimCopter.People.RoadJaywalk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterRoadJaywalkTest::RunTest(const FString& Parameters)
{
	using FAgent = ASimCopterGroundAgent;
	using FRules = FSimCopterPeopleCityRules;

	// The rule itself is the shipped table: no DAT_0058ec00 row contains the road class, which is
	// why the original's crowds never step off the kerb.
	TestTrue(TEXT("Class 7 is the road"), FRules::IsRoadTileClass(7));
	for (const int32 BehaviorClass : { 0, 1, 5, 6, 10, 13, 16, 17, 20, 21 })
	{
		TestFalse(
			*FString::Printf(TEXT("No ambient row admits the road (class %d)"), BehaviorClass),
			FRules::GetAmbientStateTileClasses(BehaviorClass).Contains(7));
	}
	TestFalse(TEXT("Nor does the ambient spawn table"),
		FRules::GetAmbientPedestrianTileClasses().Contains(7));

	// Closed window: a road step is refused even where the remake's looser safety-net rows would
	// have allowed it. That refusal IS the rule.
	TestFalse(TEXT("With the window shut the road is refused"),
		FAgent::IsPedestrianRoadStepAllowed(
			/*bAllowedByTileClassRows=*/true, 7, /*bJaywalkWindowOpen=*/false, /*bAlreadyOnRoad=*/false));

	// Open window: allowed, even though no row grants it. That permission IS the exemption.
	TestTrue(TEXT("With the window open they step out anyway"),
		FAgent::IsPedestrianRoadStepAllowed(
			/*bAllowedByTileClassRows=*/false, 7, /*bJaywalkWindowOpen=*/true, /*bAlreadyOnRoad=*/false));

	// THE TRAP. A walk step is about 8 cm and a tile is 400, so every one of the eight facings from
	// the middle of a road tile lands on that same road tile. Refusing them all does not keep
	// anybody out of the road - they are already in it, thrown there by a car - it pins them there
	// until a window happens to open, which is tens of seconds of standing perfectly still.
	TestTrue(TEXT("Somebody already in the road may always walk"),
		FAgent::IsPedestrianRoadStepAllowed(
			/*bAllowedByTileClassRows=*/false, 7, /*bJaywalkWindowOpen=*/false, /*bAlreadyOnRoad=*/true));
	// ...and reaching the kerb hands the decision straight back to the rows.
	TestTrue(TEXT("Stepping from the road onto a pavement is the rows' answer"),
		FAgent::IsPedestrianRoadStepAllowed(true, 13, false, /*bAlreadyOnRoad=*/true));
	TestFalse(TEXT("...including when the rows refuse it"),
		FAgent::IsPedestrianRoadStepAllowed(false, 13, false, /*bAlreadyOnRoad=*/true));

	// And it reaches the road and nothing else: every other class is left exactly to the rows,
	// whichever way the window happens to be and whichever side of the kerb they start on.
	for (const int32 TileClass : { 1, 2, 3, 4, 5, 10, 11, 12, 13 })
	{
		for (const bool bWindowOpen : { false, true })
		{
			for (const bool bOnRoad : { false, true })
			{
				TestTrue(
					*FString::Printf(TEXT("Class %d is the rows' answer, allowed (%d/%d)"), TileClass, bWindowOpen ? 1 : 0, bOnRoad ? 1 : 0),
					FAgent::IsPedestrianRoadStepAllowed(true, TileClass, bWindowOpen, bOnRoad));
				TestFalse(
					*FString::Printf(TEXT("Class %d is the rows' answer, refused (%d/%d)"), TileClass, bWindowOpen ? 1 : 0, bOnRoad ? 1 : 0),
					FAgent::IsPedestrianRoadStepAllowed(false, TileClass, bWindowOpen, bOnRoad));
			}
		}
	}

	// The cadence: one roll per window and no drift, whatever the frame rate. Ten seconds of 60 fps
	// frames must offer exactly one roll.
	constexpr float WindowSeconds = 10.0f;
	float SecondsRemaining = WindowSeconds;
	int32 Rolls = 0;
	for (int32 Frame = 0; Frame < 600; ++Frame)
	{
		if (FAgent::AdvanceRoadJaywalkWindow(SecondsRemaining, 1.0f / 60.0f, WindowSeconds))
		{
			++Rolls;
		}
	}
	TestEqual(TEXT("Ten seconds of 60 fps frames is one roll"), Rolls, 1);

	// A hundred seconds is ten, however the frames are chopped up - the rearm is from the deadline,
	// not from the frame that noticed it.
	SecondsRemaining = WindowSeconds;
	Rolls = 0;
	for (int32 Frame = 0; Frame < 1000; ++Frame)
	{
		if (FAgent::AdvanceRoadJaywalkWindow(SecondsRemaining, 0.1f, WindowSeconds))
		{
			++Rolls;
		}
	}
	TestEqual(TEXT("A hundred seconds is ten rolls"), Rolls, 10);

	// A hitch longer than a whole window must still leave a window in front of them, or the walker
	// re-rolls on every frame from then on.
	SecondsRemaining = WindowSeconds;
	TestTrue(TEXT("A hitch past the deadline rolls"),
		FAgent::AdvanceRoadJaywalkWindow(SecondsRemaining, 45.0f, WindowSeconds));
	TestTrue(TEXT("...and leaves a full window ahead rather than a debt"),
		SecondsRemaining > 0.0f && SecondsRemaining <= WindowSeconds + KINDA_SMALL_NUMBER);
	TestFalse(TEXT("...so the very next frame does not roll again"),
		FAgent::AdvanceRoadJaywalkWindow(SecondsRemaining, 1.0f / 60.0f, WindowSeconds));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterLyingPoseGroundTest,
	"SimCopter.People.LyingPoseClearsGround",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterLyingPoseGroundTest::RunTest(const FString& Parameters)
{
	using FFigure = FSimCopterPopulationFigure;

	// The bug this guards, against the shipped art: figure calibration pins local Z=0 to the feet of
	// the STANDING clip, because scale has to come from one pose. Nothing then holds any other pose
	// above that plane, and the lying poses are drawn low on the 1996 screen - which maps straight
	// down through the floor. Casualties and anyone a car had knocked over were buried to the
	// shoulders with only their head showing.
	const FString RootPath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reference/SimCopterOriginalGame")));
	const FString PrivAnimPath = FSimCopterPrivAnimReader::ResolvePrivAnimPath(RootPath);
	if (PrivAnimPath.IsEmpty())
	{
		AddInfo(TEXT("Original privanim.df not present; skipping the shipped-art half."));
	}
	else
	{
		FPrivAnimModel Model;
		FString Error;
		if (!TestTrue(TEXT("Parses privanim.df"), FSimCopterPrivAnimReader::LoadFromFile(PrivAnimPath, Model, Error)))
		{
			AddError(Error);
			return false;
		}

		// 44 cm is PedestrianBodyHeightCm * PopulationWorldScale, the height the agent builds at.
		constexpr float BodyHeightCm = 44.0f;
		int32 FiguresChecked = 0;
		int32 FiguresWhoseWalkDips = 0;
		bool bAnyPoseDropped = false;
		for (const FPrivAnimFigure& Figure : Model.Figures)
		{
			const FPrivAnimClip* Standing = Model.FindClip(Figure, TEXT("1Wal"));
			if (Standing == nullptr)
			{
				continue;
			}
			++FiguresChecked;
			const FFigure::FCalibration Calibration = FFigure::Calibrate(*Standing, BodyHeightCm);

			// A walking pedestrian must not move by so much as a millimetre because of this fix.
			// Note this is the LIFT, not the drop: every shipped walk cycle swings its feet a couple
			// of centimetres below its own frame 0, which is the ground they already stand on.
			TestEqual(
				*FString::Printf(TEXT("%s: the walk clip needs no lift"), *Figure.Name),
				FFigure::ComputeClipGroundLiftCm(*Standing, *Standing, Calibration), 0.0f, 0.001f);
			if (FFigure::ComputeClipDropBelowFeetCm(*Standing, Calibration) > 0.0f)
			{
				++FiguresWhoseWalkDips;
			}

			for (const TCHAR* Mnemonic : { TEXT("Dead"), TEXT("Inju"), TEXT("Slum") })
			{
				const FPrivAnimClip* Clip = Model.FindClip(Figure, Mnemonic);
				if (Clip == nullptr)
				{
					continue;
				}
				const float LiftCm = FFigure::ComputeClipGroundLiftCm(*Clip, *Standing, Calibration);
				if (LiftCm > 0.0f)
				{
					bAnyPoseDropped = true;
					AddInfo(FString::Printf(
						TEXT("%s '%s' is drawn %.2f cm into the ground of a %.0f cm body"),
						*Figure.Name, Mnemonic, LiftCm, BodyHeightCm));
					// Never more than a whole body: a lift that large would mean the calibration
					// itself is wrong rather than the pose being drawn low.
					TestTrue(
						*FString::Printf(TEXT("%s '%s' lift is within a body height"), *Figure.Name, Mnemonic),
						LiftCm <= BodyHeightCm);
				}
			}
		}
		TestTrue(TEXT("Checked at least the shipped figure set"), FiguresChecked >= 20);
		// Why the datum is the walk cycle's own dip rather than zero: almost every shipped walk
		// swings below its frame 0, and that swing is where a pedestrian visibly meets the pavement
		// today. (The two that do not are the quadrupeds, whose '1Wal' is never bound - the clip
		// remap sends them to DgRn/DgSt.)
		TestTrue(
			*FString::Printf(TEXT("Nearly every walk cycle dips below its frame 0 (%d of %d)"), FiguresWhoseWalkDips, FiguresChecked),
			FiguresWhoseWalkDips >= FiguresChecked - 2);
		// If this ever stops being true the art has changed, and the lift is dead code rather than
		// a fix - which is worth knowing either way.
		TestTrue(TEXT("At least one shipped lying pose really is drawn below the feet"), bAnyPoseDropped);
	}

	// The pure rule, with no data needed. Two model units per cm, and a standing clip whose frame 0
	// puts the feet at model Z 10.
	FFigure::FCalibration Calibration;
	Calibration.ScaleCmPerUnit = 2.0f;
	Calibration.FeetOffsetCm = 20.0f;

	// The walk cycle: frame 0 on the plane, frame 1 swinging two units (4 cm) below it. That swing
	// is where a walking pedestrian already meets the pavement.
	FPrivAnimClip Standing;
	Standing.PartCount = 1;
	Standing.FrameCount = 2;
	Standing.Segments.SetNum(2);
	Standing.Segments[0].A.Z = 10;
	Standing.Segments[1].A.Z = 12;
	TestEqual(TEXT("The walk clip's own dip is measured over every frame"),
		FFigure::ComputeClipDropBelowFeetCm(Standing, Calibration), 4.0f, 0.001f);
	TestEqual(TEXT("...and lifts it by nothing, because that dip is its ground"),
		FFigure::ComputeClipGroundLiftCm(Standing, Standing, Calibration), 0.0f, 0.001f);

	// A lying pose drawn deeper than that swing is lifted by exactly the difference.
	FPrivAnimClip Lying;
	Lying.PartCount = 1;
	Lying.FrameCount = 1;
	Lying.Segments.SetNum(1);
	Lying.Segments[0].A.Z = 25;
	TestEqual(TEXT("A pose drawn below the walk cycle is lifted by the difference"),
		FFigure::ComputeClipGroundLiftCm(Lying, Standing, Calibration), 26.0f, 0.001f);

	// One no deeper than the walk cycle is left exactly where it is, and so is one above the plane.
	Lying.Segments[0].A.Z = 12;
	TestEqual(TEXT("A pose no deeper than the walk cycle is not lifted"),
		FFigure::ComputeClipGroundLiftCm(Lying, Standing, Calibration), 0.0f, 0.001f);
	Lying.Segments[0].A.Z = 4;
	TestEqual(TEXT("Nor is one drawn entirely above the feet"),
		FFigure::ComputeClipGroundLiftCm(Lying, Standing, Calibration), 0.0f, 0.001f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
