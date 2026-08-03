// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SimCopterMapRaster.h"

namespace SimCopterMap
{
namespace
{
// FUN_00454420's six button rects, written to the object at +0xa8, +0xb8 ... +0xf8 as
// {left, top, right, bottom}. Two columns 18 px apart, three rows 19 px apart.
constexpr int32 ButtonRects[ButtonCount][4] =
{
	{  9, 54, 24, 69 },
	{ 27, 54, 42, 69 },
	{  9, 73, 24, 88 },
	{ 27, 73, 42, 88 },
	{  9, 92, 24, 107 },
	{ 27, 92, 42, 107 },
};

// FUN_00454880: where each face lands, and the 16x16 cell it comes from. Column 0/1 are the left
// and right released faces, column 2/3 the pressed ones - so the two columns of buttons do not
// share art, and a "pressed" cell is 32 px to the right of its own released cell.
constexpr int32 ButtonDrawOrigins[ButtonCount][2] =
{
	{  9, 54 }, { 27, 54 }, {  9, 73 }, { 27, 73 }, {  9, 92 }, { 27, 92 },
};
constexpr int32 ButtonSourceColumn[ButtonCount] = { 0, 1, 0, 1, 0, 1 };
constexpr int32 ButtonSourceRow[ButtonCount] = { 0, 0, 1, 1, 2, 2 };
constexpr int32 ButtonCellSize = 16;
constexpr int32 ButtonPressedColumnOffset = 2;

// FUN_004a4370's per-icon track colour, keyed by the page-12 cell: fire station orange, police
// cyan, hospital white, and yellow for a blip with no icon of its own.
uint8 GetServiceTrackColor(int32 IconIndex)
{
	switch (IconIndex)
	{
	case 0:  return Color::FireStation;
	case 1:  return Color::PoliceStation;
	case 2:  return Color::Hospital;
	default: return Color::Heading;
	}
}

bool IsSelectableSlot(const TArray<FSimCopterMapMission>& Missions, int32 Index)
{
	return Missions.IsValidIndex(Index) && Missions[Index].IsSelectable();
}

// FUN_004a3820's octagonal distance: twice the long axis plus the short one, in tiles.
int32 OctagonalLength(int32 DeltaTilesX, int32 DeltaTilesY)
{
	const int32 A = FMath::Abs(DeltaTilesX);
	const int32 B = FMath::Abs(DeltaTilesY);
	return (A > B) ? (B + A * 2) : (A + B * 2);
}
}

FIntRect GetButtonRect(const int32 ButtonIndex)
{
	if (ButtonIndex < 0 || ButtonIndex >= ButtonCount)
	{
		return FIntRect();
	}
	const int32* R = ButtonRects[ButtonIndex];
	return FIntRect(R[0], R[1], R[2], R[3]);
}

FIntPoint GetButtonDrawOrigin(const int32 ButtonIndex)
{
	if (ButtonIndex < 0 || ButtonIndex >= ButtonCount)
	{
		return FIntPoint::ZeroValue;
	}
	return FIntPoint(ButtonDrawOrigins[ButtonIndex][0], ButtonDrawOrigins[ButtonIndex][1]);
}

FIntRect GetButtonSourceRect(const int32 ButtonIndex, const bool bPressed)
{
	if (ButtonIndex < 0 || ButtonIndex >= ButtonCount)
	{
		return FIntRect();
	}
	const int32 Column = ButtonSourceColumn[ButtonIndex] + (bPressed ? ButtonPressedColumnOffset : 0);
	const int32 Left = Column * ButtonCellSize;
	const int32 Top = ButtonSourceRow[ButtonIndex] * ButtonCellSize;
	return FIntRect(Left, Top, Left + ButtonCellSize, Top + ButtonCellSize);
}

FText GetButtonToolTipText(const int32 ButtonIndex)
{
	switch (static_cast<EButton>(ButtonIndex))
	{
	case EButton::ZoomOut:
		return NSLOCTEXT("SimCopterMap", "ZoomOutToolTip", "Zoom Out");
	case EButton::ZoomIn:
		return NSLOCTEXT("SimCopterMap", "ZoomInToolTip", "Zoom In");
	case EButton::PreviousMission:
		return NSLOCTEXT("SimCopterMap", "PreviousMissionToolTip", "Previous Mission");
	case EButton::NextMission:
		return NSLOCTEXT("SimCopterMap", "NextMissionToolTip", "Next Mission");
	case EButton::ToggleMissionBlips:
		return NSLOCTEXT("SimCopterMap", "ToggleMissionBlipsToolTip", "Toggle Mission Markers");
	case EButton::ToggleServiceBlips:
		return NSLOCTEXT("SimCopterMap", "ToggleServiceBlipsToolTip", "Toggle Service Vehicle Markers");
	default:
		return FText::GetEmpty();
	}
}

// --- city grids ---------------------------------------------------------------------------------

bool FSimCopterMapCity::IsValid() const
{
	const int32 Expected = MapTiles * MapTiles;
	return Xbld.Num() == Expected && TerrainClass.Num() == Expected && AltitudeShade.Num() == Expected;
}

uint8 FSimCopterMapCity::GetXbld(const int32 X, const int32 Y) const
{
	if (X < 0 || Y < 0 || X >= MapTiles || Y >= MapTiles || Xbld.Num() != MapTiles * MapTiles)
	{
		return 0;
	}
	return Xbld[Y * MapTiles + X];
}

uint8 FSimCopterMapCity::GetTerrainClass(const int32 X, const int32 Y) const
{
	if (X < 0 || Y < 0 || X >= MapTiles || Y >= MapTiles || TerrainClass.Num() != MapTiles * MapTiles)
	{
		return 0;
	}
	return TerrainClass[Y * MapTiles + X];
}

uint8 FSimCopterMapCity::GetAltitudeShade(const int32 X, const int32 Y) const
{
	if (X < 0 || Y < 0 || X >= MapTiles || Y >= MapTiles || AltitudeShade.Num() != MapTiles * MapTiles)
	{
		return 0;
	}
	return AltitudeShade[Y * MapTiles + X];
}

bool FSimCopterMapCity::IsOnFire(const int32 X, const int32 Y) const
{
	if (X < 0 || Y < 0 || X >= MapTiles || Y >= MapTiles || OnFire.Num() != MapTiles * MapTiles)
	{
		return false;
	}
	return OnFire[Y * MapTiles + X] != 0;
}

// --- the raster ---------------------------------------------------------------------------------

FSimCopterMapRaster::FSimCopterMapRaster()
{
	Pixels.SetNumZeroed(BufferWidth * BufferHeight);
}

uint8 FSimCopterMapRaster::GetPixel(const int32 X, const int32 Y) const
{
	if (X < 0 || Y < 0 || X >= BufferWidth || Y >= BufferHeight)
	{
		return 0;
	}
	return Pixels[Y * BufferWidth + X];
}

void FSimCopterMapRaster::SetPixel(const int32 X, const int32 Y, const uint8 Color)
{
	if (X < 0 || Y < 0 || X >= BufferWidth || Y >= BufferHeight)
	{
		return;
	}
	Pixels[Y * BufferWidth + X] = Color;
}

void FSimCopterMapRaster::Clear()
{
	FMemory::Memzero(Pixels.GetData(), Pixels.Num());
}

uint8 FSimCopterMapRaster::ResolveTileColor(
	const FSimCopterMapCity& City,
	const int32 TileX,
	const int32 TileY,
	const int32 FireAnimStep)
{
	// A burning cell overrides everything and walks the 16-entry fire ramp, which is what makes
	// fires blink on the map. The step is per raster pass, not per tile.
	if (City.IsOnFire(TileX, TileY))
	{
		return static_cast<uint8>(Color::FireRampFirst + (FireAnimStep & (Color::FireRampSteps - 1)));
	}

	const uint8 Building = City.GetXbld(TileX, TileY);
	if (Building == 0xd1)
	{
		return Color::Hospital;
	}
	if (Building == 0xd2)
	{
		return Color::PoliceStation;
	}
	if (Building == 0xd3)
	{
		return Color::FireStation;
	}

	// The 4x4 block at the airport origin, which the original tests before any of the network or
	// terrain rules so the pads stay legible under whatever was stamped on them.
	const FIntPoint Airport = City.AirportOrigin;
	if (Airport.X >= 0 && Airport.Y >= 0 &&
		TileX >= Airport.X && TileX < Airport.X + 4 &&
		TileY >= Airport.Y && TileY < Airport.Y + 4)
	{
		return Color::Airport;
	}

	if ((Building >= 0x06 && Building <= 0x0d) || Building == 0xd5)
	{
		return Color::Park;
	}

	if (Building < 0x1d)
	{
		// Nothing built here, so the tile shows its ground. The original compares the terrain
		// class as a *signed* char, so a class of 0x80 or more would miss the water test; the
		// remake's grid only ever holds 0x00-0x7e, which makes this the same test.
		const uint8 Terrain = City.GetTerrainClass(TileX, TileY);
		const uint8 Shade = City.GetAltitudeShade(TileX, TileY);
		if (Terrain < 0x0a)
		{
			return Color::Water;
		}
		if (Terrain >= 0x20 && Terrain <= 0x2f)
		{
			return static_cast<uint8>(Color::LandRampFirst + Shade);
		}
		return static_cast<uint8>(Color::GroundRampFirst + Shade);
	}

	// 0x1d-0x6f is every network tile - roads, rails, power lines, highways - and 0x70 up is a
	// building.
	return (Building < 0x70) ? Color::Network : Color::Building;
}

void FSimCopterMapRaster::Render(const FSimCopterMapFrame& Frame, const FSimCopterMapSettings& Settings)
{
	Clear();

	// DAT_00505ef4: one step per pass, wrapped to the ramp's 16 entries.
	FireAnimStep = (FireAnimStep + 1) & (Color::FireRampSteps - 1);

	RasteriseTiles(Frame, Settings);
	DrawMissionLines(Frame, Settings);
	DrawHeadingNeedle(Frame);
	DrawOtherMissions(Frame, Settings);
	DrawServiceBlips(Frame, Settings);
}

void FSimCopterMapRaster::RasteriseTiles(const FSimCopterMapFrame& Frame, const FSimCopterMapSettings& Settings)
{
	const int32 Zoom = FMath::Clamp(Settings.Zoom, 0, MaxZoom);
	const int32 PixelsPerTile = 1 << Zoom;
	const int32 TilesAcross = ViewTilesX >> Zoom;
	const int32 TilesDown = ViewTilesY >> Zoom;
	const int32 HalfAcross = TilesAcross >> 1;
	const int32 HalfDown = TilesDown >> 1;

	// In North-Up orientation:
	// Screen Y (top to bottom) runs North to South: TileX decreases (CentreTile.X + HalfDown down to CentreTile.X - HalfDown)
	// Screen X (left to right) runs West to East: TileY decreases (CentreTile.Y + HalfAcross down to CentreTile.Y - HalfAcross)
	ViewOriginTile = FIntPoint(
		Frame.CentreTile.X + HalfDown,
		Frame.CentreTile.Y + HalfAcross);
	ViewMaxTile = FIntPoint(
		Frame.CentreTile.X - HalfDown,
		Frame.CentreTile.Y - HalfAcross);

	if (Frame.City == nullptr)
	{
		return;
	}
	const FSimCopterMapCity& City = *Frame.City;

	// Zoom 2 and 3 rule the tiles: a grid pixel down the left of every tile, and a whole grid row
	// in the block. The row is sub-row 1 at zoom 2 but sub-row 7 at zoom 3 - not symmetric, but
	// that is what FUN_004a28e0's two tails do, verified against the assembly.
	const bool bDrawGrid = Zoom >= 2;
	const int32 GridRow = (Zoom == 2) ? 1 : (PixelsPerTile - 1);

	for (int32 RowIndex = 0; RowIndex < TilesDown; ++RowIndex)
	{
		const int32 TileX = Frame.CentreTile.X + HalfDown - RowIndex;
		const int32 FirstPixelRow = ViewOriginY + RowIndex * PixelsPerTile;

		// Sub-row 0 carries the tile colours; the rest of the block is copied from it.
		for (int32 ColumnIndex = 0; ColumnIndex < TilesAcross; ++ColumnIndex)
		{
			const int32 TileY = Frame.CentreTile.Y + HalfAcross - ColumnIndex;
			const int32 FirstPixelColumn = ViewOriginX + ColumnIndex * PixelsPerTile;

			// Only zoom 2 and 3 mark the tile the helicopter is over; at zoom 0 and 1 a tile is
			// too small to see it.
			const bool bCentreTile = bDrawGrid && TileX == Frame.CentreTile.X && TileY == Frame.CentreTile.Y;
			const uint8 TileColor = bCentreTile
				? Color::Heading
				: ResolveTileColor(City, TileX, TileY, FireAnimStep);

			for (int32 Offset = 0; Offset < PixelsPerTile; ++Offset)
			{
				const uint8 Written = (bDrawGrid && Offset == 0) ? Color::Grid : TileColor;
				SetPixel(FirstPixelColumn + Offset, FirstPixelRow, Written);
			}
		}

		// Expand the block. The copy and fill widths are always the view's 104 pixels, whatever
		// the zoom, because the tile count shrinks as the tile grows.
		for (int32 SubRow = 1; SubRow < PixelsPerTile; ++SubRow)
		{
			const int32 DestRow = FirstPixelRow + SubRow;
			if (DestRow >= BufferHeight)
			{
				break;
			}
			if (bDrawGrid && SubRow == GridRow)
			{
				for (int32 X = 0; X < ViewTilesX; ++X)
				{
					SetPixel(ViewOriginX + X, DestRow, Color::Grid);
				}
			}
			else
			{
				for (int32 X = 0; X < ViewTilesX; ++X)
				{
					SetPixel(ViewOriginX + X, DestRow, GetPixel(ViewOriginX + X, FirstPixelRow));
				}
			}
		}
	}
}

void FSimCopterMapRaster::DrawRay(
	const int32 InDeltaX,
	const int32 InDeltaY,
	const uint8 LineColor,
	const int32 IconIndex,
	const FSimCopterMapIconSheet* Icons,
	const bool bDrawIcon)
{
	int32 PenX = CentreX;
	int32 PenY = CentreY;
	int32 StepX = 1;
	int32 StepY = 1;
	int32 DeltaX = InDeltaX;
	int32 DeltaY = InDeltaY;
	if (DeltaX < 0)
	{
		DeltaX = -DeltaX;
		StepX = -1;
	}
	if (DeltaY < 0)
	{
		DeltaY = -DeltaY;
		StepY = -1;
	}

	int32 Error = 0;
	int32 Steps = 0;
	const int32 Major = FMath::Max(DeltaX, DeltaY);
	const int32 Minor = FMath::Min(DeltaX, DeltaY);
	const bool bMajorIsX = DeltaY < DeltaX;

	while (true)
	{
		if (LineColor != 0)
		{
			SetPixel(PenX, PenY, LineColor);
		}

		Error += Minor;
		if (Error > Major)
		{
			Error -= Major;
			if (bMajorIsX)
			{
				PenY += StepY;
			}
			else
			{
				PenX += StepX;
			}
		}
		if (bMajorIsX)
		{
			PenX += StepX;
		}
		else
		{
			PenY += StepY;
		}

		// The pen stops the moment it leaves the buffer, which is what pins an off-map icon to
		// the map's edge along its bearing instead of dropping it.
		if (PenX < 0 || PenY < 0 || PenX >= BufferWidth || PenY >= BufferHeight)
		{
			break;
		}
		++Steps;
		if (Steps > Major)
		{
			break;
		}
	}

	if (bDrawIcon && IconIndex != INDEX_NONE && Icons != nullptr && Icons->IsValid())
	{
		BlitIconCentred(*Icons, IconIndex, PenX, PenY);
	}
}

void FSimCopterMapRaster::DrawLineFrom(
	const int32 StartX,
	const int32 StartY,
	const int32 InDeltaX,
	const int32 InDeltaY,
	const uint8 LineColor)
{
	int32 PenX = StartX;
	int32 PenY = StartY;
	int32 StepX = 1;
	int32 StepY = 1;
	int32 DeltaX = InDeltaX;
	int32 DeltaY = InDeltaY;
	if (DeltaX < 0)
	{
		DeltaX = -DeltaX;
		StepX = -1;
	}
	if (DeltaY < 0)
	{
		DeltaY = -DeltaY;
		StepY = -1;
	}

	int32 Error = 0;
	int32 Steps = 0;
	const int32 Major = FMath::Max(DeltaX, DeltaY);
	const int32 Minor = FMath::Min(DeltaX, DeltaY);
	const bool bMajorIsX = DeltaY < DeltaX;

	while (true)
	{
		if (LineColor != 0)
		{
			SetPixel(PenX, PenY, LineColor);
		}

		Error += Minor;
		if (Error > Major)
		{
			Error -= Major;
			if (bMajorIsX)
			{
				PenY += StepY;
			}
			else
			{
				PenX += StepX;
			}
		}
		if (bMajorIsX)
		{
			PenX += StepX;
		}
		else
		{
			PenY += StepY;
		}

		if (PenX < 0 || PenY < 0 || PenX >= BufferWidth || PenY >= BufferHeight)
		{
			break;
		}
		++Steps;
		if (Steps > Major)
		{
			break;
		}
	}
}

void FSimCopterMapRaster::BlitIconCentred(
	const FSimCopterMapIconSheet& Icons,
	const int32 IconIndex,
	const int32 CentreBufferX,
	const int32 CentreBufferY)
{
	const int32 Cell = Icons.CellSize;
	int32 Left = CentreBufferX - (Cell >> 1);
	int32 Top = CentreBufferY - (Cell >> 1);
	Left = FMath::Max(Left, 0);
	Top = FMath::Max(Top, 0);
	if (Left + Cell > BufferWidth)
	{
		Left = BufferWidth - Cell;
	}
	if (Top + Cell > BufferHeight)
	{
		Top = BufferHeight - Cell;
	}
	BlitIconAt(Icons, IconIndex, Left, Top);
}

void FSimCopterMapRaster::BlitIconAt(
	const FSimCopterMapIconSheet& Icons,
	const int32 IconIndex,
	const int32 BufferX,
	const int32 BufferY)
{
	if (IconIndex < 0 || IconIndex >= Icons.CellCount)
	{
		return;
	}

	const int32 Cell = Icons.CellSize;
	const int32 SourceLeft = IconIndex * Cell;
	for (int32 Row = 0; Row < Cell; ++Row)
	{
		const int32 SourceRow = Row * Icons.Stride + SourceLeft;
		for (int32 Column = 0; Column < Cell; ++Column)
		{
			const int32 SourceIndex = SourceRow + Column;
			if (!Icons.Pixels.IsValidIndex(SourceIndex))
			{
				continue;
			}
			// Index 0 is the sheet's hole; the original skips it rather than keying a colour.
			const uint8 Source = Icons.Pixels[SourceIndex];
			if (Source != 0)
			{
				SetPixel(BufferX + Column, BufferY + Row, Source);
			}
		}
	}
}

void FSimCopterMapRaster::DrawMissionLines(const FSimCopterMapFrame& Frame, const FSimCopterMapSettings& Settings)
{
	if (!Frame.Missions.IsValidIndex(Frame.CurrentMission))
	{
		return;
	}
	const FSimCopterMapMission& Mission = Frame.Missions[Frame.CurrentMission];
	const int32 Zoom = FMath::Clamp(Settings.Zoom, 0, MaxZoom);

	// Primary line goes to Mission.Tile (the pickup / mission site) until the player has begun the
	// mission (bBegun is true). Once begun, it points to Mission.Secondary (the dropoff/hospital).
	FIntPoint Primary = Mission.Tile;
	FIntPoint Secondary = (Mission.Secondary.X != INDEX_NONE) ? Mission.Secondary : Mission.Tertiary;

	if (Mission.bBegun && Mission.Secondary.X != INDEX_NONE)
	{
		Primary = Mission.Secondary;
		Secondary = Mission.Tertiary;
	}

	if (Primary.X != INDEX_NONE)
	{
		const int32 TileDeltaX = Primary.X - Frame.CentreTile.X;
		const int32 TileDeltaY = Primary.Y - Frame.CentreTile.Y;
		const int32 Length = OctagonalLength(TileDeltaX, TileDeltaY);
		const int32 Shade = Color::PrimaryLineBase - (Length << Color::PrimaryLineShift) / Color::LineFadeDivisor;
		DrawRay(-TileDeltaY << Zoom, -TileDeltaX << Zoom, static_cast<uint8>(Shade), INDEX_NONE, nullptr, false);
	}

	if (Secondary.X != INDEX_NONE && Secondary != Primary)
	{
		const int32 TileDeltaX = Secondary.X - Frame.CentreTile.X;
		const int32 TileDeltaY = Secondary.Y - Frame.CentreTile.Y;
		const int32 Length = OctagonalLength(TileDeltaX, TileDeltaY);
		const int32 Shade = Color::SecondaryLineBase - (Length << Color::SecondaryLineShift) / Color::LineFadeDivisor;
		DrawRay(-TileDeltaY << Zoom, -TileDeltaX << Zoom, static_cast<uint8>(Shade), INDEX_NONE, nullptr, false);
	}
}

void FSimCopterMapRaster::DrawHeadingNeedle(const FSimCopterMapFrame& Frame)
{
	// Twenty steps of the facing vector out from the centre, in 16.16. The Z component is
	// subtracted because the world's Z axis runs opposite the map's Y.
	int32 AccumulatorX = 0;
	int32 AccumulatorY = 0;
	for (int32 Step = 0; Step < HeadingNeedleSteps; ++Step)
	{
		const int32 OffsetX = AccumulatorX >> 16;
		const int32 OffsetY = AccumulatorY >> 16;
		AccumulatorX += Frame.HeadingX1616;
		AccumulatorY -= Frame.HeadingZ1616;
		SetPixel(CentreX + OffsetX, CentreY + OffsetY, Color::Heading);
	}
}

void FSimCopterMapRaster::DrawOtherMissions(const FSimCopterMapFrame& Frame, const FSimCopterMapSettings& Settings)
{
	const int32 Zoom = FMath::Clamp(Settings.Zoom, 0, MaxZoom);

	for (int32 Index = 0; Index < Frame.Missions.Num() && Index < MaxMissions; ++Index)
	{
		const FSimCopterMapMission& Mission = Frame.Missions[Index];
		// The selected mission is skipped: it is already the one with the two direction lines.
		if (!Mission.IsSelectable() || Index == Frame.CurrentMission)
		{
			continue;
		}

		int32 PrimaryIcon = INDEX_NONE;
		int32 SecondaryIcon = INDEX_NONE;
		GetMissionIcons(Mission.TypeMask, PrimaryIcon, SecondaryIcon);

		FIntPoint Primary = Mission.Tile;
		FIntPoint Secondary = (Mission.Secondary.X != INDEX_NONE) ? Mission.Secondary : Mission.Tertiary;

		if (Mission.bBegun && Mission.Secondary.X != INDEX_NONE)
		{
			Primary = Mission.Secondary;
			Secondary = Mission.Tertiary;
		}

		if (Primary.X != INDEX_NONE)
		{
			DrawRay(
				-(Primary.Y - Frame.CentreTile.Y) << Zoom,
				-(Primary.X - Frame.CentreTile.X) << Zoom,
				0,
				PrimaryIcon,
				Frame.MissionIcons,
				Settings.bShowMissionBlips);
		}

		if (Secondary.X != INDEX_NONE && Secondary != Primary)
		{
			DrawRay(
				-(Secondary.Y - Frame.CentreTile.Y) << Zoom,
				-(Secondary.X - Frame.CentreTile.X) << Zoom,
				0,
				SecondaryIcon,
				Frame.MissionIcons,
				Settings.bShowMissionBlips);
		}
	}
}

void FSimCopterMapRaster::DrawServiceBlips(const FSimCopterMapFrame& Frame, const FSimCopterMapSettings& Settings)
{
	ServiceHits.Reset();

	const int32 Zoom = FMath::Clamp(Settings.Zoom, 0, MaxZoom);
	const int32 Cell = (Frame.ServiceIcons != nullptr) ? Frame.ServiceIcons->CellSize : 0;
	const int32 TilesAcross = ViewTilesX >> Zoom;
	const int32 TilesDown = ViewTilesY >> Zoom;
	const int32 HalfAcross = TilesAcross >> 1;
	const int32 HalfDown = TilesDown >> 1;

	const int32 MinTileX = Frame.CentreTile.X - HalfDown;
	const int32 MaxTileX = Frame.CentreTile.X + HalfDown;
	const int32 MinTileY = Frame.CentreTile.Y - HalfAcross;
	const int32 MaxTileY = Frame.CentreTile.Y + HalfAcross;

	for (int32 Index = 0; Index < Frame.ServiceBlips.Num() && Index < MaxServiceBlips; ++Index)
	{
		const FSimCopterMapServiceBlip& Blip = Frame.ServiceBlips[Index];
		if (Blip.Tile.X == INDEX_NONE || Blip.Tile.Y == INDEX_NONE)
		{
			continue;
		}
		if (Blip.Tile.X < MinTileX || Blip.Tile.X > MaxTileX ||
			Blip.Tile.Y < MinTileY || Blip.Tile.Y > MaxTileY)
		{
			continue;
		}

		const int32 TileDeltaX = Blip.Tile.X - Frame.CentreTile.X;
		const int32 TileDeltaY = Blip.Tile.Y - Frame.CentreTile.Y;

		const int32 Left = CentreX + ((-TileDeltaY) << Zoom) - (Cell >> 1);
		const int32 Top = CentreY + ((-TileDeltaX) << Zoom) - (Cell >> 1);

		if (Blip.EndTile.X != INDEX_NONE && Blip.EndTile.Y != INDEX_NONE)
		{
			const int32 EndTileDeltaX = Blip.EndTile.X - Blip.Tile.X;
			const int32 EndTileDeltaY = Blip.EndTile.Y - Blip.Tile.Y;
			DrawLineFrom(
				Left + (Cell >> 1),
				Top + (Cell >> 1),
				-EndTileDeltaY << Zoom,
				-EndTileDeltaX << Zoom,
				GetServiceTrackColor(Blip.IconIndex));
		}

		if (Blip.IconIndex != INDEX_NONE && Settings.bShowServiceBlips && Frame.ServiceIcons != nullptr &&
			Frame.ServiceIcons->IsValid())
		{
			BlitIconAt(*Frame.ServiceIcons, Blip.IconIndex, Left, Top);
		}

		FSimCopterMapServiceHit Hit;
		Hit.Id = Blip.Id;
		Hit.Rect = FIntRect(Left, Top, Left + Cell, Top + Cell);
		ServiceHits.Add(Hit);
	}
}

int32 FSimCopterMapRaster::HitTestServiceBlip(const int32 BufferX, const int32 BufferY) const
{
	// The original walks the whole table and keeps the last match, so an overlapping pair
	// resolves to the later slot.
	int32 Result = INDEX_NONE;
	for (const FSimCopterMapServiceHit& Hit : ServiceHits)
	{
		if (BufferX >= Hit.Rect.Min.X && BufferX <= Hit.Rect.Max.X &&
			BufferY >= Hit.Rect.Min.Y && BufferY <= Hit.Rect.Max.Y)
		{
			Result = Hit.Id;
		}
	}
	return Result;
}

void FSimCopterMapRaster::GetMissionIcons(const int32 TypeMask, int32& OutPrimaryIcon, int32& OutSecondaryIcon)
{
	// FUN_004a4000, in its own order. The masks are the mission record's +0x50 type bits, so this
	// is keyed on the whole mask and not on individual bits: a boat rescue (0x90) is not a water
	// rescue (0x80) plus a rescue (0x10), it is its own row.
	OutPrimaryIcon = INDEX_NONE;
	OutSecondaryIcon = INDEX_NONE;

	switch (TypeMask)
	{
	case 0x1:      // building fire
	case 0x9:      // fire with debris
	case 0x408:    // car fire
		OutPrimaryIcon = 7;
		break;
	case 0xc:      // debris + medevac
	case 0x100:    // train crash
		OutPrimaryIcon = 7;
		OutSecondaryIcon = 1;
		break;
	case 0x10:     // rescue
	case 0x90:     // boat rescue
	case 0x110:    // train rescue
	case 0x80010:  // people trapped at a fire
		OutPrimaryIcon = 4;
		break;
	case 0x20:     // medevac
		OutPrimaryIcon = 1;
		OutSecondaryIcon = 4;
		break;
	case 0x40:     // transport
		OutPrimaryIcon = 3;
		OutSecondaryIcon = 3;
		break;
	case 0x200:    // criminal on foot
	case 0x2000:   // speeder
	case 0x4000:   // criminal car
	case 0x10000:
	case 0x20000:
	case 0x40000:
		OutPrimaryIcon = 2;
		break;
	case 0x800:    // traffic jam
		OutPrimaryIcon = 5;
		OutSecondaryIcon = 5;
		break;
	case 0x1000:   // riot
		OutPrimaryIcon = 6;
		break;
	default:
		break;
	}
}

int32 FSimCopterMapRaster::FindPreviousMission(const TArray<FSimCopterMapMission>& Missions, const int32 Current)
{
	if (!Missions.IsValidIndex(Current))
	{
		return Current;
	}

	// Nearest selectable slot below the current one...
	for (int32 Index = Current - 1; Index >= 0; --Index)
	{
		if (IsSelectableSlot(Missions, Index))
		{
			return Index;
		}
	}

	// ...then the highest one above it, so the list wraps rather than sticking at slot 0.
	const int32 LastSlot = FMath::Min(Missions.Num(), MaxMissions) - 1;
	for (int32 Index = LastSlot; Index > Current; --Index)
	{
		if (IsSelectableSlot(Missions, Index))
		{
			return Index;
		}
	}

	return Current;
}

int32 FSimCopterMapRaster::FindNextMission(const TArray<FSimCopterMapMission>& Missions, const int32 Current)
{
	if (!Missions.IsValidIndex(Current))
	{
		return Current;
	}

	const int32 LastSlot = FMath::Min(Missions.Num(), MaxMissions) - 1;
	for (int32 Index = Current + 1; Index <= LastSlot; ++Index)
	{
		if (IsSelectableSlot(Missions, Index))
		{
			return Index;
		}
	}

	for (int32 Index = 0; Index < Current; ++Index)
	{
		if (IsSelectableSlot(Missions, Index))
		{
			return Index;
		}
	}

	return Current;
}
}
