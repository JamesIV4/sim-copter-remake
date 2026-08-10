// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FMaxisMeshObject;

// Roadside decorations, decoded from the city builder's own switch.
//
// FUN_0047c0c0 sweeps the 128x128 XBLD grid and, for each tile, hangs one or two GEO objects off
// the scene cell it creates. Almost every case attaches a single object - the road slab, the
// building, the tree. Nine of them attach a SECOND one, and that second object is the street
// furniture: hydrants, phone boxes, post boxes, litter bins, street lights and traffic signals.
//
// The second object goes in the cell's object list behind the first (`local_2c == 2` in the
// decompile is what says there is one), at the SAME tile origin as the road slab - the decoration
// meshes carry their own in-tile offset. A road tile spans -32..+32 original units and, for
// instance, TRASH29's vertices sit at X 27.9..31.2 / Z 21.4..24.3, i.e. already parked against the
// far curb. So a decoration is placed by appending it at the tile origin and nothing else.
//
// Every one of these cases sits INSIDE the four-corner flatness test (the same one that chooses
// between a road's curbed and curbless mesh - see Docs/memory/simcopter-road-tile-variants.md), so
// a sloped road tile gets no decoration at all.
//
// The gates, verbatim:
//
//   XBLD 0x1d  straight road NS, flat, (x & 1) && (y & 1)  -> one of TRASH29 / PHONE29 / FIREH29 /
//              MAIL29 by `rand() & 0xf`
//   XBLD 0x1e  straight road EW, same gate                 -> the matching *30 variants
//   XBLD 0x23..0x26  T junctions, flat, (x & 3) == 3 && (y & 3) == 3 -> LAMP35..LAMP38, one per id
//   XBLD 0x27..0x2a  crossroads, flat, (x & 1) && (y & 1)  -> SIGNAL1
//   XBLD 0x2b        crossroads, flat, unconditional       -> SIGNAL1
//
// The `local_4` word stored alongside the decoration (4 for street furniture, 0x204 for lamps and
// signals) is the same slot the primary object's own 0x20 / 0x40 / 0x280 goes in. It is read by the
// render walk, not by the placer, and is UNEXAMINED - the port does not need it, because the
// decorations already carry their placement in their vertices.
namespace SimCopterRoadDecorations
{
// GEO object ids, from FUN_00470571's argument in each case above.
constexpr int32 Lamp35ObjectId = 0x181;   // T junction 0x23
constexpr int32 Lamp36ObjectId = 0x182;   // T junction 0x24
constexpr int32 Lamp37ObjectId = 0x183;   // T junction 0x25
constexpr int32 Lamp38ObjectId = 0x184;   // T junction 0x26
constexpr int32 Signal1ObjectId = 0x185;  // crossroads
constexpr int32 Trash29ObjectId = 0x186;
constexpr int32 Trash30ObjectId = 0x187;
constexpr int32 Phone29ObjectId = 0x188;
constexpr int32 Phone30ObjectId = 0x189;
constexpr int32 FireHydrant29ObjectId = 0x18a;
constexpr int32 FireHydrant30ObjectId = 0x18b;
constexpr int32 Mail29ObjectId = 0x18c;
constexpr int32 Mail30ObjectId = 0x18d;

// The four buckets of `rand() & 0xf`, in the order the switch tests them. Ghidra renders the roll
// as `(((ushort)r ^ s) - s & 0xf ^ s) - s` with `s = (short)r >> 15`, which is the signed-modulo
// idiom - but MSVC's rand() only ever returns 0..0x7fff, so the sign word is always zero and the
// whole expression collapses to a plain `rand() & 0xf`.
enum class EStreetFurniture : uint8
{
	FireHydrant, // rolls 0, 1, 2
	PhoneBooth,  // rolls 3, 4, 5
	MailBox,     // rolls 6, 7, 8
	TrashCan,    // the `default:` arm - rolls 9..15, so a litter bin is 7 in 16
};

SIMCOPTERREMAKE_API EStreetFurniture GetStreetFurnitureForRoll(int32 Roll);

// The *29 objects belong to XBLD 0x1d and the *30 objects to 0x1e. They are the same four pieces
// authored against the two road orientations, not different props.
SIMCOPTERREMAKE_API int32 GetStreetFurnitureObjectId(EStreetFurniture Kind, bool bEastWestRoad);

// The whole switch, in one call. Returns INDEX_NONE for a tile that takes no decoration - which is
// most of them. FurnitureRoll is only consulted on 0x1d / 0x1e.
SIMCOPTERREMAKE_API int32 GetRoadDecorationObjectId(
	uint8 BuildingId,
	int32 TileX,
	int32 TileY,
	bool bTileIsFlat,
	int32 FurnitureRoll);

// DIVERGENCE, deliberate: the original rolls the global rand() while it builds the city, so which
// prop stands on which corner changes every time the same city is loaded. The remake hashes the
// tile instead, so a city looks the same each time you fly into it and a reload does not shuffle
// the street furniture under the player. CitySeed keeps two cities from sharing a pattern.
SIMCOPTERREMAKE_API int32 MakeStreetFurnitureRoll(int32 TileX, int32 TileY, int32 CitySeed);

SIMCOPTERREMAKE_API bool IsTrafficSignalObjectId(int32 ObjectId);
SIMCOPTERREMAKE_API bool IsStreetLightObjectId(int32 ObjectId);

// Where a street light's lamp head is and how wide a cone it throws, measured off the model.
//
// LAMP35..38 draw their light as GEOMETRY: 18 face-type-11 quads in three stacked bands under the
// head, plus a 14-vertex pool on the ground. That painted cone is the original's street light, and
// it is also a complete description of the light the remake wants - apex, length and spread all
// come off those vertices instead of being invented.
struct FStreetLightEmitter
{
	// Component-space offset of the cone's apex - just under the head, where the topmost light card
	// starts. Already in centimetres, with the caller's units/scale and orientation applied.
	FVector LocalOffset = FVector::ZeroVector;
	// Apex down to the ground pool.
	float ConeLengthCm = 0.0f;
	// atan(pool radius / cone length).
	float ConeHalfAngleDegrees = 0.0f;
};

// Reads the face-type-11 cards out of a LAMP object. False when the object carries none, which is
// every object that is not a street light.
SIMCOPTERREMAKE_API bool TryGetStreetLightEmitter(
	const FMaxisMeshObject& Object,
	float ModelUnitsPerCentimeter,
	float ModelScale,
	bool bApplyCityMeshOrientation,
	FStreetLightEmitter& OutEmitter);

// The Maxis face type LAMP35..38 paint their light cone with. Face type 11 is the light-CARD type
// the night-lighting pass already knows (car headlights, rotor discs, the LAMP35..38 posts) - not
// to be confused with type 25 (blink markers) or type 26 (effect markers).
constexpr uint8 LightCardFaceType = 11;
}
