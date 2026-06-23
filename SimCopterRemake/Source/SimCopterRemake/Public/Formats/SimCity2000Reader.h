// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FSimCity2000Chunk
{
	FString Id;
	int32 StoredSize = 0;
	int32 DecodedSize = 0;
	bool bStoredUncompressed = false;
	TArray<uint8> Data;
};

struct FSimCity2000Tile
{
	uint16 RawAltitude = 0;
	uint8 Altitude = 0;
	bool bWater = false;

	uint8 Terrain = 0;
	uint8 Building = 0;
	uint8 Zone = 0;
	uint8 Underground = 0;
	uint8 Text = 0;
	uint8 BitFlags = 0;
};

struct FSimCity2000City
{
	static constexpr int32 MapSize = 128;
	static constexpr int32 TileCount = MapSize * MapSize;

	FString SourceFile;
	FString CityName;
	int32 Rotation = 0;
	int32 WaterLevel = 0;

	TArray<FSimCity2000Chunk> Chunks;
	TArray<FSimCity2000Tile> Tiles;

	const FSimCity2000Chunk* FindFirstChunk(const FString& ChunkId) const;
	bool HasChunk(const FString& ChunkId) const;
};

class FSimCity2000Reader
{
public:
	static bool LoadCityFromFile(const FString& FilePath, FSimCity2000City& OutCity, FString& OutError);
	static bool LoadCityFromBytes(const TArray<uint8>& FileData, const FString& SourceName, FSimCity2000City& OutCity, FString& OutError);

	static bool DecodeRleChunk(const TArray<uint8>& CompressedData, int32 ExpectedDecodedSize, TArray<uint8>& OutDecodedData, FString& OutError);
	static int32 GetExpectedDecodedSize(const FString& ChunkId);
	static bool IsChunkStoredUncompressed(const FString& ChunkId);
};
