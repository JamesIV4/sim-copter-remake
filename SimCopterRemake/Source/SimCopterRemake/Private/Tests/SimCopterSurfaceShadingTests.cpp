// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "City/SimCopterDayNight.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterSurfaceShadingDefaultsTest,
	"SimCopter.City.SurfaceShadingDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterSurfaceShadingDefaultsTest::RunTest(const FString& Parameters)
{
	using namespace SimCopterDayNight;

	// These nine are look knobs that get re-tuned by hand, and every one of them is a 0..1 shader
	// input that fails SILENTLY out of range: a specular of 9 instead of 0.9 does not error, it just
	// clamps, and a negative albedo ceiling turns a surface black. So the range is asserted.
	const TCHAR* const Names[] = {
		TEXT("TreeMaxBrightness"), TEXT("TreeRoughness"), TEXT("TreeSpecular"),
		TEXT("TerrainMaxBrightness"), TEXT("TerrainRoughness"), TEXT("TerrainSpecular"),
		TEXT("CityMaxBrightness"), TEXT("CityRoughness"), TEXT("CitySpecular"),
		TEXT("CityWindowRoughness"), TEXT("CityWindowSpecular"),
		TEXT("WaterMaxBrightness"), TEXT("WaterRoughness"), TEXT("WaterSpecular"),
		TEXT("WaterShoreRoughness"), TEXT("WaterShoreSpecular"), TEXT("WaterShoreFadeWidth"),
	};
	const float Values[] = {
		DefaultTreeMaxBrightness, DefaultTreeRoughness, DefaultTreeSpecular,
		DefaultTerrainMaxBrightness, DefaultTerrainRoughness, DefaultTerrainSpecular,
		DefaultCityMaxBrightness, DefaultCityRoughness, DefaultCitySpecular,
		DefaultCityWindowRoughness, DefaultCityWindowSpecular,
		DefaultWaterMaxBrightness, DefaultWaterRoughness, DefaultWaterSpecular,
		DefaultWaterShoreRoughness, DefaultWaterShoreSpecular, DefaultWaterShoreFadeWidth,
	};
	static_assert(UE_ARRAY_COUNT(Names) == UE_ARRAY_COUNT(Values), "name/value tables disagree");

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Values); ++Index)
	{
		TestTrue(
			FString::Printf(TEXT("%s is a usable 0..1 shader input"), Names[Index]),
			Values[Index] >= 0.0f && Values[Index] <= 1.0f);
	}

	// An albedo ceiling of zero is a black surface, which is the one in-range value that is never
	// what anybody meant.
	TestTrue(TEXT("Trees keep some albedo"), DefaultTreeMaxBrightness > 0.0f);
	TestTrue(TEXT("The ground keeps some albedo"), DefaultTerrainMaxBrightness > 0.0f);
	TestTrue(TEXT("The city keeps some albedo"), DefaultCityMaxBrightness > 0.0f);
	TestTrue(TEXT("Water keeps some albedo"), DefaultWaterMaxBrightness > 0.0f);

	// The relationships the comments on these constants claim. Getting one of them backwards is the
	// realistic mistake - it is what the whole change is correcting - and it reads as plausible
	// numbers, so nothing else would catch it.
	TestTrue(
		TEXT("Water is the glossiest surface in the city"),
		DefaultWaterRoughness < DefaultTerrainRoughness && DefaultWaterRoughness < DefaultTreeRoughness);
	TestTrue(
		TEXT("...and the most reflective"),
		DefaultWaterSpecular > DefaultTerrainSpecular && DefaultWaterSpecular > DefaultTreeSpecular);
	TestTrue(
		TEXT("Foliage is the least reflective - a leaf is not wet plastic"),
		DefaultTreeSpecular <= DefaultTerrainSpecular);
	// The city was split out of the terrain's numbers to be tunable, not to look different. If it
	// ever drifts far from the ground it will stand out again, which is the bug this family exists
	// to fix - so the two are held loosely together.
	TestTrue(
		TEXT("Buildings and roads stay in the ground's neighbourhood"),
		FMath::Abs(DefaultCitySpecular - DefaultTerrainSpecular) <= 0.25f &&
		FMath::Abs(DefaultCityRoughness - DefaultTerrainRoughness) <= 0.25f);

	// Windows are the exception carved out of that, and the whole point is that they are NOT the
	// wall: glassier and more reflective, or the painted mask is doing nothing.
	TestTrue(
		TEXT("Windows are glossier than the wall they are set into"),
		DefaultCityWindowRoughness < DefaultCityRoughness);
	TestTrue(
		TEXT("...and more reflective"),
		DefaultCityWindowSpecular > DefaultCitySpecular);
	TestTrue(
		TEXT("Trees are darker than the ground they stand on"),
		DefaultTreeMaxBrightness < DefaultTerrainMaxBrightness);

	// The shoreline fade only removes the hard edge if it fades TOWARDS matte. Backwards, it would
	// make the coast the shiniest part of the lake - and the numbers would still look plausible.
	TestTrue(
		TEXT("The shoreline is rougher than open water"),
		DefaultWaterShoreRoughness > DefaultWaterRoughness);
	TestTrue(
		TEXT("...and less reflective"),
		DefaultWaterShoreSpecular < DefaultWaterSpecular);
	TestTrue(
		TEXT("The fade actually spans some of the weight ramp"),
		DefaultWaterShoreFadeWidth > 0.0f);

	// This used to assert the warp's wavelength exceeded the 400 cm tile, on the argument that the
	// contour has to WANDER further than the stair-step it hides. On screen the opposite won: both
	// layers ended up far below a tile, which dissolves the boundary instead of moving it. The
	// assertion was encoding a hypothesis, so it is gone rather than loosened - what is left is what
	// is actually true of any usable setting.
	TestTrue(
		TEXT("Both shoreline warp layers are real wavelengths"),
		DefaultWaterShoreEdgeNoiseScale > 0.0f && DefaultWaterShoreEdgeNoiseScale2 > 0.0f);
	TestTrue(
		TEXT("...and both are on by default"),
		DefaultWaterShoreEdgeNoiseStrength > 0.0f && DefaultWaterShoreEdgeNoiseStrength2 > 0.0f);
	// The warp displaces a 0..1 weight, so a strength at or over 1 pushes the whole shoreline past
	// the fade in both directions and the band stops existing.
	TestTrue(
		TEXT("Neither layer can swamp the weight it displaces"),
		DefaultWaterShoreEdgeNoiseStrength + DefaultWaterShoreEdgeNoiseStrength2 < 1.0f);

	// This one is a wavelength in centimetres, not a 0..1 input, so it is excluded from the range
	// sweep above and checked on its own terms: it has to be shorter than the 400 cm tile or it
	// lines up with the quad seams it is there to break up.
	TestTrue(
		TEXT("The detail ripple is finer than a tile"),
		DefaultWaterDetailNormalScale > 1.0f && DefaultWaterDetailNormalScale < 400.0f);
	TestTrue(
		TEXT("...and is on by default"),
		DefaultWaterDetailNormalStrength > 0.0f);

	return true;
}
