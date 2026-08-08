// Copyright Epic Games, Inc. All Rights Reserved.

#include "Formats/MaxisWindowsBitmapReader.h"

#include "Misc/FileHelper.h"

namespace
{
bool BmpCanRead(const TArray<uint8>& Data, int32 Offset, int32 Size)
{
	return Offset >= 0 && Size >= 0 && Offset <= Data.Num() && Size <= Data.Num() - Offset;
}

uint16 BmpReadUInt16LE(const TArray<uint8>& Data, int32 Offset)
{
	return static_cast<uint16>(Data[Offset]) |
		(static_cast<uint16>(Data[Offset + 1]) << 8);
}

uint32 BmpReadUInt32LE(const TArray<uint8>& Data, int32 Offset)
{
	return static_cast<uint32>(Data[Offset]) |
		(static_cast<uint32>(Data[Offset + 1]) << 8) |
		(static_cast<uint32>(Data[Offset + 2]) << 16) |
		(static_cast<uint32>(Data[Offset + 3]) << 24);
}

int32 BmpReadInt32LE(const TArray<uint8>& Data, int32 Offset)
{
	return static_cast<int32>(BmpReadUInt32LE(Data, Offset));
}
}

bool FMaxisWindowsBitmapReader::LoadPalettedBitmapFromFile(
	const FString& FilePath,
	FMaxisTextureImage& OutImage,
	FString& OutError,
	int32 TransparentPaletteIndex)
{
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
	{
		OutError = FString::Printf(TEXT("Could not read Windows bitmap file '%s'."), *FilePath);
		return false;
	}

	return LoadPalettedBitmapFromBytes(FileData, FilePath, OutImage, OutError, TransparentPaletteIndex);
}

bool FMaxisWindowsBitmapReader::LoadPalettedBitmapFromBytes(
	const TArray<uint8>& FileData,
	const FString& SourceName,
	FMaxisTextureImage& OutImage,
	FString& OutError,
	int32 TransparentPaletteIndex)
{
	OutImage = FMaxisTextureImage();

	if (!BmpCanRead(FileData, 0, 54))
	{
		OutError = FString::Printf(TEXT("'%s' is too small to be a Windows bitmap."), *SourceName);
		return false;
	}

	if (FileData[0] != 'B' || FileData[1] != 'M')
	{
		OutError = FString::Printf(TEXT("'%s' does not start with a Windows bitmap BM signature."), *SourceName);
		return false;
	}

	const int32 DeclaredFileSize = BmpReadInt32LE(FileData, 2);
	const int32 PixelOffset = BmpReadInt32LE(FileData, 10);
	const int32 DibHeaderSize = BmpReadInt32LE(FileData, 14);
	if (DeclaredFileSize != FileData.Num() || PixelOffset <= 0 || DibHeaderSize < 40 || !BmpCanRead(FileData, 14, DibHeaderSize))
	{
		OutError = FString::Printf(TEXT("'%s' has an invalid Windows bitmap header."), *SourceName);
		return false;
	}

	const int32 Width = BmpReadInt32LE(FileData, 18);
	const int32 SignedHeight = BmpReadInt32LE(FileData, 22);
	const uint16 Planes = BmpReadUInt16LE(FileData, 26);
	const uint16 BitsPerPixel = BmpReadUInt16LE(FileData, 28);
	const uint32 Compression = BmpReadUInt32LE(FileData, 30);
	const int32 ColorsUsed = BmpReadInt32LE(FileData, 46);
	if (Width <= 0 || SignedHeight == 0 || FMath::Abs(SignedHeight) > 4096 || Width > 4096 || Planes != 1 || BitsPerPixel != 8 || Compression != 0)
	{
		OutError = FString::Printf(
			TEXT("'%s' is not an uncompressed 8-bit paletted Windows bitmap: width=%d height=%d planes=%u bpp=%u compression=%u."),
			*SourceName,
			Width,
			SignedHeight,
			Planes,
			BitsPerPixel,
			Compression);
		return false;
	}

	const int32 Height = FMath::Abs(SignedHeight);
	const bool bBottomUp = SignedHeight > 0;
	const int32 PaletteCount = ColorsUsed > 0 ? ColorsUsed : 256;
	const int32 PaletteOffset = 14 + DibHeaderSize;
	if (PaletteCount <= 0 || PaletteCount > 256 || !BmpCanRead(FileData, PaletteOffset, PaletteCount * 4))
	{
		OutError = FString::Printf(TEXT("'%s' has an invalid 8-bit palette."), *SourceName);
		return false;
	}

	const int32 RowStride = ((Width + 3) / 4) * 4;
	if (!BmpCanRead(FileData, PixelOffset, RowStride * Height))
	{
		OutError = FString::Printf(TEXT("'%s' pixel data is outside the file."), *SourceName);
		return false;
	}

	TArray<FColor> Palette;
	Palette.SetNumUninitialized(PaletteCount);
	for (int32 PaletteIndex = 0; PaletteIndex < PaletteCount; ++PaletteIndex)
	{
		const int32 Offset = PaletteOffset + PaletteIndex * 4;
		FColor Color(FileData[Offset + 2], FileData[Offset + 1], FileData[Offset], 255);
		if (PaletteIndex == TransparentPaletteIndex)
		{
			// Discard the chroma key's RGB too. Retaining cyan under zero alpha lets filtered UI
			// samples bleed the key colour back around a sprite's opaque silhouette.
			Color = FColor::Transparent;
		}
		Palette[PaletteIndex] = Color;
	}

	OutImage.Width = Width;
	OutImage.Height = Height;
	OutImage.Pixels.SetNumUninitialized(Width * Height);
	for (int32 FileRow = 0; FileRow < Height; ++FileRow)
	{
		const int32 DestRow = bBottomUp ? Height - 1 - FileRow : FileRow;
		const int32 SourceBase = PixelOffset + FileRow * RowStride;
		const int32 DestBase = DestRow * Width;
		for (int32 X = 0; X < Width; ++X)
		{
			const uint8 PaletteIndex = FileData[SourceBase + X];
			OutImage.Pixels[DestBase + X] = Palette.IsValidIndex(PaletteIndex) ? Palette[PaletteIndex] : FColor::Transparent;
		}
	}

	return true;
}
