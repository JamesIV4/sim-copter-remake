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

	const FSimCopterPeopleSpawnPlacement Class4Placement = FSimCopterPeopleCityRules::GetSpawnPlacementForTileClass(4);
	TestEqual(TEXT("class 4 placement mode"), Class4Placement.PlacementMode, 1);
	TestEqual(TEXT("class 4 surface mode"), Class4Placement.SurfaceMode, 2);

	const FSimCopterPeopleSpawnPlacement RoadPlacement = FSimCopterPeopleCityRules::GetSpawnPlacementForTileClass(7);
	TestEqual(TEXT("road placement mode"), RoadPlacement.PlacementMode, 1);
	TestEqual(TEXT("road surface mode"), RoadPlacement.SurfaceMode, 4);

	TestTrue(TEXT("ambient state accepts road class"), FSimCopterPeopleCityRules::IsAmbientPedestrianTileClass(7));
	TestTrue(TEXT("ambient state accepts class 13"), FSimCopterPeopleCityRules::IsAmbientPedestrianTileClass(13));
	TestFalse(TEXT("ambient state does not accept rail class"), FSimCopterPeopleCityRules::IsAmbientPedestrianTileClass(9));

	const TArray<int32>& ServiceClasses = FSimCopterPeopleCityRules::GetAmbientStateTileClasses(10);
	TestTrue(TEXT("ambient state 10 accepts class 5"), ServiceClasses.Contains(5));
	TestTrue(TEXT("ambient state 10 accepts class 4"), ServiceClasses.Contains(4));
	TestTrue(TEXT("ambient state 10 accepts class 3"), ServiceClasses.Contains(3));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
