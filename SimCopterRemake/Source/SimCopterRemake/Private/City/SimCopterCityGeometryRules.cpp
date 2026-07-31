// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/SimCopterCityGeometryRules.h"

#include "Formats/SimCopterPeopleCityRules.h"
#include "Formats/SimCity2000Reader.h"

FIntPoint FSimCopterCityGeometryRules::ClaimOriginalBuildingFootprint(
	const FSimCity2000City& City,
	int32 FileX,
	int32 FileY,
	TArray<uint8>& SceneCellState)
{
	constexpr uint8 SceneCellFree = 0;
	constexpr uint8 SceneCellClaimed = 1;
	// FUN_0047c0c0 clears an invalid multi-cell origin in the working XBLD grid. Keep that
	// distinction from an ordinary free cell so a later candidate cannot validate through it.
	constexpr uint8 SceneCellRejectedXbld = 2;
	const int32 MapSize = FSimCity2000City::MapSize;
	if (City.Tiles.Num() != FSimCity2000City::TileCount ||
		FileX < 0 || FileX >= MapSize || FileY < 0 || FileY >= MapSize)
	{
		return FIntPoint::ZeroValue;
	}

	if (SceneCellState.Num() != FSimCity2000City::TileCount)
	{
		SceneCellState.Init(SceneCellFree, FSimCity2000City::TileCount);
	}

	const int32 TileIndex = FileY * MapSize + FileX;
	if (SceneCellState[TileIndex] != SceneCellFree)
	{
		return FIntPoint::ZeroValue;
	}

	const uint8 BuildingId = City.Tiles[TileIndex].Building;
	if (BuildingId < 0x70)
	{
		return FIntPoint::ZeroValue;
	}

	const int32 FootprintSize = FSimCopterPeopleCityRules::GetFootprintSizeForBuildingId(BuildingId);
	if (FileX + FootprintSize > MapSize || FileY + FootprintSize > MapSize)
	{
		SceneCellState[TileIndex] = SceneCellRejectedXbld;
		return FIntPoint::ZeroValue;
	}

	for (int32 OffsetY = 0; OffsetY < FootprintSize; ++OffsetY)
	{
		for (int32 OffsetX = 0; OffsetX < FootprintSize; ++OffsetX)
		{
			const int32 CandidateIndex = (FileY + OffsetY) * MapSize + FileX + OffsetX;
			if (SceneCellState[CandidateIndex] == SceneCellRejectedXbld ||
				City.Tiles[CandidateIndex].Building != BuildingId)
			{
				SceneCellState[TileIndex] = SceneCellRejectedXbld;
				return FIntPoint::ZeroValue;
			}
		}
	}

	for (int32 OffsetY = 0; OffsetY < FootprintSize; ++OffsetY)
	{
		for (int32 OffsetX = 0; OffsetX < FootprintSize; ++OffsetX)
		{
			SceneCellState[(FileY + OffsetY) * MapSize + FileX + OffsetX] = SceneCellClaimed;
		}
	}

	return FIntPoint(FootprintSize, FootprintSize);
}
