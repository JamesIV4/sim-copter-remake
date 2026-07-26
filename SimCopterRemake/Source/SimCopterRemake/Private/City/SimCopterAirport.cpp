// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/SimCopterAirport.h"

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
}
