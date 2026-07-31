// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SimCopterMapArt.h"

#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "TextureResource.h"

namespace
{
uint32 ReadUInt32LE(const TArray<uint8>& Data, const int32 Offset)
{
	return static_cast<uint32>(Data[Offset]) |
		(static_cast<uint32>(Data[Offset + 1]) << 8) |
		(static_cast<uint32>(Data[Offset + 2]) << 16) |
		(static_cast<uint32>(Data[Offset + 3]) << 24);
}

int32 ReadInt32LE(const TArray<uint8>& Data, const int32 Offset)
{
	return static_cast<int32>(ReadUInt32LE(Data, Offset));
}

bool CanRead(const TArray<uint8>& Data, const int32 Offset, const int32 Size)
{
	return Offset >= 0 && Size >= 0 && Offset <= Data.Num() && Size <= Data.Num() - Offset;
}
}

FString FSimCopterMapArt::ResolveBitmapPath(const FString& OriginalGameRoot, const FString& FileName)
{
	if (OriginalGameRoot.IsEmpty() || FileName.IsEmpty())
	{
		return FString();
	}

	// Same spelling sweep the hangar art does: the shipped folder is upper case, a copy made on a
	// case-sensitive filesystem may not be.
	const TCHAR* const Folders[] = { TEXT("BMP"), TEXT("bmp") };
	const FString Names[] = { FileName, FileName.ToUpper(), FileName.ToLower() };

	for (const TCHAR* Folder : Folders)
	{
		for (const FString& Name : Names)
		{
			const FString Candidate = FPaths::Combine(OriginalGameRoot, Folder, Name);
			if (IFileManager::Get().FileExists(*Candidate))
			{
				return Candidate;
			}
		}
	}

	return FString();
}

bool FSimCopterMapArt::LoadPalette(const FString& OriginalGameRoot, TArray<FColor>& OutPalette)
{
	OutPalette.Reset();

	const FString Path = ResolveBitmapPath(OriginalGameRoot, TEXT("DASH5.BMP"));
	if (Path.IsEmpty())
	{
		return false;
	}

	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *Path))
	{
		return false;
	}

	// A plain 8-bit Windows bitmap: 14-byte file header, 40-byte info header, then 256 BGRA
	// palette entries.
	constexpr int32 PaletteOffset = 54;
	constexpr int32 PaletteEntries = 256;
	if (FileData.Num() < 30 || FileData[0] != 'B' || FileData[1] != 'M')
	{
		return false;
	}
	if (ReadInt32LE(FileData, 28) != 8 || !CanRead(FileData, PaletteOffset, PaletteEntries * 4))
	{
		return false;
	}

	OutPalette.SetNumUninitialized(PaletteEntries);
	for (int32 Index = 0; Index < PaletteEntries; ++Index)
	{
		const int32 Offset = PaletteOffset + Index * 4;
		OutPalette[Index] = FColor(FileData[Offset + 2], FileData[Offset + 1], FileData[Offset], 255);
	}

	// Index 0 is the buffer's cleared state and the icon sheets' hole; leaving it opaque would
	// paint a black rectangle over the panel art around the tile view.
	OutPalette[0].A = 0;
	return true;
}

bool FSimCopterMapArt::LoadIconSheet(
	const FString& OriginalGameRoot,
	const int32 PageIndex,
	const int32 MaxCells,
	SimCopterMap::FSimCopterMapIconSheet& OutSheet)
{
	OutSheet = SimCopterMap::FSimCopterMapIconSheet();

	const FString Path = ResolveBitmapPath(OriginalGameRoot, TEXT("SIM3D.BMP"));
	if (Path.IsEmpty())
	{
		return false;
	}

	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *Path))
	{
		return false;
	}

	// SIM3D container: {fileSize, ?, imageCount, resolutionCount}, a resolution table, then one
	// image each as {width, height, 0}, a per-row offset table, and the pixel block. FUN_0046cd20
	// hands the map the record and FUN_004a2740 reads width as the sheet stride and height as the
	// square cell size, which is why the pages are always N cells wide by one cell tall.
	if (!CanRead(FileData, 0, 16) || ReadInt32LE(FileData, 0) != FileData.Num())
	{
		return false;
	}

	const int32 ImageCount = ReadInt32LE(FileData, 8);
	const int32 ResolutionCount = ReadInt32LE(FileData, 12);
	if (ImageCount <= 0 || ResolutionCount <= 0 || PageIndex < 0 || PageIndex >= ImageCount)
	{
		return false;
	}

	int32 Cursor = 16 + ResolutionCount * 12;
	if (!CanRead(FileData, 16, ResolutionCount * 12))
	{
		return false;
	}

	for (int32 Index = 0; Index <= PageIndex; ++Index)
	{
		if (!CanRead(FileData, Cursor, 12))
		{
			return false;
		}
		const int32 Width = ReadInt32LE(FileData, Cursor);
		const int32 Height = ReadInt32LE(FileData, Cursor + 4);
		if (Width <= 0 || Height <= 0 || Width > 4096 || Height > 4096)
		{
			return false;
		}

		const int32 RowTableOffset = Cursor + 12;
		const int32 DataOffset = RowTableOffset + Height * 4;
		const int32 PixelCount = Width * Height;
		if (!CanRead(FileData, RowTableOffset, Height * 4) || !CanRead(FileData, DataOffset, PixelCount))
		{
			return false;
		}

		if (Index == PageIndex)
		{
			OutSheet.CellSize = Height;
			OutSheet.Stride = Width;
			OutSheet.CellCount = FMath::Min(MaxCells, Width / Height);
			OutSheet.Pixels.SetNumZeroed(PixelCount);
			for (int32 Row = 0; Row < Height; ++Row)
			{
				const int32 RowOffset = ReadInt32LE(FileData, RowTableOffset + Row * 4);
				if (RowOffset < 0 || RowOffset > PixelCount - Width)
				{
					return false;
				}
				// Rows stay in file order: the map blits row 0 at the top.
				FMemory::Memcpy(&OutSheet.Pixels[Row * Width], &FileData[DataOffset + RowOffset], Width);
			}
			return OutSheet.IsValid();
		}

		Cursor = DataOffset + PixelCount;
	}

	return false;
}

UTexture2D* FSimCopterMapArt::UpdateRasterTexture(
	UObject* Outer,
	UTexture2D* Existing,
	const TArray<uint8>& Pixels,
	const TArray<FColor>& Palette)
{
	using namespace SimCopterMap;

	if (Pixels.Num() != BufferWidth * BufferHeight || Palette.Num() < 256)
	{
		return Existing;
	}

	UTexture2D* Texture = Existing;
	if (Texture == nullptr)
	{
		Texture = UTexture2D::CreateTransient(BufferWidth, BufferHeight, PF_B8G8R8A8);
		if (Texture == nullptr || Texture->GetPlatformData() == nullptr || Texture->GetPlatformData()->Mips.Num() == 0)
		{
			return nullptr;
		}
		if (Outer != nullptr)
		{
			Texture->Rename(
				*MakeUniqueObjectName(Outer, UTexture2D::StaticClass(), TEXT("SimCopterMapBuffer")).ToString(),
				Outer);
		}
		Texture->CompressionSettings = TC_VectorDisplacementmap;
#if WITH_EDITORONLY_DATA
		Texture->MipGenSettings = TMGS_NoMipmaps;
#endif
		Texture->NeverStream = true;
		Texture->SRGB = true;
		// The buffer is one pixel per map pixel and is drawn magnified; the original's map has
		// hard pixel edges and filtering would turn it to mush.
		Texture->Filter = TF_Nearest;
		Texture->AddressX = TA_Clamp;
		Texture->AddressY = TA_Clamp;
	}

	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	FColor* Destination = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	if (Destination != nullptr)
	{
		for (int32 Index = 0; Index < Pixels.Num(); ++Index)
		{
			Destination[Index] = Palette[Pixels[Index]];
		}
	}
	Mip.BulkData.Unlock();
	Texture->UpdateResource();

	return Texture;
}
