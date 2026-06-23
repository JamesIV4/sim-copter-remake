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
	const FString DemoCityPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reference/SimCopterOriginalGame/cities/Demo.sc2")));
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

	return true;
}

#endif
