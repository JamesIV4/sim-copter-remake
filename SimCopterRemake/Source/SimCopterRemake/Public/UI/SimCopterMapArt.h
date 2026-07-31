// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/SimCopterMapRaster.h"

class UTexture2D;

// The two pieces of original artwork the map needs that are not a plain Slate brush: the palette
// the rasterised buffer is coloured through, and the two SIM3D icon sheets it stamps.
class SIMCOPTERREMAKE_API FSimCopterMapArt
{
public:
	// SIM3D.BMP pages FUN_004a2740 takes the map's icons from. Page 3 is ten 14x14 cells of which
	// the original builds pointers to the first eight; page 12 is three 10x10 service vehicles.
	static constexpr int32 MissionIconPage = 3;
	static constexpr int32 MissionIconCells = 8;
	static constexpr int32 ServiceIconPage = 12;
	static constexpr int32 ServiceIconCells = 3;

	// The game's 256-colour palette, read out of dash5.bmp's own table. Every original UI bitmap
	// carries the same one - dash4, dash5 and mapbttn are byte-identical - so this is the game
	// palette, not just the map panel's.
	static bool LoadPalette(const FString& OriginalGameRoot, TArray<FColor>& OutPalette);

	// One SIM3D page as raw palette indices in the file's own row order. FMaxisTextureReader is
	// not used here on purpose: it resolves colours and flips the image bottom-up for texture UVs,
	// and the map blits these rows top-down as indices.
	static bool LoadIconSheet(
		const FString& OriginalGameRoot,
		int32 PageIndex,
		int32 MaxCells,
		SimCopterMap::FSimCopterMapIconSheet& OutSheet);

	// Colours the raster through the palette into an existing texture, creating it the first
	// time. Index 0 stays transparent so the panel art shows through the buffer's margins.
	static UTexture2D* UpdateRasterTexture(
		UObject* Outer,
		UTexture2D* Existing,
		const TArray<uint8>& Pixels,
		const TArray<FColor>& Palette);

	// Resolves BMP/<FileName> case-insensitively under the original game root; empty when absent.
	static FString ResolveBitmapPath(const FString& OriginalGameRoot, const FString& FileName);
};
