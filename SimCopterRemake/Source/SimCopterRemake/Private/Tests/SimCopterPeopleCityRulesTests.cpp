// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Formats/SimCopterPeopleCityRules.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterPeopleCityRulesClassMapTest,
	"SimCopter.City.PeopleRules.ClassMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterPeopleCityRulesClassMapTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("empty land maps to class 2"), FSimCopterPeopleCityRules::GetTileClassForBuildingId(0x00), 2);
	TestEqual(TEXT("low water/shore objects map to class 4"), FSimCopterPeopleCityRules::GetTileClassForBuildingId(0x04), 4);
	TestEqual(TEXT("surface roads map to class 7"), FSimCopterPeopleCityRules::GetTileClassForBuildingId(0x1D), 7);
	TestEqual(TEXT("road bridge/crossing ids map to class 7"), FSimCopterPeopleCityRules::GetTileClassForBuildingId(0x49), 7);
	TestEqual(TEXT("rail ids map to class 9"), FSimCopterPeopleCityRules::GetTileClassForBuildingId(0x2C), 9);
	TestEqual(TEXT("large service/special ids map to class 13"), FSimCopterPeopleCityRules::GetTileClassForBuildingId(0x82), 13);
	TestEqual(TEXT("class 11 fallback group"), FSimCopterPeopleCityRules::GetTileClassForBuildingId(0x7C), 11);
	TestEqual(TEXT("footprint size below building range"), FSimCopterPeopleCityRules::GetFootprintSizeForBuildingId(0x1D), 1);
	TestEqual(TEXT("bridge footprint size"), FSimCopterPeopleCityRules::GetFootprintSizeForBuildingId(0x49), 2);
	TestEqual(TEXT("building footprint size table 0x8c"), FSimCopterPeopleCityRules::GetFootprintSizeForBuildingId(0x8C), 2);
	TestEqual(TEXT("building footprint size table 0xc9"), FSimCopterPeopleCityRules::GetFootprintSizeForBuildingId(0xC9), 4);

	const FSimCopterPeopleSpawnPlacement Class4Placement = FSimCopterPeopleCityRules::GetSpawnPlacementForTileClass(4);
	TestEqual(TEXT("class 4 placement mode"), Class4Placement.PlacementMode, 1);
	TestEqual(TEXT("class 4 surface mode"), Class4Placement.SurfaceMode, 2);

	const FSimCopterPeopleSpawnPlacement RoadPlacement = FSimCopterPeopleCityRules::GetSpawnPlacementForTileClass(7);
	TestEqual(TEXT("road placement mode"), RoadPlacement.PlacementMode, 1);
	TestEqual(TEXT("road surface mode"), RoadPlacement.SurfaceMode, 4);

	TestFalse(TEXT("ambient spawner does not start on road class"), FSimCopterPeopleCityRules::IsAmbientPedestrianTileClass(7));
	TestTrue(TEXT("ambient spawner accepts class 13"), FSimCopterPeopleCityRules::IsAmbientPedestrianTileClass(13));
	TestTrue(TEXT("ambient spawner accepts service class 5"), FSimCopterPeopleCityRules::IsAmbientPedestrianTileClass(5));
	TestTrue(TEXT("ambient spawner accepts service class 4"), FSimCopterPeopleCityRules::IsAmbientPedestrianTileClass(4));
	TestTrue(TEXT("ambient spawner accepts service class 3"), FSimCopterPeopleCityRules::IsAmbientPedestrianTileClass(3));
	TestFalse(TEXT("ambient spawner does not accept rail class"), FSimCopterPeopleCityRules::IsAmbientPedestrianTileClass(9));

	const TArray<int32>& ServiceClasses = FSimCopterPeopleCityRules::GetAmbientStateTileClasses(10);
	TestTrue(TEXT("ambient behavior class 10 accepts class 5"), ServiceClasses.Contains(5));
	TestTrue(TEXT("ambient behavior class 10 accepts class 4"), ServiceClasses.Contains(4));
	TestTrue(TEXT("ambient behavior class 10 accepts class 3"), ServiceClasses.Contains(3));

	uint16 RandomState = 0x1234;
	TestEqual(TEXT("FUN_004ce9d0 first raw value"), FSimCopterPeopleCityRules::NextPeopleRandomRaw(RandomState), uint16(0x2468));
	TestEqual(TEXT("FUN_004ce9d0 first stored state"), RandomState, uint16(0x365c));

	RandomState = 0x1234;
	const FSimCopterPeopleLocalOffset PerimeterOffset =
		FSimCopterPeopleCityRules::ChooseSpawnLocalOffset(1, 0, RandomState);
	TestEqual(TEXT("FUN_004c02a0 mode 0 x"), PerimeterOffset.OriginalX, -31);
	TestEqual(TEXT("FUN_004c02a0 mode 0 y"), PerimeterOffset.OriginalY, 11);

	RandomState = 0x1234;
	const FSimCopterPeopleLocalOffset InteriorOffset =
		FSimCopterPeopleCityRules::ChooseSpawnLocalOffset(1, 1, RandomState);
	TestEqual(TEXT("FUN_004c02a0 mode 1 x"), InteriorOffset.OriginalX, 11);
	TestEqual(TEXT("FUN_004c02a0 mode 1 y"), InteriorOffset.OriginalY, -25);

	RandomState = 0x1234;
	TestEqual(TEXT("FUN_004c2450 candidate class"), FSimCopterPeopleCityRules::ChooseAmbientBehaviorClassForTileClass(12, RandomState), 5);

	// FUN_004c3eb0 passes -1 for an unspecified mission-person class. FUN_004c71c0 resolves it
	// through FUN_004c7190; leaving the field at its C++ default made every fare class 0.
	RandomState = 0x1234;
	TestEqual(TEXT("FUN_004c7190 unspecified class"), FSimCopterPeopleCityRules::ChooseUnspecifiedBehaviorClass(RandomState), 2);
	RandomState = 1;
	TestEqual(TEXT("FUN_004c7190 varies mission people"), FSimCopterPeopleCityRules::ChooseUnspecifiedBehaviorClass(RandomState), 6);
	RandomState = 3;
	TestEqual(TEXT("FUN_004c7190 may choose class zero"), FSimCopterPeopleCityRules::ChooseUnspecifiedBehaviorClass(RandomState), 0);

	// FUN_004c71c0: behavior class -> figure (dog/cow/celebrity classes included).
	TestEqual(TEXT("class 0 figure"), FSimCopterPeopleCityRules::GetFigureNameForBehaviorClass(0), FString(TEXT("Blonde")));
	TestEqual(TEXT("class 10 figure is the dog"), FSimCopterPeopleCityRules::GetFigureNameForBehaviorClass(10), FString(TEXT("2DOGG")));
	TestEqual(TEXT("class 17 figure is the cow"), FSimCopterPeopleCityRules::GetFigureNameForBehaviorClass(17), FString(TEXT("Coww")));
	TestEqual(TEXT("class 20 figure is Elvis"), FSimCopterPeopleCityRules::GetFigureNameForBehaviorClass(20), FString(TEXT("Elvis")));
	TestEqual(TEXT("class 14 figure is the cop"), FSimCopterPeopleCityRules::GetFigureNameForBehaviorClass(14), FString(TEXT("Kopp")));
	TestEqual(TEXT("class 16 figure is Nessie"), FSimCopterPeopleCityRules::GetFigureNameForBehaviorClass(16), FString(TEXT("Nessie")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
