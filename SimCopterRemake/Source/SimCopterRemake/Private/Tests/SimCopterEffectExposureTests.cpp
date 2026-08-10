// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/SimCopterEffectExposure.h"
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterEffectExposureTest,
	"SimCopter.City.EffectExposure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterEffectExposureTest::RunTest(const FString& Parameters)
{
	using namespace SimCopterEffectExposure;

	// A light component's forward axis points the way the light travels, so the sun overhead points
	// straight DOWN. Getting this backwards is silent - it just makes the effects brightest at
	// midnight - so it is pinned here.
	{
		const float Noon = ComputeGroundIlluminanceLux(120000.0f, FVector(0.0f, 0.0f, -1.0f));
		TestEqual(TEXT("Sun overhead delivers its full intensity"), Noon, 120000.0f);

		const float Underneath = ComputeGroundIlluminanceLux(120000.0f, FVector(0.0f, 0.0f, 1.0f));
		TestEqual(TEXT("A light pointing up lights nothing"), Underneath, 0.0f);

		const float Horizon = ComputeGroundIlluminanceLux(120000.0f, FVector(1.0f, 0.0f, 0.0f));
		TestEqual(TEXT("A light along the horizon lights nothing"), Horizon, 0.0f);

		// 30 degrees above the horizon: cos of the angle from vertical is sin(30) = 0.5.
		const FVector Low = FVector(FMath::Cos(FMath::DegreesToRadians(30.0f)), 0.0f,
			-FMath::Sin(FMath::DegreesToRadians(30.0f)));
		TestNearlyEqual(TEXT("Low sun delivers half"), ComputeGroundIlluminanceLux(120000.0f, Low), 60000.0f, 1.0f);

		TestEqual(TEXT("A dark light delivers nothing"), ComputeGroundIlluminanceLux(0.0f, FVector(0.0f, 0.0f, -1.0f)), 0.0f);
		TestEqual(TEXT("A degenerate direction delivers nothing"), ComputeGroundIlluminanceLux(120000.0f, FVector::ZeroVector), 0.0f);
	}

	// The number that matters: at the celestial vault's 120,000 lux noon an effect card has to emit
	// tens of thousands of nits to read at all. This is the assertion that would have caught the
	// original bug, where the cards emitted 1.4 and tonemapped to black.
	{
		const float Noon = ComputeEffectEmissiveNits(120000.0f);
		TestTrue(TEXT("A noon effect card is tens of thousands of nits"), Noon > 20000.0f);
		TestNearlyEqual(TEXT("Noon card is Brightness x white ground"), Noon,
			120000.0f / UE_PI * DefaultEffectBrightness, 1.0f);
		// The material's own authored 1.4 boost rides on top of this, so the default here is 1 and
		// not 1.4 - the two multiplying would double every effect's brightness.
		TestEqual(TEXT("Default brightness leaves the material's boost alone"), DefaultEffectBrightness, 1.0f);

		// The old day/night actor ran the sun at 4 lux, which is the scale everything unlit in the
		// remake was authored against. That the two answers differ by ~30,000x IS the bug.
		const float OldScale = ComputeEffectEmissiveNits(4.0f);
		TestTrue(TEXT("The old 4 lux sun asks for a tiny fraction of the nits"), OldScale * 10000.0f < Noon);
	}

	// Night has to floor, or the effects go dark exactly when they are the only thing to look at.
	{
		const float Midnight = ComputeEffectEmissiveNits(0.1f);
		TestEqual(TEXT("Moonlight falls back to the floor"), Midnight, DefaultMinimumEmissiveNits);
		TestEqual(TEXT("No light at all still floors"), ComputeEffectEmissiveNits(0.0f), DefaultMinimumEmissiveNits);
		TestEqual(TEXT("Negative illuminance floors"), ComputeEffectEmissiveNits(-5.0f), DefaultMinimumEmissiveNits);
	}

	// Monotonic across the day, so dusk cannot be brighter than noon.
	{
		float Previous = 0.0f;
		for (const float Lux : {0.0f, 100.0f, 1000.0f, 20000.0f, 60000.0f, 120000.0f})
		{
			const float Nits = ComputeEffectEmissiveNits(Lux);
			TestTrue(TEXT("Effect brightness never falls as the sun rises"), Nits >= Previous);
			Previous = Nits;
		}
	}

	return true;
}
