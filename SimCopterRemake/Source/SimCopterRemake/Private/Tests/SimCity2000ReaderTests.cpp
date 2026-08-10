// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Formats/SimCity2000Reader.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCity2000RleTest,
	"SimCopter.Formats.SimCity2000.Rle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCity2000RleTest::RunTest(const FString& Parameters)
{
	const uint8 Bytes[] = { 0x03, 0x01, 0x02, 0x03, 0x82, 0xAA };
	TArray<uint8> Compressed;
	Compressed.Append(Bytes, UE_ARRAY_COUNT(Bytes));

	TArray<uint8> Decoded;
	FString Error;
	if (!TestTrue(TEXT("RLE decode succeeds"), FSimCity2000Reader::DecodeRleChunk(Compressed, 6, Decoded, Error)))
	{
		AddError(Error);
		return false;
	}

	TestEqual(TEXT("Decoded byte count"), Decoded.Num(), 6);
	TestEqual(TEXT("Literal byte 0"), Decoded[0], static_cast<uint8>(0x01));
	TestEqual(TEXT("Literal byte 2"), Decoded[2], static_cast<uint8>(0x03));
	TestEqual(TEXT("Repeated byte 3"), Decoded[3], static_cast<uint8>(0xAA));
	TestEqual(TEXT("Repeated byte 5"), Decoded[5], static_cast<uint8>(0xAA));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCity2000ReferenceCityTest,
	"SimCopter.Formats.SimCity2000.ReferenceCity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCity2000ReferenceCityTest::RunTest(const FString& Parameters)
{
	FString DemoCityPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reference/SimCopterOriginalGame/cities/career/city0.sc2")));
	if (!FPaths::FileExists(DemoCityPath))
	{
		DemoCityPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reference/SimCopterOriginalGame/cities/Demo.sc2")));
	}
	if (!FPaths::FileExists(DemoCityPath))
	{
		AddWarning(FString::Printf(TEXT("Skipping optional reference city test because '%s' is not present."), *DemoCityPath));
		return true;
	}

	FSimCity2000City City;
	FString Error;
	if (!TestTrue(FString::Printf(TEXT("Loads '%s'"), *DemoCityPath), FSimCity2000Reader::LoadCityFromFile(DemoCityPath, City, Error)))
	{
		AddError(Error);
		return false;
	}

	TestEqual(TEXT("Tile count"), City.Tiles.Num(), FSimCity2000City::TileCount);
	TestEqual(TEXT("Chunk count"), City.Chunks.Num(), 21);
	TestTrue(TEXT("City name is populated"), !City.CityName.IsEmpty());
	TestTrue(TEXT("ALTM chunk exists"), City.HasChunk(TEXT("ALTM")));
	TestTrue(TEXT("XBLD chunk exists"), City.HasChunk(TEXT("XBLD")));

	const FSimCity2000Tile* FlatLandTile = City.Tiles.FindByPredicate([](const FSimCity2000Tile& Tile)
	{
		return Tile.RawAltitude == 0x0004 && Tile.Terrain < 0x10;
	});
	TestNotNull(TEXT("Finds flat land ALTM sample"), FlatLandTile);
	if (FlatLandTile != nullptr)
	{
		TestEqual(TEXT("Flat land base altitude"), FlatLandTile->Altitude, static_cast<uint8>(4));
		TestEqual(TEXT("Flat land secondary altitude"), FlatLandTile->SecondaryAltitude, static_cast<uint8>(0));
		TestFalse(TEXT("Flat land is not water"), FlatLandTile->bWater);
	}

	const FSimCity2000Tile* WaterTile = City.Tiles.FindByPredicate([](const FSimCity2000Tile& Tile)
	{
		return Tile.RawAltitude == 0x0082 && Tile.Terrain > 0x0F;
	});
	TestNotNull(TEXT("Finds water ALTM sample"), WaterTile);
	if (WaterTile != nullptr)
	{
		TestEqual(TEXT("Water base altitude"), WaterTile->Altitude, static_cast<uint8>(2));
		TestEqual(TEXT("Water secondary altitude"), WaterTile->SecondaryAltitude, static_cast<uint8>(4));
		TestTrue(TEXT("Water flag comes from terrain code"), WaterTile->bWater);
	}

	const FSimCity2000Tile* SlopedTile = City.Tiles.FindByPredicate([](const FSimCity2000Tile& Tile)
	{
		return Tile.RawAltitude == 0x1408;
	});
	TestNotNull(TEXT("Finds sloped ALTM sample"), SlopedTile);
	if (SlopedTile != nullptr)
	{
		TestEqual(TEXT("Slope bits"), SlopedTile->Slope, static_cast<uint8>(5));
	}

	return true;
}

#endif
