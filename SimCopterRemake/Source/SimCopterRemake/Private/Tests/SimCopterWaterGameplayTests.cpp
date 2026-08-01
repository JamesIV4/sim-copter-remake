// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/SimCopterWaterGameplay.h"
#include "Flight/SimCopterHelicopterPawn.h"
#include "City/SimCity2000CityActor.h"
#include "Misc/AutomationTest.h"

using namespace SimCopterWaterGameplay;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterWaterTextureAnimationRateTest,
	"SimCopter.Water.TextureAnimationRate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterWaterTextureAnimationRateTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("flowing default is 4 Hz"), ASimCity2000CityActor::DefaultWaterTextureFramesPerSecond, 4.0f);
	TestEqual(TEXT("zero freezes the texture"), ASimCity2000CityActor::SanitizeWaterTextureFramesPerSecond(0.0f), 0.0f);
	TestEqual(TEXT("debug rate clamps at 120 FPS"), ASimCity2000CityActor::SanitizeWaterTextureFramesPerSecond(200.0f), 120.0f);
	TestEqual(TEXT("invalid rate returns to default"), ASimCity2000CityActor::SanitizeWaterTextureFramesPerSecond(NAN), 4.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterRopeAutoZoomGateTest,
	"SimCopter.Water.RopeAutoZoomGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterRopeAutoZoomGateTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("View 1 closest zoom and full rope backs out"),
		ASimCopterHelicopterPawn::ShouldUseRopeAutoZoom(
			ESimCopterCameraMode::Chase, 0.0f, SimCopterWinch::LoweredNode));
	TestFalse(TEXT("A player-selected wider zoom is preserved"),
		ASimCopterHelicopterPawn::ShouldUseRopeAutoZoom(
			ESimCopterCameraMode::Chase, 0.01f, SimCopterWinch::LoweredNode));
	TestFalse(TEXT("Pulling in one node returns to the player's zoom"),
		ASimCopterHelicopterPawn::ShouldUseRopeAutoZoom(
			ESimCopterCameraMode::Chase, 0.0f, SimCopterWinch::LoweredNode + 1));
	TestFalse(TEXT("Other camera views do not auto-zoom"),
		ASimCopterHelicopterPawn::ShouldUseRopeAutoZoom(
			ESimCopterCameraMode::Cockpit, 0.0f, SimCopterWinch::LoweredNode));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterWaterFillDumpTest,
	"SimCopter.Water.FillDumpPounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterWaterFillDumpTest::RunTest(const FString& Parameters)
{
	TestTrue(
		TEXT("active bucket at surface plus two units fills on class 9"),
		CanFillBucket(true, 0x120000, 0x100000, 9));
	TestFalse(
		TEXT("attachment gate"),
		CanFillBucket(false, 0x100000, 0x100000, 0));
	TestFalse(
		TEXT("surface-height gate"),
		CanFillBucket(true, 0x120001, 0x100000, 0));
	TestFalse(
		TEXT("terrain-class gate"),
		CanFillBucket(true, 0x100000, 0x100000, 10));

	TestEqual(TEXT("one fill frame adds 30 pounds"), FillBucketFrame(0, 1548, FillPoundsPerFrame), 30);
	TestEqual(TEXT("fill caps at max load"), FillBucketFrame(1540, 1548, FillPoundsPerFrame), 1548);
	TestEqual(TEXT("one dump frame removes 21 pounds"), DumpBucketFrame(100, DumpPoundsPerFrame), 79);
	TestEqual(TEXT("dump clamps at empty"), DumpBucketFrame(12, DumpPoundsPerFrame), 0);
	TestEqual(TEXT("cannon drains half the integer dump rate"), CannonPoundsPerFrame, 10);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterWaterParticleConstantsTest,
	"SimCopter.Water.ParticleLifeAndDrag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterWaterParticleConstantsTest::RunTest(const FString& Parameters)
{
	FWaterParticleMotion Cannon;
	Cannon.Direction1616 = FIntVector(FixedOne, 0, 0);
	Cannon.Speed1616 = 10 * FixedOne;
	Cannon.Life1616 = InitialParticleLife1616;
	const FWaterParticleFrame CannonDragFrame =
		AdvanceParticleFrame(Cannon, EWaterEmitter::Cannon, 0);
	TestTrue(TEXT("zero-time drag frame remains active"), CannonDragFrame.bAlive);
	TestEqual(
		TEXT("type-5 drag is 0x51e per frame"),
		Cannon.Speed1616,
		10 * FixedOne - FixedMul(CannonDrag1616, 10 * FixedOne));

	FWaterParticleMotion Bucket;
	Bucket.Direction1616 = FIntVector(FixedOne, 0, 0);
	Bucket.Speed1616 = 10 * FixedOne;
	Bucket.Life1616 = InitialParticleLife1616;
	AdvanceParticleFrame(Bucket, EWaterEmitter::Bucket, 0);
	TestEqual(
		TEXT("type-6 drag is 0x28f per frame"),
		Bucket.Speed1616,
		10 * FixedOne - FixedMul(BucketDrag1616, 10 * FixedOne));
	TestTrue(TEXT("bucket drag is weaker"), Bucket.Speed1616 > Cannon.Speed1616);

	const int32 FrameDelta = FixedOne / 60;
	FWaterParticleMotion Falling;
	Falling.Direction1616 = FIntVector(FixedOne, 0, 0);
	Falling.Speed1616 = 10 * FixedOne;
	Falling.Life1616 = InitialParticleLife1616;
	const FWaterParticleFrame FallingFrame =
		AdvanceParticleFrame(Falling, EWaterEmitter::Bucket, FrameDelta);
	TestEqual(
		TEXT("life decays by fixed frame delta"),
		Falling.Life1616,
		InitialParticleLife1616 - FrameDelta);
	TestTrue(TEXT("gravity bends trajectory down"), Falling.Direction1616.Z < 0);
	TestTrue(TEXT("particle travels during a nonzero frame"), FallingFrame.Travel1616 != FIntVector::ZeroValue);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterWaterImpactStrengthTest,
	"SimCopter.Water.ImpactStrength",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterWaterImpactStrengthTest::RunTest(const FString& Parameters)
{
	const int32 RemainingLife = 0x30000;
	const FWaterImpact Cannon = ResolveImpact(EWaterEmitter::Cannon, RemainingLife, false);
	const FWaterImpact Bucket = ResolveImpact(EWaterEmitter::Bucket, RemainingLife, false);
	TestTrue(TEXT("cannon land impact douses"), Cannon.bDouse);
	TestTrue(TEXT("bucket land impact douses"), Bucket.bDouse);
	TestEqual(TEXT("cannon strength is remaining life"), Cannon.DouseStrength1616, RemainingLife);
	TestEqual(TEXT("bucket strength is one quarter remaining life"), Bucket.DouseStrength1616, RemainingLife >> 2);
	TestEqual(TEXT("same-life cannon to bucket ratio is four"), Cannon.DouseStrength1616 / Bucket.DouseStrength1616, 4);
	TestEqual(TEXT("strong land impact uses class 9"), Cannon.PuffClass, static_cast<uint8>(9));

	const FWaterImpact Weak = ResolveImpact(EWaterEmitter::Cannon, 0x1ffff, false);
	TestEqual(TEXT("weak land impact uses class 8"), Weak.PuffClass, static_cast<uint8>(8));

	const FWaterImpact Water = ResolveImpact(EWaterEmitter::Cannon, RemainingLife, true);
	TestFalse(TEXT("water-surface landing never douses"), Water.bDouse);
	TestEqual(TEXT("water-surface landing strength is zero"), Water.DouseStrength1616, 0);
	TestEqual(TEXT("water-surface landing uses class 8"), Water.PuffClass, static_cast<uint8>(8));
	TestEqual(TEXT("water-surface landing sound id"), Water.SoundId, static_cast<uint8>(0x0f));

	FWaterParticleMotion Travel;
	Travel.Direction1616 = FIntVector(FixedOne, 0, 0);
	Travel.Speed1616 = 20 * FixedOne;
	for (int32 FrameIndex = 0; FrameIndex < 30; ++FrameIndex)
	{
		AdvanceParticleFrame(Travel, EWaterEmitter::Cannon, FixedOne / 60);
	}
	const FWaterImpact TravelImpact =
		ResolveImpact(EWaterEmitter::Cannon, Travel.Life1616, false);
	TestEqual(
		TEXT("impact strength follows life remaining after travel"),
		TravelImpact.DouseStrength1616,
		Travel.Life1616);
	TestTrue(
		TEXT("longer travel produces less douse strength"),
		TravelImpact.DouseStrength1616 < InitialParticleLife1616);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterWaterDeterminismTest,
	"SimCopter.Water.ParticleDeterminism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterWaterDeterminismTest::RunTest(const FString& Parameters)
{
	FWaterParticleMotion A;
	A.Direction1616 = DirectionToFixed(FVector(0.82f, 0.25f, 0.51f));
	A.Speed1616 = FMath::RoundToInt(128.6f * static_cast<float>(FixedOne));
	A.Life1616 = InitialParticleLife1616;
	FWaterParticleMotion B = A;
	FIntVector TravelA = FIntVector::ZeroValue;
	FIntVector TravelB = FIntVector::ZeroValue;
	const int32 FrameDelta = FixedOne / 60;
	for (int32 FrameIndex = 0; FrameIndex < 120; ++FrameIndex)
	{
		const FWaterParticleFrame FrameA =
			AdvanceParticleFrame(A, EWaterEmitter::Cannon, FrameDelta);
		const FWaterParticleFrame FrameB =
			AdvanceParticleFrame(B, EWaterEmitter::Cannon, FrameDelta);
		TravelA += FrameA.Travel1616;
		TravelB += FrameB.Travel1616;
	}

	TestTrue(TEXT("direction is deterministic"), A.Direction1616 == B.Direction1616);
	TestEqual(TEXT("speed is deterministic"), A.Speed1616, B.Speed1616);
	TestEqual(TEXT("life is deterministic"), A.Life1616, B.Life1616);
	TestTrue(TEXT("integrated path is deterministic"), TravelA == TravelB);
	return true;
}

// Regression: FUN_004a5ca0 substitutes the swept elevation into a *unit* aim vector
// (FUN_004b9b10 normalises before storing it), so 4.0 of elevation against a horizontal
// component of at most 1.0 throws the shot ~76 degrees up. Building the direction from the
// raw target delta instead - hundreds of units of horizontal against the same 4.0 - flattens
// the whole sweep into under a degree, which is a stream that dribbles onto the road.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterFireTruckJetSweepTest,
	"SimCopter.Water.FireTruckJetSweep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterFireTruckJetSweepTest::RunTest(const FString& Parameters)
{
	const FIntVector UnitAim = DirectionToFixed(FVector(1.0f, 0.0f, 0.0f));
	const int32 Distance1616 = 200 * FixedOne;

	FFireTruckJetSweep Sweep;
	float MaxElevationDegrees = 0.0f;
	float MinElevationDegrees = 90.0f;
	int32 Reversals = 0;
	int32 PreviousStep = Sweep.Step1616;

	// A full sweep up and back: 4.0 / 0.1 is 40 shots each way.
	for (int32 Shot = 0; Shot < 100; ++Shot)
	{
		const FFireTruckJetLaunch Launch = AdvanceFireTruckJet(
			Sweep,
			FireTruckBuildingElevationMax1616,
			UnitAim,
			Distance1616,
			0);
		if (Sweep.Step1616 != PreviousStep)
		{
			++Reversals;
			PreviousStep = Sweep.Step1616;
		}
		const FVector Direction = DirectionToFloat(Launch.Direction1616);
		const float Degrees = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Direction.Z, -1.0f, 1.0f)));
		MaxElevationDegrees = FMath::Max(MaxElevationDegrees, Degrees);
		MinElevationDegrees = FMath::Min(MinElevationDegrees, Degrees);
		TestTrue(TEXT("the shot always has forward reach"), Direction.X > 0.0f);
	}

	TestTrue(
		*FString::Printf(TEXT("the sweep reaches a steep arc, got %.1f degrees"), MaxElevationDegrees),
		MaxElevationDegrees > 70.0f);
	TestTrue(
		*FString::Printf(TEXT("the sweep also comes back down flat, got %.1f degrees"), MinElevationDegrees),
		MinElevationDegrees < 10.0f);
	TestTrue(TEXT("the sweep reverses at both ends"), Reversals >= 2);
	TestEqual(TEXT("elevation stays inside the building bound"), Sweep.Elevation1616 <= FireTruckBuildingElevationMax1616, true);

	// speed = rand() % 100 + distance / 2
	FFireTruckJetSweep SpeedSweep;
	const FFireTruckJetLaunch Rolled =
		AdvanceFireTruckJet(SpeedSweep, FireTruckBuildingElevationMax1616, UnitAim, Distance1616, 37);
	TestEqual(TEXT("speed rolls rand()%100 on top of half the range"),
		Rolled.Speed1616,
		37 * FixedOne + Distance1616 / 2);

	// The tile-object variant tops out lower.
	FFireTruckJetSweep ObjectSweep;
	for (int32 Shot = 0; Shot < 60; ++Shot)
	{
		AdvanceFireTruckJet(ObjectSweep, FireTruckObjectElevationMax1616, UnitAim, Distance1616, 0);
		TestTrue(
			TEXT("object sweep never exceeds 3.0"),
			ObjectSweep.Elevation1616 <= FireTruckObjectElevationMax1616);
	}
	return true;
}

// Regression: FUN_0048ed00's drag is a per-frame multiplier, so a droplet stepped once per
// rendered frame loses reach as the frame rate climbs - the same shot that carries 2.2 tiles
// on the original's clock carries 0.9 at 120fps, and a fire truck's water lands in the road
// short of the building. The updater accumulates real time and spends it in whole
// SimulationStep1616 steps; this checks that the path no longer depends on who is calling.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterWaterFixedStepTest,
	"SimCopter.Water.FixedStepReach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterWaterFixedStepTest::RunTest(const FString& Parameters)
{
	// Mirrors USimCopterParticleFXComponent::AdvanceWaterTrajectory's accumulator.
	auto IntegrateAtFrameRate = [](const int32 FramesPerSecond, const int32 Seconds)
	{
		FWaterParticleMotion Motion;
		Motion.Direction1616 = DirectionToFixed(FVector(0.707f, 0.0f, 0.707f));
		Motion.Speed1616 = 150 * FixedOne;
		Motion.Life1616 = InitialParticleLife1616;

		FIntVector Travel = FIntVector::ZeroValue;
		int32 Carry = 0;
		const int32 FrameDelta = FixedOne / FramesPerSecond;
		for (int32 Frame = 0; Frame < FramesPerSecond * Seconds; ++Frame)
		{
			Carry += FrameDelta;
			while (Carry >= SimulationStep1616)
			{
				Carry -= SimulationStep1616;
				const FWaterParticleFrame Step =
					AdvanceParticleFrame(Motion, EWaterEmitter::Bucket, SimulationStep1616);
				if (!Step.bAlive)
				{
					return Travel;
				}
				Travel += Step.Travel1616;
			}
		}
		return Travel;
	};

	const FIntVector At30 = IntegrateAtFrameRate(30, 3);
	const FIntVector At60 = IntegrateAtFrameRate(60, 3);
	const FIntVector At120 = IntegrateAtFrameRate(120, 3);
	TestTrue(TEXT("30fps and 60fps integrate the same path"), At30 == At60);
	TestTrue(TEXT("60fps and 120fps integrate the same path"), At60 == At120);
	TestTrue(TEXT("the shot actually carries downrange"), At60.X > 100 * FixedOne);

	// And the defect it replaces: stepping on the rendered frame shortens the throw.
	auto IntegratePerRenderedFrame = [](const int32 FramesPerSecond, const int32 Seconds)
	{
		FWaterParticleMotion Motion;
		Motion.Direction1616 = DirectionToFixed(FVector(0.707f, 0.0f, 0.707f));
		Motion.Speed1616 = 150 * FixedOne;
		Motion.Life1616 = InitialParticleLife1616;

		FIntVector Travel = FIntVector::ZeroValue;
		const int32 FrameDelta = FixedOne / FramesPerSecond;
		for (int32 Frame = 0; Frame < FramesPerSecond * Seconds; ++Frame)
		{
			const FWaterParticleFrame Step =
				AdvanceParticleFrame(Motion, EWaterEmitter::Bucket, FrameDelta);
			if (!Step.bAlive)
			{
				break;
			}
			Travel += Step.Travel1616;
		}
		return Travel;
	};
	TestTrue(
		TEXT("per-rendered-frame stepping is what made reach frame-rate dependent"),
		IntegratePerRenderedFrame(120, 3).X < IntegratePerRenderedFrame(30, 3).X);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterWaterTerrainAndRopeTest,
	"SimCopter.Water.TerrainAndRopeConstants",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterWaterTerrainAndRopeTest::RunTest(const FString& Parameters)
{
	TestTrue(
		TEXT("first terrain triangle matches V0/V1/V2 plane"),
		FMath::IsNearlyEqual(
			SampleTerrainTriangleHeight(0.75f, 0.25f, 0.0f, 10.0f, 30.0f, 20.0f),
			12.5f));
	TestTrue(
		TEXT("second terrain triangle matches V0/V2/V3 plane"),
		FMath::IsNearlyEqual(
			SampleTerrainTriangleHeight(0.25f, 0.75f, 0.0f, 10.0f, 30.0f, 20.0f),
			17.5f));
	TestEqual(TEXT("rope has 20 nodes"), RopeNodeCount, 20);
	TestEqual(TEXT("rope stows at first-active node 17"), RopeStowedFirstActiveNode, 17);
	TestEqual(TEXT("rope stops deploying at first-active node 3"), RopeMinimumFirstActiveNode, 3);
	TestEqual(TEXT("rope segment is four original units"), RopeSegmentLength1616, 0x40000);
	return true;
}
