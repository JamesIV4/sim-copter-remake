// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/SimCity2000CityActor.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Formats/MaxisMeshLibrary.h"
#include "Formats/MaxisMeshReader.h"
#include "Formats/MaxisTextureReader.h"
#include "Formats/SimCity2000Reader.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCity2000CityActor, Log, All);

namespace
{
FLinearColor ResolveMaxisFaceColor(const TArray<FColor>* ColorMap, uint8 FaceType, uint8 MaterialIndex, const FLinearColor& TexturedFaceFallbackColor)
{
	if (FaceType == 13 || FaceType == 18)
	{
		return TexturedFaceFallbackColor;
	}

	if (ColorMap != nullptr && ColorMap->IsValidIndex(MaterialIndex))
	{
		return FLinearColor((*ColorMap)[MaterialIndex]);
	}

	return FLinearColor::White;
}

bool IsTexturedMaxisFace(uint8 FaceType)
{
	return FaceType == 13 || FaceType == 18;
}

int32 MakeMaxisTextureKey(uint8 TextureFile, uint8 TextureNumber)
{
	return (static_cast<int32>(TextureFile) << 8) | static_cast<int32>(TextureNumber);
}

int32 GetMaxisFaceTextureKey(const FMaxisMeshFace& Face)
{
	if (Face.FaceType == 13)
	{
		return MakeMaxisTextureKey(0, Face.MaterialIndex);
	}

	if (Face.FaceType == 18)
	{
		return MakeMaxisTextureKey(Face.TextureAtlasIndex, Face.MaterialIndex);
	}

	return INDEX_NONE;
}

constexpr int32 SimCopterSkyGroundTextureFile = 20;
constexpr int32 SimCopterSkyGroundImageIndex = 4;
constexpr int32 SimCopterHighTerrainAtlasImageIndex = 13;
constexpr int32 SimCopterTerrainTextureNameIndex = 100000;
constexpr uint8 SimCopterHighTerrainTypeBase = 0x40;

struct FOriginalMeshSectionData
{
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> VertexColors;
	TArray<FProcMeshTangent> Tangents;
	int32 TriangleCount = 0;
};

struct FTileFootprint
{
	bool bShouldRender = true;
	bool bSuppressedChildTile = false;
	int32 Width = 1;
	int32 Height = 1;
};

UTexture2D* CreateTextureFromMaxisImage(const FMaxisTextureImage& Image, UObject* Outer, int32 ImageIndex)
{
	if (Image.Width <= 0 || Image.Height <= 0 || Image.Pixels.Num() != Image.Width * Image.Height)
	{
		return nullptr;
	}

	UTexture2D* Texture = UTexture2D::CreateTransient(Image.Width, Image.Height, PF_B8G8R8A8);
	if (Texture == nullptr || Texture->GetPlatformData() == nullptr || Texture->GetPlatformData()->Mips.Num() == 0)
	{
		return nullptr;
	}

	if (Outer != nullptr)
	{
		const FName TextureName = MakeUniqueObjectName(Outer, UTexture2D::StaticClass(), *FString::Printf(TEXT("SimCopterTexture_%d"), ImageIndex));
		Texture->Rename(*TextureName.ToString(), Outer);
	}

	Texture->SRGB = true;
	Texture->Filter = TF_Nearest;
	Texture->AddressX = TA_Wrap;
	Texture->AddressY = TA_Wrap;
#if WITH_EDITORONLY_DATA
	Texture->MipGenSettings = TMGS_NoMipmaps;
#endif

	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	void* TextureData = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(TextureData, Image.Pixels.GetData(), Image.Pixels.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	Texture->UpdateResource();

	return Texture;
}

bool AddOriginalTexture(
	int32 TextureKey,
	const FMaxisTextureImage& Image,
	UObject* Outer,
	TMap<int32, UTexture2D*>& TexturesByKey,
	TSet<int32>& AvailableTextureKeys,
	TArray<TObjectPtr<UTexture2D>>& TextureCache)
{
	if (TexturesByKey.Contains(TextureKey))
	{
		return true;
	}

	UTexture2D* Texture = CreateTextureFromMaxisImage(Image, Outer, TextureKey);
	if (Texture == nullptr)
	{
		return false;
	}

	TextureCache.Add(Texture);
	TexturesByKey.Add(TextureKey, Texture);
	AvailableTextureKeys.Add(TextureKey);
	return true;
}

int32 AddAtlasTiles(
	uint8 TextureFile,
	const FMaxisTextureImage& AtlasImage,
	UObject* Outer,
	TMap<int32, UTexture2D*>& TexturesByKey,
	TSet<int32>& AvailableTextureKeys,
	TArray<TObjectPtr<UTexture2D>>& TextureCache)
{
	int32 AddedCount = 0;
	const int32 RowCount = AtlasImage.Height / FMaxisTextureReader::AtlasTileSize;
	const int32 TileCount = FMaxisTextureReader::AtlasColumnCount * RowCount;

	for (int32 TileIndex = 0; TileIndex < TileCount; ++TileIndex)
	{
		FMaxisTextureImage TileImage;
		FString TileError;
		if (!FMaxisTextureReader::ExtractAtlasTile(AtlasImage, TileIndex, TileImage, TileError))
		{
			UE_LOG(LogSimCity2000CityActor, Warning, TEXT("%s"), *TileError);
			continue;
		}

		if (AddOriginalTexture(MakeMaxisTextureKey(TextureFile, static_cast<uint8>(TileIndex)), TileImage, Outer, TexturesByKey, AvailableTextureKeys, TextureCache))
		{
			++AddedCount;
		}
	}

	return AddedCount;
}

float GetWorldGridCoordinate(int32 FileCoordinate, float TileSize, float HalfMapSize)
{
	return static_cast<float>(FileCoordinate) * TileSize - HalfMapSize;
}

float GetWorldTileCenterCoordinate(float FileCoordinate, float TileSize, float HalfMapSize)
{
	return (FileCoordinate + 0.5f) * TileSize - HalfMapSize;
}

int32 GetOriginalTerrainHeightStep(const FSimCity2000Tile& Tile)
{
	const int32 BaseAltitude = static_cast<int32>(Tile.Altitude);
	const int32 SecondaryAltitude = static_cast<int32>(Tile.SecondaryAltitude);
	return (SecondaryAltitude > BaseAltitude && Tile.Terrain > 0x0F) ? SecondaryAltitude : BaseAltitude;
}

float GetTerrainSurfaceZ(const FSimCity2000Tile& Tile, float TerrainHeightScale)
{
	const int32 TunnelHeightOffset = (Tile.Terrain == 0x0D || Tile.Terrain == 0x0E) ? 1 : 0;
	return static_cast<float>(GetOriginalTerrainHeightStep(Tile) + TunnelHeightOffset + 1) * TerrainHeightScale;
}

float GetTerrainTileCenterZ(const FSimCity2000City& City, int32 FileX, int32 FileY, float TerrainHeightScale)
{
	if (FileX < 0 || FileX >= FSimCity2000City::MapSize || FileY < 0 || FileY >= FSimCity2000City::MapSize)
	{
		return 0.0f;
	}

	return GetTerrainSurfaceZ(City.Tiles[FileY * FSimCity2000City::MapSize + FileX], TerrainHeightScale);
}

float GetTerrainGridVertexZ(const FSimCity2000City& City, int32 GridX, int32 GridY, float TerrainHeightScale)
{
	// SimCopter's tmap builder seeds tile centers, then fills grid points by copying/averaging neighbors.
	float HeightSum = 0.0f;
	int32 HeightCount = 0;

	for (int32 OffsetY = -1; OffsetY <= 0; ++OffsetY)
	{
		for (int32 OffsetX = -1; OffsetX <= 0; ++OffsetX)
		{
			const int32 TileX = GridX + OffsetX;
			const int32 TileY = GridY + OffsetY;
			if (TileX >= 0 && TileX < FSimCity2000City::MapSize && TileY >= 0 && TileY < FSimCity2000City::MapSize)
			{
				HeightSum += GetTerrainTileCenterZ(City, TileX, TileY, TerrainHeightScale);
				++HeightCount;
			}
		}
	}

	return HeightCount > 0 ? HeightSum / static_cast<float>(HeightCount) : 0.0f;
}

float GetAverageTerrainSurfaceZ(const FSimCity2000City& City, int32 FileX, int32 FileY, int32 Width, int32 Height, float TerrainHeightScale)
{
	float HeightSum = 0.0f;
	int32 HeightCount = 0;
	for (int32 Y = FileY; Y < FileY + Height && Y < FSimCity2000City::MapSize; ++Y)
	{
		for (int32 X = FileX; X < FileX + Width && X < FSimCity2000City::MapSize; ++X)
		{
			HeightSum += GetTerrainTileCenterZ(City, X, Y, TerrainHeightScale);
			++HeightCount;
		}
	}

	return HeightCount > 0 ? HeightSum / static_cast<float>(HeightCount) : 0.0f;
}

FVector2D GetMaxisTerrainAtlasCellUV(int32 TileIndex, float LocalU, float LocalV)
{
	const int32 ClampedTileIndex = FMath::Clamp(TileIndex, 0, 63);
	const int32 Column = ClampedTileIndex % FMaxisTextureReader::AtlasColumnCount;
	const int32 RawRow = ClampedTileIndex / FMaxisTextureReader::AtlasColumnCount;
	const int32 DecodedTextureRow = FMaxisTextureReader::AtlasColumnCount - 1 - RawRow;
	const float CellScale = 1.0f / static_cast<float>(FMaxisTextureReader::AtlasColumnCount);
	constexpr float LocalPixelInset = 0.5f / static_cast<float>(FMaxisTextureReader::AtlasTileSize);
	const float InsetLocalU = FMath::Lerp(LocalPixelInset, 1.0f - LocalPixelInset, LocalU);
	const float InsetLocalV = FMath::Lerp(LocalPixelInset, 1.0f - LocalPixelInset, LocalV);

	// Original terrain pages are addressed in raw top-to-bottom texture memory. The composite
	// reader flips rows into Unreal's top-down texture layout, so mirror the atlas row but keep
	// local V in the original terrain orientation.
	return FVector2D(
		(static_cast<float>(Column) + InsetLocalU) * CellScale,
		(static_cast<float>(DecodedTextureRow) + InsetLocalV) * CellScale);
}

int32 GetTerrainHeightMapSample(const FSimCity2000Tile& Tile)
{
	const int32 TunnelHeightOffset = (Tile.Terrain == 0x0D || Tile.Terrain == 0x0E) ? 1 : 0;
	return (GetOriginalTerrainHeightStep(Tile) + TunnelHeightOffset + 1) * 0x20;
}

int32 GetTerrainTileCenterHeightMapSample(const FSimCity2000City& City, int32 FileX, int32 FileY)
{
	if (FileX < 0 || FileX >= FSimCity2000City::MapSize || FileY < 0 || FileY >= FSimCity2000City::MapSize)
	{
		return 0;
	}

	return GetTerrainHeightMapSample(City.Tiles[FileY * FSimCity2000City::MapSize + FileX]);
}

int32 GetTerrainGridHeightMapSample(const FSimCity2000City& City, int32 GridX, int32 GridY)
{
	int32 HeightSum = 0;
	int32 HeightCount = 0;

	for (int32 OffsetY = -1; OffsetY <= 0; ++OffsetY)
	{
		for (int32 OffsetX = -1; OffsetX <= 0; ++OffsetX)
		{
			const int32 TileX = GridX + OffsetX;
			const int32 TileY = GridY + OffsetY;
			if (TileX >= 0 && TileX < FSimCity2000City::MapSize && TileY >= 0 && TileY < FSimCity2000City::MapSize)
			{
				HeightSum += GetTerrainTileCenterHeightMapSample(City, TileX, TileY);
				++HeightCount;
			}
		}
	}

	return HeightCount > 0 ? HeightSum / HeightCount : 0;
}

int32 GetTerrainTileAverageHeightMapSample(const FSimCity2000City& City, int32 FileX, int32 FileY)
{
	return (
		GetTerrainGridHeightMapSample(City, FileX, FileY) +
		GetTerrainGridHeightMapSample(City, FileX + 1, FileY) +
		GetTerrainGridHeightMapSample(City, FileX + 1, FileY + 1) +
		GetTerrainGridHeightMapSample(City, FileX, FileY + 1)) >> 2;
}

// Reproduces the original city builder's flat-vs-sloped test (FUN_0047c0c0): a tile is
// flat when its four tmap corner heights match. GetTerrainGridHeightMapSample is the
// remake's tmap-corner equivalent - the same samples the terrain quad corners use.
bool IsOriginalTerrainTileFlat(const FSimCity2000City& City, int32 FileX, int32 FileY)
{
	const int32 Corner00 = GetTerrainGridHeightMapSample(City, FileX, FileY);
	const int32 Corner10 = GetTerrainGridHeightMapSample(City, FileX + 1, FileY);
	const int32 Corner01 = GetTerrainGridHeightMapSample(City, FileX, FileY + 1);
	const int32 Corner11 = GetTerrainGridHeightMapSample(City, FileX + 1, FileY + 1);
	return Corner00 == Corner10 && Corner00 == Corner01 && Corner00 == Corner11;
}

// Bridge tile dispatch transcribed from FUN_0047c0c0 (XBLD ids 0x3f..0x48). Each bridge
// id selects a specific globally-unique mesh object Id (resolved via
// FMaxisMeshLibrary::FindObjectByObjectId, the remake's FUN_00470571), which is what
// encodes the correct bridge piece and orientation - the heuristic XBLD->mesh table
// mis-selected these. Cases 0x43..0x46 pick a flat vs sloped variant from the tile's
// corner heights. Returns INDEX_NONE for non-bridge tiles. The small detail props the
// original layers on top (object Ids 0x2c/0x2d) are intentionally left out for now.
int32 GetOriginalBridgeObjectId(uint8 BuildingId, bool bTileIsFlat)
{
	switch (BuildingId)
	{
	case 0x3f: return 0x178;
	case 0x40: return 0x179;
	case 0x41: return 0x17a;
	case 0x42: return 0x17b;
	case 0x43: return bTileIsFlat ? 0x128 : 0x17f;
	case 0x44: return bTileIsFlat ? 0x129 : 0x180;
	case 0x45: return bTileIsFlat ? 0x3b : 0x1d;
	case 0x46: return bTileIsFlat ? 0x3c : 0x1e;
	case 0x47: return 0x17d;
	case 0x48: return 0x17e;
	default: return INDEX_NONE;
	}
}

uint32 HashTerrainTile(int32 X, int32 Y, uint32 Salt)
{
	uint32 Hash = static_cast<uint32>(X) * 0x8da6b343u;
	Hash ^= static_cast<uint32>(Y) * 0xd8163841u;
	Hash ^= Salt * 0xcb1ab31fu;
	Hash ^= Hash >> 16;
	Hash *= 0x7feb352du;
	Hash ^= Hash >> 15;
	Hash *= 0x846ca68bu;
	Hash ^= Hash >> 16;
	return Hash;
}

uint8 ResolveOriginalTerrainDetailType(uint8 TerrainType, int32 X, int32 Y)
{
	uint8 DetailBase = 0;
	switch (TerrainType)
	{
	case 0x10:
		DetailBase = 0x70;
		break;
	case 0x20:
		DetailBase = 0x73;
		break;
	case 0x30:
		DetailBase = 0x76;
		break;
	case 0x40:
		DetailBase = 0x79;
		break;
	case 0x60:
		DetailBase = 0x7C;
		break;
	default:
		return TerrainType;
	}

	// SimCopter uses clock-seeded MSVCRT rand() here, after terrain perturbation has consumed
	// earlier draws. Keep the exact candidate set and probabilities, but make editor rebuilds stable.
	const uint32 RandA = HashTerrainTile(X, Y, 0x4AD700u) & 0x7FFFu;
	if ((RandA & 1u) == 0u)
	{
		return TerrainType;
	}

	const uint32 RandB = HashTerrainTile(X, Y, 0x4AD701u ^ (RandA << 1)) & 0x7FFFu;
	return static_cast<uint8>(DetailBase + (RandB % 3u));
}

// Reproduces SimCopter's terrain texture type grid (FUN_004abce0 in SimCopter.exe).
// The renderer uses the type code through DAT_005cde90: 0x00..0x3f select page 0x14
// (TILED1.BMP), while 0x40..0x7f select page 0x0d (SIM3D.BMP image 13) with code-0x40.
TArray<uint8> BuildTerrainTextureTypeGrid(const FSimCity2000City& City)
{
	const int32 N = FSimCity2000City::MapSize;
	TArray<uint8> Grid;
	Grid.SetNumUninitialized(N * N);

	int32 MinHeight = TNumericLimits<int32>::Max();
	int32 MaxHeight = 0;
	for (const FSimCity2000Tile& Tile : City.Tiles)
	{
		const int32 Height = GetTerrainHeightMapSample(Tile);
		MinHeight = FMath::Min(MinHeight, Height);
		MaxHeight = FMath::Max(MaxHeight, Height);
	}

	MinHeight = FMath::Max(0, MinHeight - 0x32);
	MaxHeight += 100;
	const int32 HeightRange = MaxHeight - MinHeight;
	const int32 HeightBandStep = HeightRange >> 4;

	auto GetGridTypeOrDefault = [&Grid, N](int32 X, int32 Y) -> uint8
	{
		if (X < 0 || X >= N || Y < 0 || Y >= N)
		{
			return 0x30;
		}
		return Grid[Y * N + X];
	};

	auto AnyNeighbor8Is = [&Grid, N](int32 X, int32 Y, auto Predicate) -> bool
	{
		for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
		{
			for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
			{
				if (OffsetX == 0 && OffsetY == 0)
				{
					continue;
				}
				const int32 NeighborX = X + OffsetX;
				const int32 NeighborY = Y + OffsetY;
				if (NeighborX < 0 || NeighborX >= N || NeighborY < 0 || NeighborY >= N)
				{
					continue;
				}
				if (Predicate(Grid[NeighborY * N + NeighborX]))
				{
					return true;
				}
			}
		}
		return false;
	};

	auto AnyOrthogonalNeighborIs = [&Grid, N](int32 X, int32 Y, auto Predicate) -> bool
	{
		const int32 NeighborOffsets[4][2] = {{0, -1}, {0, 1}, {1, 0}, {-1, 0}};
		for (const int32* Offset : NeighborOffsets)
		{
			const int32 NeighborX = X + Offset[0];
			const int32 NeighborY = Y + Offset[1];
			if (NeighborX < 0 || NeighborX >= N || NeighborY < 0 || NeighborY >= N)
			{
				continue;
			}
			if (Predicate(Grid[NeighborY * N + NeighborX]))
			{
				return true;
			}
		}
		return false;
	};

	auto AllOrthogonalNeighborsAre = [&Grid, N](int32 X, int32 Y, auto Predicate) -> bool
	{
		const int32 NeighborOffsets[4][2] = {{0, -1}, {0, 1}, {1, 0}, {-1, 0}};
		for (const int32* Offset : NeighborOffsets)
		{
			const int32 NeighborX = X + Offset[0];
			const int32 NeighborY = Y + Offset[1];
			const uint8 NeighborType = (NeighborX < 0 || NeighborX >= N || NeighborY < 0 || NeighborY >= N)
				? 0x30
				: Grid[NeighborY * N + NeighborX];
			if (!Predicate(NeighborType))
			{
				return false;
			}
		}
		return true;
	};

	for (int32 Y = 0; Y < N; ++Y)
	{
		for (int32 X = 0; X < N; ++X)
		{
			const FSimCity2000Tile& Tile = City.Tiles[Y * N + X];
			const uint8 Building = Tile.Building;
			uint8 Type = 0x30; // inland grass/land
			if (Building == 0 || Building > 4)
			{
				if ((Building < 6 || Building > 0x0D) && Building != 0xD5 && Building != 0xDA)
				{
					if (Building == 0xF8)
					{
						Type = 0x10;
					}
					else if (Tile.Terrain > 0x0F)
					{
						Type = 5; // water
					}
					// else stays 0x30
				}
				else
				{
					Type = 0x20; // wooded/feature ground (XBLD 6..0x0D, 0xD5, 0xDA)
				}
			}
			else
			{
				Type = static_cast<uint8>(10 + (HashTerrainTile(X, Y, 0x1001u) & 1u));
			}
			Grid[Y * N + X] = Type;
		}
	}

	for (int32 Y = 1; Y < N - 1; ++Y)
	{
		for (int32 X = 1; X < N - 1; ++X)
		{
			const uint8 Type = Grid[Y * N + X];
			if ((Type == 0x30 || Type == 0x20) && AnyNeighbor8Is(X, Y, [](uint8 T) { return T == 5; }))
			{
				Grid[Y * N + X] = 0x10;
			}
		}
	}

	for (int32 Y = 1; Y < N - 1; ++Y)
	{
		for (int32 X = 1; X < N - 1; ++X)
		{
			if (Grid[Y * N + X] == 0x30 && AnyNeighbor8Is(X, Y, [](uint8 T) { return T == 0x10; }))
			{
				Grid[Y * N + X] = 0x20;
			}
		}
	}

	for (int32 Y = 0; Y < N; ++Y)
	{
		for (int32 X = 0; X < N; ++X)
		{
			if (Grid[Y * N + X] == 5 && AnyNeighbor8Is(X, Y, [](uint8 T) { return T > 9; }))
			{
				Grid[Y * N + X] = 0;
			}
		}
	}

	for (int32 Y = 0; Y < N; ++Y)
	{
		for (int32 X = 0; X < N; ++X)
		{
			const int32 AverageHeight = GetTerrainTileAverageHeightMapSample(City, X, Y);
			if (Grid[Y * N + X] == 0x30)
			{
				if (AnyOrthogonalNeighborIs(X, Y, [](uint8 T) { return T == 0; }) || AverageHeight <= MinHeight + HeightBandStep * 2)
				{
					Grid[Y * N + X] = 0x10;
				}
			}
		}
	}

	for (int32 Y = 0; Y < N; ++Y)
	{
		for (int32 X = 0; X < N; ++X)
		{
			const int32 AverageHeight = GetTerrainTileAverageHeightMapSample(City, X, Y);
			if (Grid[Y * N + X] == 0x30)
			{
				if (AnyOrthogonalNeighborIs(X, Y, [](uint8 T) { return T == 0x10; }) || AverageHeight <= MinHeight + HeightBandStep * 5)
				{
					Grid[Y * N + X] = 0x20;
				}
			}
		}
	}

	for (int32 Y = 0; Y < N; ++Y)
	{
		for (int32 X = 0; X < N; ++X)
		{
			const int32 AverageHeight = GetTerrainTileAverageHeightMapSample(City, X, Y);
			if (Grid[Y * N + X] == 0x30 &&
				AverageHeight >= MaxHeight - HeightBandStep * 6 &&
				AllOrthogonalNeighborsAre(X, Y, [](uint8 T) { return T == 0x30 || T == 0x40; }))
			{
				Grid[Y * N + X] = 0x40;
			}
		}
	}

	for (int32 Y = 0; Y < N; ++Y)
	{
		for (int32 X = 0; X < N; ++X)
		{
			const int32 AverageHeight = GetTerrainTileAverageHeightMapSample(City, X, Y);
			if (Grid[Y * N + X] == 0x40 &&
				AverageHeight >= MaxHeight - HeightBandStep * 3 &&
				AllOrthogonalNeighborsAre(X, Y, [](uint8 T) { return T == 0x40 || T == 0x50; }))
			{
				Grid[Y * N + X] = 0x50;
			}
		}
	}

	for (int32 Y = 0; Y < N; ++Y)
	{
		for (int32 X = 0; X < N; ++X)
		{
			const int32 AverageHeight = GetTerrainTileAverageHeightMapSample(City, X, Y);
			if (Grid[Y * N + X] == 0x50 &&
				AverageHeight >= MaxHeight + HeightBandStep &&
				AllOrthogonalNeighborsAre(X, Y, [](uint8 T) { return T == 0x50 || T == 0x60; }))
			{
				Grid[Y * N + X] = 0x60;
			}
		}
	}

	auto ApplyTransitionMask = [&Grid, N, &GetGridTypeOrDefault](uint8 BaseType, auto IsLowerBand)
	{
		for (int32 Y = 0; Y < N; ++Y)
		{
			for (int32 X = 0; X < N; ++X)
			{
				const int32 Index = Y * N + X;
				if (Grid[Index] != BaseType)
				{
					continue;
				}

				uint8 Mask = 0;
				// DAT_005bde80 is addressed as x * 0x100 + y in SimCopter. With our row-major
				// Grid[y * N + x], the original atlas bits correspond to N/E/S/W.
				if (IsLowerBand(GetGridTypeOrDefault(X, Y - 1)))
				{
					Mask |= 1;
				}
				if (IsLowerBand(GetGridTypeOrDefault(X + 1, Y)))
				{
					Mask |= 2;
				}
				if (IsLowerBand(GetGridTypeOrDefault(X, Y + 1)))
				{
					Mask |= 4;
				}
				if (IsLowerBand(GetGridTypeOrDefault(X - 1, Y)))
				{
					Mask |= 8;
				}
				Grid[Index] = BaseType + Mask;
			}
		}
	};

	ApplyTransitionMask(0x10, [](uint8 T) { return T < 10; });
	ApplyTransitionMask(0x20, [](uint8 T) { return T >= 0x10 && T < 0x20; });
	ApplyTransitionMask(0x30, [](uint8 T) { return T >= 0x20 && T < 0x30; });
	ApplyTransitionMask(0x40, [](uint8 T) { return T >= 0x30 && T < 0x40; });
	ApplyTransitionMask(0x50, [](uint8 T) { return T >= 0x40 && T < 0x50; });
	ApplyTransitionMask(0x60, [](uint8 T) { return T >= 0x50 && T < 0x60; });

	for (int32 Y = 0; Y < N; ++Y)
	{
		for (int32 X = 0; X < N; ++X)
		{
			const int32 Index = Y * N + X;
			Grid[Index] = ResolveOriginalTerrainDetailType(Grid[Index], X, Y);
		}
	}

	return Grid;
}

void AppendTerrainTile(
	const FSimCity2000City& City,
	int32 FileX,
	int32 FileY,
	float TileSize,
	float TerrainHeightScale,
	float HalfMapSize,
	int32 AtlasTileIndex,
	FOriginalMeshSectionData& Section)
{
	const int32 VertexStart = Section.Vertices.Num();

	// World Y (the file-Y / row axis) is negated to match the city pass's grid-to-world mapping and
	// global 180-degree yaw (see GetWorldTileCenterCoordinate usage in LoadAndRenderCity).
	const FVector V0(GetWorldGridCoordinate(FileX, TileSize, HalfMapSize), -GetWorldGridCoordinate(FileY, TileSize, HalfMapSize), GetTerrainGridVertexZ(City, FileX, FileY, TerrainHeightScale));
	const FVector V1(GetWorldGridCoordinate(FileX + 1, TileSize, HalfMapSize), -GetWorldGridCoordinate(FileY, TileSize, HalfMapSize), GetTerrainGridVertexZ(City, FileX + 1, FileY, TerrainHeightScale));
	const FVector V2(GetWorldGridCoordinate(FileX + 1, TileSize, HalfMapSize), -GetWorldGridCoordinate(FileY + 1, TileSize, HalfMapSize), GetTerrainGridVertexZ(City, FileX + 1, FileY + 1, TerrainHeightScale));
	const FVector V3(GetWorldGridCoordinate(FileX, TileSize, HalfMapSize), -GetWorldGridCoordinate(FileY + 1, TileSize, HalfMapSize), GetTerrainGridVertexZ(City, FileX, FileY + 1, TerrainHeightScale));

	Section.Vertices.Add(V0);
	Section.Vertices.Add(V1);
	Section.Vertices.Add(V2);
	Section.Vertices.Add(V3);

	Section.UVs.Add(GetMaxisTerrainAtlasCellUV(AtlasTileIndex, 0.0f, 1.0f));
	Section.UVs.Add(GetMaxisTerrainAtlasCellUV(AtlasTileIndex, 1.0f, 1.0f));
	Section.UVs.Add(GetMaxisTerrainAtlasCellUV(AtlasTileIndex, 1.0f, 0.0f));
	Section.UVs.Add(GetMaxisTerrainAtlasCellUV(AtlasTileIndex, 0.0f, 0.0f));

	// Negating world Y mirrors the quad, which reverses its winding; swap the cross-product
	// operands (and the triangle order below) so faces still wind up-facing.
	FVector Normal = FVector::CrossProduct(V2 - V0, V1 - V0).GetSafeNormal();
	if (Normal.IsNearlyZero())
	{
		Normal = FVector::UpVector;
	}
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Section.Normals.Add(Normal);
		Section.VertexColors.Add(FLinearColor::White);
		Section.Tangents.Add(FProcMeshTangent(1.0f, 0.0f, 0.0f));
	}

	Section.Triangles.Add(VertexStart);
	Section.Triangles.Add(VertexStart + 1);
	Section.Triangles.Add(VertexStart + 2);
	Section.Triangles.Add(VertexStart);
	Section.Triangles.Add(VertexStart + 2);
	Section.Triangles.Add(VertexStart + 3);
	Section.TriangleCount += 2;
}

FTileFootprint ResolveOriginalMeshFootprint(const FSimCity2000City& City, int32 FileX, int32 FileY)
{
	const int32 TileIndex = FileY * FSimCity2000City::MapSize + FileX;
	const FSimCity2000Tile& Tile = City.Tiles[TileIndex];
	FTileFootprint Footprint;

	if (Tile.Building < 0x70)
	{
		return Footprint;
	}

	const uint8 ZoneHigh = Tile.Zone & 0xF0;
	const uint8 ZoneLow = Tile.Zone & 0x0F;
	if (ZoneHigh == 0xF0)
	{
		return Footprint;
	}

	if ((Tile.Zone & 0x80) == 0)
	{
		Footprint.bShouldRender = false;
		Footprint.bSuppressedChildTile = true;
		return Footprint;
	}

	int32 Width = 1;
	for (int32 X = FileX; X < FSimCity2000City::MapSize; ++X)
	{
		const FSimCity2000Tile& Candidate = City.Tiles[FileY * FSimCity2000City::MapSize + X];
		if (Candidate.Building != Tile.Building || (Candidate.Zone & 0x0F) != ZoneLow)
		{
			break;
		}

		if ((Candidate.Zone & 0x40) != 0)
		{
			Width = X - FileX + 1;
			break;
		}
	}

	int32 Height = 1;
	for (int32 Y = FileY; Y < FSimCity2000City::MapSize; ++Y)
	{
		const FSimCity2000Tile& Candidate = City.Tiles[Y * FSimCity2000City::MapSize + FileX];
		if (Candidate.Building != Tile.Building || (Candidate.Zone & 0x0F) != ZoneLow)
		{
			break;
		}

		if ((Candidate.Zone & 0x10) != 0)
		{
			Height = Y - FileY + 1;
			break;
		}
	}

	Footprint.Width = Width;
	Footprint.Height = Height;
	return Footprint;
}

// The original SimCopter city builder (FUN_0047c0c0 in SimCopter.exe) applies NO
// per-tile rotation to road/building/bridge meshes. Each SC2 tile id is dispatched
// to a specific, pre-oriented mesh object; the engine places it at the tile position
// with mesh X->world X, mesh Z->world Y, mesh Y->up. Orientation is therefore baked
// into the chosen mesh, and correctness comes from the grid->world axis mapping.
// The only transform applied here is the global 180-degree yaw (negating the local
// X/Y) that the tile placement also folds in, so the whole city faces the same way
// as the original game.
int32 AppendMaxisMeshObject(
	const FMaxisMeshObject& MeshObject,
	const TArray<FColor>* ColorMap,
	const FVector& TileOrigin,
	float MeshUnitsPerCentimeter,
	float MeshScale,
	bool bRenderBackfaces,
	bool bUseOriginalTextures,
	const TSet<int32>& AvailableTextureKeys,
	const FLinearColor& TexturedFaceFallbackColor,
	TMap<int32, FOriginalMeshSectionData>& Sections,
	int32& OutTexturedTriangleCount)
{
	int32 AddedTriangleCount = 0;

	// Orient face normals so they point away from the object's centroid. The raw
	// Maxis winding (run through ConvertMaxisVertexToUnreal + the 180-degree yaw)
	// yields inward-facing normals for exterior faces - the same reason the
	// terrain quad has to use a reversed cross product to face up. Inward normals
	// left buildings/roads unlit by both the directional light and Lumen's
	// surface cache (dark everywhere except where Lumen's screen trace faded out
	// at the edges). The centroid is built in the same local space the vertices
	// are stored in, with TileOrigin folded in so it compares directly against
	// per-face centers below.
	FVector ObjectCenter = TileOrigin;
	if (MeshObject.Vertices.Num() > 0)
	{
		FVector LocalCenter = FVector::ZeroVector;
		for (const FMaxisMeshVertex& CentroidVertex : MeshObject.Vertices)
		{
			const FVector CentroidConverted = FMaxisMeshReader::ConvertMaxisVertexToUnreal(CentroidVertex, MeshUnitsPerCentimeter) * MeshScale;
			LocalCenter += FVector(-CentroidConverted.X, -CentroidConverted.Y, CentroidConverted.Z);
		}
		ObjectCenter += LocalCenter / static_cast<float>(MeshObject.Vertices.Num());
	}

	for (const FMaxisMeshFace& Face : MeshObject.Faces)
	{
		if (Face.VertexIndices.Num() < 3)
		{
			continue;
		}

		const int32 TextureKey = GetMaxisFaceTextureKey(Face);
		const bool bTexturedFace = bUseOriginalTextures && IsTexturedMaxisFace(Face.FaceType) && AvailableTextureKeys.Contains(TextureKey);
		const int32 SectionKey = bTexturedFace ? TextureKey : INDEX_NONE;
		FOriginalMeshSectionData& Section = Sections.FindOrAdd(SectionKey);
		const int32 FaceVertexStart = Section.Vertices.Num();
		const FLinearColor FaceColor = bTexturedFace
			? FLinearColor::White
			: ResolveMaxisFaceColor(ColorMap, Face.FaceType, Face.MaterialIndex, TexturedFaceFallbackColor);

		for (int32 FaceVertexIndex = 0; FaceVertexIndex < Face.VertexIndices.Num(); ++FaceVertexIndex)
		{
			const uint16 SourceVertexIndex = Face.VertexIndices[FaceVertexIndex];
			if (!MeshObject.Vertices.IsValidIndex(SourceVertexIndex))
			{
				continue;
			}

			const FVector ConvertedVertex = FMaxisMeshReader::ConvertMaxisVertexToUnreal(MeshObject.Vertices[SourceVertexIndex], MeshUnitsPerCentimeter) * MeshScale;
			// Global 180-degree yaw about world up (matches the negated tile-center signs above).
			const FVector LocalVertex(-ConvertedVertex.X, -ConvertedVertex.Y, ConvertedVertex.Z);
			Section.Vertices.Add(TileOrigin + LocalVertex);
			Section.UVs.Add(Face.RawUVs.IsValidIndex(FaceVertexIndex) ? FMaxisMeshReader::ConvertMaxisUVToUnreal(Face.RawUVs[FaceVertexIndex]) : FVector2D::ZeroVector);
			Section.VertexColors.Add(FaceColor);
			Section.Tangents.Add(FProcMeshTangent(1.0f, 0.0f, 0.0f));
		}

		const int32 FaceVertexCount = Section.Vertices.Num() - FaceVertexStart;
		if (FaceVertexCount < 3)
		{
			Section.Vertices.SetNum(FaceVertexStart);
			Section.UVs.SetNum(FaceVertexStart);
			Section.VertexColors.SetNum(FaceVertexStart);
			Section.Tangents.SetNum(FaceVertexStart);
			continue;
		}

		FVector FaceNormal = FVector::CrossProduct(
			Section.Vertices[FaceVertexStart + 1] - Section.Vertices[FaceVertexStart],
			Section.Vertices[FaceVertexStart + 2] - Section.Vertices[FaceVertexStart]).GetSafeNormal();

		// Flip the winding-derived normal to point outward (away from the object
		// centroid) when it came out inward, so the visible exterior is lit.
		FVector FaceCenter = FVector::ZeroVector;
		for (int32 Index = 0; Index < FaceVertexCount; ++Index)
		{
			FaceCenter += Section.Vertices[FaceVertexStart + Index];
		}
		FaceCenter /= static_cast<float>(FaceVertexCount);
		if (FVector::DotProduct(FaceNormal, FaceCenter - ObjectCenter) < 0.0f)
		{
			FaceNormal = -FaceNormal;
		}

		for (int32 Index = 0; Index < FaceVertexCount; ++Index)
		{
			Section.Normals.Add(FaceNormal);
		}

		for (int32 TriangleIndex = 1; TriangleIndex < FaceVertexCount - 1; ++TriangleIndex)
		{
			Section.Triangles.Add(FaceVertexStart);
			Section.Triangles.Add(FaceVertexStart + TriangleIndex);
			Section.Triangles.Add(FaceVertexStart + TriangleIndex + 1);
			++AddedTriangleCount;
			++Section.TriangleCount;
			if (bTexturedFace)
			{
				++OutTexturedTriangleCount;
			}

			if (bRenderBackfaces)
			{
				Section.Triangles.Add(FaceVertexStart);
				Section.Triangles.Add(FaceVertexStart + TriangleIndex + 1);
				Section.Triangles.Add(FaceVertexStart + TriangleIndex);
				++AddedTriangleCount;
				++Section.TriangleCount;
				if (bTexturedFace)
				{
					++OutTexturedTriangleCount;
				}
			}
		}
	}

	return AddedTriangleCount;
}
}

ASimCity2000CityActor::ASimCity2000CityActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	TerrainInstances = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("TerrainInstances"));
	TerrainInstances->SetupAttachment(SceneRoot);

	TerrainMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TerrainMeshComponent"));
	TerrainMeshComponent->SetupAttachment(SceneRoot);
	TerrainMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TerrainMeshComponent->SetCanEverAffectNavigation(false);
	TerrainMeshComponent->SetCastShadow(false);

	WaterInstances = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("WaterInstances"));
	WaterInstances->SetupAttachment(SceneRoot);

	RoadInstances = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("RoadInstances"));
	RoadInstances->SetupAttachment(SceneRoot);

	BuildingInstances = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("BuildingInstances"));
	BuildingInstances->SetupAttachment(SceneRoot);

	OriginalMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("OriginalMeshComponent"));
	OriginalMeshComponent->SetupAttachment(SceneRoot);
	OriginalMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OriginalMeshComponent->SetCanEverAffectNavigation(false);
	OriginalMeshComponent->SetCastShadow(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMeshFinder.Succeeded())
	{
		SharedCubeMesh = CubeMeshFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (MaterialFinder.Succeeded())
	{
		SharedBaseMaterial = MaterialFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> VertexColorMaterialFinder(TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
	if (VertexColorMaterialFinder.Succeeded())
	{
		VertexColorMaterial = VertexColorMaterialFinder.Object;
		OriginalMeshComponent->SetMaterial(0, VertexColorMaterial);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> TexturedMaterialFinder(TEXT("/Engine/EngineMaterials/EmissiveTexturedMaterial.EmissiveTexturedMaterial"));
	if (TexturedMaterialFinder.Succeeded())
	{
		TexturedMaterial = TexturedMaterialFinder.Object;
	}

	ConfigureInstanceComponent(TerrainInstances);
	ConfigureInstanceComponent(WaterInstances);
	ConfigureInstanceComponent(RoadInstances);
	ConfigureInstanceComponent(BuildingInstances);

	CityFile.FilePath = TEXT("../Reference/SimCopterOriginalGame/cities/Demo.sc2");
	OriginalGameRoot.Path = TEXT("../Reference/SimCopterOriginalGame");
}

void ASimCity2000CityActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (bLoadOnConstruction)
	{
		RebuildCity();
	}
}

void ASimCity2000CityActor::BeginPlay()
{
	Super::BeginPlay();

	if (bLoadOnBeginPlay)
	{
		RebuildCity();
	}
}

void ASimCity2000CityActor::RebuildCity()
{
	LastLoadError.Reset();
	LastLoadedCityName.Reset();
	LastOriginalMeshTileCount = 0;
	LastMissingOriginalMeshTileCount = 0;
	LastOriginalMeshTriangleCount = 0;
	LastOriginalTextureCount = 0;
	LastOriginalTexturedTriangleCount = 0;
	OriginalTextureCache.Reset();
	OriginalTextureMaterials.Reset();

	TerrainInstances->ClearInstances();
	TerrainMeshComponent->ClearAllMeshSections();
	WaterInstances->ClearInstances();
	RoadInstances->ClearInstances();
	BuildingInstances->ClearInstances();
	OriginalMeshComponent->ClearAllMeshSections();
	TerrainMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OriginalMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	ApplyComponentMaterial(TerrainInstances, TerrainColor);
	ApplyComponentMaterial(WaterInstances, WaterColor);
	ApplyComponentMaterial(RoadInstances, RoadColor);
	ApplyComponentMaterial(BuildingInstances, BuildingColor);
	if (VertexColorMaterial != nullptr)
	{
		OriginalMeshComponent->SetMaterial(0, VertexColorMaterial);
	}

	const FString ResolvedCityPath = ResolveCityPath();
	if (ResolvedCityPath.IsEmpty())
	{
		LastLoadError = TEXT("No city file is configured.");
		UE_LOG(LogSimCity2000CityActor, Warning, TEXT("%s"), *LastLoadError);
		return;
	}

	FSimCity2000City City;
	FString Error;
	if (!FSimCity2000Reader::LoadCityFromFile(ResolvedCityPath, City, Error))
	{
		LastLoadError = Error;
		UE_LOG(LogSimCity2000CityActor, Warning, TEXT("%s"), *LastLoadError);
		return;
	}

	LastLoadedCityName = City.CityName;

	FMaxisMeshLibrary MeshLibrary;
	bool bOriginalMeshLibraryLoaded = false;
	FMaxisCompositeBitmap OriginalTextures;
	TMap<int32, UTexture2D*> OriginalTexturesByKey;
	TSet<int32> AvailableOriginalTextureKeys;
	UTexture2D* TerrainTexture = nullptr;
	UTexture2D* HighTerrainTexture = nullptr;
	bool bOriginalTexturesLoaded = false;
	const bool bNeedOriginalAssetPalette = bRenderOriginalMeshes || (bRenderTerrain && bRenderOriginalTextures && TexturedMaterial != nullptr);
	if (bNeedOriginalAssetPalette)
	{
		FString MeshLibraryError;
		const FString ResolvedOriginalGameRoot = ResolveOriginalGameRoot();
		bOriginalMeshLibraryLoaded = MeshLibrary.LoadFromOriginalGameRoot(ResolvedOriginalGameRoot, MeshLibraryError);
		if (!bOriginalMeshLibraryLoaded)
		{
			UE_LOG(LogSimCity2000CityActor, Warning, TEXT("Could not load original SimCopter meshes: %s"), *MeshLibraryError);
		}
		else if (bRenderOriginalTextures && TexturedMaterial != nullptr)
		{
			const TArray<FColor>* SharedColorMap = MeshLibrary.GetSharedColorMap();
			if (SharedColorMap != nullptr)
			{
				FString TextureError;
				const FString TexturePath = FPaths::Combine(ResolvedOriginalGameRoot, TEXT("BMP/SIM3D.BMP"));
				bOriginalTexturesLoaded = FMaxisTextureReader::LoadCompositeBitmapFromFile(TexturePath, *SharedColorMap, OriginalTextures, TextureError);
				if (bOriginalTexturesLoaded)
				{
					for (int32 TextureIndex = 0; TextureIndex < OriginalTextures.Images.Num(); ++TextureIndex)
					{
						if (AddOriginalTexture(
							MakeMaxisTextureKey(0, static_cast<uint8>(TextureIndex)),
							OriginalTextures.Images[TextureIndex],
							this,
							OriginalTexturesByKey,
							AvailableOriginalTextureKeys,
							OriginalTextureCache))
						{
							++LastOriginalTextureCount;
						}

						if (OriginalTextures.Images[TextureIndex].Width == FMaxisTextureReader::AtlasTileSize * FMaxisTextureReader::AtlasColumnCount &&
							OriginalTextures.Images[TextureIndex].Height == FMaxisTextureReader::AtlasTileSize * FMaxisTextureReader::AtlasColumnCount)
						{
							LastOriginalTextureCount += AddAtlasTiles(
								static_cast<uint8>(TextureIndex),
								OriginalTextures.Images[TextureIndex],
								this,
								OriginalTexturesByKey,
								AvailableOriginalTextureKeys,
								OriginalTextureCache);
						}
					}

					if (UTexture2D* const* LoadedHighTerrainTexture = OriginalTexturesByKey.Find(MakeMaxisTextureKey(0, SimCopterHighTerrainAtlasImageIndex)))
					{
						HighTerrainTexture = *LoadedHighTerrainTexture;
					}

					FMaxisCompositeBitmap SkyTextures;
					FString SkyTextureError;
					const FString SkyTexturePath = FPaths::Combine(ResolvedOriginalGameRoot, TEXT("BMP/SKY.BMP"));
					if (FMaxisTextureReader::LoadCompositeBitmapFromFile(SkyTexturePath, *SharedColorMap, SkyTextures, SkyTextureError))
					{
						const FMaxisTextureImage* SkyGroundAtlas = SkyTextures.FindImage(SimCopterSkyGroundImageIndex);
						if (SkyGroundAtlas != nullptr)
						{
							LastOriginalTextureCount += AddAtlasTiles(
								SimCopterSkyGroundTextureFile,
								*SkyGroundAtlas,
								this,
								OriginalTexturesByKey,
								AvailableOriginalTextureKeys,
								OriginalTextureCache);
						}
					}
					else
					{
						UE_LOG(LogSimCity2000CityActor, Warning, TEXT("Could not load original SimCopter sky/ground atlas: %s"), *SkyTextureError);
					}

					FMaxisCompositeBitmap TerrainTextures;
					FString TerrainTextureError;
					const FString TerrainTexturePath = FPaths::Combine(ResolvedOriginalGameRoot, TEXT("BMP/TILED1.BMP"));
					if (FMaxisTextureReader::LoadCompositeBitmapFromFile(TerrainTexturePath, *SharedColorMap, TerrainTextures, TerrainTextureError))
					{
						const FMaxisTextureImage* TerrainImage = TerrainTextures.FindImage(0);
						if (TerrainImage != nullptr)
						{
							TerrainTexture = CreateTextureFromMaxisImage(*TerrainImage, this, SimCopterTerrainTextureNameIndex);
							if (TerrainTexture != nullptr)
							{
								OriginalTextureCache.Add(TerrainTexture);
								++LastOriginalTextureCount;
							}
						}
					}
					else
					{
						UE_LOG(LogSimCity2000CityActor, Warning, TEXT("Could not load original SimCopter terrain atlas: %s"), *TerrainTextureError);
					}
				}
				else
				{
					UE_LOG(LogSimCity2000CityActor, Warning, TEXT("Could not load original SimCopter textures: %s"), *TextureError);
				}
			}
		}
	}

	TerrainInstances->PreAllocateInstancesMemory(FSimCity2000City::TileCount);
	WaterInstances->PreAllocateInstancesMemory(FSimCity2000City::TileCount / 4);
	RoadInstances->PreAllocateInstancesMemory(FSimCity2000City::TileCount / 4);
	BuildingInstances->PreAllocateInstancesMemory(FSimCity2000City::TileCount / 2);

	const float HalfMapSize = FSimCity2000City::MapSize * TileSize * 0.5f;
	const float CubeToUnrealScale = 1.0f / 100.0f;
	const float OriginalMeshScale = OriginalMeshSourceTileSize > 0.0f ? TileSize / OriginalMeshSourceTileSize : 1.0f;
	const float EffectiveTerrainHeightScale = bUseOriginalTerrainHeightScale ? TileSize * 0.5f : TerrainHeightScale;

	FOriginalMeshSectionData TerrainPage14Section;
	FOriginalMeshSectionData TerrainPage0DSection;
	TMap<int32, FOriginalMeshSectionData> OriginalMeshSections;
	const bool bUseTexturedTerrainSurface = (TerrainTexture != nullptr || HighTerrainTexture != nullptr) && TexturedMaterial != nullptr;
	const bool bUseHighTerrainAtlas = HighTerrainTexture != nullptr && TexturedMaterial != nullptr;

	// SimCopter selects each ground tile's TILED1 atlas cell from a per-tile terrain type code
	// (the type IS the cell index). Reproduce that grid once for the whole map.
	const TArray<uint8> TerrainTypeGrid = BuildTerrainTextureTypeGrid(City);

	int32 TerrainCount = 0;
	int32 WaterCount = 0;
	int32 RoadCount = 0;
	int32 BuildingCount = 0;
	int32 OriginalMeshTriangleCount = 0;

	for (int32 FileY = 0; FileY < FSimCity2000City::MapSize; ++FileY)
	{
		for (int32 FileX = 0; FileX < FSimCity2000City::MapSize; ++FileX)
		{
			const int32 TileIndex = FileY * FSimCity2000City::MapSize + FileX;
			const FSimCity2000Tile& Tile = City.Tiles[TileIndex];

			// Grid-to-world mapping reproduced from SimCopter's city builder (FUN_0047c0c0):
			// the game negates the column axis (world Y = (127.5 - col) * tile), which removes the
			// reflection that no rotation could fix. On top of that we apply a global 180-degree yaw
			// (negate world X and Y, plus the mesh-local X/Y in AppendMaxisMeshObject) so the city's
			// absolute orientation matches the original game. The net per-tile signs below already
			// fold both steps together.
			const float WorldX = GetWorldTileCenterCoordinate(static_cast<float>(FileX), TileSize, HalfMapSize);
			const float WorldY = -GetWorldTileCenterCoordinate(static_cast<float>(FileY), TileSize, HalfMapSize);
			const float TerrainTopZ = GetTerrainTileCenterZ(City, FileX, FileY, EffectiveTerrainHeightScale);
			const bool bRoadLikeTile = IsRoadLikeTile(Tile.Building);
			const bool bBuildingLikeTile = IsBuildingLikeTile(Tile.Building);
			bool bRenderedOriginalMesh = false;
			bool bSuppressPlaceholderForFootprintChild = false;

			if (bRenderTerrain)
			{
				const uint8 TerrainType = TerrainTypeGrid[TileIndex];
				const bool bUseHighPageForTile = TerrainType >= SimCopterHighTerrainTypeBase && bUseHighTerrainAtlas;
				const int32 TerrainAtlasTileIndex = bUseHighPageForTile
					? static_cast<int32>(TerrainType - SimCopterHighTerrainTypeBase)
					: static_cast<int32>(TerrainType & 0x3f);
				AppendTerrainTile(
					City,
					FileX,
					FileY,
					TileSize,
					EffectiveTerrainHeightScale,
					HalfMapSize,
					TerrainAtlasTileIndex,
					bUseHighPageForTile ? TerrainPage0DSection : TerrainPage14Section);
				++TerrainCount;
			}

			if (bRenderWater && Tile.bWater && !bUseTexturedTerrainSurface)
			{
				const float WaterThickness = FMath::Max(RoadPlateHeight, 6.0f);
				const FVector WaterScale(TileSize * CubeToUnrealScale, TileSize * CubeToUnrealScale, WaterThickness * CubeToUnrealScale);
				const FTransform WaterTransform(FRotator::ZeroRotator, FVector(WorldX, WorldY, TerrainTopZ + WaterThickness * 0.5f + 1.0f), WaterScale);
				WaterInstances->AddInstance(WaterTransform);
				++WaterCount;
			}

			if (bRenderOriginalMeshes && bOriginalMeshLibraryLoaded && Tile.Building > 0 && (bRoadLikeTile || bBuildingLikeTile))
			{
				const FTileFootprint Footprint = ResolveOriginalMeshFootprint(City, FileX, FileY);
				if (!Footprint.bShouldRender)
				{
					bSuppressPlaceholderForFootprintChild = Footprint.bSuppressedChildTile;
				}
				else
				{
					const TArray<FColor>* ColorMap = nullptr;
					// Bridges (XBLD 0x3f..0x48) are dispatched by the original builder to specific
					// object Ids rather than through the heuristic XBLD->mesh table, so route them
					// through the exact object-Id lookup. Everything else keeps the table mapping.
					const int32 BridgeObjectId = GetOriginalBridgeObjectId(Tile.Building, IsOriginalTerrainTileFlat(City, FileX, FileY));
					const FMaxisMeshObject* MeshObject = (BridgeObjectId != INDEX_NONE)
						? MeshLibrary.FindObjectByObjectId(BridgeObjectId, &ColorMap)
						: MeshLibrary.FindObjectByTileId(Tile.Building, &ColorMap);
					if (MeshObject != nullptr)
					{
						const float MeshWorldX = GetWorldTileCenterCoordinate(static_cast<float>(FileX) + (static_cast<float>(Footprint.Width) - 1.0f) * 0.5f, TileSize, HalfMapSize);
						const float MeshWorldY = -GetWorldTileCenterCoordinate(static_cast<float>(FileY) + (static_cast<float>(Footprint.Height) - 1.0f) * 0.5f, TileSize, HalfMapSize);
						const float MeshTerrainTopZ = GetAverageTerrainSurfaceZ(City, FileX, FileY, Footprint.Width, Footprint.Height, EffectiveTerrainHeightScale);
						const FVector TileOrigin(MeshWorldX, MeshWorldY, MeshTerrainTopZ + OriginalMeshZOffset);
						OriginalMeshTriangleCount += AppendMaxisMeshObject(
							*MeshObject,
							ColorMap,
							TileOrigin,
							OriginalMeshUnitsPerCentimeter,
							OriginalMeshScale,
							bRenderOriginalMeshBackfaces,
							bOriginalTexturesLoaded,
							AvailableOriginalTextureKeys,
							OriginalTexturedFaceFallbackColor,
							OriginalMeshSections,
							LastOriginalTexturedTriangleCount);
						bRenderedOriginalMesh = true;
						++LastOriginalMeshTileCount;
					}
					else
					{
						++LastMissingOriginalMeshTileCount;
					}
				}
			}

			const bool bOriginalMeshAttemptedForThisTile = bRenderOriginalMeshes && (bRoadLikeTile || bBuildingLikeTile);
			const bool bRenderPlaceholderForThisTile = !bOriginalMeshAttemptedForThisTile || (!bSuppressPlaceholderForFootprintChild && !bRenderedOriginalMesh && bRenderPlaceholderForMissingOriginalMeshes);
			if (bRenderRoads && bRoadLikeTile && bRenderPlaceholderForThisTile)
			{
				const FVector RoadScale(TileSize * 0.92f * CubeToUnrealScale, TileSize * 0.92f * CubeToUnrealScale, RoadPlateHeight * CubeToUnrealScale);
				const FTransform RoadTransform(FRotator::ZeroRotator, FVector(WorldX, WorldY, TerrainTopZ + RoadPlateHeight * 0.5f + 2.0f), RoadScale);
				RoadInstances->AddInstance(RoadTransform);
				++RoadCount;
			}

			if (bRenderBuildings && bBuildingLikeTile && bRenderPlaceholderForThisTile)
			{
				const float BuildingHeight = EstimateBuildingFloors(Tile.Building) * BuildingHeightScale;
				const FVector BuildingScale(TileSize * 0.82f * CubeToUnrealScale, TileSize * 0.82f * CubeToUnrealScale, BuildingHeight * CubeToUnrealScale);
				const FTransform BuildingTransform(FRotator::ZeroRotator, FVector(WorldX, WorldY, TerrainTopZ + BuildingHeight * 0.5f + 4.0f), BuildingScale);
				BuildingInstances->AddInstance(BuildingTransform);
				++BuildingCount;
			}
		}
	}

	int32 TerrainMeshSectionIndex = 0;
	auto CreateTerrainSurfaceSection = [&](const FOriginalMeshSectionData& TerrainSection, UTexture2D* SurfaceTexture)
	{
		if (TerrainSection.Vertices.Num() == 0)
		{
			return;
		}

		const int32 SectionIndex = TerrainMeshSectionIndex++;
		TerrainMeshComponent->CreateMeshSection_LinearColor(
			SectionIndex,
			TerrainSection.Vertices,
			TerrainSection.Triangles,
			TerrainSection.Normals,
			TerrainSection.UVs,
			TerrainSection.VertexColors,
			TerrainSection.Tangents,
			false);

		if (SurfaceTexture != nullptr && TexturedMaterial != nullptr)
		{
			UMaterialInstanceDynamic* TerrainMaterial = UMaterialInstanceDynamic::Create(TexturedMaterial, this);
			if (TerrainMaterial != nullptr)
			{
				TerrainMaterial->SetTextureParameterValue(TEXT("Texture"), SurfaceTexture);
				OriginalTextureMaterials.Add(TerrainMaterial);
				TerrainMeshComponent->SetMaterial(SectionIndex, TerrainMaterial);
			}
		}
		else if (VertexColorMaterial != nullptr)
		{
			TerrainMeshComponent->SetMaterial(SectionIndex, VertexColorMaterial);
		}
	};

	CreateTerrainSurfaceSection(TerrainPage14Section, TerrainTexture);
	CreateTerrainSurfaceSection(TerrainPage0DSection, HighTerrainTexture);

	int32 MeshSectionIndex = 0;
	if (const FOriginalMeshSectionData* PaletteSection = OriginalMeshSections.Find(INDEX_NONE))
	{
		if (PaletteSection->Vertices.Num() > 0)
		{
			OriginalMeshComponent->CreateMeshSection_LinearColor(
				MeshSectionIndex,
				PaletteSection->Vertices,
				PaletteSection->Triangles,
				PaletteSection->Normals,
				PaletteSection->UVs,
				PaletteSection->VertexColors,
				PaletteSection->Tangents,
				false);
			if (VertexColorMaterial != nullptr)
			{
				OriginalMeshComponent->SetMaterial(MeshSectionIndex, VertexColorMaterial);
			}
			++MeshSectionIndex;
		}
	}

	TArray<int32> TextureSectionKeys;
	OriginalMeshSections.GetKeys(TextureSectionKeys);
	TextureSectionKeys.Remove(INDEX_NONE);
	TextureSectionKeys.Sort();

	for (const int32 TextureKey : TextureSectionKeys)
	{
		const FOriginalMeshSectionData* TextureSection = OriginalMeshSections.Find(TextureKey);
		UTexture2D* const* Texture = OriginalTexturesByKey.Find(TextureKey);
		if (TextureSection == nullptr || TextureSection->Vertices.Num() == 0 || Texture == nullptr || *Texture == nullptr || TexturedMaterial == nullptr)
		{
			continue;
		}

		OriginalMeshComponent->CreateMeshSection_LinearColor(
			MeshSectionIndex,
			TextureSection->Vertices,
			TextureSection->Triangles,
			TextureSection->Normals,
			TextureSection->UVs,
			TextureSection->VertexColors,
			TextureSection->Tangents,
			false);

		UMaterialInstanceDynamic* TextureMaterial = UMaterialInstanceDynamic::Create(TexturedMaterial, this);
		if (TextureMaterial != nullptr)
		{
			TextureMaterial->SetTextureParameterValue(TEXT("Texture"), *Texture);
			OriginalTextureMaterials.Add(TextureMaterial);
			OriginalMeshComponent->SetMaterial(MeshSectionIndex, TextureMaterial);
		}

		++MeshSectionIndex;
	}
	LastOriginalMeshTriangleCount = OriginalMeshTriangleCount;

	UE_LOG(
		LogSimCity2000CityActor,
		Display,
		TEXT("Rendered SC2 city '%s' from '%s': terrain=%d water=%d roads=%d buildings=%d originalMeshTiles=%d missingOriginalMeshTiles=%d originalTriangles=%d texturedTriangles=%d originalTextures=%d chunks=%d rotation=%d waterLevel=%d terrainHeightScale=%.2f"),
		*City.CityName,
		*ResolvedCityPath,
		TerrainCount,
		WaterCount,
		RoadCount,
		BuildingCount,
		LastOriginalMeshTileCount,
		LastMissingOriginalMeshTileCount,
		LastOriginalMeshTriangleCount,
		LastOriginalTexturedTriangleCount,
		LastOriginalTextureCount,
		City.Chunks.Num(),
		City.Rotation,
		City.WaterLevel,
		EffectiveTerrainHeightScale);
}

FString ASimCity2000CityActor::ResolveCityPath() const
{
	const FString ConfiguredPath = CityFile.FilePath.TrimStartAndEnd();
	if (ConfiguredPath.IsEmpty())
	{
		return FString();
	}

	if (FPaths::IsRelative(ConfiguredPath))
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), ConfiguredPath));
	}

	return FPaths::ConvertRelativePathToFull(ConfiguredPath);
}

FString ASimCity2000CityActor::ResolveOriginalGameRoot() const
{
	const FString ConfiguredPath = OriginalGameRoot.Path.TrimStartAndEnd();
	if (ConfiguredPath.IsEmpty())
	{
		return FString();
	}

	if (FPaths::IsRelative(ConfiguredPath))
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), ConfiguredPath));
	}

	return FPaths::ConvertRelativePathToFull(ConfiguredPath);
}

void ASimCity2000CityActor::ConfigureInstanceComponent(UHierarchicalInstancedStaticMeshComponent* Component) const
{
	if (Component == nullptr)
	{
		return;
	}

	Component->SetStaticMesh(SharedCubeMesh);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetCanEverAffectNavigation(false);
	Component->SetCastShadow(false);
}

void ASimCity2000CityActor::ApplyComponentMaterial(UHierarchicalInstancedStaticMeshComponent* Component, const FLinearColor& Color)
{
	if (Component == nullptr || SharedBaseMaterial == nullptr)
	{
		return;
	}

	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(SharedBaseMaterial, this);
	if (Material == nullptr)
	{
		return;
	}

	Material->SetVectorParameterValue(TEXT("Color"), Color);
	Material->SetVectorParameterValue(TEXT("BaseColor"), Color);
	Component->SetMaterial(0, Material);
}

bool ASimCity2000CityActor::IsRoadLikeTile(uint8 BuildingId)
{
	return BuildingId >= 0x0E && BuildingId <= 0x6F;
}

bool ASimCity2000CityActor::IsBuildingLikeTile(uint8 BuildingId)
{
	return BuildingId >= 0x70;
}

float ASimCity2000CityActor::EstimateBuildingFloors(uint8 BuildingId)
{
	if (BuildingId >= 0xFB)
	{
		return 16.0f;
	}

	if (BuildingId >= 0xC9 && BuildingId <= 0xCF)
	{
		return 5.0f;
	}

	if (BuildingId >= 0xD0)
	{
		return 3.5f;
	}

	if (BuildingId >= 0xAE && BuildingId <= 0xC5)
	{
		return 6.0f;
	}

	if (BuildingId >= 0x8C && BuildingId <= 0xAD)
	{
		return 3.5f;
	}

	return 1.8f;
}
