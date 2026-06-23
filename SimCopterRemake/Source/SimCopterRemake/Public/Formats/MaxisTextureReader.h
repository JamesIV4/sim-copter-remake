// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FMaxisTextureImage
{
	int32 Width = 0;
	int32 Height = 0;
	TArray<FColor> Pixels;
};

struct FMaxisCompositeBitmap
{
	FString SourceFile;
	int32 FileSize = 0;
	int32 ImageCount = 0;
	int32 ResolutionCount = 0;
	TArray<FMaxisTextureImage> Images;

	const FMaxisTextureImage* FindImage(int32 ImageIndex) const;
};

class FMaxisTextureReader
{
public:
	static constexpr int32 AtlasTileSize = 32;
	static constexpr int32 AtlasColumnCount = 8;

	static bool LoadCompositeBitmapFromFile(
		const FString& FilePath,
		const TArray<FColor>& Palette,
		FMaxisCompositeBitmap& OutBitmap,
		FString& OutError,
		bool bFirstPaletteColorTransparent = true);

	static bool LoadCompositeBitmapFromBytes(
		const TArray<uint8>& FileData,
		const FString& SourceName,
		const TArray<FColor>& Palette,
		FMaxisCompositeBitmap& OutBitmap,
		FString& OutError,
		bool bFirstPaletteColorTransparent = true);

	static bool ExtractAtlasTile(
		const FMaxisTextureImage& AtlasImage,
		int32 TileIndex,
		FMaxisTextureImage& OutTileImage,
		FString& OutError,
		int32 TileSize = AtlasTileSize,
		int32 ColumnCount = AtlasColumnCount);
};
