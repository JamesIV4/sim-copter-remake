// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/SimCopterAirport.h"

#include "Formats/SimCity2000Reader.h"
#include "Formats/SimCopterPeopleCityRules.h"

namespace SimCopterAirport
{
namespace
{
// FUN_004829f0's tail, verbatim: the twelve (dx, dy) pairs it writes into DAT_005d9170,
// lowest address first. They cover the 4x4 block's perimeter, but not in ring order.
constexpr int32 PadOffsets[PadCount][2] =
{
	{ 2, 3 }, // DAT_005d9170
	{ 1, 3 }, // DAT_005d9178
	{ 3, 3 }, // DAT_005d9180
	{ 0, 3 }, // DAT_005d9188
	{ 3, 2 }, // DAT_005d9190
	{ 3, 1 }, // DAT_005d9198
	{ 0, 1 }, // DAT_005d91a0
	{ 0, 2 }, // DAT_005d91a8
	{ 0, 0 }, // DAT_005d91b0
	{ 3, 0 }, // DAT_005d91b8
	{ 1, 0 }, // DAT_005d91c0
	{ 2, 0 }, // DAT_005d91c8
};
}

int32 GetSignedXbldFootprintSize(const int32 XbldId)
{
	const int32 Signed = static_cast<int32>(static_cast<int8>(XbldId));
	if (Signed < 0)
	{
		// Sign-extended: below every range FUN_004e4f80 tests, so it falls out of the
		// `< 0x70` branch with 1 and never reaches the footprint table.
		return 1;
	}
	return FSimCopterPeopleCityRules::GetFootprintSizeForBuildingId(static_cast<uint8>(Signed));
}

bool IsAirportBlockAt(
	const FTileReader& GetZone,
	const FTileReader& GetXbld,
	const int32 OriginX,
	const int32 OriginY)
{
	// FUN_004829f0 runs the whole validation under `param_1 < 0x80 && param_2 < 0x80`. The
	// fallback block sits at (128, 128) and is built without any of it.
	if (OriginX >= MapSize || OriginY >= MapSize)
	{
		return true;
	}
	if (OriginX < 0 || OriginY < 0)
	{
		return false;
	}

	for (int32 TileX = OriginX; TileX < OriginX + BlockSpan; ++TileX)
	{
		for (int32 TileY = OriginY; TileY < OriginY + BlockSpan; ++TileY)
		{
			if (TileX >= MapSize || TileY >= MapSize)
			{
				return false;
			}
			if ((GetZone(TileX, TileY) & 0x0f) != AirportZoneType)
			{
				return false;
			}

			// Far column and far row: a 2x2 building may not straddle the block's edge. The
			// second test lets a building that starts one tile in through, and rejects one
			// that starts two in - i.e. the block has to land on the building's own parity.
			if (TileX - OriginX == BlockSpan - 1)
			{
				const int32 Id = GetXbld(TileX, TileY);
				if (GetSignedXbldFootprintSize(Id) == 2)
				{
					if (GetXbld(TileX - 1, TileY) != Id)
					{
						return false;
					}
					if (GetXbld(TileX - 2, TileY) == Id && GetXbld(TileX - 3, TileY) != Id)
					{
						return false;
					}
				}
			}
			if (TileY - OriginY == BlockSpan - 1)
			{
				const int32 Id = GetXbld(TileX, TileY);
				if (GetSignedXbldFootprintSize(Id) == 2)
				{
					if (GetXbld(TileX, TileY - 1) != Id)
					{
						return false;
					}
					if (GetXbld(TileX, TileY - 2) == Id && GetXbld(TileX, TileY - 3) != Id)
					{
						return false;
					}
				}
			}
		}
	}

	return true;
}

FIntPoint FindAirportOrigin(const FTileReader& GetZone, const FTileReader& GetXbld)
{
	// FUN_0047c0c0's sweep also skips a tile whose cell already exists, which only happens for
	// tiles a previously built airport claimed - and it builds at most one - so scanning every
	// tile gives the same answer.
	for (int32 TileX = 0; TileX < MapSize; ++TileX)
	{
		for (int32 TileY = 0; TileY < MapSize; ++TileY)
		{
			if ((GetZone(TileX, TileY) & 0x0f) != AirportZoneType)
			{
				continue;
			}
			if (IsAirportBlockAt(GetZone, GetXbld, TileX, TileY))
			{
				return FIntPoint(TileX, TileY);
			}
		}
	}

	return FIntPoint(FallbackOriginTile, FallbackOriginTile);
}

bool IsFallbackAirportOrigin(const FIntPoint& Origin)
{
	return Origin.X >= MapSize || Origin.Y >= MapSize;
}

FIntPoint GetPadTile(const FIntPoint& Origin, const int32 PadIndex)
{
	if (PadIndex < 0 || PadIndex >= PadCount)
	{
		return FIntPoint(INDEX_NONE, INDEX_NONE);
	}
	return FIntPoint(Origin.X + PadOffsets[PadIndex][0], Origin.Y + PadOffsets[PadIndex][1]);
}

FIntPoint GetTerminalTile(const FIntPoint& Origin)
{
	return FIntPoint(Origin.X + 1, Origin.Y + 1);
}

int32 FindFreePadIndex(
	TFunctionRef<bool(int32 PadIndex)> HasCellOccupants,
	TFunctionRef<bool(int32 PadIndex)> HasBlockingOccupant)
{
	int32 Chosen = INDEX_NONE;
	for (int32 PadIndex = 0; PadIndex < PadCount; ++PadIndex)
	{
		if (!HasCellOccupants(PadIndex))
		{
			// FUN_0048b000 returns straight out of the loop on an empty cell list.
			return PadIndex;
		}
		Chosen = HasBlockingOccupant(PadIndex) ? Chosen : PadIndex;
	}
	return Chosen;
}

int32 GetStampedXbldId(const FIntPoint& Origin, const int32 TileX, const int32 TileY)
{
	const int32 DeltaX = TileX - Origin.X;
	const int32 DeltaY = TileY - Origin.Y;
	if (DeltaX < 0 || DeltaX >= BlockSpan || DeltaY < 0 || DeltaY >= BlockSpan)
	{
		return INDEX_NONE;
	}

	// FUN_004829f0 stamps the middle 2x2 first, then walks the perimeter: the near and far rows
	// whole, then the two tiles left over on each of the near and far columns.
	const bool bTerminal = DeltaX >= 1 && DeltaX <= 2 && DeltaY >= 1 && DeltaY <= 2;
	return bTerminal ? TerminalXbldId : PadXbldId;
}

int32 GetStampedZoneHighNibble(const FIntPoint& Origin, const int32 TileX, const int32 TileY)
{
	const int32 DeltaX = TileX - Origin.X;
	const int32 DeltaY = TileY - Origin.Y;
	if (DeltaX < 0 || DeltaX >= BlockSpan || DeltaY < 0 || DeltaY >= BlockSpan)
	{
		return INDEX_NONE;
	}

	const bool bTerminal = DeltaX >= 1 && DeltaX <= 2 && DeltaY >= 1 && DeltaY <= 2;
	if (!bTerminal)
	{
		// Every pad is its own 1x1, which SimCity 2000 marks with all four corners at once.
		return 0xf0;
	}

	// The terminal is one 2x2 anchored at origin + (1, 1), so each of its four tiles carries
	// exactly the corner it occupies.
	const bool bLeft = DeltaX == 1;
	const bool bTop = DeltaY == 1;
	if (bTop)
	{
		return bLeft ? 0x80 : 0x40;
	}
	return bLeft ? 0x10 : 0x20;
}

void StampBlockTiles(FSimCity2000City& City, const FIntPoint& Origin)
{
	// The whole of FUN_004829f0's XBLD writing sits under `param_1 < 0x80 && param_2 < 0x80`.
	if (IsFallbackAirportOrigin(Origin) || Origin.X < 0 || Origin.Y < 0 ||
		City.Tiles.Num() != FSimCity2000City::TileCount)
	{
		return;
	}

	for (int32 DeltaY = 0; DeltaY < BlockSpan; ++DeltaY)
	{
		for (int32 DeltaX = 0; DeltaX < BlockSpan; ++DeltaX)
		{
			const int32 TileX = Origin.X + DeltaX;
			const int32 TileY = Origin.Y + DeltaY;
			if (TileX >= MapSize || TileY >= MapSize)
			{
				continue;
			}
			FSimCity2000Tile& Tile = City.Tiles[TileY * MapSize + TileX];
			Tile.Building = static_cast<uint8>(GetStampedXbldId(Origin, TileX, TileY));
			// The low nibble is the zone type, which stays 8 - it is how the block was found.
			Tile.Zone = static_cast<uint8>(
				(Tile.Zone & 0x0f) | GetStampedZoneHighNibble(Origin, TileX, TileY));
		}
	}
}

void FlattenBlockCorners(TArray<int16>& Corners, const int32 GridSize, const FIntPoint& Origin)
{
	if (IsFallbackAirportOrigin(Origin) || Origin.X < 0 || Origin.Y < 0 ||
		Corners.Num() != GridSize * GridSize)
	{
		return;
	}

	// The sample is read before the patch is written, and it is the terminal tile's own top-left
	// corner - the one FUN_004abce0 last set from the terminal's ALTM when a building stands
	// there, which is every shipped airport bar a handful whose middle tile is empty ground.
	const int32 SampleX = FMath::Clamp(Origin.X + 1, 0, GridSize - 1);
	const int32 SampleY = FMath::Clamp(Origin.Y + 1, 0, GridSize - 1);
	const int16 Sample = Corners[SampleY * GridSize + SampleX];

	// Inclusive on both ends: the 4x4 of tiles has 5x5 corners, and the original's loops run
	// `<= param + 4`.
	for (int32 GridY = Origin.Y; GridY <= Origin.Y + BlockSpan; ++GridY)
	{
		for (int32 GridX = Origin.X; GridX <= Origin.X + BlockSpan; ++GridX)
		{
			if (GridX < 0 || GridX >= GridSize || GridY < 0 || GridY >= GridSize)
			{
				continue;
			}
			Corners[GridY * GridSize + GridX] = Sample;
		}
	}
}

FIntPoint BuildAirportIntoCity(FSimCity2000City& City, TArray<int16>* ConditionedCorners)
{
	auto ReadZone = [&City](const int32 TileX, const int32 TileY)
	{
		const bool bInMap = TileX >= 0 && TileX < MapSize && TileY >= 0 && TileY < MapSize;
		return bInMap ? int32(City.Tiles[TileY * MapSize + TileX].Zone) : 0;
	};
	auto ReadXbld = [&City](const int32 TileX, const int32 TileY)
	{
		const bool bInMap = TileX >= 0 && TileX < MapSize && TileY >= 0 && TileY < MapSize;
		return bInMap ? int32(City.Tiles[TileY * MapSize + TileX].Building) : 0;
	};

	if (City.Tiles.Num() != FSimCity2000City::TileCount)
	{
		return FIntPoint(FallbackOriginTile, FallbackOriginTile);
	}

	const FIntPoint Origin = FindAirportOrigin(ReadZone, ReadXbld);

	// FUN_0047bb20 runs FUN_004abce0 first and FUN_0047c0c0 - and so FUN_004829f0 - second, so
	// the flatten reads corners conditioned from the city's *original* XBLD. Both callers hand
	// this a city freshly parsed from disk, so the pair always runs exactly once per load.
	if (ConditionedCorners != nullptr)
	{
		constexpr int32 GridSize = FSimCity2000City::MapSize + 1;
		FlattenBlockCorners(*ConditionedCorners, GridSize, Origin);
	}

	StampBlockTiles(City, Origin);
	return Origin;
}
}
