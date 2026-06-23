// Copyright Epic Games, Inc. All Rights Reserved.

#include "Formats/MaxisTextureReader.h"

#include "Misc/FileHelper.h"

namespace
{
bool CanRead(const TArray<uint8>& Data, int32 Offset, int32 Size)
{
	return Offset >= 0 && Size >= 0 && Offset <= Data.Num() && Size <= Data.Num() - Offset;
}

uint32 ReadUInt32LE(const TArray<uint8>& Data, int32 Offset)
{
	return static_cast<uint32>(Data[Offset]) |
		(static_cast<uint32>(Data[Offset + 1]) << 8) |
		(static_cast<uint32>(Data[Offset + 2]) << 16) |
		(static_cast<uint32>(Data[Offset + 3]) << 24);
}

int32 ReadInt32LE(const TArray<uint8>& Data, int32 Offset)
{
	return static_cast<int32>(ReadUInt32LE(Data, Offset));
}

FColor ResolvePaletteColor(const TArray<FColor>& Palette, uint8 PaletteIndex, bool bFirstPaletteColorTransparent)
{
	if (!Palette.IsValidIndex(PaletteIndex))
	{
		return FColor::Transparent;
	}

	FColor Color = Palette[PaletteIndex];
	Color.A = (bFirstPaletteColorTransparent && PaletteIndex == 0) ? 0 : 255;
	return Color;
}
}

const FMaxisTextureImage* FMaxisCompositeBitmap::FindImage(int32 ImageIndex) const
{
	return Images.IsValidIndex(ImageIndex) ? &Images[ImageIndex] : nullptr;
}

bool FMaxisTextureReader::LoadCompositeBitmapFromFile(
	const FString& FilePath,
	const TArray<FColor>& Palette,
	FMaxisCompositeBitmap& OutBitmap,
	FString& OutError,
	bool bFirstPaletteColorTransparent)
{
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
	{
		OutError = FString::Printf(TEXT("Could not read Maxis composite bitmap file '%s'."), *FilePath);
		return false;
	}

	return LoadCompositeBitmapFromBytes(FileData, FilePath, Palette, OutBitmap, OutError, bFirstPaletteColorTransparent);
}

bool FMaxisTextureReader::LoadCompositeBitmapFromBytes(
	const TArray<uint8>& FileData,
	const FString& SourceName,
	const TArray<FColor>& Palette,
	FMaxisCompositeBitmap& OutBitmap,
	FString& OutError,
	bool bFirstPaletteColorTransparent)
{
	OutBitmap = FMaxisCompositeBitmap();
	OutBitmap.SourceFile = SourceName;
	OutBitmap.FileSize = FileData.Num();

	if (Palette.Num() < 256)
	{
		OutError = FString::Printf(TEXT("'%s' cannot be decoded without a 256-color Maxis palette."), *SourceName);
		return false;
	}

	if (!CanRead(FileData, 0, 16))
	{
		OutError = FString::Printf(TEXT("'%s' is too small to be a Maxis composite bitmap."), *SourceName);
		return false;
	}

	if (FileData[0] == 'B' && FileData[1] == 'M')
	{
		OutError = FString::Printf(TEXT("'%s' is a Windows bitmap, not a Maxis composite bitmap."), *SourceName);
		return false;
	}

	const int32 DeclaredFileSize = ReadInt32LE(FileData, 0);
	if (DeclaredFileSize != FileData.Num())
	{
		OutError = FString::Printf(TEXT("'%s' declares file size %d but actual size is %d."), *SourceName, DeclaredFileSize, FileData.Num());
		return false;
	}

	OutBitmap.ImageCount = ReadInt32LE(FileData, 8);
	OutBitmap.ResolutionCount = ReadInt32LE(FileData, 12);
	if (OutBitmap.ImageCount <= 0 || OutBitmap.ResolutionCount <= 0 || OutBitmap.ImageCount > 4096 || OutBitmap.ResolutionCount > 4096)
	{
		OutError = FString::Printf(TEXT("'%s' has invalid image counts: images=%d resolutions=%d."),
			*SourceName,
			OutBitmap.ImageCount,
			OutBitmap.ResolutionCount);
		return false;
	}

	int32 Cursor = 16 + OutBitmap.ResolutionCount * 12;
	if (!CanRead(FileData, 16, OutBitmap.ResolutionCount * 12))
	{
		OutError = FString::Printf(TEXT("'%s' has a resolution table outside the file."), *SourceName);
		return false;
	}

	OutBitmap.Images.Reset();
	OutBitmap.Images.Reserve(OutBitmap.ImageCount);

	for (int32 ImageIndex = 0; ImageIndex < OutBitmap.ImageCount; ++ImageIndex)
	{
		if (!CanRead(FileData, Cursor, 12))
		{
			OutError = FString::Printf(TEXT("'%s' is truncated before image %d header."), *SourceName, ImageIndex);
			return false;
		}

		FMaxisTextureImage Image;
		Image.Width = ReadInt32LE(FileData, Cursor);
		Image.Height = ReadInt32LE(FileData, Cursor + 4);
		const int32 ImageUnknown = ReadInt32LE(FileData, Cursor + 8);
		if (Image.Width <= 0 || Image.Height <= 0 || Image.Width > 4096 || Image.Height > 4096 || ImageUnknown != 0)
		{
			OutError = FString::Printf(TEXT("'%s' image %d has invalid header: width=%d height=%d unknown=%d."),
				*SourceName,
				ImageIndex,
				Image.Width,
				Image.Height,
				ImageUnknown);
			return false;
		}

		const int32 RowTableOffset = Cursor + 12;
		const int32 DataOffset = RowTableOffset + Image.Height * 4;
		const int32 PixelCount = Image.Width * Image.Height;
		if (!CanRead(FileData, RowTableOffset, Image.Height * 4) || !CanRead(FileData, DataOffset, PixelCount))
		{
			OutError = FString::Printf(TEXT("'%s' image %d data is outside the file."), *SourceName, ImageIndex);
			return false;
		}

		Image.Pixels.SetNumUninitialized(PixelCount);
		for (int32 Row = 0; Row < Image.Height; ++Row)
		{
			const int32 RowOffset = ReadInt32LE(FileData, RowTableOffset + Row * 4);
			if (RowOffset < 0 || RowOffset > PixelCount - Image.Width)
			{
				OutError = FString::Printf(TEXT("'%s' image %d row %d has invalid row offset %d."),
					*SourceName,
					ImageIndex,
					Row,
					RowOffset);
				return false;
			}

			for (int32 Column = 0; Column < Image.Width; ++Column)
			{
				const uint8 PaletteIndex = FileData[DataOffset + RowOffset + Column];
				const int32 DestRow = Image.Height - 1 - Row;
				Image.Pixels[DestRow * Image.Width + Column] = ResolvePaletteColor(Palette, PaletteIndex, bFirstPaletteColorTransparent);
			}
		}

		OutBitmap.Images.Add(MoveTemp(Image));
		Cursor = DataOffset + PixelCount;
	}

	if (Cursor != FileData.Num())
	{
		OutError = FString::Printf(TEXT("'%s' ended at byte %d while parser stopped at byte %d."), *SourceName, FileData.Num(), Cursor);
		return false;
	}

	return true;
}

bool FMaxisTextureReader::ExtractAtlasTile(
	const FMaxisTextureImage& AtlasImage,
	int32 TileIndex,
	FMaxisTextureImage& OutTileImage,
	FString& OutError,
	int32 TileSize,
	int32 ColumnCount)
{
	OutTileImage = FMaxisTextureImage();

	if (TileSize <= 0 || ColumnCount <= 0)
	{
		OutError = TEXT("Atlas tile size and column count must be positive.");
		return false;
	}

	const int32 RowCount = AtlasImage.Height / TileSize;
	const int32 TileCount = ColumnCount * RowCount;
	if (AtlasImage.Width < TileSize * ColumnCount || AtlasImage.Height < TileSize || AtlasImage.Pixels.Num() != AtlasImage.Width * AtlasImage.Height || TileIndex < 0 || TileIndex >= TileCount)
	{
		OutError = FString::Printf(
			TEXT("Cannot extract atlas tile %d from %dx%d image with tile size %d and %d columns."),
			TileIndex,
			AtlasImage.Width,
			AtlasImage.Height,
			TileSize,
			ColumnCount);
		return false;
	}

	const int32 TileColumn = TileIndex % ColumnCount;
	const int32 TileRowFromBottom = TileIndex / ColumnCount;
	const int32 SourceX = TileColumn * TileSize;
	const int32 SourceY = (RowCount - 1 - TileRowFromBottom) * TileSize;

	OutTileImage.Width = TileSize;
	OutTileImage.Height = TileSize;
	OutTileImage.Pixels.SetNumUninitialized(TileSize * TileSize);

	for (int32 Y = 0; Y < TileSize; ++Y)
	{
		const int32 SourceBase = (SourceY + Y) * AtlasImage.Width + SourceX;
		const int32 DestBase = Y * TileSize;
		for (int32 X = 0; X < TileSize; ++X)
		{
			OutTileImage.Pixels[DestBase + X] = AtlasImage.Pixels[SourceBase + X];
		}
	}

	return true;
}
