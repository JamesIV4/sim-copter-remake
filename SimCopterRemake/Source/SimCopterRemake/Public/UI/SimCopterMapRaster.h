// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// The cockpit map (dash5.bmp), ported from the original's map module at 0x004a2740-0x004a4780.
//
// The original does not draw the map with primitives: FUN_004a28e0 rasterises a 124x98 8-bit
// buffer of palette indices from the city's tile grids, stamps the overlays into the same
// buffer, and FUN_004a3b20 blits it into the panel surface at (54,13). Everything here is that
// buffer. Keeping it as palette indices rather than colours is not nostalgia - the fire cells
// animate by *counting up the palette* (0x10..0x1f), and the height shading is an index add, so
// converting early would mean re-deriving both.
//
// Panel geometry, all from FUN_004a2740 unless noted:
//
//   dash5.bmp       185x148   the panel, at screen (455,290) (FUN_00412440's rect for 0x454420)
//   buffer          124x98    DAT_00505ed8/edc, blitted to panel (54,13) (DAT_00505ec8/ecc)
//   tile view       104x80    DAT_00505eec/ef0, at buffer (10,9) (DAT_00505ee0), so panel (64,22)
//   label           (30,126)-(175,139)  the current mission's name (FUN_004547a0, +0x108 rect)
//   six buttons     15x15 each          FUN_00454420's six rects at +0xa8..+0x107
//
// The tile view is always centred on the helicopter, and zoom is a shift: at zoom Z one tile is
// (1 << Z) pixels, so the view covers 104>>Z by 80>>Z tiles. The band written per row is always
// 104 px wide and the row advance is +20 to reach the next of the 124.
namespace SimCopterMap
{
// --- panel geometry ---------------------------------------------------------------------------

constexpr int32 PanelWidth = 185;
constexpr int32 PanelHeight = 148;

constexpr int32 BufferWidth = 124;   // DAT_00505ed8
constexpr int32 BufferHeight = 98;   // DAT_00505edc
constexpr int32 BufferPanelX = 54;   // DAT_00505ec8
constexpr int32 BufferPanelY = 13;   // DAT_00505ecc

// DAT_00505ee0 = buffer + 9 * width + 10: where the tile view starts inside the buffer.
constexpr int32 ViewOriginX = 10;
constexpr int32 ViewOriginY = 9;
constexpr int32 ViewTilesX = 104;    // DAT_00505eec
constexpr int32 ViewTilesY = 80;     // DAT_00505ef0

// DAT_00505ee8: the centre pixel every overlay ray starts from.
constexpr int32 CentreX = BufferWidth / 2;
constexpr int32 CentreY = BufferHeight / 2;

constexpr int32 MaxZoom = 3;         // FUN_004a3d50 clamps _DAT_00505f08 to 0..3
constexpr int32 HeadingNeedleSteps = 20; // DAT_00505ef8
constexpr int32 MaxServiceBlips = 20;    // DAT_005d3eb0 .. DAT_005d41d0, stride 40
constexpr int32 MaxMissions = 30;        // DAT_0057f9dc, stride 0xd4

constexpr int32 MapTiles = 128;

// FUN_004547a0's +0x108 rect, in panel pixels.
constexpr int32 LabelLeft = 30;
constexpr int32 LabelTop = 126;
constexpr int32 LabelRight = 175;
constexpr int32 LabelBottom = 139;
constexpr int32 LabelFontSize = 12;  // FUN_00460e30(0xc, 0, 0) in the constructor

// --- palette indices --------------------------------------------------------------------------
//
// Every UI bitmap ships the same 256-colour palette (verified: dash4/dash5/mapbttn are
// byte-identical), so these are indices into dash5.bmp's own table.
namespace Color
{
constexpr uint8 FireRampFirst = 0x10;  // + DAT_00505ef4, a 16-step animation counter
constexpr uint8 FireRampSteps = 16;
constexpr uint8 FireStation = 0x1a;    // XBLD 0xd3
constexpr uint8 Building = 0x3b;       // XBLD >= 0x70
constexpr uint8 LandRampFirst = 0x50;  // + altitude shade, terrain class 0x20-0x2f
constexpr uint8 Park = 0x5c;           // XBLD 0x06-0x0d and 0xd5
constexpr uint8 Heading = 0x70;        // the 20-step needle, and the centre tile at zoom 2/3
constexpr uint8 GroundRampFirst = 0x80;// + altitude shade, every other terrain class
constexpr uint8 Water = 0x90;          // terrain class 0x00-0x09
constexpr uint8 PoliceStation = 0x9a;  // XBLD 0xd2
constexpr uint8 Airport = 0xca;        // the 4x4 block at the airport origin
constexpr uint8 Network = 0xd4;        // XBLD 0x1d-0x6f: roads, rails, power, highways
constexpr uint8 Grid = 0xe8;           // the tile grid drawn at zoom 2 and 3
constexpr uint8 Hospital = 0xea;       // XBLD 0xd1

// FUN_004a3820's two direction lines fade with distance along their own ramps.
constexpr int32 PrimaryLineBase = 0x3f;
constexpr int32 PrimaryLineShift = 4;
constexpr int32 SecondaryLineBase = 0x6a;
constexpr int32 SecondaryLineShift = 3;
constexpr int32 LineFadeDivisor = 0x184;

// The label band the original clears before printing the mission name, and a near-white from the
// grey ramp for the text itself (the original's font carries its own colour; this matches it).
constexpr uint8 LabelBackground = 0x31;
constexpr uint8 LabelText = 0x3f;
}

// --- buttons ----------------------------------------------------------------------------------
//
// Six 15x15 buttons in two columns of three. Rects are FUN_00454420's; source cells are
// FUN_00454880's, which cuts mapbttn.bmp (64x48) as four columns of 16x16: the released face for
// the left and right column, then the pressed face for each.
constexpr int32 ButtonCount = 6;

enum class EButton : int32
{
	ZoomOut = 0,      // FUN_004a3d80
	ZoomIn = 1,       // FUN_004a3d50
	PreviousMission = 2, // FUN_004a3ec0
	NextMission = 3,  // FUN_004a3ed0
	ToggleMissionBlips = 4, // FUN_004a3ee0 -> DAT_00505f0c
	ToggleServiceBlips = 5, // FUN_004a3f00 -> DAT_00505f10
};

// Click rect in panel pixels (right/bottom exclusive, as the original compares them).
SIMCOPTERREMAKE_API FIntRect GetButtonRect(int32 ButtonIndex);
// Where the face is blitted, and which mapbttn.bmp cell to take.
SIMCOPTERREMAKE_API FIntPoint GetButtonDrawOrigin(int32 ButtonIndex);
SIMCOPTERREMAKE_API FIntRect GetButtonSourceRect(int32 ButtonIndex, bool bPressed);

// --- world inputs -------------------------------------------------------------------------------

// One square icon sheet: SIM3D.BMP page 3 (10 cells of 14x14, the first 8 used) for the mission
// blips, page 12 (3 cells of 10x10) for the dispatched service vehicles. Pixels are palette
// indices in the original's row order and index 0 is transparent, exactly as FUN_004a3f20 blits
// them.
struct SIMCOPTERREMAKE_API FSimCopterMapIconSheet
{
	int32 CellSize = 0;
	int32 Stride = 0;
	int32 CellCount = 0;
	TArray<uint8> Pixels;

	bool IsValid() const { return CellSize > 0 && CellCount > 0 && Pixels.Num() >= Stride * CellSize; }
};

// The per-city grids FUN_004a28e0 reads. Indexed [Y * MapTiles + X] to match the rest of the
// remake's city code; the original's arrays are [x][y], which is the same tile either way.
struct SIMCOPTERREMAKE_API FSimCopterMapCity
{
	TArray<uint8> Xbld;           // (&DAT_005910b0)[x] + y
	TArray<uint8> TerrainClass;   // DAT_005bde80
	TArray<uint8> AltitudeShade;  // DAT_005cde80 sample >> 6, clamped to 15
	TArray<uint8> OnFire;         // tile record byte 0 bit 0x20
	FIntPoint AirportOrigin = FIntPoint(INDEX_NONE, INDEX_NONE); // _DAT_005d91b0/_DAT_005d91b4

	bool IsValid() const;
	// Off the 128x128 map the original reads past its XBLD column table into zeroed terrain, which
	// paints the surrounding ocean. Reproduced rather than clipped, because the ocean border is
	// what makes a coastal city read correctly at zoom 0.
	uint8 GetXbld(int32 X, int32 Y) const;
	uint8 GetTerrainClass(int32 X, int32 Y) const;
	uint8 GetAltitudeShade(int32 X, int32 Y) const;
	bool IsOnFire(int32 X, int32 Y) const;
};

// A mission record as the map sees it: the fields FUN_004a4200 and FUN_004a3820 read out of the
// 0xd4-byte record at DAT_0057f9dc + slot * 0xd4.
struct SIMCOPTERREMAKE_API FSimCopterMapMission
{
	FString Name;                 // +0x00, printed in the label
	int32 EventId = INDEX_NONE;   // +0x24
	int32 TypeMask = 0;           // +0x50, picks the icons
	int32 Category = 0;           // +0x54, 2 = finished and off the map
	bool bActive = false;         // +0x4c bit 0
	FIntPoint Tile = FIntPoint(INDEX_NONE, INDEX_NONE);      // +0x28/+0x2c
	FIntPoint Secondary = FIntPoint(INDEX_NONE, INDEX_NONE); // +0x30/+0x34
	FIntPoint Tertiary = FIntPoint(INDEX_NONE, INDEX_NONE);  // +0x38/+0x3c

	// FUN_004a3ec0/FUN_004a3ed0's test for a mission worth cycling to.
	bool IsSelectable() const { return bActive && Category != 2; }
};

// One row of DAT_005d3eb0: a dispatched service vehicle, drawn from page 12 and clickable.
struct SIMCOPTERREMAKE_API FSimCopterMapServiceBlip
{
	int32 Id = INDEX_NONE;      // +0x04, the object the click menu acts on
	int32 IconIndex = INDEX_NONE; // +0x08, page 12 cell (-1 draws the track but no icon)
	FIntPoint Tile = FIntPoint(INDEX_NONE, INDEX_NONE);    // +0x10, where it is now
	FIntPoint EndTile = FIntPoint(INDEX_NONE, INDEX_NONE); // +0x14, where it is heading
};

// Everything one FUN_004a28e0 + FUN_004a3820 + FUN_004a4200 + FUN_004a4370 pass needs.
struct SIMCOPTERREMAKE_API FSimCopterMapFrame
{
	const FSimCopterMapCity* City = nullptr;
	const FSimCopterMapIconSheet* MissionIcons = nullptr;  // SIM3D page 3
	const FSimCopterMapIconSheet* ServiceIcons = nullptr;  // SIM3D page 12

	// The helicopter's tile (DAT_005040d0 + 0x18 / + 0x1c). The view centres here.
	FIntPoint CentreTile = FIntPoint(64, 64);

	// Its facing as the original stores it: a 16.16 unit vector the needle steps along, with the
	// Z component subtracted because the map's Y runs the other way (FUN_004a3820's `local_4 -=`).
	int32 HeadingX1616 = 0;
	int32 HeadingZ1616 = 0;

	TArray<FSimCopterMapMission> Missions;
	// Index into Missions of the mission the buttons cycle and the label names, or INDEX_NONE.
	int32 CurrentMission = INDEX_NONE;

	TArray<FSimCopterMapServiceBlip> ServiceBlips;
};

// The two toggles and the zoom, which outlive a frame. The original keeps them in globals
// (_DAT_00505f08, DAT_00505f0c, DAT_00505f10) and saves the zoom with the game.
struct SIMCOPTERREMAKE_API FSimCopterMapSettings
{
	int32 Zoom = 0;
	bool bShowMissionBlips = true;
	bool bShowServiceBlips = true;
};

// Where a rasterised service blip ended up, so a click can be resolved (FUN_004a3d00). Rects are
// in buffer pixels, inclusive at both ends as the original compares them.
struct SIMCOPTERREMAKE_API FSimCopterMapServiceHit
{
	int32 Id = INDEX_NONE;
	FIntRect Rect;
};

// --- the raster ---------------------------------------------------------------------------------

class SIMCOPTERREMAKE_API FSimCopterMapRaster
{
public:
	FSimCopterMapRaster();

	// One FUN_004a28e0 pass: clear, paint the tile view, then the mission lines, the other
	// missions' icons, the heading needle and the service blips. Advances the fire animation.
	void Render(const FSimCopterMapFrame& Frame, const FSimCopterMapSettings& Settings);

	const TArray<uint8>& GetPixels() const { return Pixels; }
	uint8 GetPixel(int32 X, int32 Y) const;

	// FUN_004a3d00, in buffer pixels. INDEX_NONE when nothing is under the point; only ever hits
	// when the service blips are switched on, which is the original's gate.
	int32 HitTestServiceBlip(int32 BufferX, int32 BufferY) const;
	const TArray<FSimCopterMapServiceHit>& GetServiceHits() const { return ServiceHits; }

	// The tile the view was last centred on, and the top-left tile it started from. Callers use
	// these to turn a click into a tile.
	FIntPoint GetViewOriginTile() const { return ViewOriginTile; }

	// FUN_004a4000: the two page-3 cells a mission type is drawn with, or INDEX_NONE.
	static void GetMissionIcons(int32 TypeMask, int32& OutPrimaryIcon, int32& OutSecondaryIcon);

	// FUN_004a3ec0 / FUN_004a3ed0: the previous/next selectable mission, wrapping the way the
	// original does (scan away from the current slot, then from the far end). Returns the current
	// index unchanged when nothing else qualifies.
	static int32 FindPreviousMission(const TArray<FSimCopterMapMission>& Missions, int32 Current);
	static int32 FindNextMission(const TArray<FSimCopterMapMission>& Missions, int32 Current);

	// The colour a tile paints at, before the fire animation and grid are applied. Exposed for
	// the tests, which is cheaper than reading it back out of the buffer.
	static uint8 ResolveTileColor(const FSimCopterMapCity& City, int32 TileX, int32 TileY, int32 FireAnimStep);

private:
	TArray<uint8> Pixels;
	TArray<FSimCopterMapServiceHit> ServiceHits;
	FIntPoint ViewOriginTile = FIntPoint::ZeroValue;
	FIntPoint ViewMaxTile = FIntPoint::ZeroValue;
	int32 FireAnimStep = 0;   // DAT_00505ef4

	void Clear();
	void RasteriseTiles(const FSimCopterMapFrame& Frame, const FSimCopterMapSettings& Settings);
	void DrawMissionLines(const FSimCopterMapFrame& Frame, const FSimCopterMapSettings& Settings);
	void DrawOtherMissions(const FSimCopterMapFrame& Frame, const FSimCopterMapSettings& Settings);
	void DrawHeadingNeedle(const FSimCopterMapFrame& Frame);
	void DrawServiceBlips(const FSimCopterMapFrame& Frame, const FSimCopterMapSettings& Settings);

	// FUN_004a3a00: walk a ray out from the centre by (DeltaX, DeltaY), painting LineColor where
	// it is non-zero, stopping at the buffer edge, then stamping IconIndex where it stopped.
	void DrawRay(
		int32 DeltaX,
		int32 DeltaY,
		uint8 LineColor,
		int32 IconIndex,
		const FSimCopterMapIconSheet* Icons,
		bool bDrawIcon);

	// FUN_004a4530: the same walk but from an arbitrary start, used for the service tracks.
	void DrawLineFrom(int32 StartX, int32 StartY, int32 DeltaX, int32 DeltaY, uint8 LineColor);

	// FUN_004a3f20 / FUN_004a4370's inner blit. Centred forms clamp into the buffer.
	void BlitIconCentred(const FSimCopterMapIconSheet& Icons, int32 IconIndex, int32 CentreBufferX, int32 CentreBufferY);
	void BlitIconAt(const FSimCopterMapIconSheet& Icons, int32 IconIndex, int32 BufferX, int32 BufferY);

	void SetPixel(int32 X, int32 Y, uint8 Color);
};
}
