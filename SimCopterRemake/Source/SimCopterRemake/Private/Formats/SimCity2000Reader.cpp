// Copyright Epic Games, Inc. All Rights Reserved.

#include "Formats/SimCity2000Reader.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
uint16 ReadUInt16BE(const TArray<uint8>& Data, int32 Offset)
{
	return static_cast<uint16>((static_cast<uint16>(Data[Offset]) << 8) | static_cast<uint16>(Data[Offset + 1]));
}

uint32 ReadUInt32BE(const TArray<uint8>& Data, int32 Offset)
{
	return (static_cast<uint32>(Data[Offset]) << 24) |
		(static_cast<uint32>(Data[Offset + 1]) << 16) |
		(static_cast<uint32>(Data[Offset + 2]) << 8) |
		static_cast<uint32>(Data[Offset + 3]);
}

int32 ReadInt32BE(const TArray<uint8>& Data, int32 Offset)
{
	return static_cast<int32>(ReadUInt32BE(Data, Offset));
}

FString ReadFourCC(const TArray<uint8>& Data, int32 Offset)
{
	ANSICHAR Buffer[5] = {
		static_cast<ANSICHAR>(Data[Offset]),
		static_cast<ANSICHAR>(Data[Offset + 1]),
		static_cast<ANSICHAR>(Data[Offset + 2]),
		static_cast<ANSICHAR>(Data[Offset + 3]),
		'\0'
	};

	return FString(ANSI_TO_TCHAR(Buffer));
}

FString ReadPrintableAscii(const TArray<uint8>& Data, int32 Offset, int32 MaxLength)
{
	FString Result;
	Result.Reserve(MaxLength);

	for (int32 Index = 0; Index < MaxLength && Offset + Index < Data.Num(); ++Index)
	{
		const uint8 Value = Data[Offset + Index];
		if (Value == 0 || Value < 32 || Value > 126)
		{
			break;
		}

		Result.AppendChar(static_cast<TCHAR>(Value));
	}

	return Result;
}

const TArray<uint8>* FindChunkData(const FSimCity2000City& City, const FString& ChunkId)
{
	if (const FSimCity2000Chunk* Chunk = City.FindFirstChunk(ChunkId))
	{
		return &Chunk->Data;
	}

	return nullptr;
}

bool RequireChunkData(const FSimCity2000City& City, const FString& ChunkId, int32 ExpectedSize, const TArray<uint8>*& OutData, FString& OutError)
{
	OutData = FindChunkData(City, ChunkId);
	if (OutData == nullptr)
	{
		OutError = FString::Printf(TEXT("Missing required SC2 chunk '%s'."), *ChunkId);
		return false;
	}

	if (OutData->Num() != ExpectedSize)
	{
		OutError = FString::Printf(TEXT("Chunk '%s' decoded to %d bytes, expected %d."), *ChunkId, OutData->Num(), ExpectedSize);
		return false;
	}

	return true;
}

void PopulateCityMetadata(FSimCity2000City& City)
{
	if (const TArray<uint8>* CityNameData = FindChunkData(City, TEXT("CNAM")))
	{
		City.CityName = ReadPrintableAscii(*CityNameData, 1, 31);
	}

	if (City.CityName.IsEmpty())
	{
		City.CityName = FPaths::GetBaseFilename(City.SourceFile).ToUpper();
	}

	if (const TArray<uint8>* MiscData = FindChunkData(City, TEXT("MISC")))
	{
		if (MiscData->Num() >= 0x0E44)
		{
			City.Rotation = ReadInt32BE(*MiscData, 0x0008);
			City.WaterLevel = ReadInt32BE(*MiscData, 0x0E40);
		}
	}
}

bool PopulateCityTiles(FSimCity2000City& City, FString& OutError)
{
	const TArray<uint8>* AltitudeData = nullptr;
	const TArray<uint8>* TerrainData = nullptr;
	const TArray<uint8>* BuildingData = nullptr;
	const TArray<uint8>* ZoneData = nullptr;
	const TArray<uint8>* UndergroundData = nullptr;
	const TArray<uint8>* TextData = nullptr;
	const TArray<uint8>* BitData = nullptr;

	if (!RequireChunkData(City, TEXT("ALTM"), FSimCity2000City::TileCount * 2, AltitudeData, OutError) ||
		!RequireChunkData(City, TEXT("XTER"), FSimCity2000City::TileCount, TerrainData, OutError) ||
		!RequireChunkData(City, TEXT("XBLD"), FSimCity2000City::TileCount, BuildingData, OutError) ||
		!RequireChunkData(City, TEXT("XZON"), FSimCity2000City::TileCount, ZoneData, OutError) ||
		!RequireChunkData(City, TEXT("XUND"), FSimCity2000City::TileCount, UndergroundData, OutError) ||
		!RequireChunkData(City, TEXT("XTXT"), FSimCity2000City::TileCount, TextData, OutError) ||
		!RequireChunkData(City, TEXT("XBIT"), FSimCity2000City::TileCount, BitData, OutError))
	{
		return false;
	}

	City.Tiles.SetNum(FSimCity2000City::TileCount);

	for (int32 TileIndex = 0; TileIndex < FSimCity2000City::TileCount; ++TileIndex)
	{
		FSimCity2000Tile& Tile = City.Tiles[TileIndex];
		Tile.RawAltitude = ReadUInt16BE(*AltitudeData, TileIndex * 2);
		Tile.Altitude = static_cast<uint8>(Tile.RawAltitude & 0x001F);
		Tile.SecondaryAltitude = static_cast<uint8>((Tile.RawAltitude & 0x03E0) >> 5);
		Tile.Slope = static_cast<uint8>((Tile.RawAltitude & 0x7C00) >> 10);
		Tile.Terrain = (*TerrainData)[TileIndex];
		Tile.bWater = Tile.Terrain > 0x0F;
		Tile.Building = (*BuildingData)[TileIndex];
		Tile.Zone = (*ZoneData)[TileIndex];
		Tile.Underground = (*UndergroundData)[TileIndex];
		Tile.Text = (*TextData)[TileIndex];
		Tile.BitFlags = (*BitData)[TileIndex];
	}

	return true;
}
}

const FSimCity2000Chunk* FSimCity2000City::FindFirstChunk(const FString& ChunkId) const
{
	for (const FSimCity2000Chunk& Chunk : Chunks)
	{
		if (Chunk.Id.Equals(ChunkId, ESearchCase::CaseSensitive))
		{
			return &Chunk;
		}
	}

	return nullptr;
}

bool FSimCity2000City::HasChunk(const FString& ChunkId) const
{
	return FindFirstChunk(ChunkId) != nullptr;
}

bool FSimCity2000Reader::LoadCityFromFile(const FString& FilePath, FSimCity2000City& OutCity, FString& OutError)
{
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
	{
		OutError = FString::Printf(TEXT("Could not read SC2 file '%s'."), *FilePath);
		return false;
	}

	return LoadCityFromBytes(FileData, FilePath, OutCity, OutError);
}

bool FSimCity2000Reader::LoadCityFromBytes(const TArray<uint8>& FileData, const FString& SourceName, FSimCity2000City& OutCity, FString& OutError)
{
	OutCity = FSimCity2000City();
	OutCity.SourceFile = SourceName;

	if (FileData.Num() < 12)
	{
		OutError = FString::Printf(TEXT("SC2 file '%s' is too small for an IFF header."), *SourceName);
		return false;
	}

	if (!ReadFourCC(FileData, 0).Equals(TEXT("FORM"), ESearchCase::CaseSensitive) ||
		!ReadFourCC(FileData, 8).Equals(TEXT("SCDH"), ESearchCase::CaseSensitive))
	{
		OutError = FString::Printf(TEXT("SC2 file '%s' does not start with FORM/SCDH."), *SourceName);
		return false;
	}

	const uint32 DeclaredSize = ReadUInt32BE(FileData, 4);
	if (DeclaredSize + 8u != static_cast<uint32>(FileData.Num()))
	{
		OutError = FString::Printf(TEXT("SC2 file '%s' has declared size %u but actual size is %d."), *SourceName, DeclaredSize, FileData.Num());
		return false;
	}

	int32 Offset = 12;
	while (Offset < FileData.Num())
	{
		if (Offset + 8 > FileData.Num())
		{
			OutError = FString::Printf(TEXT("SC2 file '%s' has a truncated chunk header at byte %d."), *SourceName, Offset);
			return false;
		}

		const FString ChunkId = ReadFourCC(FileData, Offset);
		const uint32 StoredSize = ReadUInt32BE(FileData, Offset + 4);
		Offset += 8;

		if (StoredSize > static_cast<uint32>(FileData.Num() - Offset))
		{
			OutError = FString::Printf(TEXT("SC2 chunk '%s' in '%s' overruns the file."), *ChunkId, *SourceName);
			return false;
		}

		TArray<uint8> StoredData;
		StoredData.Append(FileData.GetData() + Offset, static_cast<int32>(StoredSize));
		Offset += static_cast<int32>(StoredSize);

		FSimCity2000Chunk Chunk;
		Chunk.Id = ChunkId;
		Chunk.StoredSize = static_cast<int32>(StoredSize);
		Chunk.bStoredUncompressed = IsChunkStoredUncompressed(ChunkId);

		if (Chunk.bStoredUncompressed)
		{
			Chunk.Data = MoveTemp(StoredData);
		}
		else if (!DecodeRleChunk(StoredData, GetExpectedDecodedSize(ChunkId), Chunk.Data, OutError))
		{
			OutError = FString::Printf(TEXT("Could not decode chunk '%s' in '%s': %s"), *ChunkId, *SourceName, *OutError);
			return false;
		}

		Chunk.DecodedSize = Chunk.Data.Num();

		const int32 ExpectedDecodedSize = GetExpectedDecodedSize(ChunkId);
		if (ExpectedDecodedSize > 0 && Chunk.DecodedSize != ExpectedDecodedSize)
		{
			OutError = FString::Printf(TEXT("Chunk '%s' in '%s' decoded to %d bytes, expected %d."), *ChunkId, *SourceName, Chunk.DecodedSize, ExpectedDecodedSize);
			return false;
		}

		OutCity.Chunks.Add(MoveTemp(Chunk));
	}

	PopulateCityMetadata(OutCity);
	return PopulateCityTiles(OutCity, OutError);
}

bool FSimCity2000Reader::DecodeRleChunk(const TArray<uint8>& CompressedData, int32 ExpectedDecodedSize, TArray<uint8>& OutDecodedData, FString& OutError)
{
	OutDecodedData.Reset();
	OutDecodedData.Reserve(ExpectedDecodedSize > 0 ? ExpectedDecodedSize : CompressedData.Num());

	int32 Offset = 0;
	while (Offset < CompressedData.Num())
	{
		const uint8 Control = CompressedData[Offset++];
		if (Control <= 127)
		{
			const int32 LiteralCount = Control;
			if (Offset + LiteralCount > CompressedData.Num())
			{
				OutError = TEXT("Literal run exceeds compressed data length.");
				return false;
			}

			OutDecodedData.Append(CompressedData.GetData() + Offset, LiteralCount);
			Offset += LiteralCount;
		}
		else if (Control >= 129)
		{
			if (Offset >= CompressedData.Num())
			{
				OutError = TEXT("Repeat run is missing its byte value.");
				return false;
			}

			const int32 RepeatCount = static_cast<int32>(Control) - 127;
			const uint8 Value = CompressedData[Offset++];
			const int32 StartIndex = OutDecodedData.AddUninitialized(RepeatCount);
			for (int32 Index = 0; Index < RepeatCount; ++Index)
			{
				OutDecodedData[StartIndex + Index] = Value;
			}
		}
		else
		{
			OutError = TEXT("RLE control byte 128 is reserved and not valid in SC2 data.");
			return false;
		}
	}

	if (ExpectedDecodedSize > 0 && OutDecodedData.Num() != ExpectedDecodedSize)
	{
		OutError = FString::Printf(TEXT("Decoded RLE size was %d bytes, expected %d."), OutDecodedData.Num(), ExpectedDecodedSize);
		return false;
	}

	return true;
}

int32 FSimCity2000Reader::GetExpectedDecodedSize(const FString& ChunkId)
{
	if (ChunkId == TEXT("CNAM"))
	{
		return 32;
	}
	if (ChunkId == TEXT("MISC"))
	{
		return 4800;
	}
	if (ChunkId == TEXT("ALTM"))
	{
		return 32768;
	}
	if (ChunkId == TEXT("XLAB"))
	{
		return 6400;
	}
	if (ChunkId == TEXT("XMIC"))
	{
		return 1200;
	}
	if (ChunkId == TEXT("XTHG"))
	{
		return 480;
	}
	if (ChunkId == TEXT("XTRF") || ChunkId == TEXT("XPLT") || ChunkId == TEXT("XVAL") || ChunkId == TEXT("XCRM"))
	{
		return 4096;
	}
	if (ChunkId == TEXT("XPLC") || ChunkId == TEXT("XFIR") || ChunkId == TEXT("XPOP") || ChunkId == TEXT("XROG"))
	{
		return 1024;
	}
	if (ChunkId == TEXT("XGRP"))
	{
		return 3328;
	}
	if (ChunkId == TEXT("XTER") || ChunkId == TEXT("XBLD") || ChunkId == TEXT("XZON") ||
		ChunkId == TEXT("XUND") || ChunkId == TEXT("XTXT") || ChunkId == TEXT("XBIT"))
	{
		return FSimCity2000City::TileCount;
	}

	return INDEX_NONE;
}

bool FSimCity2000Reader::IsChunkStoredUncompressed(const FString& ChunkId)
{
	return ChunkId == TEXT("ALTM") ||
		ChunkId == TEXT("CNAM") ||
		ChunkId == TEXT("TEXT") ||
		ChunkId == TEXT("SCEN") ||
		ChunkId == TEXT("PICT") ||
		ChunkId == TEXT("TMPL");
}
