// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FSimCity2000City;

// The city's airport and its twelve helipads - where every session starts.
//
// Decoded from SimCopter.exe:
//
//   FUN_0047c0c0  city cell build. Sweeps the 128x128 map x-outer/y-inner and, the first time
//                 it meets a tile whose XZON zone is 8 (airport), hands that tile to
//                 FUN_004829f0. If the whole sweep finds none it builds one at (0x80, 0x80) -
//                 one block past the map's far corner, which is the "if the city has no
//                 airport, one will be built just outside the southeast corner" the shipped
//                 help file (help/English/37ref.htm) promises for a user city.
//   FUN_004829f0  airport builder. Validates a 4x4 block, flattens the height map over it,
//                 stamps XBLD 0xf6 on the middle 2x2 (the terminal) and 0xde on the twelve
//                 perimeter tiles (the helipads), and publishes those twelve tiles as the pad
//                 table at DAT_005d9170.
//   FUN_0048b000  free-pad search over that table.
//   FUN_0047a240  city entry: places each owned helicopter on a free pad (FUN_00484790), and
//                 points the camera at pad 0.
//   FUN_0048a8b0  crash respawn: the same free-pad search, inlined.
//
// This header carries the tile arithmetic only. Turning a pad into a world location needs the
// city's terrain, so that lives on the traffic system actor, which already owns the grid.
namespace SimCopterAirport
{
// The gameplay map. The pad table's tile coordinates are only masked to 8 bits by the original,
// so the fallback block at 128 is a legal address in the 256x256 cell map, not an overflow.
constexpr int32 MapSize = 128;

// XZON low nibble. SimCity 2000 zone 8 is the airport zone.
constexpr int32 AirportZoneType = 8;

// The airport is always a 4x4 block: a 2x2 terminal ringed by twelve 1x1 pads.
constexpr int32 BlockSpan = 4;
constexpr int32 PadCount = 12;

// FUN_0047c0c0's fallback origin when the city has no airport zone at all.
constexpr int32 FallbackOriginTile = MapSize;

// Reads one tile of a city map; must return 0 outside the 128x128 map.
using FTileReader = TFunctionRef<int32(int32 FileX, int32 FileY)>;

// FUN_004e4f80 as FUN_004829f0 calls it - with a *signed* char XBLD id. Every id from 0x80 up
// therefore arrives negative, takes the `< 0x70` branch and reports footprint 1 whatever the
// real table says, so the block-edge test below can only ever fire on ids 0x49-0x50 and
// 0x61-0x6b. Reproduced rather than corrected: it is what decides whether a block validates.
SIMCOPTERREMAKE_API int32 GetSignedXbldFootprintSize(int32 XbldId);

// FUN_004829f0's opening test: the 4x4 block starting here is all airport zone, fits in the
// map, and does not cut a 2x2 building in half along its far column or far row. A block whose
// origin is outside the map is never validated - that is the fallback airport, which the
// original builds unconditionally.
SIMCOPTERREMAKE_API bool IsAirportBlockAt(
	const FTileReader& GetZone,
	const FTileReader& GetXbld,
	int32 OriginX,
	int32 OriginY);

// FUN_0047c0c0's sweep. Returns the first validating block in x-outer/y-inner order, or
// (128, 128) when the city has no airport.
SIMCOPTERREMAKE_API FIntPoint FindAirportOrigin(const FTileReader& GetZone, const FTileReader& GetXbld);

// True when the airport had to be built outside the city rather than found in it.
SIMCOPTERREMAKE_API bool IsFallbackAirportOrigin(const FIntPoint& Origin);

// The twelve pad tiles in the order FUN_004829f0 writes them into DAT_005d9170. The order is
// not the ring's - it is the publication order, and it is what decides which pad a helicopter
// gets, so it is reproduced exactly. Pad 0 is the one the camera looks at on city entry.
SIMCOPTERREMAKE_API FIntPoint GetPadTile(const FIntPoint& Origin, int32 PadIndex);

// The 2x2 terminal's top-left tile. FUN_004829f0 samples the height map here and flattens the
// whole block to it, so this is the tile that gives every pad its altitude.
SIMCOPTERREMAKE_API FIntPoint GetTerminalTile(const FIntPoint& Origin);

// FUN_0048b000. Walks the pad table once: a pad with no cell occupants at all wins outright and
// ends the search, otherwise the *last* pad with no occupant flagged 4 wins. Returns INDEX_NONE
// when every pad is taken - the original returns null there and the caller dereferences it, so
// it never expected that to happen.
SIMCOPTERREMAKE_API int32 FindFreePadIndex(
	TFunctionRef<bool(int32 PadIndex)> HasCellOccupants,
	TFunctionRef<bool(int32 PadIndex)> HasBlockingOccupant);

// The XBLD ids FUN_004829f0 stamps over the block, demolishing whatever SimCity 2000 had
// zoned there. Both already have a dispatch case in the city builder: 0xde is the bare
// helipad slab (object 0x08b, no base) and 0xf6 is the terminal (object 0x096) which, on an
// airport-zone 2x2, takes base 0x165 - exactly the two objects FUN_004829f0 builds by hand.
constexpr int32 PadXbldId = 0xde;
constexpr int32 TerminalXbldId = 0xf6;

// What FUN_004829f0 leaves on one tile of the block: the terminal id on the middle 2x2, the pad
// id on the twelve perimeter tiles, INDEX_NONE for a tile outside the block.
SIMCOPTERREMAKE_API int32 GetStampedXbldId(const FIntPoint& Origin, int32 TileX, int32 TileY);

// The XZON high nibble to normalize beside GetStampedXbldId. SimCity 2000 marks a building's
// footprint by flagging its four corners (0x80/0x40/0x10/0x20, or 0xf0 for a 1x1). SimCopter's
// city renderer claims meshes from XBLD squares and FUN_004829f0 never reads XZON, but keeping the
// stamped SC2 grid internally coherent prevents other zone-marker consumers from inheriting the
// demolished airport buildings' corners.
// Returns INDEX_NONE for a tile outside the block.
SIMCOPTERREMAKE_API int32 GetStampedZoneHighNibble(const FIntPoint& Origin, int32 TileX, int32 TileY);

// FUN_004829f0's XBLD writes: 0xf6 over the middle 2x2 and 0xde over the twelve pads, plus the
// remake's XZON metadata normalization above. Without this the SimCity 2000 airport - runways, hangars,
// control tower - stays standing on the pads and a helicopter parked on one ends up inside a
// building. Does nothing for the fallback block, which is off the map and has no tiles to write.
SIMCOPTERREMAKE_API void StampBlockTiles(FSimCity2000City& City, const FIntPoint& Origin);

// FUN_004829f0's height-map write, which runs *after* FUN_004abce0's conditioning pass: it reads
// one conditioned corner - the terminal tile's own top-left - and copies it over the 5x5 corner
// patch spanning the block, levelling the ground under the whole airport however it sloped
// before. Corners is the (MapSize + 1) square grid BuildConditionedTerrainCornerSamples returns.
SIMCOPTERREMAKE_API void FlattenBlockCorners(TArray<int16>& Corners, int32 GridSize, const FIntPoint& Origin);

// Convenience for the two places that load a city for play: find the block, stamp it, and hand
// back the origin. Pass the conditioned corner grid to also level the terrain under it; the
// traffic system has no corner grid of its own and passes nullptr.
SIMCOPTERREMAKE_API FIntPoint BuildAirportIntoCity(FSimCity2000City& City, TArray<int16>* ConditionedCorners);
}
