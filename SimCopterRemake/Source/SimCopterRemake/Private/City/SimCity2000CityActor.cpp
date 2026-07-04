// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/SimCity2000CityActor.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Formats/MaxisMeshLibrary.h"
#include "Formats/MaxisMeshReader.h"
#include "Formats/MaxisTextureReader.h"
#include "Formats/SimCopterPeopleCityRules.h"
#include "Formats/SimCity2000Reader.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
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
constexpr int32 BakedAtlasPageSectionKeyFlag = 0x10000;
constexpr int32 BakedDirectImageSectionKeyFlag = 0x20000;

struct FOriginalMeshSectionData
{
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	// Page-atlas mode only: per-vertex cell column/row, fed to M_SimCopterCityAtlas's TexCoord1
	// so one full atlas page can be sampled instead of per-cell textures. Empty in legacy mode.
	TArray<FVector2D> UV1;
	TArray<FLinearColor> VertexColors;
	TArray<FProcMeshTangent> Tangents;
	int32 TriangleCount = 0;
};

struct FBakedCityAtlasMaterials
{
	TMap<int32, UMaterialInterface*> PageMaterials;
	TMap<int32, UMaterialInterface*> DirectImageMaterials;
	UMaterialInterface* TerrainLowMaterial = nullptr;
	UMaterialInterface* TerrainHighMaterial = nullptr;

	int32 NumLoadedMaterials() const
	{
		return PageMaterials.Num() + DirectImageMaterials.Num() + (TerrainLowMaterial != nullptr ? 1 : 0) + (TerrainHighMaterial != nullptr ? 1 : 0);
	}
};

struct FTileFootprint
{
	bool bShouldRender = true;
	bool bSuppressedChildTile = false;
	int32 Width = 1;
	int32 Height = 1;
};

enum class ERoadOpening : uint8
{
	North = 1 << 0,
	East = 1 << 1,
	South = 1 << 2,
	West = 1 << 3,
};

struct FOriginalBridgeDispatch
{
	int32 PrimaryObjectId = INDEX_NONE;
	int32 SecondaryObjectId = INDEX_NONE;
};

struct FOriginalCityObjectDispatch
{
	int32 PrimaryObjectId = INDEX_NONE;
	int32 SecondaryObjectId = INDEX_NONE;
};

struct FExtendedTerrainData
{
	int32 ExtensionTiles = 0;
	int32 MinTileCoordinate = 0;
	int32 TileGridSize = 0;
	TArray<float> GridVertexZ;
	TArray<uint8> TerrainTypes;
	TArray<uint8> WaterMask;

	bool IsEnabled() const
	{
		return ExtensionTiles > 0 && TileGridSize > FSimCity2000City::MapSize;
	}

	bool IsOriginalMapTile(int32 FileX, int32 FileY) const
	{
		return FileX >= 0 && FileX < FSimCity2000City::MapSize && FileY >= 0 && FileY < FSimCity2000City::MapSize;
	}

	bool ContainsTile(int32 FileX, int32 FileY) const
	{
		return FileX >= MinTileCoordinate && FileX < MinTileCoordinate + TileGridSize &&
			FileY >= MinTileCoordinate && FileY < MinTileCoordinate + TileGridSize;
	}

	bool ContainsGridVertex(int32 GridX, int32 GridY) const
	{
		return GridX >= MinTileCoordinate && GridX <= MinTileCoordinate + TileGridSize &&
			GridY >= MinTileCoordinate && GridY <= MinTileCoordinate + TileGridSize;
	}

	int32 GetTileIndex(int32 FileX, int32 FileY) const
	{
		return (FileY - MinTileCoordinate) * TileGridSize + (FileX - MinTileCoordinate);
	}

	int32 GetGridVertexIndex(int32 GridX, int32 GridY) const
	{
		const int32 GridSize = TileGridSize + 1;
		return (GridY - MinTileCoordinate) * GridSize + (GridX - MinTileCoordinate);
	}

	uint8 GetTerrainType(int32 FileX, int32 FileY) const
	{
		return ContainsTile(FileX, FileY) ? TerrainTypes[GetTileIndex(FileX, FileY)] : 0x30;
	}

	bool IsWaterTile(int32 FileX, int32 FileY) const
	{
		return ContainsTile(FileX, FileY) && WaterMask[GetTileIndex(FileX, FileY)] != 0;
	}

	float GetGridVertexZ(int32 GridX, int32 GridY) const
	{
		return ContainsGridVertex(GridX, GridY) ? GridVertexZ[GetGridVertexIndex(GridX, GridY)] : 0.0f;
	}
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

int32 MakeBakedAtlasPageSectionKey(uint8 TextureFile)
{
	return BakedAtlasPageSectionKeyFlag | static_cast<int32>(TextureFile);
}

int32 MakeBakedDirectImageSectionKey(uint8 ImageIndex)
{
	return BakedDirectImageSectionKeyFlag | static_cast<int32>(ImageIndex);
}

bool IsBakedAtlasPageSectionKey(int32 SectionKey)
{
	return (SectionKey & BakedAtlasPageSectionKeyFlag) != 0;
}

bool IsBakedDirectImageSectionKey(int32 SectionKey)
{
	return (SectionKey & BakedDirectImageSectionKeyFlag) != 0;
}

int32 GetBakedSectionAssetIndex(int32 SectionKey)
{
	return SectionKey & 0xff;
}

UMaterialInterface* LoadGeneratedCityAtlasMaterial(const FString& AssetName)
{
	const FString PackagePath = FString::Printf(TEXT("/Game/Generated/CityAtlas/%s"), *AssetName);
	if (!FPackageName::DoesPackageExist(PackagePath))
	{
		return nullptr;
	}

	const FString ObjectPath = PackagePath + TEXT(".") + AssetName;
	return LoadObject<UMaterialInterface>(nullptr, *ObjectPath);
}

FBakedCityAtlasMaterials LoadBakedCityAtlasMaterials()
{
	FBakedCityAtlasMaterials Materials;

	for (int32 AssetIndex = 0; AssetIndex <= 255; ++AssetIndex)
	{
		if (UMaterialInterface* PageMaterial = LoadGeneratedCityAtlasMaterial(FString::Printf(TEXT("MI_CityPage_%d"), AssetIndex)))
		{
			Materials.PageMaterials.Add(AssetIndex, PageMaterial);
		}

		if (UMaterialInterface* DirectImageMaterial = LoadGeneratedCityAtlasMaterial(FString::Printf(TEXT("MI_CityImage_%d"), AssetIndex)))
		{
			Materials.DirectImageMaterials.Add(AssetIndex, DirectImageMaterial);
		}
	}

	Materials.TerrainLowMaterial = LoadGeneratedCityAtlasMaterial(TEXT("MI_TerrainLow"));
	Materials.TerrainHighMaterial = LoadGeneratedCityAtlasMaterial(TEXT("MI_TerrainHigh"));
	return Materials;
}

void CreateOriginalMeshSection(
	UProceduralMeshComponent* MeshComponent,
	int32 SectionIndex,
	const FOriginalMeshSectionData& Section,
	bool bCreateCollision)
{
	if (MeshComponent == nullptr)
	{
		return;
	}

	if (Section.UV1.Num() == Section.Vertices.Num())
	{
		const TArray<FVector2D> EmptyUVs;
		MeshComponent->CreateMeshSection_LinearColor(
			SectionIndex,
			Section.Vertices,
			Section.Triangles,
			Section.Normals,
			Section.UVs,
			Section.UV1,
			EmptyUVs,
			EmptyUVs,
			Section.VertexColors,
			Section.Tangents,
			bCreateCollision);
		return;
	}

	MeshComponent->CreateMeshSection_LinearColor(
		SectionIndex,
		Section.Vertices,
		Section.Triangles,
		Section.Normals,
		Section.UVs,
		Section.VertexColors,
		Section.Tangents,
		bCreateCollision);
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

// Reads the conditioned tmap corner grid built by BuildConditionedTerrainCornerSamples.
int32 GetConditionedTerrainCornerSample(const TArray<int16>& ConditionedCorners, int32 GridX, int32 GridY)
{
	constexpr int32 GridSize = FSimCity2000City::MapSize + 1;
	const int32 ClampedX = FMath::Clamp(GridX, 0, GridSize - 1);
	const int32 ClampedY = FMath::Clamp(GridY, 0, GridSize - 1);
	return ConditionedCorners[ClampedY * GridSize + ClampedX];
}

float GetTerrainGridVertexZ(const TArray<int16>& ConditionedCorners, int32 GridX, int32 GridY, float TerrainHeightScale)
{
	// Height-map samples store (height step + 1) * 0x20 (FUN_004abce0), so one altitude step
	// spans 0x20 sample units.
	return static_cast<float>(GetConditionedTerrainCornerSample(ConditionedCorners, GridX, GridY)) * TerrainHeightScale / 32.0f;
}

float GetTerrainGridBilinearZ(const TArray<int16>& ConditionedCorners, float GridX, float GridY, float TerrainHeightScale)
{
	const int32 X0 = FMath::FloorToInt(GridX);
	const int32 Y0 = FMath::FloorToInt(GridY);
	const int32 X1 = X0 + 1;
	const int32 Y1 = Y0 + 1;
	const float LocalX = FMath::Clamp(GridX - static_cast<float>(X0), 0.0f, 1.0f);
	const float LocalY = FMath::Clamp(GridY - static_cast<float>(Y0), 0.0f, 1.0f);
	const float Z00 = GetTerrainGridVertexZ(ConditionedCorners, X0, Y0, TerrainHeightScale);
	const float Z10 = GetTerrainGridVertexZ(ConditionedCorners, X1, Y0, TerrainHeightScale);
	const float Z11 = GetTerrainGridVertexZ(ConditionedCorners, X1, Y1, TerrainHeightScale);
	const float Z01 = GetTerrainGridVertexZ(ConditionedCorners, X0, Y1, TerrainHeightScale);
	return FMath::Lerp(FMath::Lerp(Z00, Z10, LocalX), FMath::Lerp(Z01, Z11, LocalX), LocalY);
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

int32 GetTerrainTileAverageHeightMapSample(const TArray<int16>& ConditionedCorners, int32 FileX, int32 FileY)
{
	return (
		GetConditionedTerrainCornerSample(ConditionedCorners, FileX, FileY) +
		GetConditionedTerrainCornerSample(ConditionedCorners, FileX + 1, FileY) +
		GetConditionedTerrainCornerSample(ConditionedCorners, FileX + 1, FileY + 1) +
		GetConditionedTerrainCornerSample(ConditionedCorners, FileX, FileY + 1)) >> 2;
}

// Reproduces the original city builder's flat-vs-sloped test (FUN_0047c0c0): a tile is
// flat when its four tmap corner heights match - read from the conditioned grid, the
// same samples the terrain quad corners use.
bool IsOriginalTerrainTileFlat(const TArray<int16>& ConditionedCorners, int32 FileX, int32 FileY)
{
	const int32 Corner00 = GetConditionedTerrainCornerSample(ConditionedCorners, FileX, FileY);
	const int32 Corner10 = GetConditionedTerrainCornerSample(ConditionedCorners, FileX + 1, FileY);
	const int32 Corner01 = GetConditionedTerrainCornerSample(ConditionedCorners, FileX, FileY + 1);
	const int32 Corner11 = GetConditionedTerrainCornerSample(ConditionedCorners, FileX + 1, FileY + 1);
	return Corner00 == Corner10 && Corner00 == Corner01 && Corner00 == Corner11;
}

// FUN_004abce0's tmap conditioning, ported exactly. The seeded corner grid (averages of the
// up-to-4 adjacent tile-center samples, matching the original's seed-then-interpolate fill)
// is then modified in place:
//  1. plain ground water tiles (XTER > 0x0f) at even/even tile coordinates dip their origin
//     corner by 8 (a quarter step) - the original's subtle water waviness;
//  2. buildings (XBLD >= 0x70) and flat network tiles {0x1d, 0x1e, 0x23..0x2d, 0x32..0x3a}
//     (roads, rails, crossings) force all four of their corners to the tile's own ALTM
//     sample - the auto-flatten under roads and buildings;
//  3. raised spans 0x3f..0x42 pull their low-edge corner pair one full step (+0x20) above
//     the opposite edge, wedging the terrain up so it meets the raised road model with no
//     gap underneath.
// The pass is a single raster sweep whose ramp reads see earlier writes, so it must stay a
// sweep over a shared grid rather than a per-corner evaluation.
TArray<int16> BuildConditionedTerrainCornerSamples(const FSimCity2000City& City)
{
	constexpr int32 MapSize = FSimCity2000City::MapSize;
	constexpr int32 GridSize = MapSize + 1;
	TArray<int16> Corners;
	Corners.SetNumUninitialized(GridSize * GridSize);
	for (int32 GridY = 0; GridY < GridSize; ++GridY)
	{
		for (int32 GridX = 0; GridX < GridSize; ++GridX)
		{
			Corners[GridY * GridSize + GridX] = static_cast<int16>(GetTerrainGridHeightMapSample(City, GridX, GridY));
		}
	}

	for (int32 FileY = 0; FileY < MapSize; ++FileY)
	{
		for (int32 FileX = 0; FileX < MapSize; ++FileX)
		{
			const FSimCity2000Tile& Tile = City.Tiles[FileY * MapSize + FileX];
			const uint8 Building = Tile.Building;
			const bool bPlainGround = (Building == 0 || Building > 4) &&
				(Building < 6 || Building > 0x0D) && Building != 0xD5 && Building != 0xDA && Building != 0xF8;
			if (bPlainGround && Tile.Terrain > 0x0F && (FileX & 1) == 0 && (FileY & 1) == 0)
			{
				Corners[FileY * GridSize + FileX] -= 8;
			}
		}
	}

	const auto Set = [&Corners](int32 GridX, int32 GridY, int32 Sample)
	{
		Corners[GridY * GridSize + GridX] = static_cast<int16>(Sample);
	};

	for (int32 FileY = 0; FileY < MapSize; ++FileY)
	{
		for (int32 FileX = 0; FileX < MapSize; ++FileX)
		{
			const FSimCity2000Tile& Tile = City.Tiles[FileY * MapSize + FileX];
			const uint8 Building = Tile.Building;
			const bool bFlatNetworkTile =
				Building == 0x1D || Building == 0x1E ||
				(Building >= 0x23 && Building <= 0x2D) ||
				(Building >= 0x32 && Building <= 0x3A);
			if (Building >= 0x70 || bFlatNetworkTile)
			{
				const int32 Sample = GetTerrainHeightMapSample(Tile);
				Set(FileX, FileY, Sample);
				Set(FileX + 1, FileY, Sample);
				Set(FileX, FileY + 1, Sample);
				Set(FileX + 1, FileY + 1, Sample);
			}
			else if (Building == 0x3F)
			{
				const int32 Sample = GetConditionedTerrainCornerSample(Corners, FileX + 1, FileY) + 0x20;
				Set(FileX, FileY, Sample);
				Set(FileX, FileY + 1, Sample);
			}
			else if (Building == 0x40)
			{
				const int32 Sample = GetConditionedTerrainCornerSample(Corners, FileX + 1, FileY + 1) + 0x20;
				Set(FileX, FileY, Sample);
				Set(FileX + 1, FileY, Sample);
			}
			else if (Building == 0x41)
			{
				const int32 Sample = GetConditionedTerrainCornerSample(Corners, FileX, FileY) + 0x20;
				Set(FileX + 1, FileY, Sample);
				Set(FileX + 1, FileY + 1, Sample);
			}
			else if (Building == 0x42)
			{
				const int32 Sample = GetConditionedTerrainCornerSample(Corners, FileX, FileY) + 0x20;
				Set(FileX, FileY + 1, Sample);
				Set(FileX + 1, FileY + 1, Sample);
			}
		}
	}

	return Corners;
}

// Bridge/elevated-road tile dispatch transcribed from FUN_0047c0c0 (XBLD ids
// 0x3f..0x6b). Each tile id selects one or two globally-unique mesh object Ids (resolved via
// FMaxisMeshLibrary::FindObjectByObjectId, the remake's FUN_00470571), which is what
// encodes the correct bridge piece and orientation - the heuristic XBLD->mesh table
// mis-selected these. Cases 0x43..0x46 pick a flat vs sloped primary from the tile's
// corner heights. Some later bridge ids select an F-suffixed orientation variant from XBIT bit 1.
FOriginalBridgeDispatch GetOriginalBridgeDispatch(uint8 BuildingId, bool bTileIsFlat, uint8 BitFlags)
{
	const int32 BitVariant = (BitFlags & 0x02) != 0 ? 1 : 0;

	switch (BuildingId)
	{
	case 0x3f: return { 0x178 };
	case 0x40: return { 0x179 };
	case 0x41: return { 0x17a };
	case 0x42: return { 0x17b };
	case 0x43: return { bTileIsFlat ? 0x128 : 0x17f };
	case 0x44: return { bTileIsFlat ? 0x129 : 0x180 };
	case 0x45: return { bTileIsFlat ? 0x3b : 0x1d, 0x2d };
	case 0x46: return { bTileIsFlat ? 0x3c : 0x1e, 0x2c };
	case 0x47: return { 0x17d };
	case 0x48: return { 0x17e };
	case 0x49: return { 0x0f7 };
	case 0x4a: return { 0x0f8 };
	case 0x4b: return { 0x0f9 };
	case 0x4c: return { 0x0fa };
	case 0x4d: return { 0x0f7, 0x2d };
	case 0x4e: return { 0x0f8, 0x2c };
	case 0x4f: return { 0x0f7 };
	case 0x50: return { 0x0f8 };
	case 0x51: return { 0x066 + BitVariant };
	case 0x52: return { 0x068 + BitVariant };
	case 0x53: return { 0x06a + BitVariant };
	case 0x54: return { 0x06c + BitVariant };
	case 0x55: return { 0x06e + BitVariant };
	case 0x56: return { 0x070 + BitVariant };
	case 0x57:
	case 0x58:
		return { 0x064 + BitVariant };
	case 0x5a:
	case 0x5b:
		return { 0x072 + BitVariant };
	case 0x5c: return { 0x074 + BitVariant };
	case 0x5d: return { 0x0fb + BitVariant };
	case 0x5e: return { 0x0fd + BitVariant };
	case 0x5f: return { 0x0ff + BitVariant };
	case 0x60: return { 0x101 + BitVariant };
	case 0x61: return { 0x103 };
	case 0x62: return { 0x104 };
	case 0x63: return { 0x105 };
	case 0x64: return { 0x106 };
	case 0x65: return { 0x107 };
	case 0x66: return { 0x108 };
	case 0x67: return { 0x109 };
	case 0x68: return { 0x10a };
	case 0x69: return { 0x10b };
	case 0x6a:
	case 0x6b:
		return { 0x114 + BitVariant };
	default: return {};
	}
}

// FUN_00482890 selects the original ground/base object for building tiles that
// need a second scene object. The low XZON nibble chooses residential/commercial/
// industrial base art; the building footprint size chooses 1x1..4x4.
int32 GetOriginalBuildingBaseObjectId(uint8 Zone, int32 FootprintSize)
{
	const uint8 ZoneClass = Zone & 0x0F;
	switch (FootprintSize)
	{
	case 1:
		if (ZoneClass == 1 || ZoneClass == 2)
		{
			return 0x001;
		}
		if (ZoneClass == 3 || ZoneClass == 4)
		{
			return 0x168;
		}
		return 0x164;
	case 2:
		if (ZoneClass == 1 || ZoneClass == 2)
		{
			return 0x002;
		}
		if (ZoneClass == 3 || ZoneClass == 4)
		{
			return 0x169;
		}
		return 0x165;
	case 3:
		if (ZoneClass == 1 || ZoneClass == 2)
		{
			return 0x003;
		}
		return 0x16A;
	case 4:
		if (ZoneClass == 1 || ZoneClass == 2)
		{
			return 0x004;
		}
		if (ZoneClass == 3 || ZoneClass == 4)
		{
			return 0x16B;
		}
		return 0x167;
	default:
		return 0x164;
	}
}

// Building dispatch transcribed from FUN_0047c0c0. These are runtime object Ids
// consumed by FUN_00470571, not heuristic table indexes.
FOriginalCityObjectDispatch GetOriginalBuildingDispatch(
	uint8 BuildingId,
	uint8 Zone,
	uint8 BitFlags,
	int32 FootprintSize,
	int32 SavedRotation,
	bool& bSpecialE7Placed)
{
	const auto WithBase = [&](int32 PrimaryObjectId) -> FOriginalCityObjectDispatch
	{
		return { PrimaryObjectId, GetOriginalBuildingBaseObjectId(Zone, FootprintSize) };
	};
	const auto RotationParityNoBase = [&](int32 ObjectWhenXbitClearMatchesRotationParity, int32 ObjectWhenDifferent) -> FOriginalCityObjectDispatch
	{
		// DAT_004fa9e0 is loaded from MISC dword offset 0x0008, the same field
		// this reader exposes as City.Rotation. The original compares its low bit
		// against whether XBIT bit 1 is clear for XBLD 0xdd and 0xdf.
		const bool bXbitBit1Clear = (BitFlags & 0x02) == 0;
		const bool bSavedRotationOdd = (SavedRotation & 1) != 0;
		return { bXbitBit1Clear == bSavedRotationOdd ? ObjectWhenXbitClearMatchesRotationParity : ObjectWhenDifferent };
	};

	int32 PrimaryObjectId = INDEX_NONE;
	switch (BuildingId)
	{
	case 0x70: PrimaryObjectId = 0x0AF; break;
	case 0x71: PrimaryObjectId = 0x0B0; break;
	case 0x72: PrimaryObjectId = 0x0B1; break;
	case 0x73: PrimaryObjectId = 0x0B2; break;
	case 0x74: PrimaryObjectId = 0x0B3; break;
	case 0x75: PrimaryObjectId = 0x0B4; break;
	case 0x76: PrimaryObjectId = 0x0B5; break;
	case 0x77: PrimaryObjectId = 0x0B6; break;
	case 0x78: PrimaryObjectId = 0x0B7; break;
	case 0x79: PrimaryObjectId = 0x0B8; break;
	case 0x7A: PrimaryObjectId = 0x0B9; break;
	case 0x7B: PrimaryObjectId = 0x0BA; break;
	case 0x7C: PrimaryObjectId = 0x0BB; break;
	case 0x7D: PrimaryObjectId = 0x0BC; break;
	case 0x7E: PrimaryObjectId = 0x0BD; break;
	case 0x7F: PrimaryObjectId = 0x0BE; break;
	case 0x80: PrimaryObjectId = 0x0BF; break;
	case 0x81: PrimaryObjectId = 0x0C0; break;
	case 0x82: PrimaryObjectId = 0x0A9; break;
	case 0x83: PrimaryObjectId = 0x0C1; break;
	case 0x84: PrimaryObjectId = 0x09D; break;
	case 0x85: PrimaryObjectId = 0x0C2; break;
	case 0x86: PrimaryObjectId = 0x0C3; break;
	case 0x87: PrimaryObjectId = 0x09E; break;
	case 0x88: PrimaryObjectId = 0x09A; break;
	case 0x89: PrimaryObjectId = 0x09B; break;
	case 0x8A: PrimaryObjectId = 0x084; break;
	case 0x8B: PrimaryObjectId = 0x085; break;
	case 0x8C: PrimaryObjectId = 0x0C4; break;
	case 0x8D: PrimaryObjectId = 0x019; break;
	case 0x8E: PrimaryObjectId = 0x01A; break;
	case 0x8F: PrimaryObjectId = 0x0C5; break;
	case 0x90: PrimaryObjectId = 0x0C6; break;
	case 0x91: PrimaryObjectId = 0x080; break;
	case 0x92: PrimaryObjectId = 0x0C7; break;
	case 0x93: PrimaryObjectId = 0x0C8; break;
	case 0x94: PrimaryObjectId = 0x081; break;
	case 0x95: PrimaryObjectId = 0x0C9; break;
	case 0x96: PrimaryObjectId = 0x005; break;
	case 0x97: PrimaryObjectId = 0x006; break;
	case 0x98: PrimaryObjectId = 0x00E; break;
	case 0x99: PrimaryObjectId = 0x0CA; break;
	case 0x9A: PrimaryObjectId = 0x00F; break;
	case 0x9B: PrimaryObjectId = 0x010; break;
	case 0x9C: PrimaryObjectId = 0x011; break;
	case 0x9D: PrimaryObjectId = 0x012; break;
	case 0x9E: PrimaryObjectId = 0x09F; break;
	case 0x9F: PrimaryObjectId = 0x0A0; break;
	case 0xA0: PrimaryObjectId = 0x0A1; break;
	case 0xA1: PrimaryObjectId = 0x0A2; break;
	case 0xA2: PrimaryObjectId = 0x0A3; break;
	case 0xA3: PrimaryObjectId = 0x0A4; break;
	case 0xA4: PrimaryObjectId = 0x0A5; break;
	case 0xA5: PrimaryObjectId = 0x0A6; break;
	case 0xA6: PrimaryObjectId = 0x0CB; break;
	case 0xA7: PrimaryObjectId = 0x0CC; break;
	case 0xA8: PrimaryObjectId = 0x09C; break;
	case 0xA9: PrimaryObjectId = 0x0CD; break;
	case 0xAA: PrimaryObjectId = 0x086; break;
	case 0xAB: PrimaryObjectId = 0x087; break;
	case 0xAC: PrimaryObjectId = 0x088; break;
	case 0xAD: PrimaryObjectId = 0x089; break;
	case 0xAE: PrimaryObjectId = 0x0CE; break;
	case 0xAF: PrimaryObjectId = 0x00B; break;
	case 0xB0: PrimaryObjectId = 0x0CF; break;
	case 0xB1: PrimaryObjectId = 0x00C; break;
	case 0xB2: PrimaryObjectId = 0x013; break;
	case 0xB3: PrimaryObjectId = 0x0AA; break;
	case 0xB4: PrimaryObjectId = 0x0AB; break;
	case 0xB5: PrimaryObjectId = 0x0AC; break;
	case 0xB6: PrimaryObjectId = 0x007; break;
	case 0xB7: PrimaryObjectId = 0x008; break;
	case 0xB8: PrimaryObjectId = 0x009; break;
	case 0xB9: PrimaryObjectId = 0x0D0; break;
	case 0xBA: PrimaryObjectId = 0x0AD; break;
	case 0xBB: PrimaryObjectId = 0x00A; break;
	case 0xBC: PrimaryObjectId = 0x0A7; break;
	case 0xBD: PrimaryObjectId = 0x0D1; break;
	case 0xBE: PrimaryObjectId = 0x0A8; break;
	case 0xBF: PrimaryObjectId = 0x0D2; break;
	case 0xC0: PrimaryObjectId = 0x0D3; break;
	case 0xC1: PrimaryObjectId = 0x0D4; break;
	case 0xC2: PrimaryObjectId = 0x0D5; break;
	case 0xC3: PrimaryObjectId = 0x0D6; break;
	case 0xC4: PrimaryObjectId = 0x0D7; break;
	case 0xC5: PrimaryObjectId = 0x0D8; break;
	case 0xC6: return { 0x0D9 };
	case 0xC7: return { 0x0DA };
	case 0xC8: PrimaryObjectId = 0x0DB; break;
	case 0xC9: PrimaryObjectId = 0x0DC; break;
	case 0xCA: PrimaryObjectId = 0x0DD; break;
	case 0xCB: PrimaryObjectId = 0x0DE; break;
	case 0xCC: PrimaryObjectId = 0x0DF; break;
	case 0xCD: PrimaryObjectId = 0x0E0; break;
	case 0xCE: PrimaryObjectId = 0x0E1; break;
	case 0xCF: PrimaryObjectId = 0x0E2; break;
	case 0xD0: PrimaryObjectId = 0x00D; break;
	case 0xD1: PrimaryObjectId = 0x016; break;
	case 0xD2: PrimaryObjectId = 0x000; break;
	case 0xD3: PrimaryObjectId = 0x0E3; break;
	case 0xD4: PrimaryObjectId = 0x018; break;
	case 0xD5: return { 0x144, 0x003 };
	case 0xD6: PrimaryObjectId = 0x01B; break;
	case 0xD7: return { 0x082, 0x004 };
	case 0xD8: PrimaryObjectId = 0x0E4; break;
	case 0xD9: PrimaryObjectId = 0x0E5; break;
	case 0xDA: return { 0x16F, 0x004 };
	case 0xDB: PrimaryObjectId = 0x0E6; break;
	case 0xDC: PrimaryObjectId = 0x0E7; break;
	case 0xDD: return RotationParityNoBase(0x11B, 0x08A);
	case 0xDE: return { 0x08B };
	case 0xDF: return RotationParityNoBase(0x10C, 0x08C);
	case 0xE0: PrimaryObjectId = 0x08D; break;
	case 0xE1: PrimaryObjectId = 0x08E; break;
	case 0xE2: PrimaryObjectId = 0x08F; break;
	case 0xE3: PrimaryObjectId = 0x090; break;
	case 0xE4: PrimaryObjectId = 0x091; break;
	case 0xE5: PrimaryObjectId = 0x092; break;
	case 0xE6: PrimaryObjectId = 0x093; break;
	case 0xE7:
		if (!bSpecialE7Placed)
		{
			bSpecialE7Placed = true;
			return { GetOriginalBuildingBaseObjectId(Zone, FootprintSize) };
		}
		PrimaryObjectId = 0x094;
		break;
	case 0xE8: PrimaryObjectId = 0x095; break;
	case 0xE9: PrimaryObjectId = 0x0E8; break;
	case 0xEA: PrimaryObjectId = 0x0E9; break;
	case 0xEB: PrimaryObjectId = 0x01C; break;
	case 0xEC: PrimaryObjectId = 0x0EA; break;
	case 0xED: PrimaryObjectId = 0x0EB; break;
	case 0xEE: return { 0x0EC, 0x169 };
	case 0xEF: return { 0x0ED, 0x169 };
	case 0xF0: PrimaryObjectId = 0x0EE; break;
	case 0xF1: PrimaryObjectId = 0x0EF; break;
	case 0xF2: PrimaryObjectId = 0x0F0; break;
	case 0xF3: PrimaryObjectId = 0x0F1; break;
	case 0xF4: PrimaryObjectId = 0x0F2; break;
	case 0xF5: PrimaryObjectId = 0x017; break;
	case 0xF6: PrimaryObjectId = 0x096; break;
	case 0xF7: PrimaryObjectId = 0x014; break;
	case 0xF8: return { 0x0F3, 0x166 };
	case 0xF9: PrimaryObjectId = 0x0F4; break;
	case 0xFA: PrimaryObjectId = 0x015; break;
	case 0xFB: PrimaryObjectId = 0x097; break;
	case 0xFC: PrimaryObjectId = 0x0F5; break;
	case 0xFD: PrimaryObjectId = 0x0F6; break;
	case 0xFE: PrimaryObjectId = 0x098; break;
	case 0xFF: PrimaryObjectId = 0x099; break;
	default: return {};
	}

	// Ordinary building cases break into the original shared tail, which calls
	// FUN_00482890 and links the returned base/foundation as a second object.
	return WithBase(PrimaryObjectId);
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

float HashUnitFloat(int32 X, int32 Y, uint32 Salt)
{
	return static_cast<float>(HashTerrainTile(X, Y, Salt) & 0x00FFFFFFu) / static_cast<float>(0x00FFFFFFu);
}

float SmoothNoise2D(float X, float Y, uint32 Salt)
{
	const int32 X0 = FMath::FloorToInt(X);
	const int32 Y0 = FMath::FloorToInt(Y);
	const int32 X1 = X0 + 1;
	const int32 Y1 = Y0 + 1;
	const float LocalX = X - static_cast<float>(X0);
	const float LocalY = Y - static_cast<float>(Y0);
	const float BlendX = LocalX * LocalX * (3.0f - 2.0f * LocalX);
	const float BlendY = LocalY * LocalY * (3.0f - 2.0f * LocalY);

	const float N00 = HashUnitFloat(X0, Y0, Salt);
	const float N10 = HashUnitFloat(X1, Y0, Salt);
	const float N01 = HashUnitFloat(X0, Y1, Salt);
	const float N11 = HashUnitFloat(X1, Y1, Salt);
	const float NX0 = FMath::Lerp(N00, N10, BlendX);
	const float NX1 = FMath::Lerp(N01, N11, BlendX);
	return FMath::Lerp(NX0, NX1, BlendY);
}

float FractalNoise2D(float X, float Y, uint32 Salt)
{
	const float Low = SmoothNoise2D(X * 0.055f, Y * 0.055f, Salt);
	const float Mid = SmoothNoise2D(X * 0.145f + 37.0f, Y * 0.145f - 19.0f, Salt ^ 0x6bf21c11u);
	const float High = SmoothNoise2D(X * 0.33f - 11.0f, Y * 0.33f + 23.0f, Salt ^ 0x21f0aaadu);
	return Low * 0.58f + Mid * 0.30f + High * 0.12f;
}

uint8 GetTerrainTypeBase(uint8 TerrainType)
{
	if (TerrainType < 10)
	{
		return 5;
	}

	if (TerrainType >= 0x70 && TerrainType <= 0x72)
	{
		return 0x10;
	}
	if (TerrainType >= 0x73 && TerrainType <= 0x75)
	{
		return 0x20;
	}
	if (TerrainType >= 0x76 && TerrainType <= 0x78)
	{
		return 0x30;
	}
	if (TerrainType >= 0x79 && TerrainType <= 0x7B)
	{
		return 0x40;
	}
	if (TerrainType >= 0x7C && TerrainType <= 0x7E)
	{
		return 0x60;
	}

	return TerrainType & 0xF0;
}

bool IsWaterTerrainBase(uint8 TerrainBase)
{
	return TerrainBase < 10;
}

int32 GetLandBandIndex(uint8 TerrainBase)
{
	if (TerrainBase <= 0x10)
	{
		return 0;
	}
	if (TerrainBase >= 0x60)
	{
		return 5;
	}

	return FMath::Clamp((static_cast<int32>(TerrainBase) - 0x10) / 0x10, 0, 5);
}

uint8 GetLandBaseTypeFromBand(int32 BandIndex)
{
	static constexpr uint8 LandBases[] = { 0x10, 0x20, 0x30, 0x40, 0x50, 0x60 };
	return LandBases[FMath::Clamp(BandIndex, 0, static_cast<int32>(UE_ARRAY_COUNT(LandBases)) - 1)];
}

int32 GetDistanceOutsideOriginalMap(int32 FileX, int32 FileY)
{
	const int32 MaxMapTile = FSimCity2000City::MapSize - 1;
	return FMath::Max(
		FMath::Max(FMath::Max(0, -FileX), FMath::Max(0, FileX - MaxMapTile)),
		FMath::Max(FMath::Max(0, -FileY), FMath::Max(0, FileY - MaxMapTile)));
}

int32 GetDistanceOutsideOriginalGrid(int32 GridX, int32 GridY)
{
	const int32 MaxMapGrid = FSimCity2000City::MapSize;
	return FMath::Max(
		FMath::Max(FMath::Max(0, -GridX), FMath::Max(0, GridX - MaxMapGrid)),
		FMath::Max(FMath::Max(0, -GridY), FMath::Max(0, GridY - MaxMapGrid)));
}

float Smooth01(float Value)
{
	const float ClampedValue = FMath::Clamp(Value, 0.0f, 1.0f);
	return ClampedValue * ClampedValue * (3.0f - 2.0f * ClampedValue);
}

enum class EBoundarySide : uint8
{
	Top,
	Bottom,
	Left,
	Right
};

struct FBoundaryProfile
{
	float Landness = 0.0f;
	float LandBand = 0.0f;
	float HeightZ = 0.0f;
	float Weight = 0.0f;
};

void AddBoundaryProfile(
	const FSimCity2000City& City,
	const TArray<uint8>& InnerTerrainTypeGrid,
	EBoundarySide Side,
	float AlongCoordinate,
	int32 DistanceFromSide,
	int32 ExtensionTiles,
	float TerrainHeightScale,
	uint32 Salt,
	FBoundaryProfile& Profile)
{
	const int32 N = FSimCity2000City::MapSize;
	const float DistanceAlpha = ExtensionTiles > 0
		? FMath::Clamp(static_cast<float>(DistanceFromSide) / static_cast<float>(ExtensionTiles), 0.0f, 1.0f)
		: 0.0f;
	const float WarpAmplitude = FMath::Min(static_cast<float>(DistanceFromSide) * 0.55f, static_cast<float>(ExtensionTiles) * 0.20f);
	const float Warp = (FractalNoise2D(AlongCoordinate * 0.29f + static_cast<float>(DistanceFromSide) * 0.37f, AlongCoordinate * 0.11f - static_cast<float>(DistanceFromSide) * 0.23f, Salt) * 2.0f - 1.0f) * WarpAmplitude * Smooth01(DistanceAlpha);
	const int32 Center = FMath::RoundToInt(AlongCoordinate + Warp);
	const int32 Radius = 6;
	const float SideWeight = 1.0f / FMath::Square(static_cast<float>(DistanceFromSide) + 1.0f);

	for (int32 Offset = -Radius; Offset <= Radius; ++Offset)
	{
		const int32 EdgeCoordinate = FMath::Clamp(Center + Offset, 0, N - 1);
		int32 FileX = 0;
		int32 FileY = 0;
		switch (Side)
		{
		case EBoundarySide::Top:
			FileX = EdgeCoordinate;
			FileY = 0;
			break;
		case EBoundarySide::Bottom:
			FileX = EdgeCoordinate;
			FileY = N - 1;
			break;
		case EBoundarySide::Left:
			FileX = 0;
			FileY = EdgeCoordinate;
			break;
		case EBoundarySide::Right:
			FileX = N - 1;
			FileY = EdgeCoordinate;
			break;
		}

		const int32 TileIndex = FileY * N + FileX;
		const uint8 BaseType = GetTerrainTypeBase(InnerTerrainTypeGrid[TileIndex]);
		const bool bLand = !City.Tiles[TileIndex].bWater && !IsWaterTerrainBase(BaseType);
		const float SampleWeight = static_cast<float>(Radius + 1 - FMath::Abs(Offset)) * SideWeight;
		Profile.Landness += (bLand ? 1.0f : 0.0f) * SampleWeight;
		Profile.LandBand += static_cast<float>(bLand ? GetLandBandIndex(BaseType) : 0) * SampleWeight;
		Profile.HeightZ += GetTerrainTileCenterZ(City, FileX, FileY, TerrainHeightScale) * SampleWeight;
		Profile.Weight += SampleWeight;
	}
}

FBoundaryProfile NormalizeBoundaryProfile(FBoundaryProfile Profile, float DefaultHeightZ)
{
	if (Profile.Weight <= KINDA_SMALL_NUMBER)
	{
		Profile.Landness = 0.5f;
		Profile.LandBand = 2.0f;
		Profile.HeightZ = DefaultHeightZ;
		return Profile;
	}

	Profile.Landness /= Profile.Weight;
	Profile.LandBand /= Profile.Weight;
	Profile.HeightZ /= Profile.Weight;
	return Profile;
}

FBoundaryProfile SampleOutsideTileBoundaryProfile(
	const FSimCity2000City& City,
	const TArray<uint8>& InnerTerrainTypeGrid,
	int32 FileX,
	int32 FileY,
	int32 ExtensionTiles,
	float TerrainHeightScale,
	float DefaultHeightZ)
{
	const int32 N = FSimCity2000City::MapSize;
	FBoundaryProfile Profile;
	if (FileY < 0)
	{
		AddBoundaryProfile(City, InnerTerrainTypeGrid, EBoundarySide::Top, static_cast<float>(FileX), -FileY, ExtensionTiles, TerrainHeightScale, 0xa13f4f31u, Profile);
	}
	if (FileY >= N)
	{
		AddBoundaryProfile(City, InnerTerrainTypeGrid, EBoundarySide::Bottom, static_cast<float>(FileX), FileY - N + 1, ExtensionTiles, TerrainHeightScale, 0x9d447b73u, Profile);
	}
	if (FileX < 0)
	{
		AddBoundaryProfile(City, InnerTerrainTypeGrid, EBoundarySide::Left, static_cast<float>(FileY), -FileX, ExtensionTiles, TerrainHeightScale, 0x64a0dca5u, Profile);
	}
	if (FileX >= N)
	{
		AddBoundaryProfile(City, InnerTerrainTypeGrid, EBoundarySide::Right, static_cast<float>(FileY), FileX - N + 1, ExtensionTiles, TerrainHeightScale, 0xcb13e839u, Profile);
	}
	return NormalizeBoundaryProfile(Profile, DefaultHeightZ);
}

FBoundaryProfile SampleOutsideGridBoundaryProfile(
	const FSimCity2000City& City,
	const TArray<uint8>& InnerTerrainTypeGrid,
	int32 GridX,
	int32 GridY,
	int32 ExtensionTiles,
	float TerrainHeightScale,
	float DefaultHeightZ)
{
	const int32 N = FSimCity2000City::MapSize;
	FBoundaryProfile Profile;
	if (GridY < 0)
	{
		AddBoundaryProfile(City, InnerTerrainTypeGrid, EBoundarySide::Top, static_cast<float>(GridX), -GridY, ExtensionTiles, TerrainHeightScale, 0x1f123bb5u, Profile);
	}
	if (GridY > N)
	{
		AddBoundaryProfile(City, InnerTerrainTypeGrid, EBoundarySide::Bottom, static_cast<float>(GridX), GridY - N, ExtensionTiles, TerrainHeightScale, 0x34591b07u, Profile);
	}
	if (GridX < 0)
	{
		AddBoundaryProfile(City, InnerTerrainTypeGrid, EBoundarySide::Left, static_cast<float>(GridY), -GridX, ExtensionTiles, TerrainHeightScale, 0x8dba174du, Profile);
	}
	if (GridX > N)
	{
		AddBoundaryProfile(City, InnerTerrainTypeGrid, EBoundarySide::Right, static_cast<float>(GridY), GridX - N, ExtensionTiles, TerrainHeightScale, 0xf2e1662bu, Profile);
	}
	return NormalizeBoundaryProfile(Profile, DefaultHeightZ);
}

float GetBoundaryConstraint(int32 DistanceFromMap, float BoundaryLandness, int32 ExtensionTiles)
{
	const float WaterPersistence = 1.0f - BoundaryLandness;
	const float DecayTiles = static_cast<float>(ExtensionTiles) * FMath::Lerp(0.26f, 0.82f, WaterPersistence);
	return FMath::Clamp(FMath::Exp(-static_cast<float>(DistanceFromMap) / FMath::Max(1.0f, DecayTiles)), 0.0f, 1.0f);
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
TArray<uint8> BuildTerrainTextureTypeGrid(const FSimCity2000City& City, const TArray<int16>& ConditionedCorners)
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
			const int32 AverageHeight = GetTerrainTileAverageHeightMapSample(ConditionedCorners, X, Y);
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
			const int32 AverageHeight = GetTerrainTileAverageHeightMapSample(ConditionedCorners, X, Y);
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
			const int32 AverageHeight = GetTerrainTileAverageHeightMapSample(ConditionedCorners, X, Y);
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
			const int32 AverageHeight = GetTerrainTileAverageHeightMapSample(ConditionedCorners, X, Y);
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
			const int32 AverageHeight = GetTerrainTileAverageHeightMapSample(ConditionedCorners, X, Y);
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

FExtendedTerrainData BuildProceduralExtendedTerrain(
	const FSimCity2000City& City,
	const TArray<int16>& ConditionedCorners,
	const TArray<uint8>& InnerTerrainTypeGrid,
	int32 RequestedExtensionTiles,
	float TerrainHeightScale)
{
	FExtendedTerrainData Data;
	Data.ExtensionTiles = FMath::Clamp(RequestedExtensionTiles, 0, 256);
	if (Data.ExtensionTiles <= 0)
	{
		return Data;
	}

	const int32 N = FSimCity2000City::MapSize;
	Data.MinTileCoordinate = -Data.ExtensionTiles;
	Data.TileGridSize = N + Data.ExtensionTiles * 2;
	Data.TerrainTypes.SetNumUninitialized(Data.TileGridSize * Data.TileGridSize);
	Data.WaterMask.SetNumZeroed(Data.TileGridSize * Data.TileGridSize);
	Data.GridVertexZ.SetNumUninitialized((Data.TileGridSize + 1) * (Data.TileGridSize + 1));

	TArray<uint8> BaseTypes;
	BaseTypes.SetNumUninitialized(Data.TileGridSize * Data.TileGridSize);

	float MinOriginalGridZ = TNumericLimits<float>::Max();
	for (int32 GridY = 0; GridY <= N; ++GridY)
	{
		for (int32 GridX = 0; GridX <= N; ++GridX)
		{
			const float GridZ = GetTerrainGridVertexZ(ConditionedCorners, GridX, GridY, TerrainHeightScale);
			MinOriginalGridZ = FMath::Min(MinOriginalGridZ, GridZ);
		}
	}

	float WaterZSum = 0.0f;
	int32 WaterZCount = 0;
	for (int32 FileY = 0; FileY < N; ++FileY)
	{
		for (int32 FileX = 0; FileX < N; ++FileX)
		{
			const int32 TileIndex = FileY * N + FileX;
			const uint8 BaseType = GetTerrainTypeBase(InnerTerrainTypeGrid[TileIndex]);
			if (City.Tiles[TileIndex].bWater || IsWaterTerrainBase(BaseType))
			{
				WaterZSum += GetTerrainTileCenterZ(City, FileX, FileY, TerrainHeightScale);
				++WaterZCount;
			}
		}
	}

	const float OceanSurfaceZ = WaterZCount > 0 ? WaterZSum / static_cast<float>(WaterZCount) : MinOriginalGridZ;
	const float LandFloorZ = OceanSurfaceZ + TerrainHeightScale * 0.35f;

	for (int32 FileY = 0; FileY < N; ++FileY)
	{
		for (int32 FileX = 0; FileX < N; ++FileX)
		{
			const int32 ExpandedIndex = Data.GetTileIndex(FileX, FileY);
			const int32 InnerIndex = FileY * N + FileX;
			const uint8 BaseType = GetTerrainTypeBase(InnerTerrainTypeGrid[InnerIndex]);
			BaseTypes[ExpandedIndex] = BaseType;
			Data.TerrainTypes[ExpandedIndex] = InnerTerrainTypeGrid[InnerIndex];
			Data.WaterMask[ExpandedIndex] = IsWaterTerrainBase(BaseType) ? 1 : 0;
		}
	}

	for (int32 FileY = Data.MinTileCoordinate; FileY < Data.MinTileCoordinate + Data.TileGridSize; ++FileY)
	{
		for (int32 FileX = Data.MinTileCoordinate; FileX < Data.MinTileCoordinate + Data.TileGridSize; ++FileX)
		{
			if (Data.IsOriginalMapTile(FileX, FileY))
			{
				continue;
			}

			const int32 DistanceFromMap = GetDistanceOutsideOriginalMap(FileX, FileY);
			const FBoundaryProfile BoundaryProfile = SampleOutsideTileBoundaryProfile(
				City,
				InnerTerrainTypeGrid,
				FileX,
				FileY,
				Data.ExtensionTiles,
				TerrainHeightScale,
				OceanSurfaceZ);
			const float BoundaryConstraint = GetBoundaryConstraint(DistanceFromMap, BoundaryProfile.Landness, Data.ExtensionTiles);
			const float LargeLandNoise = FractalNoise2D(static_cast<float>(FileX) * 0.075f, static_cast<float>(FileY) * 0.075f, 0x95f2a6c7u);
			const float CoastNoise = FractalNoise2D(static_cast<float>(FileX) * 0.19f + 41.0f, static_cast<float>(FileY) * 0.19f - 17.0f, 0xf0b84e29u);
			const float DetailLandNoise = FractalNoise2D(static_cast<float>(FileX) * 0.46f - 23.0f, static_cast<float>(FileY) * 0.46f + 67.0f, 0x3aa927ddu);
			const float ProceduralLandness = FMath::Clamp(LargeLandNoise * 0.58f + CoastNoise * 0.32f + DetailLandNoise * 0.10f, 0.0f, 1.0f);
			float LandPotential = FMath::Lerp(ProceduralLandness, BoundaryProfile.Landness, BoundaryConstraint);
			LandPotential += (DetailLandNoise - 0.5f) * 0.16f * (1.0f - BoundaryConstraint * 0.70f);
			LandPotential = FMath::Clamp(LandPotential, 0.0f, 1.0f);

			const float WaterBoundaryBias = BoundaryProfile.Landness < 0.5f ? 0.64f : 0.36f;
			const float LandThreshold = FMath::Lerp(0.48f, WaterBoundaryBias, BoundaryConstraint);
			bool bGeneratedWater = LandPotential < LandThreshold;
			if (DistanceFromMap <= 2 && BoundaryProfile.Landness > 0.82f)
			{
				bGeneratedWater = false;
			}
			else if (DistanceFromMap <= 4 && BoundaryProfile.Landness < 0.18f)
			{
				bGeneratedWater = true;
			}

			uint8 BaseType = 5;
			if (!bGeneratedWater)
			{
				const float ElevationNoise = FractalNoise2D(static_cast<float>(FileX) * 0.085f - 79.0f, static_cast<float>(FileY) * 0.085f + 113.0f, 0x6b735f41u);
				const float RidgeNoise = FractalNoise2D(static_cast<float>(FileX) * 0.16f + 7.0f, static_cast<float>(FileY) * 0.16f - 29.0f, 0x7fb2c45du);
				const float ProceduralBand = FMath::Clamp(0.35f + ElevationNoise * 4.35f + FMath::Max(0.0f, RidgeNoise - 0.62f) * 3.0f, 0.0f, 5.0f);
				float Band = FMath::Lerp(ProceduralBand, BoundaryProfile.LandBand, BoundaryConstraint);
				const float ShoreBlend = Smooth01(FMath::Clamp((LandPotential - LandThreshold) / 0.26f, 0.0f, 1.0f));
				Band = FMath::Lerp(0.0f, Band, ShoreBlend);
				BaseType = GetLandBaseTypeFromBand(FMath::RoundToInt(Band));
			}

			const int32 ExpandedIndex = Data.GetTileIndex(FileX, FileY);
			BaseTypes[ExpandedIndex] = BaseType;
			Data.TerrainTypes[ExpandedIndex] = BaseType;
			Data.WaterMask[ExpandedIndex] = IsWaterTerrainBase(BaseType) ? 1 : 0;
		}
	}

	auto GetClampedBaseType = [&BaseTypes, &Data](int32 FileX, int32 FileY) -> uint8
	{
		const int32 ClampedX = FMath::Clamp(FileX, Data.MinTileCoordinate, Data.MinTileCoordinate + Data.TileGridSize - 1);
		const int32 ClampedY = FMath::Clamp(FileY, Data.MinTileCoordinate, Data.MinTileCoordinate + Data.TileGridSize - 1);
		return BaseTypes[Data.GetTileIndex(ClampedX, ClampedY)];
	};

	for (int32 FileY = Data.MinTileCoordinate; FileY < Data.MinTileCoordinate + Data.TileGridSize; ++FileY)
	{
		for (int32 FileX = Data.MinTileCoordinate; FileX < Data.MinTileCoordinate + Data.TileGridSize; ++FileX)
		{
			if (Data.IsOriginalMapTile(FileX, FileY))
			{
				continue;
			}

			const int32 Index = Data.GetTileIndex(FileX, FileY);
			if (IsWaterTerrainBase(BaseTypes[Index]))
			{
				continue;
			}

			bool bTouchesWater = false;
			bool bNearWater = false;
			for (int32 OffsetY = -2; OffsetY <= 2; ++OffsetY)
			{
				for (int32 OffsetX = -2; OffsetX <= 2; ++OffsetX)
				{
					if (OffsetX == 0 && OffsetY == 0)
					{
						continue;
					}

					const bool bNeighborWater = IsWaterTerrainBase(GetClampedBaseType(FileX + OffsetX, FileY + OffsetY));
					bNearWater = bNearWater || bNeighborWater;
					bTouchesWater = bTouchesWater || (bNeighborWater && FMath::Abs(OffsetX) <= 1 && FMath::Abs(OffsetY) <= 1);
				}
			}

			if (bTouchesWater)
			{
				BaseTypes[Index] = 0x10;
			}
			else if (bNearWater)
			{
				BaseTypes[Index] = GetLandBaseTypeFromBand(FMath::Min(GetLandBandIndex(BaseTypes[Index]), 1));
			}
		}
	}

	for (int32 PassIndex = 0; PassIndex < 3; ++PassIndex)
	{
		TArray<uint8> AdjustedBaseTypes = BaseTypes;
		for (int32 FileY = Data.MinTileCoordinate; FileY < Data.MinTileCoordinate + Data.TileGridSize; ++FileY)
		{
			for (int32 FileX = Data.MinTileCoordinate; FileX < Data.MinTileCoordinate + Data.TileGridSize; ++FileX)
			{
				if (Data.IsOriginalMapTile(FileX, FileY))
				{
					continue;
				}

				const int32 Index = Data.GetTileIndex(FileX, FileY);
				if (IsWaterTerrainBase(BaseTypes[Index]))
				{
					continue;
				}

				int32 Band = GetLandBandIndex(BaseTypes[Index]);
				const int32 NeighborOffsets[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
				for (const int32* Offset : NeighborOffsets)
				{
					const uint8 NeighborBaseType = GetClampedBaseType(FileX + Offset[0], FileY + Offset[1]);
					if (IsWaterTerrainBase(NeighborBaseType))
					{
						Band = FMath::Min(Band, 1);
						continue;
					}

					const int32 NeighborBand = GetLandBandIndex(NeighborBaseType);
					Band = FMath::Clamp(Band, NeighborBand - 1, NeighborBand + 1);
				}

				AdjustedBaseTypes[Index] = GetLandBaseTypeFromBand(Band);
			}
		}
		BaseTypes = MoveTemp(AdjustedBaseTypes);
	}

	for (int32 FileY = Data.MinTileCoordinate; FileY < Data.MinTileCoordinate + Data.TileGridSize; ++FileY)
	{
		for (int32 FileX = Data.MinTileCoordinate; FileX < Data.MinTileCoordinate + Data.TileGridSize; ++FileX)
		{
			if (Data.IsOriginalMapTile(FileX, FileY))
			{
				continue;
			}

			const int32 Index = Data.GetTileIndex(FileX, FileY);
			Data.TerrainTypes[Index] = BaseTypes[Index];
			Data.WaterMask[Index] = IsWaterTerrainBase(BaseTypes[Index]) ? 1 : 0;
		}
	}

	for (int32 FileY = Data.MinTileCoordinate; FileY < Data.MinTileCoordinate + Data.TileGridSize; ++FileY)
	{
		for (int32 FileX = Data.MinTileCoordinate; FileX < Data.MinTileCoordinate + Data.TileGridSize; ++FileX)
		{
			if (Data.IsOriginalMapTile(FileX, FileY))
			{
				continue;
			}

			const int32 Index = Data.GetTileIndex(FileX, FileY);
			if (!IsWaterTerrainBase(BaseTypes[Index]))
			{
				continue;
			}

			bool bTouchesLand = false;
			for (int32 OffsetY = -1; OffsetY <= 1 && !bTouchesLand; ++OffsetY)
			{
				for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
				{
					if ((OffsetX != 0 || OffsetY != 0) && !IsWaterTerrainBase(GetClampedBaseType(FileX + OffsetX, FileY + OffsetY)))
					{
						bTouchesLand = true;
						break;
					}
				}
			}

			Data.TerrainTypes[Index] = bTouchesLand ? 0 : 5;
		}
	}

	auto ApplyTransitionMask = [&Data, &BaseTypes, &GetClampedBaseType](uint8 BaseType, auto IsLowerBand)
	{
		for (int32 FileY = Data.MinTileCoordinate; FileY < Data.MinTileCoordinate + Data.TileGridSize; ++FileY)
		{
			for (int32 FileX = Data.MinTileCoordinate; FileX < Data.MinTileCoordinate + Data.TileGridSize; ++FileX)
			{
				if (Data.IsOriginalMapTile(FileX, FileY))
				{
					continue;
				}

				const int32 Index = Data.GetTileIndex(FileX, FileY);
				if (BaseTypes[Index] != BaseType)
				{
					continue;
				}

				uint8 Mask = 0;
				if (IsLowerBand(GetClampedBaseType(FileX, FileY - 1)))
				{
					Mask |= 1;
				}
				if (IsLowerBand(GetClampedBaseType(FileX + 1, FileY)))
				{
					Mask |= 2;
				}
				if (IsLowerBand(GetClampedBaseType(FileX, FileY + 1)))
				{
					Mask |= 4;
				}
				if (IsLowerBand(GetClampedBaseType(FileX - 1, FileY)))
				{
					Mask |= 8;
				}
				Data.TerrainTypes[Index] = BaseType + Mask;
			}
		}
	};

	ApplyTransitionMask(0x10, [](uint8 T) { return T < 10; });
	ApplyTransitionMask(0x20, [](uint8 T) { return T >= 0x10 && T < 0x20; });
	ApplyTransitionMask(0x30, [](uint8 T) { return T >= 0x20 && T < 0x30; });
	ApplyTransitionMask(0x40, [](uint8 T) { return T >= 0x30 && T < 0x40; });
	ApplyTransitionMask(0x50, [](uint8 T) { return T >= 0x40 && T < 0x50; });
	ApplyTransitionMask(0x60, [](uint8 T) { return T >= 0x50 && T < 0x60; });

	for (int32 FileY = Data.MinTileCoordinate; FileY < Data.MinTileCoordinate + Data.TileGridSize; ++FileY)
	{
		for (int32 FileX = Data.MinTileCoordinate; FileX < Data.MinTileCoordinate + Data.TileGridSize; ++FileX)
		{
			if (!Data.IsOriginalMapTile(FileX, FileY))
			{
				const int32 Index = Data.GetTileIndex(FileX, FileY);
				Data.TerrainTypes[Index] = ResolveOriginalTerrainDetailType(Data.TerrainTypes[Index], FileX, FileY);
			}
		}
	}

	for (int32 GridY = 0; GridY <= N; ++GridY)
	{
		for (int32 GridX = 0; GridX <= N; ++GridX)
		{
			Data.GridVertexZ[Data.GetGridVertexIndex(GridX, GridY)] = GetTerrainGridVertexZ(ConditionedCorners, GridX, GridY, TerrainHeightScale);
		}
	}

	for (int32 GridY = Data.MinTileCoordinate; GridY <= Data.MinTileCoordinate + Data.TileGridSize; ++GridY)
	{
		for (int32 GridX = Data.MinTileCoordinate; GridX <= Data.MinTileCoordinate + Data.TileGridSize; ++GridX)
		{
			if (GridX >= 0 && GridX <= N && GridY >= 0 && GridY <= N)
			{
				continue;
			}

			const int32 GridIndex = Data.GetGridVertexIndex(GridX, GridY);
			const int32 DistanceFromMap = GetDistanceOutsideOriginalGrid(GridX, GridY);
			const FBoundaryProfile BoundaryProfile = SampleOutsideGridBoundaryProfile(
				City,
				InnerTerrainTypeGrid,
				GridX,
				GridY,
				Data.ExtensionTiles,
				TerrainHeightScale,
				OceanSurfaceZ);
			const float BoundaryConstraint = GetBoundaryConstraint(DistanceFromMap, BoundaryProfile.Landness, Data.ExtensionTiles);
			int32 AdjacentWaterCount = 0;
			int32 AdjacentLandCount = 0;
			float AdjacentLandBandSum = 0.0f;
			for (int32 OffsetY = -1; OffsetY <= 0; ++OffsetY)
			{
				for (int32 OffsetX = -1; OffsetX <= 0; ++OffsetX)
				{
					const int32 TileX = GridX + OffsetX;
					const int32 TileY = GridY + OffsetY;
					if (!Data.ContainsTile(TileX, TileY))
					{
						continue;
					}

					const int32 TileIndex = Data.GetTileIndex(TileX, TileY);
					if (Data.WaterMask[TileIndex] != 0)
					{
						++AdjacentWaterCount;
					}
					else
					{
						++AdjacentLandCount;
						AdjacentLandBandSum += static_cast<float>(GetLandBandIndex(BaseTypes[TileIndex]));
					}
				}
			}

			const float DistanceAlpha = Data.ExtensionTiles > 0
				? FMath::Clamp(static_cast<float>(DistanceFromMap) / static_cast<float>(Data.ExtensionTiles), 0.0f, 1.0f)
				: 0.0f;
			const float ShapedDistance = Smooth01(DistanceAlpha);
			if (AdjacentWaterCount > 0 && AdjacentLandCount == 0)
			{
				const float OceanNoise = (FractalNoise2D(static_cast<float>(GridX) * 0.23f, static_cast<float>(GridY) * 0.23f, 0x6d912af7u) * 2.0f - 1.0f) * TerrainHeightScale * 0.025f;
				const float ProceduralOceanZ = OceanSurfaceZ + OceanNoise;
				Data.GridVertexZ[GridIndex] = FMath::Lerp(ProceduralOceanZ, BoundaryProfile.HeightZ, BoundaryConstraint);
				continue;
			}

			const float LandBand = AdjacentLandCount > 0
				? AdjacentLandBandSum / static_cast<float>(AdjacentLandCount)
				: BoundaryProfile.LandBand;
			const float BroadNoise = FractalNoise2D(static_cast<float>(GridX) * 0.065f, static_cast<float>(GridY) * 0.065f, 0x3398fd53u) * 2.0f - 1.0f;
			const float MidNoise = FractalNoise2D(static_cast<float>(GridX) * 0.19f + 101.0f, static_cast<float>(GridY) * 0.19f - 47.0f, 0x44ba9871u) * 2.0f - 1.0f;
			const float RidgeNoise = FractalNoise2D(static_cast<float>(GridX) * 0.11f - 31.0f, static_cast<float>(GridY) * 0.11f + 59.0f, 0x203b18fdu);
			const float BaseLandZ = OceanSurfaceZ + TerrainHeightScale * (0.48f + LandBand * 0.34f);
			const float TerrainAmplitude = TerrainHeightScale * (0.72f + LandBand * 0.14f) * FMath::Lerp(0.45f, 1.0f, ShapedDistance);
			const float RidgeLift = FMath::Max(0.0f, RidgeNoise - 0.55f) * TerrainHeightScale * (0.65f + LandBand * 0.15f);
			float ProceduralLandZ = BaseLandZ + (BroadNoise * 0.78f + MidNoise * 0.28f) * TerrainAmplitude + RidgeLift;
			if (AdjacentWaterCount > 0)
			{
				const float ShoreMaxZ = LandFloorZ + TerrainHeightScale * (0.85f + LandBand * 0.18f);
				ProceduralLandZ = FMath::Clamp(ProceduralLandZ, LandFloorZ, ShoreMaxZ);
			}
			Data.GridVertexZ[GridIndex] = FMath::Lerp(ProceduralLandZ, BoundaryProfile.HeightZ, BoundaryConstraint);
		}
	}

	return Data;
}

uint8 GetOriginalRoadMarkingOpeningMask(uint8 BuildingId)
{
	constexpr uint8 N = static_cast<uint8>(ERoadOpening::North);
	constexpr uint8 E = static_cast<uint8>(ERoadOpening::East);
	constexpr uint8 S = static_cast<uint8>(ERoadOpening::South);
	constexpr uint8 W = static_cast<uint8>(ERoadOpening::West);

	switch (BuildingId)
	{
	case 0x1D: return E | W;
	case 0x1E: return N | S;
	case 0x1F: return N | S;
	case 0x20: return E | W;
	case 0x21: return N | S;
	case 0x22: return E | W;
	case 0x23: return S | W;
	case 0x24: return E | S;
	case 0x25: return N | E;
	case 0x26: return N | W;
	case 0x27: return N | S | W;
	case 0x28: return E | S | W;
	case 0x29: return N | E | S;
	case 0x2A: return N | E | W;
	case 0x2B: return N | E | S | W;
	case 0x3F: return N | S;
	case 0x40: return E | W;
	case 0x41: return N | S;
	case 0x42: return E | W;
	case 0x43: return E | W;
	case 0x44: return N | S;
	case 0x45: return E | W;
	case 0x46: return N | S;
	case 0x49: return E | W;
	case 0x4A: return N | S;
	case 0x4D: return E | W;
	case 0x4E: return N | S;
	case 0x4F: return E | W;
	case 0x50: return N | S;
	default: return 0;
	}
}

bool HasRoadOpening(uint8 OpeningMask, ERoadOpening Opening)
{
	return (OpeningMask & static_cast<uint8>(Opening)) != 0;
}

int32 CountRoadOpenings(uint8 OpeningMask)
{
	int32 Count = 0;
	for (const ERoadOpening Opening : { ERoadOpening::North, ERoadOpening::East, ERoadOpening::South, ERoadOpening::West })
	{
		Count += HasRoadOpening(OpeningMask, Opening) ? 1 : 0;
	}
	return Count;
}

FVector2D GetRoadOpeningPoint(ERoadOpening Opening, float TileSize, float EdgeInset)
{
	const float Half = TileSize * 0.5f;
	switch (Opening)
	{
	case ERoadOpening::North: return FVector2D(0.0f, Half - EdgeInset);
	case ERoadOpening::East: return FVector2D(Half - EdgeInset, 0.0f);
	case ERoadOpening::South: return FVector2D(0.0f, -Half + EdgeInset);
	case ERoadOpening::West: return FVector2D(-Half + EdgeInset, 0.0f);
	default: return FVector2D::ZeroVector;
	}
}

FVector MakeRoadMarkingWorldPoint(
	const TArray<int16>& ConditionedCorners,
	int32 FileX,
	int32 FileY,
	const FVector2D& LocalPoint,
	float TileSize,
	float HalfMapSize,
	float TerrainHeightScale,
	float ZOffset)
{
	const float CenterX = GetWorldTileCenterCoordinate(static_cast<float>(FileX), TileSize, HalfMapSize);
	const float CenterY = -GetWorldTileCenterCoordinate(static_cast<float>(FileY), TileSize, HalfMapSize);
	const float GridX = static_cast<float>(FileX) + 0.5f + LocalPoint.X / TileSize;
	const float GridY = static_cast<float>(FileY) + 0.5f - LocalPoint.Y / TileSize;
	return FVector(
		CenterX + LocalPoint.X,
		CenterY + LocalPoint.Y,
		GetTerrainGridBilinearZ(ConditionedCorners, GridX, GridY, TerrainHeightScale) + ZOffset);
}

void AppendRoadMarkingSegment(
	FOriginalMeshSectionData& Section,
	const TArray<int16>& ConditionedCorners,
	int32 FileX,
	int32 FileY,
	const FVector2D& Start,
	const FVector2D& End,
	float TileSize,
	float HalfMapSize,
	float TerrainHeightScale,
	float ZOffset,
	float Width,
	const FLinearColor& Color)
{
	const FVector2D Delta = End - Start;
	const float Length = Delta.Size();
	if (Length <= KINDA_SMALL_NUMBER || Width <= 0.0f)
	{
		return;
	}

	const FVector2D Direction = Delta / Length;
	const FVector2D Perp(-Direction.Y, Direction.X);
	const float HalfWidth = Width * 0.5f;
	const FVector2D P0 = Start + Perp * HalfWidth;
	const FVector2D P1 = End + Perp * HalfWidth;
	const FVector2D P2 = End - Perp * HalfWidth;
	const FVector2D P3 = Start - Perp * HalfWidth;
	const int32 VertexStart = Section.Vertices.Num();

	Section.Vertices.Add(MakeRoadMarkingWorldPoint(ConditionedCorners, FileX, FileY, P0, TileSize, HalfMapSize, TerrainHeightScale, ZOffset));
	Section.Vertices.Add(MakeRoadMarkingWorldPoint(ConditionedCorners, FileX, FileY, P1, TileSize, HalfMapSize, TerrainHeightScale, ZOffset));
	Section.Vertices.Add(MakeRoadMarkingWorldPoint(ConditionedCorners, FileX, FileY, P2, TileSize, HalfMapSize, TerrainHeightScale, ZOffset));
	Section.Vertices.Add(MakeRoadMarkingWorldPoint(ConditionedCorners, FileX, FileY, P3, TileSize, HalfMapSize, TerrainHeightScale, ZOffset));

	Section.Triangles.Add(VertexStart);
	Section.Triangles.Add(VertexStart + 1);
	Section.Triangles.Add(VertexStart + 2);
	Section.Triangles.Add(VertexStart);
	Section.Triangles.Add(VertexStart + 2);
	Section.Triangles.Add(VertexStart + 3);
	Section.TriangleCount += 2;

	const FProcMeshTangent Tangent(Direction.X, Direction.Y, 0.0f);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Section.Normals.Add(FVector::UpVector);
		Section.UVs.Add(FVector2D(Index == 1 || Index == 2 ? 1.0f : 0.0f, Index >= 2 ? 1.0f : 0.0f));
		Section.VertexColors.Add(Color);
		Section.Tangents.Add(Tangent);
	}
}

void AppendTiledDashedRoadMarkingSegment(
	FOriginalMeshSectionData& Section,
	const TArray<int16>& ConditionedCorners,
	int32 FileX,
	int32 FileY,
	const FVector2D& Start,
	const FVector2D& End,
	float TileSize,
	float HalfMapSize,
	float TerrainHeightScale,
	float ZOffset,
	float Width,
	int32 SegmentCount,
	const FLinearColor& Color)
{
	const FVector2D Delta = End - Start;
	const float Length = Delta.Size();
	if (Length <= KINDA_SMALL_NUMBER || SegmentCount <= 0)
	{
		return;
	}

	constexpr float DashFillRatio = 0.52f;
	const FVector2D Direction = Delta / Length;
	const float Period = Length / static_cast<float>(SegmentCount);
	const float DashLength = Period * DashFillRatio;
	const float EndGap = (Period - DashLength) * 0.5f;

	for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
	{
		const float SegmentStartDistance = EndGap + static_cast<float>(SegmentIndex) * Period;
		const float SegmentEndDistance = SegmentStartDistance + DashLength;
		AppendRoadMarkingSegment(
			Section,
			ConditionedCorners,
			FileX,
			FileY,
			Start + Direction * SegmentStartDistance,
			Start + Direction * SegmentEndDistance,
			TileSize,
			HalfMapSize,
			TerrainHeightScale,
			ZOffset,
			Width,
			Color);
	}
}

void AppendRoadMarkingsForTile(
	FOriginalMeshSectionData& Section,
	const TArray<int16>& ConditionedCorners,
	uint8 BuildingId,
	int32 FileX,
	int32 FileY,
	float TileSize,
	float HalfMapSize,
	float TerrainHeightScale,
	float ZOffset,
	float Width,
	const FLinearColor& Color)
{
	const uint8 Openings = GetOriginalRoadMarkingOpeningMask(BuildingId);
	if (Openings == 0)
	{
		return;
	}

	const int32 OpeningCount = CountRoadOpenings(Openings);
	if (OpeningCount != 2)
	{
		return;
	}

	TArray<ERoadOpening, TInlineAllocator<2>> RoadOpenings;
	for (const ERoadOpening Opening : { ERoadOpening::North, ERoadOpening::East, ERoadOpening::South, ERoadOpening::West })
	{
		if (HasRoadOpening(Openings, Opening))
		{
			RoadOpenings.Add(Opening);
		}
	}

	if (RoadOpenings.Num() != 2)
	{
		return;
	}

	const bool bOpposingOpenings =
		(HasRoadOpening(Openings, ERoadOpening::North) && HasRoadOpening(Openings, ERoadOpening::South)) ||
		(HasRoadOpening(Openings, ERoadOpening::East) && HasRoadOpening(Openings, ERoadOpening::West));
	const int32 SegmentCount = bOpposingOpenings ? 2 : 1;
	AppendTiledDashedRoadMarkingSegment(
		Section,
		ConditionedCorners,
		FileX,
		FileY,
		GetRoadOpeningPoint(RoadOpenings[0], TileSize, 0.0f),
		GetRoadOpeningPoint(RoadOpenings[1], TileSize, 0.0f),
		TileSize,
		HalfMapSize,
		TerrainHeightScale,
		ZOffset,
		Width,
		SegmentCount,
		Color);
}

void AppendTerrainTileWithHeights(
	int32 FileX,
	int32 FileY,
	float TileSize,
	float HalfMapSize,
	float Z00,
	float Z10,
	float Z11,
	float Z01,
	int32 AtlasTileIndex,
	FOriginalMeshSectionData& Section)
{
	const int32 VertexStart = Section.Vertices.Num();

	// World Y (the file-Y / row axis) is negated to match the city pass's grid-to-world mapping and
	// global 180-degree yaw (see GetWorldTileCenterCoordinate usage in LoadAndRenderCity).
	const FVector V0(GetWorldGridCoordinate(FileX, TileSize, HalfMapSize), -GetWorldGridCoordinate(FileY, TileSize, HalfMapSize), Z00);
	const FVector V1(GetWorldGridCoordinate(FileX + 1, TileSize, HalfMapSize), -GetWorldGridCoordinate(FileY, TileSize, HalfMapSize), Z10);
	const FVector V2(GetWorldGridCoordinate(FileX + 1, TileSize, HalfMapSize), -GetWorldGridCoordinate(FileY + 1, TileSize, HalfMapSize), Z11);
	const FVector V3(GetWorldGridCoordinate(FileX, TileSize, HalfMapSize), -GetWorldGridCoordinate(FileY + 1, TileSize, HalfMapSize), Z01);

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

void AppendTerrainTile(
	const TArray<int16>& ConditionedCorners,
	int32 FileX,
	int32 FileY,
	float TileSize,
	float TerrainHeightScale,
	float HalfMapSize,
	int32 AtlasTileIndex,
	FOriginalMeshSectionData& Section)
{
	AppendTerrainTileWithHeights(
		FileX,
		FileY,
		TileSize,
		HalfMapSize,
		GetTerrainGridVertexZ(ConditionedCorners, FileX, FileY, TerrainHeightScale),
		GetTerrainGridVertexZ(ConditionedCorners, FileX + 1, FileY, TerrainHeightScale),
		GetTerrainGridVertexZ(ConditionedCorners, FileX + 1, FileY + 1, TerrainHeightScale),
		GetTerrainGridVertexZ(ConditionedCorners, FileX, FileY + 1, TerrainHeightScale),
		AtlasTileIndex,
		Section);
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
	const TSet<int32>& AvailableRuntimeTextureKeys,
	const TSet<int32>& AvailableBakedAtlasPageIds,
	const TSet<int32>& AvailableBakedDirectImageIds,
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
		const bool bAtlasCellInRange = Face.MaterialIndex < FMaxisTextureReader::AtlasColumnCount * FMaxisTextureReader::AtlasColumnCount;
		const bool bBakedAtlasTexturedFace = bUseOriginalTextures && Face.FaceType == 18 && bAtlasCellInRange && AvailableBakedAtlasPageIds.Contains(Face.TextureAtlasIndex);
		const bool bBakedDirectTexturedFace = bUseOriginalTextures && Face.FaceType == 13 && AvailableBakedDirectImageIds.Contains(Face.MaterialIndex);
		const bool bRuntimeTexturedFace = bUseOriginalTextures && !bBakedAtlasTexturedFace && !bBakedDirectTexturedFace && IsTexturedMaxisFace(Face.FaceType) && AvailableRuntimeTextureKeys.Contains(TextureKey);
		const bool bTexturedFace = bBakedAtlasTexturedFace || bBakedDirectTexturedFace || bRuntimeTexturedFace;
		const int32 SectionKey = bBakedAtlasTexturedFace
			? MakeBakedAtlasPageSectionKey(Face.TextureAtlasIndex)
			: (bBakedDirectTexturedFace ? MakeBakedDirectImageSectionKey(Face.MaterialIndex) : (bRuntimeTexturedFace ? TextureKey : INDEX_NONE));
		FOriginalMeshSectionData& Section = Sections.FindOrAdd(SectionKey);
		const int32 FaceVertexStart = Section.Vertices.Num();
		const FLinearColor FaceColor = bTexturedFace
			? FLinearColor::White
			: ResolveMaxisFaceColor(ColorMap, Face.FaceType, Face.MaterialIndex, TexturedFaceFallbackColor);
		const int32 AtlasColumn = static_cast<int32>(Face.MaterialIndex % FMaxisTextureReader::AtlasColumnCount);
		const int32 AtlasRawRow = static_cast<int32>(Face.MaterialIndex / FMaxisTextureReader::AtlasColumnCount);
		const int32 AtlasDecodedRow = FMaxisTextureReader::AtlasColumnCount - 1 - AtlasRawRow;

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
			if (bBakedAtlasTexturedFace)
			{
				Section.UV1.Add(FVector2D(static_cast<float>(AtlasColumn), static_cast<float>(AtlasDecodedRow)));
			}
			Section.VertexColors.Add(FaceColor);
			Section.Tangents.Add(FProcMeshTangent(1.0f, 0.0f, 0.0f));
		}

		const int32 FaceVertexCount = Section.Vertices.Num() - FaceVertexStart;
		if (FaceVertexCount < 3)
		{
			Section.Vertices.SetNum(FaceVertexStart);
			Section.UVs.SetNum(FaceVertexStart);
			if (bBakedAtlasTexturedFace)
			{
				Section.UV1.SetNum(FaceVertexStart);
			}
			Section.VertexColors.SetNum(FaceVertexStart);
			Section.Tangents.SetNum(FaceVertexStart);
			continue;
		}

		// Newell's method for the face normal. A plain cross product of the first three
		// vertices returns a zero vector whenever those three happen to be collinear,
		// which leaves the face with no usable normal and renders it unlit; summing over
		// every edge is immune to that and gives the true area-weighted polygon normal.
		FVector FaceNormal = FVector::ZeroVector;
		for (int32 Index = 0; Index < FaceVertexCount; ++Index)
		{
			const FVector& CurrentVertex = Section.Vertices[FaceVertexStart + Index];
			const FVector& NextVertex = Section.Vertices[FaceVertexStart + ((Index + 1) % FaceVertexCount)];
			FaceNormal.X += (CurrentVertex.Y - NextVertex.Y) * (CurrentVertex.Z + NextVertex.Z);
			FaceNormal.Y += (CurrentVertex.Z - NextVertex.Z) * (CurrentVertex.X + NextVertex.X);
			FaceNormal.Z += (CurrentVertex.X - NextVertex.X) * (CurrentVertex.Y + NextVertex.Y);
		}
		FaceNormal = FaceNormal.GetSafeNormal();
		if (FaceNormal.IsNearlyZero())
		{
			FaceNormal = FVector::UpVector;
		}

		// Orient the normal so the visible surface is lit. Near-horizontal faces are the
		// ground/road/roof surfaces of a city viewed from above, so they must face up toward
		// the sky regardless of where they sit relative to the object centroid. Road asphalt
		// sits *below* its own tile centroid (the road mesh's raised curbs/sidewalk strips
		// pull the centroid up above the asphalt plane), so the old "outward from centroid"
		// test flipped the asphalt to face down and left it unlit with no shadows - while the
		// curb strips, being higher/vertical, oriented correctly and stayed lit. Only truly
		// vertical faces (building walls) still resolve outward from the centroid.
		FVector FaceCenter = FVector::ZeroVector;
		for (int32 Index = 0; Index < FaceVertexCount; ++Index)
		{
			FaceCenter += Section.Vertices[FaceVertexStart + Index];
		}
		FaceCenter /= static_cast<float>(FaceVertexCount);
		if (FMath::Abs(FaceNormal.Z) > 0.85f)
		{
			if (FaceNormal.Z < 0.0f)
			{
				FaceNormal = -FaceNormal;
			}
		}
		else if (FVector::DotProduct(FaceNormal, FaceCenter - ObjectCenter) < 0.0f)
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
	TerrainMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	TerrainMeshComponent->SetCollisionObjectType(ECC_WorldStatic);
	TerrainMeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
	TerrainMeshComponent->bUseComplexAsSimpleCollision = true;
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
	OriginalMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	OriginalMeshComponent->SetCollisionObjectType(ECC_WorldStatic);
	OriginalMeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
	OriginalMeshComponent->bUseComplexAsSimpleCollision = true;
	OriginalMeshComponent->SetCanEverAffectNavigation(false);
	OriginalMeshComponent->SetCastShadow(false);

	RoadMarkingMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("RoadMarkingMeshComponent"));
	RoadMarkingMeshComponent->SetupAttachment(SceneRoot);
	RoadMarkingMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RoadMarkingMeshComponent->SetCanEverAffectNavigation(false);
	RoadMarkingMeshComponent->SetCastShadow(false);

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

	// Project-authored lit materials replace the engine's emissive/unlit debug materials so the
	// city responds to the scene's directional/sky lighting and to dynamic night lights (street
	// lights, car headlights, the helicopter spotlight). Both expose a low "SelfIllum" floor plus
	// "Roughness"/"Specular" scalar parameters so day<->night can be tuned at runtime.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> VertexColorMaterialFinder(TEXT("/Game/Materials/M_SimCopterLitVertexColor.M_SimCopterLitVertexColor"));
	if (VertexColorMaterialFinder.Succeeded())
	{
		VertexColorMaterial = VertexColorMaterialFinder.Object;
		OriginalMeshComponent->SetMaterial(0, VertexColorMaterial);
		RoadMarkingMeshComponent->SetMaterial(0, VertexColorMaterial);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> TexturedMaterialFinder(TEXT("/Game/Materials/M_SimCopterLitTexture.M_SimCopterLitTexture"));
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
	BuildingTileFlags.Reset();
	OriginalTextureCache.Reset();
	OriginalTextureMaterials.Reset();

	TerrainInstances->ClearInstances();
	TerrainMeshComponent->ClearAllMeshSections();
	WaterInstances->ClearInstances();
	RoadInstances->ClearInstances();
	BuildingInstances->ClearInstances();
	OriginalMeshComponent->ClearAllMeshSections();
	RoadMarkingMeshComponent->ClearAllMeshSections();
	ConfigureInstanceComponent(TerrainInstances);
	ConfigureInstanceComponent(WaterInstances);
	ConfigureInstanceComponent(RoadInstances);
	ConfigureInstanceComponent(BuildingInstances);
	TerrainMeshComponent->SetCollisionEnabled(bEnableTerrainCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	OriginalMeshComponent->SetCollisionEnabled(bEnableOriginalMeshCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	RoadMarkingMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	ApplyComponentMaterial(TerrainInstances, TerrainColor);
	ApplyComponentMaterial(WaterInstances, WaterColor);
	ApplyComponentMaterial(RoadInstances, RoadColor);
	ApplyComponentMaterial(BuildingInstances, BuildingColor);
	if (VertexColorMaterial != nullptr)
	{
		OriginalMeshComponent->SetMaterial(0, VertexColorMaterial);
		RoadMarkingMeshComponent->SetMaterial(0, VertexColorMaterial);
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
	BuildingTileFlags.SetNumZeroed(FSimCity2000City::TileCount);
	const int32 TileCountToCache = FMath::Min(FSimCity2000City::TileCount, City.Tiles.Num());
	for (int32 TileIndex = 0; TileIndex < TileCountToCache; ++TileIndex)
	{
		BuildingTileFlags[TileIndex] = IsBuildingLikeTile(City.Tiles[TileIndex].Building) ? 1 : 0;
	}

	FMaxisMeshLibrary MeshLibrary;
	bool bOriginalMeshLibraryLoaded = false;
	FMaxisCompositeBitmap OriginalTextures;
	TMap<int32, UTexture2D*> OriginalTexturesByKey;
	TSet<int32> AvailableOriginalTextureKeys;
	UTexture2D* TerrainTexture = nullptr;
	UTexture2D* HighTerrainTexture = nullptr;
	const FBakedCityAtlasMaterials BakedCityAtlasMaterials = bRenderOriginalTextures ? LoadBakedCityAtlasMaterials() : FBakedCityAtlasMaterials();
	TSet<int32> AvailableBakedAtlasPageIds;
	TSet<int32> AvailableBakedDirectImageIds;
	BakedCityAtlasMaterials.PageMaterials.GetKeys(AvailableBakedAtlasPageIds);
	BakedCityAtlasMaterials.DirectImageMaterials.GetKeys(AvailableBakedDirectImageIds);
	LastOriginalTextureCount += BakedCityAtlasMaterials.NumLoadedMaterials();

	const bool bUseBakedCityMeshTextures = BakedCityAtlasMaterials.PageMaterials.Num() > 0 || BakedCityAtlasMaterials.DirectImageMaterials.Num() > 0;
	const bool bUseBakedTerrainTextures = BakedCityAtlasMaterials.TerrainLowMaterial != nullptr || BakedCityAtlasMaterials.TerrainHighMaterial != nullptr;
	const bool bNeedRuntimeMeshTextures = bRenderOriginalTextures && bRenderOriginalMeshes && TexturedMaterial != nullptr &&
		(BakedCityAtlasMaterials.PageMaterials.Num() == 0 || BakedCityAtlasMaterials.DirectImageMaterials.Num() == 0);
	const bool bNeedRuntimeTerrainTextures = bRenderOriginalTextures && bRenderTerrain && TexturedMaterial != nullptr && !bUseBakedTerrainTextures;
	const bool bNeedRuntimeOriginalTextures = bNeedRuntimeMeshTextures || bNeedRuntimeTerrainTextures;
	bool bOriginalTexturesLoaded = bRenderOriginalTextures && bUseBakedCityMeshTextures;
	const bool bNeedOriginalAssetPalette = bRenderOriginalMeshes || bNeedRuntimeOriginalTextures;
	if (bNeedOriginalAssetPalette)
	{
		FString MeshLibraryError;
		const FString ResolvedOriginalGameRoot = ResolveOriginalGameRoot();
		bOriginalMeshLibraryLoaded = MeshLibrary.LoadFromOriginalGameRoot(ResolvedOriginalGameRoot, MeshLibraryError);
		if (!bOriginalMeshLibraryLoaded)
		{
			UE_LOG(LogSimCity2000CityActor, Warning, TEXT("Could not load original SimCopter meshes: %s"), *MeshLibraryError);
		}
		else if (bNeedRuntimeOriginalTextures)
		{
			const TArray<FColor>* SharedColorMap = MeshLibrary.GetSharedColorMap();
			if (SharedColorMap != nullptr)
			{
				FString TextureError;
				const FString TexturePath = FPaths::Combine(ResolvedOriginalGameRoot, TEXT("BMP/SIM3D.BMP"));
				const bool bRuntimeOriginalTexturesLoaded = FMaxisTextureReader::LoadCompositeBitmapFromFile(TexturePath, *SharedColorMap, OriginalTextures, TextureError);
				if (bRuntimeOriginalTexturesLoaded)
				{
					bOriginalTexturesLoaded = true;
					for (int32 TextureIndex = 0; TextureIndex < OriginalTextures.Images.Num(); ++TextureIndex)
					{
						const bool bNeedDirectImageTexture = bNeedRuntimeMeshTextures && !BakedCityAtlasMaterials.DirectImageMaterials.Contains(TextureIndex);
						const bool bNeedHighTerrainTexture = bNeedRuntimeTerrainTextures && TextureIndex == SimCopterHighTerrainAtlasImageIndex;
						if ((bNeedDirectImageTexture || bNeedHighTerrainTexture) && AddOriginalTexture(
							MakeMaxisTextureKey(0, static_cast<uint8>(TextureIndex)),
							OriginalTextures.Images[TextureIndex],
							this,
							OriginalTexturesByKey,
							AvailableOriginalTextureKeys,
							OriginalTextureCache))
						{
							++LastOriginalTextureCount;
						}

						if (bNeedRuntimeMeshTextures &&
							!BakedCityAtlasMaterials.PageMaterials.Contains(TextureIndex) &&
							OriginalTextures.Images[TextureIndex].Width == FMaxisTextureReader::AtlasTileSize * FMaxisTextureReader::AtlasColumnCount &&
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
					if (bNeedRuntimeMeshTextures &&
						!BakedCityAtlasMaterials.PageMaterials.Contains(SimCopterSkyGroundTextureFile) &&
						FMaxisTextureReader::LoadCompositeBitmapFromFile(SkyTexturePath, *SharedColorMap, SkyTextures, SkyTextureError))
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
					else if (bNeedRuntimeMeshTextures && !BakedCityAtlasMaterials.PageMaterials.Contains(SimCopterSkyGroundTextureFile))
					{
						UE_LOG(LogSimCity2000CityActor, Warning, TEXT("Could not load original SimCopter sky/ground atlas: %s"), *SkyTextureError);
					}

					FMaxisCompositeBitmap TerrainTextures;
					FString TerrainTextureError;
					const FString TerrainTexturePath = FPaths::Combine(ResolvedOriginalGameRoot, TEXT("BMP/TILED1.BMP"));
					if (bNeedRuntimeTerrainTextures && BakedCityAtlasMaterials.TerrainLowMaterial == nullptr &&
						FMaxisTextureReader::LoadCompositeBitmapFromFile(TerrainTexturePath, *SharedColorMap, TerrainTextures, TerrainTextureError))
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
					else if (bNeedRuntimeTerrainTextures && BakedCityAtlasMaterials.TerrainLowMaterial == nullptr)
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
	FOriginalMeshSectionData RoadMarkingSection;
	TMap<int32, FOriginalMeshSectionData> OriginalMeshSections;
	const bool bUseTexturedTerrainSurface = BakedCityAtlasMaterials.TerrainLowMaterial != nullptr || BakedCityAtlasMaterials.TerrainHighMaterial != nullptr ||
		((TerrainTexture != nullptr || HighTerrainTexture != nullptr) && TexturedMaterial != nullptr);
	const bool bUseHighTerrainAtlas = BakedCityAtlasMaterials.TerrainHighMaterial != nullptr || (HighTerrainTexture != nullptr && TexturedMaterial != nullptr);

	// The original conditions the tmap corner grid before anything reads it: flatten under
	// buildings/flat network tiles, wedge ramps under raised spans, dip water corners.
	const TArray<int16> ConditionedTerrainCorners = BuildConditionedTerrainCornerSamples(City);

	// SimCopter selects each ground tile's TILED1 atlas cell from a per-tile terrain type code
	// (the type IS the cell index). Reproduce that grid once for the whole map.
	const TArray<uint8> TerrainTypeGrid = BuildTerrainTextureTypeGrid(City, ConditionedTerrainCorners);
	const FExtendedTerrainData ExtendedTerrain = BuildProceduralExtendedTerrain(
		City,
		ConditionedTerrainCorners,
		TerrainTypeGrid,
		(bRenderTerrain && bRenderProceduralMapExtension) ? ProceduralMapExtensionTiles : 0,
		EffectiveTerrainHeightScale);

	int32 TerrainCount = 0;
	int32 ExtensionTerrainCount = 0;
	int32 WaterCount = 0;
	int32 ExtensionWaterCount = 0;
	int32 RoadCount = 0;
	int32 BuildingCount = 0;
	int32 OriginalMeshTriangleCount = 0;
	bool bOriginalSpecialE7BuildingPlaced = false;

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
					ConditionedTerrainCorners,
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

			if (bRenderRoadMarkings && bRenderRoads)
			{
				AppendRoadMarkingsForTile(
					RoadMarkingSection,
					ConditionedTerrainCorners,
					Tile.Building,
					FileX,
					FileY,
					TileSize,
					HalfMapSize,
					EffectiveTerrainHeightScale,
					OriginalMeshZOffset + RoadMarkingZOffset,
					RoadMarkingWidth,
					RoadMarkingColor);
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
					// Bridges/elevated roads and buildings are dispatched by the original builder to
					// specific object Ids rather than through the heuristic XBLD->mesh table.
					const bool bUseBridgeDispatch = Tile.Building >= 0x3f && Tile.Building <= 0x6b;
					const FOriginalBridgeDispatch BridgeDispatch = bUseBridgeDispatch
						? GetOriginalBridgeDispatch(Tile.Building, IsOriginalTerrainTileFlat(ConditionedTerrainCorners, FileX, FileY), Tile.BitFlags)
						: FOriginalBridgeDispatch();
					const FOriginalCityObjectDispatch BuildingDispatch = (!bUseBridgeDispatch && bBuildingLikeTile)
						? GetOriginalBuildingDispatch(
							Tile.Building,
							Tile.Zone,
							Tile.BitFlags,
							FSimCopterPeopleCityRules::GetFootprintSizeForBuildingId(Tile.Building),
							City.Rotation,
							bOriginalSpecialE7BuildingPlaced)
						: FOriginalCityObjectDispatch();
					const int32 PrimaryObjectId = BridgeDispatch.PrimaryObjectId != INDEX_NONE
						? BridgeDispatch.PrimaryObjectId
						: BuildingDispatch.PrimaryObjectId;
					const int32 SecondaryObjectId = BridgeDispatch.SecondaryObjectId != INDEX_NONE
						? BridgeDispatch.SecondaryObjectId
						: BuildingDispatch.SecondaryObjectId;
					const FMaxisMeshObject* MeshObject = (PrimaryObjectId != INDEX_NONE)
						? MeshLibrary.FindObjectByObjectId(PrimaryObjectId, &ColorMap)
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
							AvailableBakedAtlasPageIds,
							AvailableBakedDirectImageIds,
							OriginalTexturedFaceFallbackColor,
							OriginalMeshSections,
							LastOriginalTexturedTriangleCount);

						if (SecondaryObjectId != INDEX_NONE)
						{
							const TArray<FColor>* SecondaryColorMap = nullptr;
							const FMaxisMeshObject* SecondaryMeshObject = MeshLibrary.FindObjectByObjectId(SecondaryObjectId, &SecondaryColorMap);
							if (SecondaryMeshObject != nullptr)
							{
								OriginalMeshTriangleCount += AppendMaxisMeshObject(
									*SecondaryMeshObject,
									SecondaryColorMap,
									TileOrigin,
									OriginalMeshUnitsPerCentimeter,
									OriginalMeshScale,
									bRenderOriginalMeshBackfaces,
									bOriginalTexturesLoaded,
									AvailableOriginalTextureKeys,
									AvailableBakedAtlasPageIds,
									AvailableBakedDirectImageIds,
									OriginalTexturedFaceFallbackColor,
									OriginalMeshSections,
									LastOriginalTexturedTriangleCount);
							}
							else
							{
								UE_LOG(LogSimCity2000CityActor, Warning, TEXT("Could not resolve secondary object id 0x%x for XBLD 0x%x."), SecondaryObjectId, Tile.Building);
							}
						}

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

	if (bRenderTerrain && ExtendedTerrain.IsEnabled())
	{
		for (int32 FileY = ExtendedTerrain.MinTileCoordinate; FileY < ExtendedTerrain.MinTileCoordinate + ExtendedTerrain.TileGridSize; ++FileY)
		{
			for (int32 FileX = ExtendedTerrain.MinTileCoordinate; FileX < ExtendedTerrain.MinTileCoordinate + ExtendedTerrain.TileGridSize; ++FileX)
			{
				if (ExtendedTerrain.IsOriginalMapTile(FileX, FileY))
				{
					continue;
				}

				const uint8 TerrainType = ExtendedTerrain.GetTerrainType(FileX, FileY);
				const bool bUseHighPageForTile = TerrainType >= SimCopterHighTerrainTypeBase && bUseHighTerrainAtlas;
				const int32 TerrainAtlasTileIndex = bUseHighPageForTile
					? static_cast<int32>(TerrainType - SimCopterHighTerrainTypeBase)
					: static_cast<int32>(TerrainType & 0x3f);
				const float Z00 = ExtendedTerrain.GetGridVertexZ(FileX, FileY);
				const float Z10 = ExtendedTerrain.GetGridVertexZ(FileX + 1, FileY);
				const float Z11 = ExtendedTerrain.GetGridVertexZ(FileX + 1, FileY + 1);
				const float Z01 = ExtendedTerrain.GetGridVertexZ(FileX, FileY + 1);
				AppendTerrainTileWithHeights(
					FileX,
					FileY,
					TileSize,
					HalfMapSize,
					Z00,
					Z10,
					Z11,
					Z01,
					TerrainAtlasTileIndex,
					bUseHighPageForTile ? TerrainPage0DSection : TerrainPage14Section);
				++TerrainCount;
				++ExtensionTerrainCount;

				if (bRenderWater && ExtendedTerrain.IsWaterTile(FileX, FileY) && !bUseTexturedTerrainSurface)
				{
					const float WaterThickness = FMath::Max(RoadPlateHeight, 6.0f);
					const FVector WaterScale(TileSize * CubeToUnrealScale, TileSize * CubeToUnrealScale, WaterThickness * CubeToUnrealScale);
					const float WorldX = GetWorldTileCenterCoordinate(static_cast<float>(FileX), TileSize, HalfMapSize);
					const float WorldY = -GetWorldTileCenterCoordinate(static_cast<float>(FileY), TileSize, HalfMapSize);
					const float TerrainTopZ = (Z00 + Z10 + Z11 + Z01) * 0.25f;
					const FTransform WaterTransform(FRotator::ZeroRotator, FVector(WorldX, WorldY, TerrainTopZ + WaterThickness * 0.5f + 1.0f), WaterScale);
					WaterInstances->AddInstance(WaterTransform);
					++WaterCount;
					++ExtensionWaterCount;
				}
			}
		}
	}

	int32 TerrainMeshSectionIndex = 0;
	auto CreateTerrainSurfaceSection = [&](const FOriginalMeshSectionData& TerrainSection, UMaterialInterface* SurfaceMaterial, UTexture2D* SurfaceTexture)
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
			bEnableTerrainCollision);

		if (SurfaceMaterial != nullptr)
		{
			TerrainMeshComponent->SetMaterial(SectionIndex, SurfaceMaterial);
		}
		else if (SurfaceTexture != nullptr && TexturedMaterial != nullptr)
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

	CreateTerrainSurfaceSection(TerrainPage14Section, BakedCityAtlasMaterials.TerrainLowMaterial, TerrainTexture);
	CreateTerrainSurfaceSection(TerrainPage0DSection, BakedCityAtlasMaterials.TerrainHighMaterial, HighTerrainTexture);

	if (RoadMarkingSection.Vertices.Num() > 0)
	{
		CreateOriginalMeshSection(
			RoadMarkingMeshComponent,
			0,
			RoadMarkingSection,
			false);
		if (VertexColorMaterial != nullptr)
		{
			RoadMarkingMeshComponent->SetMaterial(0, VertexColorMaterial);
		}
	}

	int32 MeshSectionIndex = 0;
	if (const FOriginalMeshSectionData* PaletteSection = OriginalMeshSections.Find(INDEX_NONE))
	{
		if (PaletteSection->Vertices.Num() > 0)
		{
			CreateOriginalMeshSection(
				OriginalMeshComponent,
				MeshSectionIndex,
				*PaletteSection,
				bEnableOriginalMeshCollision);
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
		if (TextureSection == nullptr || TextureSection->Vertices.Num() == 0)
		{
			continue;
		}

		UMaterialInterface* SectionMaterial = nullptr;
		UTexture2D* RuntimeTexture = nullptr;
		if (IsBakedAtlasPageSectionKey(TextureKey))
		{
			if (UMaterialInterface* const* BakedMaterial = BakedCityAtlasMaterials.PageMaterials.Find(GetBakedSectionAssetIndex(TextureKey)))
			{
				SectionMaterial = *BakedMaterial;
			}
		}
		else if (IsBakedDirectImageSectionKey(TextureKey))
		{
			if (UMaterialInterface* const* BakedMaterial = BakedCityAtlasMaterials.DirectImageMaterials.Find(GetBakedSectionAssetIndex(TextureKey)))
			{
				SectionMaterial = *BakedMaterial;
			}
		}
		else
		{
			if (UTexture2D* const* Texture = OriginalTexturesByKey.Find(TextureKey))
			{
				RuntimeTexture = *Texture;
			}
		}

		if (SectionMaterial == nullptr && (RuntimeTexture == nullptr || TexturedMaterial == nullptr))
		{
			continue;
		}

		CreateOriginalMeshSection(
			OriginalMeshComponent,
			MeshSectionIndex,
			*TextureSection,
			bEnableOriginalMeshCollision);

		if (SectionMaterial != nullptr)
		{
			OriginalMeshComponent->SetMaterial(MeshSectionIndex, SectionMaterial);
		}
		else
		{
			UMaterialInstanceDynamic* TextureMaterial = UMaterialInstanceDynamic::Create(TexturedMaterial, this);
			if (TextureMaterial != nullptr)
			{
				TextureMaterial->SetTextureParameterValue(TEXT("Texture"), RuntimeTexture);
				OriginalTextureMaterials.Add(TextureMaterial);
				OriginalMeshComponent->SetMaterial(MeshSectionIndex, TextureMaterial);
			}
		}

		++MeshSectionIndex;
	}
	LastOriginalMeshTriangleCount = OriginalMeshTriangleCount;

	UE_LOG(
		LogSimCity2000CityActor,
		Display,
		TEXT("Rendered SC2 city '%s' from '%s': terrain=%d extensionTerrain=%d water=%d extensionWater=%d roads=%d buildings=%d originalMeshTiles=%d missingOriginalMeshTiles=%d originalTriangles=%d texturedTriangles=%d originalTextures=%d chunks=%d rotation=%d waterLevel=%d terrainHeightScale=%.2f"),
		*City.CityName,
		*ResolvedCityPath,
		TerrainCount,
		ExtensionTerrainCount,
		WaterCount,
		ExtensionWaterCount,
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

FString ASimCity2000CityActor::GetResolvedCityPath() const
{
	return ResolveCityPath();
}

FString ASimCity2000CityActor::GetResolvedOriginalGameRoot() const
{
	return ResolveOriginalGameRoot();
}

float ASimCity2000CityActor::GetTileSize() const
{
	return TileSize;
}

bool ASimCity2000CityActor::UsesOriginalTerrainHeightScale() const
{
	return bUseOriginalTerrainHeightScale;
}

float ASimCity2000CityActor::GetTerrainHeightScale() const
{
	return TerrainHeightScale;
}

float ASimCity2000CityActor::GetEffectiveTerrainHeightScale() const
{
	return bUseOriginalTerrainHeightScale ? TileSize * 0.5f : TerrainHeightScale;
}

bool ASimCity2000CityActor::IsBuildingCollisionHit(
	const UPrimitiveComponent* HitComponent,
	const FVector& WorldLocation) const
{
	if (HitComponent == nullptr)
	{
		return false;
	}

	if (HitComponent == BuildingInstances.Get())
	{
		return true;
	}

	if (HitComponent == RoadInstances.Get() ||
		HitComponent == TerrainInstances.Get() ||
		HitComponent == TerrainMeshComponent.Get() ||
		HitComponent == WaterInstances.Get() ||
		HitComponent == RoadMarkingMeshComponent.Get())
	{
		return false;
	}

	if (HitComponent != OriginalMeshComponent.Get() ||
		BuildingTileFlags.Num() != FSimCity2000City::TileCount ||
		TileSize <= UE_SMALL_NUMBER)
	{
		return false;
	}

	constexpr int32 MapSize = FSimCity2000City::MapSize;
	const float HalfMapSize = MapSize * TileSize * 0.5f;
	const FVector LocalLocation = GetActorTransform().InverseTransformPosition(WorldLocation);
	const int32 FileX = FMath::FloorToInt((LocalLocation.X + HalfMapSize) / TileSize);
	const int32 FileY = FMath::FloorToInt((HalfMapSize - LocalLocation.Y) / TileSize);
	if (FileX < 0 || FileX >= MapSize || FileY < 0 || FileY >= MapSize)
	{
		return false;
	}

	return BuildingTileFlags[FileY * MapSize + FileX] != 0;
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
	Component->SetCollisionEnabled(bEnablePlaceholderCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	Component->SetCollisionObjectType(ECC_WorldStatic);
	Component->SetCollisionResponseToAllChannels(ECR_Block);
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
