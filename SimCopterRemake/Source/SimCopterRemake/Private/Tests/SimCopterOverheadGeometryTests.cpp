// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/SimCopterHelicopterPawn.h"
#include "Ground/SimCopterGroundAgent.h"

#include "Misc/AutomationTest.h"

// Overhead geometry is not ground, and it is not a wall either.
//
// The original city is 2.5D: FUN_004c82c0 answers "what is the top of this cell", and in a world
// where every cell is a stack there is nothing to get wrong. The remake renders real GEO meshes,
// where a bridge arch, a power span, an elevated rail deck or a first-floor overhang passes OVER
// cells whose ground is the street below it. Both of the remake's downward probes used to start
// above the world and take their first hit, which returns that span - so every mesh behaved as a
// solid box extruded from its highest point down to the terrain. Pedestrians were walled in under
// one (all eight facings refused, which is what stranded police mid-chase) and the flight model was
// handed a "ground" above the aircraft.
//
// These are the pure halves of the fix: where the walk probe may start, and the shape of the body
// swept against the real mesh to answer the lateral question the probe deliberately no longer does.

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
// Original units are 1/64 of a 400 cm tile; the max-climb attribute (person+0x144) is 5 of them.
constexpr float CmPerOriginalUnit = 400.0f / 64.0f;
constexpr float MaxStepClimbCm = 5.0f * CmPerOriginalUnit; // ~31 cm
constexpr float ProbeMarginCm = 8.0f;

// The pedestrian collision capsule.
constexpr float CapsuleRadiusCm = 32.0f;
constexpr float CapsuleHalfHeightCm = 88.0f;

// What a downward line trace starting at StartZ reports: the highest surface at or below it.
// Surfaces are given top-down, the way the world stacks them over one column.
bool FirstHitBelow(const TArray<float>& SurfaceZs, const float StartZ, float& OutHitZ)
{
	bool bFound = false;
	for (const float SurfaceZ : SurfaceZs)
	{
		if (SurfaceZ <= StartZ && (!bFound || SurfaceZ > OutHitZ))
		{
			OutHitZ = SurfaceZ;
			bFound = true;
		}
	}
	return bFound;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterWalkProbeOverheadTest,
	"SimCopter.Collision.WalkProbeIgnoresOverhead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterWalkProbeOverheadTest::RunTest(const FString& Parameters)
{
	using FAgent = ASimCopterGroundAgent;

	// A roadway with a bridge arch 4 m over it and a power span 6 m over that - the two cases that
	// walled people in. The walker's feet are on the road.
	const float RoadZ = 0.0f;
	const TArray<float> UnderTheBridge = { 1000.0f, 400.0f, RoadZ };

	const float CeilingZ = FAgent::GetPedestrianWalkProbeCeilingZ(RoadZ, MaxStepClimbCm, ProbeMarginCm);
	TestTrue(TEXT("The probe ceiling is above everything the walker may step onto"),
		CeilingZ >= RoadZ + MaxStepClimbCm);
	TestTrue(TEXT("...and below the arch they are walking under"), CeilingZ < 400.0f);

	float SurfaceZ = 0.0f;
	TestTrue(TEXT("A walker under a bridge still finds a surface"),
		FirstHitBelow(UnderTheBridge, CeilingZ, SurfaceZ));
	TestEqual(TEXT("...and it is the road, not the deck over their head"), SurfaceZ, RoadZ);
	TestTrue(TEXT("So the climb gate lets the step through"),
		FAgent::IsPedestrianHeightTransitionAllowed(
			SurfaceZ - RoadZ, MaxStepClimbCm, /*bMoveThroughWalls=*/false));

	// The regression this replaces: starting above the world returns the span, and a 4 m rise fails
	// the climb gate on every facing - the invisible wall.
	float OldSurfaceZ = 0.0f;
	TestTrue(TEXT("The old high probe finds something"),
		FirstHitBelow(UnderTheBridge, RoadZ + 12000.0f, OldSurfaceZ));
	TestEqual(TEXT("...namely the topmost span"), OldSurfaceZ, 1000.0f);
	TestFalse(TEXT("...which the climb gate refuses, in every direction"),
		FAgent::IsPedestrianHeightTransitionAllowed(
			OldSurfaceZ - RoadZ, MaxStepClimbCm, /*bMoveThroughWalls=*/false));

	// Kerbs and road lips are still steps, not walls: the ceiling has to clear them.
	const TArray<float> Kerb = { 12.0f };
	TestTrue(TEXT("A kerb is inside the probe ceiling"), FirstHitBelow(Kerb, CeilingZ, SurfaceZ));
	TestEqual(TEXT("...and is the surface found"), SurfaceZ, 12.0f);

	// A one-storey roof is not. It is above the climb allowance, so the probe passes under it and
	// the step is decided by the mesh sweep instead - which is what stops people entering walls.
	const float SingleStoreyRoofZ = 150.0f;
	TestTrue(TEXT("A one-storey roof is above the probe ceiling"), SingleStoreyRoofZ > CeilingZ);

	// Someone standing on that roof probes from their own feet, so the roof is their ground.
	const float RoofCeilingZ =
		FAgent::GetPedestrianWalkProbeCeilingZ(SingleStoreyRoofZ, MaxStepClimbCm, ProbeMarginCm);
	const TArray<float> OnTheRoof = { SingleStoreyRoofZ, RoadZ };
	TestTrue(TEXT("A walker on a roof finds a surface"), FirstHitBelow(OnTheRoof, RoofCeilingZ, SurfaceZ));
	TestEqual(TEXT("...the roof under them, not the street below it"), SurfaceZ, SingleStoreyRoofZ);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterStepSweepShapeTest,
	"SimCopter.Collision.StepSweepShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterStepSweepShapeTest::RunTest(const FString& Parameters)
{
	using FAgent = ASimCopterGroundAgent;

	float RadiusCm = 0.0f;
	float HalfHeightCm = 0.0f;
	float CenterZ = 0.0f;

	// Flat ground, no step.
	FAgent::ComputePedestrianStepSweepShape(
		/*SourceFeetZ=*/0.0f,
		/*TargetSurfaceZ=*/0.0f,
		MaxStepClimbCm,
		CapsuleRadiusCm,
		CapsuleHalfHeightCm,
		/*RadiusScale=*/0.95f,
		RadiusCm,
		HalfHeightCm,
		CenterZ);

	const float BottomZ = CenterZ - HalfHeightCm;
	const float TopZ = CenterZ + HalfHeightCm;
	TestTrue(TEXT("The swept body starts above the climb band, so a kerb is not an obstacle"),
		BottomZ >= MaxStepClimbCm - KINDA_SMALL_NUMBER);
	TestTrue(TEXT("...and no higher, or a knee-high bollard would be missed"),
		BottomZ <= MaxStepClimbCm + KINDA_SMALL_NUMBER);
	TestTrue(TEXT("It reaches the top of the walker, so a waist-high railing blocks"),
		TopZ >= 2.0f * CapsuleHalfHeightCm - KINDA_SMALL_NUMBER);
	TestTrue(TEXT("It is inset from the capsule, so contact cannot read as penetration next frame"),
		RadiusCm < CapsuleRadiusCm);
	// The inset is a float-tolerance margin, not a passability allowance. At population scale the
	// capsule is 8 cm and the rendered figure is wider than a generous inset would sweep, so a
	// walker whose body must stay out of walls needs nearly the whole radius.
	TestTrue(TEXT("...by a margin only, never enough to let the body clip a wall"),
		RadiusCm >= 0.9f * CapsuleRadiusCm);
	TestTrue(TEXT("A capsule's half height is never below its radius"), HalfHeightCm >= RadiusCm);

	// Stepping UP onto a kerb: the body must be measured from where the walker will be standing,
	// or the kerb they are climbing onto reads as a wall in front of them.
	FAgent::ComputePedestrianStepSweepShape(
		/*SourceFeetZ=*/0.0f,
		/*TargetSurfaceZ=*/20.0f,
		MaxStepClimbCm,
		CapsuleRadiusCm,
		CapsuleHalfHeightCm,
		/*RadiusScale=*/0.95f,
		RadiusCm,
		HalfHeightCm,
		CenterZ);
	TestTrue(TEXT("A step up lifts the swept body clear of the surface it is stepping onto"),
		CenterZ - HalfHeightCm >= 20.0f + MaxStepClimbCm - KINDA_SMALL_NUMBER);

	// Stepping DOWN off one: the drop must not lower the body into the ground it is leaving.
	FAgent::ComputePedestrianStepSweepShape(
		/*SourceFeetZ=*/20.0f,
		/*TargetSurfaceZ=*/0.0f,
		MaxStepClimbCm,
		CapsuleRadiusCm,
		CapsuleHalfHeightCm,
		/*RadiusScale=*/0.95f,
		RadiusCm,
		HalfHeightCm,
		CenterZ);
	TestTrue(TEXT("A step down keeps the swept body above the kerb being left"),
		CenterZ - HalfHeightCm >= 20.0f + MaxStepClimbCm - KINDA_SMALL_NUMBER);

	// The population is authored at PopulationWorldScale (0.25) against a tile that stands in for
	// the original's much larger one, so a person is 44 cm tall while the climb allowance is still
	// the decoded 31 cm. Taken literally that leaves 13 cm of body to sweep with and a walker
	// passes through nearly everything, so the climb band is capped as a fraction of the body.
	constexpr float PopulationWorldScale = 0.25f;
	const float ScaledHalfHeightCm = CapsuleHalfHeightCm * PopulationWorldScale;
	const float ScaledBodyHeightCm = 2.0f * ScaledHalfHeightCm;
	FAgent::ComputePedestrianStepSweepShape(
		0.0f,
		0.0f,
		MaxStepClimbCm,
		CapsuleRadiusCm * PopulationWorldScale,
		ScaledHalfHeightCm,
		0.95f,
		RadiusCm,
		HalfHeightCm,
		CenterZ);
	TestTrue(TEXT("A population-scale walker still gets a legal capsule"), HalfHeightCm >= RadiusCm);
	TestTrue(TEXT("...with a positive radius"), RadiusCm > 0.0f);
	TestTrue(TEXT("The climb band cannot eat the whole body at population scale"),
		CenterZ - HalfHeightCm < MaxStepClimbCm);
	TestTrue(TEXT("...and most of the walker is still swept"),
		2.0f * HalfHeightCm >= ScaledBodyHeightCm * 0.6f);
	TestTrue(TEXT("...while a kerb at this scale still passes underneath"),
		CenterZ - HalfHeightCm > 3.0f * PopulationWorldScale);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterWallContainmentStepTest,
	"SimCopter.Collision.WallContainmentStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterWallContainmentStepTest::RunTest(const FString& Parameters)
{
	using FAgent = ASimCopterGroundAgent;
	using EStep = FAgent::EWallContainmentStep;

	constexpr float MinCm = 0.5f;
	constexpr float MaxCm = 200.0f;

	// Standing still: no scene query. Most of the population, most of the time.
	TestEqual(TEXT("A stationary walker is not swept"),
		int32(FAgent::ClassifyWallContainmentStep(0.0f, MinCm, MaxCm)), int32(EStep::Ignore));
	TestEqual(TEXT("Neither is sub-millimetre jitter"),
		int32(FAgent::ClassifyWallContainmentStep(0.4f, MinCm, MaxCm)), int32(EStep::Ignore));

	// An ordinary walk step, and a separation nudge: both are swept, which is the point.
	TestEqual(TEXT("A walk step is swept"),
		int32(FAgent::ClassifyWallContainmentStep(4.0f, MinCm, MaxCm)), int32(EStep::Sweep));
	TestEqual(TEXT("So is a crowd-separation nudge"),
		int32(FAgent::ClassifyWallContainmentStep(0.9f, MinCm, MaxCm)), int32(EStep::Sweep));

	// A teleport must drop the anchor rather than sweep from it: boarding, alighting, mission
	// placement and a restored save all move a person further than they can walk, and sweeping
	// across that gap would stop them against the first wall between the two points.
	TestEqual(TEXT("A teleport re-anchors instead of sweeping"),
		int32(FAgent::ClassifyWallContainmentStep(900.0f, MinCm, MaxCm)), int32(EStep::Rebase));
	TestEqual(TEXT("...and the boundary itself re-anchors"),
		int32(FAgent::ClassifyWallContainmentStep(MaxCm, MinCm, MaxCm)), int32(EStep::Rebase));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterAirframeColliderFitTest,
	"SimCopter.Collision.AirframeColliderFit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterAirframeColliderFitTest::RunTest(const FString& Parameters)
{
	using FHeli = ASimCopterHelicopterPawn;

	// What shipped: InitCapsuleSize(95, 82) clamps the half height UP to the radius, so the
	// collider was a 190 cm sphere - reaching 95 cm above the actor origin when the fuselage stops
	// 15 cm below it. That 110 cm of phantom is what caught bridge soffits and power spans.
	constexpr float OldHalfHeightCm = 95.0f;
	// Roughly a remake airframe in ModelPivot's frame, skids at the bottom of the old capsule.
	const FBox Airframe(FVector(-125.0, -30.0, -95.0), FVector(125.0, 30.0, -15.0));

	float RadiusCm = 0.0f;
	float HalfHeightCm = 0.0f;
	float PivotZCm = 0.0f;
	FHeli::ComputeAirframeCollisionFit(Airframe, 20.0f, 25.0f, RadiusCm, HalfHeightCm, PivotZCm);

	// The capsule is now the fuselage's own cross-section, and crucially no taller than it.
	TestEqual(TEXT("Collider half height is the fuselage half height"), HalfHeightCm, 40.0f);
	TestEqual(TEXT("Collider radius is the fuselage beam"), RadiusCm, 30.0f);
	TestTrue(TEXT("Radius never exceeds half height, or SetCapsuleSize clamps the height back up"),
		RadiusCm <= HalfHeightCm);
	TestTrue(TEXT("The collider is dramatically shorter than the sphere it replaces"),
		HalfHeightCm < OldHalfHeightCm * 0.5f);

	// THE ALTITUDE DATUM. The capsule bottom is `originZ - halfHeight` and the flight model calls
	// that the aircraft's altitude, so the fuselage floor has to land exactly on it or a landed
	// helicopter would hover or sink.
	const float FuselageFloorZ = static_cast<float>(Airframe.Min.Z) + PivotZCm;
	TestEqual(TEXT("The fuselage floor sits exactly on the capsule floor"), FuselageFloorZ, -HalfHeightCm);

	// ...and because both the collider and the model are pinned to that same floor, nothing the
	// player sees moves: the fuselage roof stays the same height above the skids as before.
	const float RoofAboveSkids = static_cast<float>(Airframe.Max.Z - Airframe.Min.Z);
	const float NewRoofAboveCapsuleFloor = (static_cast<float>(Airframe.Max.Z) + PivotZCm) + HalfHeightCm;
	TestEqual(TEXT("Fuselage roof keeps its height above the skids"), NewRoofAboveCapsuleFloor, RoofAboveSkids);

	// A missing or degenerate fuselage must still produce a collider that can register a hit.
	FHeli::ComputeAirframeCollisionFit(
		FBox(FVector::ZeroVector, FVector::ZeroVector), 20.0f, 25.0f, RadiusCm, HalfHeightCm, PivotZCm);
	TestEqual(TEXT("A degenerate body falls back to the minimum half height"), HalfHeightCm, 25.0f);
	TestEqual(TEXT("...and the minimum radius"), RadiusCm, 20.0f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
