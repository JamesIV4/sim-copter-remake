// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/SimCity2000CityActor.h"

#include "Algo/Count.h"
#include "City/SimCopterAirport.h"
#include "City/SimCopterCityGeometryRules.h"
#include "City/SimCopterRuntimeStaticMesh.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Formats/MaxisMeshLibrary.h"
#include "Formats/MaxisProceduralMeshBuilder.h"
#include "Formats/MaxisMeshReader.h"
#include "Formats/MaxisTextureReader.h"
#include "Formats/SimCopterOriginalGamePaths.h"
#include "Formats/SimCopterPeopleCityRules.h"
#include "Formats/SimCity2000Reader.h"
#include "Ground/SimCopterFlashingLights.h"
#include "City/SimCopterRoadDecorations.h"
#include "City/SimCopterSmokeStacks.h"
#include "City/SimCopterStreetLights.h"
#include "Flight/SimCopterWaterGameplay.h"
#include "Game/SimCopterLowPowerMode.h"
#include "Game/SimCopterSessionSubsystem.h"
#include "Engine/GameInstance.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCity2000CityActor, Log, All);

namespace
{
// Buildings and rubble are drawn by UInstancedStaticMeshComponents, and a base material that has
// never been marked "used with instanced static meshes" is silently replaced by the default
// checkerboard material ("missing usage flag InstancedStaticMeshes! Default Material will be used
// in game"). That is what turned every building into a checkerboard in -game while they looked
// right in the editor, which sets the flag on first use. The flag is saved on the base materials
// too; this keeps a freshly authored or re-imported material from regressing the whole city.
void EnsureInstancedStaticMeshUsage(UMaterialInterface* Material)
{
#if WITH_EDITOR
	if (Material == nullptr)
	{
		return;
	}

	if (UMaterial* BaseMaterial = Material->GetMaterial())
	{
		BaseMaterial->SetMaterialUsage(MATUSAGE_InstancedStaticMeshes);
	}
#endif
}

FLinearColor ResolveMaxisFaceColor(const TArray<FColor>* ColorMap, uint8 FaceType, uint8 MaterialIndex, const FLinearColor& TexturedFaceFallbackColor)
{
	if (FaceType == 13 || FaceType == 18 || FaceType == 2)
	{
		return TexturedFaceFallbackColor;
	}

	if (ColorMap != nullptr && ColorMap->IsValidIndex(MaterialIndex))
	{
		return FLinearColor((*ColorMap)[MaterialIndex]);
	}

	return FLinearColor::White;
}

// Face type 2 is a textured SPRITE CARD, not a line. Its FACE record carries two vertex indices,
// but they are not the ends of a segment: the first is the card's centre and the second is its
// (+halfWidth, +halfHeight) corner, so the card spans centre +/- (delta) and stands on the ground.
// The tree objects prove the encoding - TREE6 stores the card corners alongside a base vertex at
// (cardCentreX + halfWidth, 0) and a top vertex at (cardCentreX + halfWidth, 2 * centreY), i.e.
// exactly the bottom-right and top-right corners of the rectangle the two face vertices describe.
// The face's two UV entries are the 0.1/0.9 corners of the sprite rect inside direct image
// MaterialIndex. Drawing these as palette-coloured line segments is what made every tree in the
// city render as a white or orange stick.
bool IsTexturedMaxisFace(uint8 FaceType)
{
	return FaceType == 13 || FaceType == 18 || FaceType == 2;
}

bool IsMaxisSpriteCardFace(const FMaxisMeshFace& Face)
{
	return Face.FaceType == 2 && Face.VertexIndices.Num() == 2;
}

// SIM3D page 2 cell 0 is a digitised photograph of a face - a Maxis test image, not artwork the
// shipped game ever puts on screen. Exactly four objects sample it, and always as the last face of
// the model: CO182's wall billboard (one panel) and the front/back pair on CO124, CO126 and CO127's
// street signs. Those panels are standalone boards, so dropping the faces drops the sign and leaves
// no hole - there is no wall behind them. Measured with Docs/scratchpad/list_atlas_cell_users.py;
// the neighbouring cells on the same page are the police shield (8), hospital H (9), helipad
// crosshair (10) and fire F (11), which is what fixes the cell numbering.
constexpr uint8 DebugPortraitAtlasPage = 2;
constexpr uint8 DebugPortraitAtlasCell = 0;

bool IsDebugPortraitFace(const FMaxisMeshFace& Face)
{
	return Face.FaceType == 18 &&
		Face.TextureAtlasIndex == DebugPortraitAtlasPage &&
		Face.MaterialIndex == DebugPortraitAtlasCell;
}

int32 MakeMaxisTextureKey(uint8 TextureFile, uint8 TextureNumber)
{
	return (static_cast<int32>(TextureFile) << 8) | static_cast<int32>(TextureNumber);
}

int32 GetMaxisFaceTextureKey(const FMaxisMeshFace& Face)
{
	// Face type 2 keys the same direct-image space as 13 (TextureAtlasIndex is always 0 on them).
	if (Face.FaceType == 13 || Face.FaceType == 2)
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
// Untextured face type 11 - the alpha-blended disc - gets its own section so it can be drawn with
// a translucent material instead of landing in the opaque INDEX_NONE palette section.
constexpr int32 TranslucentDiscSectionKeyFlag = 0x40000;

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

// Identifies a distinct building model. Two tiles resolving to the same ids produce identical
// local-space geometry - only their placement differs - so they share one static mesh and one
// collision cook, and differ only by instance transform.
struct FBuildingModelKey
{
	int32 PrimaryObjectId = INDEX_NONE;
	int32 SecondaryObjectId = INDEX_NONE;
	int32 MeshTileId = INDEX_NONE;

	bool operator==(const FBuildingModelKey& Other) const
	{
		return PrimaryObjectId == Other.PrimaryObjectId &&
			SecondaryObjectId == Other.SecondaryObjectId &&
			MeshTileId == Other.MeshTileId;
	}
};

uint32 GetTypeHash(const FBuildingModelKey& Key)
{
	return HashCombine(
		HashCombine(::GetTypeHash(Key.PrimaryObjectId), ::GetTypeHash(Key.SecondaryObjectId)),
		::GetTypeHash(Key.MeshTileId));
}

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
	// Average world Z of the city's water tiles - the datum the cockpit altimeter reads from.
	float OceanSurfaceZ = 0.0f;
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

bool IsTranslucentDiscSectionKey(int32 SectionKey)
{
	return SectionKey != INDEX_NONE && (SectionKey & TranslucentDiscSectionKeyFlag) != 0;
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

// Converts one model's local-space sections into a runtime static mesh, one material slot per
// section, using the same per-section material the merged path would have picked. A section whose
// material cannot be resolved is dropped, exactly as the merged path skips it.
UStaticMesh* BuildBuildingModelStaticMesh(
	UObject* Outer,
	const TMap<int32, FOriginalMeshSectionData>& Sections,
	TFunctionRef<UMaterialInterface*(int32 SectionKey)> ResolveMaterial)
{
	TArray<int32> SectionKeys;
	Sections.GetKeys(SectionKeys);
	// Palette section first, then texture keys ascending, matching the merged mesh's order.
	SectionKeys.Sort();

	TArray<FSimCopterRuntimeMeshSection> RuntimeSections;
	RuntimeSections.Reserve(SectionKeys.Num());
	for (const int32 SectionKey : SectionKeys)
	{
		const FOriginalMeshSectionData& Source = Sections[SectionKey];
		if (Source.Vertices.Num() == 0 || Source.Triangles.Num() < 3)
		{
			continue;
		}

		UMaterialInterface* Material = ResolveMaterial(SectionKey);
		if (Material == nullptr)
		{
			continue;
		}

		FSimCopterRuntimeMeshSection& Runtime = RuntimeSections.AddDefaulted_GetRef();
		Runtime.Vertices = Source.Vertices;
		Runtime.Triangles = Source.Triangles;
		Runtime.Normals = Source.Normals;
		Runtime.UV0 = Source.UVs;
		Runtime.UV1 = Source.UV1;
		Runtime.VertexColors = Source.VertexColors;
		Runtime.Tangents = Source.Tangents;
		Runtime.Material = Material;
	}

	if (RuntimeSections.Num() == 0)
	{
		return nullptr;
	}

	return SimCopterRuntimeStaticMesh::Build(Outer, RuntimeSections, /*bWithComplexCollision*/ true);
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

// XBLD 0x0D, the only "natural" tile that is a built pad rather than foliage: object 0x143, LP13,
// named "small park" in the GEO. 0x06..0x0C are TREE6..TREE12, actual trees.
bool IsOriginalParkTile(uint8 BuildingId)
{
	return BuildingId == 0x0D;
}

// Which surface ramps keep the original's untouched tmap instead of the remake's one-step wedge
// (see the 0x1f..0x22 case in BuildConditionedTerrainCornerSamples).
//
// The wedge exists to stop a ramp between two flattened streets from floating, and that is the only
// case it is wanted in. A ramp climbing onto a bridge deck already has its grade resolved by the
// raised-span rule and the bridge object's own one-step top, so wedging the ground as well drives
// it up into the deck; and a ramp beside water must leave the shoreline alone, since raising a
// corner there pushes land through the water surface. Both keep the decoded behaviour.
//
// The neighbourhood is all eight surrounding cells, not just the four edge-sharing ones: this pass
// writes CORNER samples, and a diagonal neighbour shares one of them.
bool IsRampTerrainClampSuppressed(const FSimCity2000City& City, int32 FileX, int32 FileY)
{
	constexpr int32 MapSize = FSimCity2000City::MapSize;
	for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
	{
		for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
		{
			const int32 NeighborX = FileX + OffsetX;
			const int32 NeighborY = FileY + OffsetY;
			if (NeighborX < 0 || NeighborX >= MapSize || NeighborY < 0 || NeighborY >= MapSize)
			{
				continue;
			}

			const FSimCity2000Tile& Neighbor = City.Tiles[NeighborY * MapSize + NeighborX];
			if (Neighbor.bWater || ASimCity2000CityActor::IsOneStepRaisedRoadDeckTile(Neighbor.Building))
			{
				return true;
			}
		}
	}

	return false;
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
				(Building >= 0x32 && Building <= 0x3A) ||
				// Divergence, deliberate: 0x43/0x44 are not in the original's flatten set because
				// there the power crossing brought its own road slab and its own sloped RD67H/RD68H
				// variant. The remake gives those tiles the ordinary straight-road piece instead
				// (GetOriginalBridgeDispatch), so they need the same auto-flatten every other flat
				// street gets - without it the crossing stands up out of the road as a raised block.
				Building == 0x43 || Building == 0x44;
			// Divergence, deliberate: the original leaves 0x0D on whatever grade the ALTM had,
			// because it treats the whole 0x06..0x0D band as natural cover. But LP13 is not cover -
			// it is a flat authored slab with paths and benches printed on it, the same kind of pad
			// every building sits on. On a slope its corners cut into the hill on one side and hang
			// in the air on the other. The trees either side of it in the band genuinely are foliage
			// and stay on the slope, which is why this is the one id and not the range.
			if (Building >= 0x70 || bFlatNetworkTile || IsOriginalParkTile(Building))
			{
				const int32 Sample = GetTerrainHeightMapSample(Tile);
				Set(FileX, FileY, Sample);
				Set(FileX + 1, FileY, Sample);
				Set(FileX, FileY + 1, Sample);
				Set(FileX + 1, FileY + 1, Sample);
			}
			// Surface road ramps RD31..RD34. Divergence, deliberate: the original leaves the tmap
			// alone here and lets the piece span whatever grade the ALTM already had, but the remake
			// flattens every road tile around them, which drags the shared corners to the
			// neighbours' levels and leaves the ramp deck hanging in the air. Wedge the tile to the
			// ramp's own grade instead - the same shape as cases 0x3f..0x42 below, but anchored to
			// this tile's own ALTM sample rather than a neighbouring corner, because that sample is
			// exactly where GetAverageTerrainSurfaceZ places the ramp mesh. The terrain then meets
			// the deck at both edges whatever order the sweep visits the neighbours in.
			//
			// One step is the right rise: each ramp's authored asphalt climbs 32 mesh units, and
			// 32 units x 25 cm x the 400/1600 mesh scale = 200 cm = one TerrainHeightScale = 0x20.
			// The high edges are measured off the meshes (Docs/scratchpad/dump_ramp_direction.py),
			// not guessed: north, west, south, east - the same order as rail slopes 0x2e..0x31.
			//
			// Bridge approaches and shoreline ramps opt out - see IsRampTerrainClampSuppressed.
			else if (Building >= 0x1F && Building <= 0x22 && !IsRampTerrainClampSuppressed(City, FileX, FileY))
			{
				const int32 LowSample = GetTerrainHeightMapSample(Tile);
				const int32 HighSample = LowSample + 0x20;
				const bool bHighNorth = Building == 0x1F;
				const bool bHighWest = Building == 0x20;
				const bool bHighSouth = Building == 0x21;
				const bool bHighEast = Building == 0x22;
				// Grid row FileY is the north edge, column FileX the west edge.
				Set(FileX, FileY, bHighNorth || bHighWest ? HighSample : LowSample);
				Set(FileX + 1, FileY, bHighNorth || bHighEast ? HighSample : LowSample);
				Set(FileX, FileY + 1, bHighSouth || bHighWest ? HighSample : LowSample);
				Set(FileX + 1, FileY + 1, bHighSouth || bHighEast ? HighSample : LowSample);
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
	// Power line over road. The original packs the road and the pylon into one object (RD67/RD68,
	// or RD67H/RD68H on a slope), and that packed road half is a different piece of asphalt from the
	// street either side of it - it reads as a raised block across the crossing and carries its own
	// centre line at its own cadence. Split it the way 0x45/0x46 split the rail crossing instead:
	// the ordinary straight-road piece is the primary, so the tile gets the same surface and the
	// same procedural dashes as its neighbours, and the crossing object rides along as the secondary
	// for its pylon and wires. AppendMaxisMeshObject drops the secondary's asphalt and centre line.
	case 0x43: return { bTileIsFlat ? 0x3b : 0x1d, bTileIsFlat ? 0x128 : 0x17f };
	case 0x44: return { bTileIsFlat ? 0x3c : 0x1e, bTileIsFlat ? 0x129 : 0x180 };
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

// Rail network tile dispatch transcribed from FUN_0047c0c0 (XBLD 0x2c..0x3e). Unlike the roads
// above it there is no flat/sloped variant test - it is a flat table - but it is emphatically NOT
// the identity, which is the trap: the rail meshes are named RL44..RL58 after the SimCity piece
// number, and their GEO *object ids* are permuted across 0x2e..0x35 relative to those names. XBLD
// 0x2e (piece 46, a slope) lives at object id 0x32, while XBLD 0x32 (piece 50, a diagonal) lives
// at object id 0x2e. The heuristic XBLD->mesh table resolves by object id, so it draws a diagonal
// where a slope belongs and a slope where a diagonal belongs - which breaks the line visibly at
// every grade change. XBLD 0x3b..0x3e (rail crossing under a road) have no RL object of their own
// and reuse the two straight pieces.
int32 GetOriginalRailTileObjectId(uint8 BuildingId)
{
	switch (BuildingId)
	{
	case 0x2c: return 0x2c; // RL44 straight
	case 0x3c: return 0x2c;
	case 0x3e: return 0x2c;
	case 0x2d: return 0x2d; // RL45 straight, other axis
	case 0x3b: return 0x2d;
	case 0x3d: return 0x2d;
	case 0x2e: return 0x32; // RL46..RL49: the four slope pieces
	case 0x2f: return 0x33;
	case 0x30: return 0x34;
	case 0x31: return 0x35;
	case 0x32: return 0x2e; // RL50..RL53: the four diagonal (corner) pieces
	case 0x33: return 0x2f;
	case 0x34: return 0x30;
	case 0x35: return 0x31;
	case 0x36: return 0x36; // RL54..RL58: junctions and crossings, identity
	case 0x37: return 0x37;
	case 0x38: return 0x38;
	case 0x39: return 0x39;
	case 0x3a: return 0x3a;
	default: return INDEX_NONE;
	}
}

// Surface-road tile dispatch transcribed from FUN_0047c0c0's XBLD 0x1d..0x2b cases. Every one of
// them runs the same four-corner tmap comparison the bridges use and picks a DIFFERENT mesh for a
// flat tile than for a sloped one: RD29/RD30 and RD35..RD43 carry a raised curb along their edges
// because they have to meet the terrain across a grade change, and RD29L/RD30L/RD35L..RD43L are the
// flat, curbless slabs laid on level ground. The two sets are object ids 0x1d..0x2b and 0x3b..0x45.
//
// The remake resolved these through the heuristic XBLD->mesh table instead, and that table scores
// the plain "RD29" name above the "RD29L" variant (MappingVariantPenalty in MaxisMeshLibrary
// demotes the F/H/L suffixes), so every road tile in the city drew the sloped, curbed piece - a
// curb down both sides of every flat street. Only 0x1f..0x22, the four dedicated slope pieces, have
// no flat counterpart and are unconditional in the original too.
int32 GetOriginalRoadTileObjectId(uint8 BuildingId, bool bTileIsFlat)
{
	if (BuildingId < 0x1d || BuildingId > 0x2b)
	{
		return INDEX_NONE;
	}

	if (bTileIsFlat)
	{
		switch (BuildingId)
		{
		case 0x1d: return 0x3b; // RD29L, straight east-west
		case 0x1e: return 0x3c; // RD30L, straight north-south
		case 0x23: return 0x3d; // RD35L..RD43L, the corners, tees and crossroads
		case 0x24: return 0x3e;
		case 0x25: return 0x3f;
		case 0x26: return 0x40;
		case 0x27: return 0x41;
		case 0x28: return 0x42;
		case 0x29: return 0x43;
		case 0x2a: return 0x44;
		case 0x2b: return 0x45;
		default: break; // 0x1f..0x22 are slope-only; they fall through to the sloped piece.
		}
	}

	// The sloped/fallback piece is the object whose id equals the XBLD id (RD29..RD43).
	return static_cast<int32>(BuildingId);
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

// Natural scenery dispatch transcribed from FUN_0047c0c0's `bVar2 < 0x1d` switch: the seven
// SimCity 2000 tree densities (XBLD 0x06 "Trees 1" .. 0x0C "Trees 7") and the small park
// (0x0D) each place one runtime object. The original also takes the MINIMUM conditioned corner
// sample over the tile as the placement height (`local_3a`) instead of the flattened pad the
// building path uses, so a tree on a slope sits on the low corner rather than floating.
//
// These tiles used to render nothing at all, which is not only a visual gap: the ambient people
// spawner's placement sampler (FUN_004c02a0) rejects a sampled point whose walked surface
// (FUN_004c82c0 = max of the scene cell's object meshes) rises more than 10 original units above
// the cell base. With no canopy geometry to hit, every forest tile handed out a spawn point - and
// a forest tile is people tile class 3, whose only DAT_0058ec00 matches are behaviour class 10
// (dog) and 17 (cow). Missing trees were therefore also the reason the city filled with animals.
// FUN_0047c0c0's `if (bVar2 < 5)` arm, taken before the `bVar2 < 0x1d` switch: all four SimCity
// 2000 rubble ids share one GRUBBLE1 debris mesh. Unlike the trees this keeps the default
// `local_3a` placement height, so it sits on the tile surface like roads and buildings do.
// (XBLD 0x05, radioactive waste, deliberately places nothing - it falls through to the switch
// default.)
int32 GetOriginalRubbleObjectId(uint8 BuildingId)
{
	return (BuildingId >= 0x01 && BuildingId <= 0x04) ? 0x14F : INDEX_NONE;
}

int32 GetOriginalNaturalObjectId(uint8 BuildingId)
{
	switch (BuildingId)
	{
	case 0x06: return 0x10D;
	case 0x07: return 0x10E;
	case 0x08: return 0x10F;
	case 0x09: return 0x110;
	case 0x0A: return 0x111;
	case 0x0B: return 0x112;
	case 0x0C: return 0x113;
	case 0x0D: return 0x143;
	default: return INDEX_NONE;
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
	// Handed back so the cockpit altimeter can read zero at the water rather than at the bottom
	// of the terrain range.
	Data.OceanSurfaceZ = OceanSurfaceZ;
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

	// This procedural table is the road-line system for the whole surface network: it is what sets
	// the dash frequency (AppendRoadMarkingsForTile's two dashes across a straight tile, one across
	// a corner) and the spacing (AppendTiledDashedRoadMarkingSegment's DashFillRatio). Rendering the
	// RD objects' own face-type-20 endpoints instead replaces that cadence with whatever the MAX
	// exporter authored per piece, so the two cannot both be on.
	//
	// The three-and-four-way ids below deliberately declare all of their openings: AppendRoadMarkings-
	// ForTile only draws a tile with exactly two, which is how yellow lines stay off intersections.
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
	// RD67/RD68, the power-line-over-road crossing. Its road half is no longer drawn from the
	// crossing object at all (see GetOriginalBridgeDispatch), so the tile takes the ordinary
	// straight-road piece and the ordinary dashes over it. 0x43 carries traffic east-west.
	case 0x43: return E | W;
	case 0x44: return N | S;
	case 0x45: return E | W;
	case 0x46: return N | S;
	// RD73/RD74 are reused by all six simple road-bridge ids and already carry their face-type-20
	// centre line on the raised deck, which the bridge ids do render (they are outside the
	// bBuildVectorLines exclusion). A procedural duplicate is both unnecessary and dangerous:
	// before the raised-plane fix it was the yellow line seen on the water/ground under the bridge.
	case 0x49:
	case 0x4A:
	case 0x4D:
	case 0x4E:
	case 0x4F:
	case 0x50:
		return 0;
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

// Where a tile's dashes sit in Z. The authored asphalt plane pulled off the placed road object is
// the first choice, because it is the surface the player sees and it climbs every ramp; the
// conditioned terrain grid is only the fallback for a tile that never placed a road mesh. Drawing
// on terrain is what left ramp markings hanging in the air below their deck.
struct FRoadMarkingSurface
{
	const FSimCopterRoadSurfaceProfile* RoadSurface = nullptr;
	float TerrainZOffset = 0.0f; // on top of the terrain grid sample (includes the mesh Z offset)
	float SurfaceZOffset = 0.0f; // on top of the asphalt plane (which already carries it)

	bool HasRoadSurface() const { return RoadSurface != nullptr && RoadSurface->bValid; }
};

FVector MakeRoadMarkingWorldPoint(
	const TArray<int16>& ConditionedCorners,
	int32 FileX,
	int32 FileY,
	const FVector2D& LocalPoint,
	float TileSize,
	float HalfMapSize,
	float TerrainHeightScale,
	const FRoadMarkingSurface& Surface)
{
	const float CenterX = GetWorldTileCenterCoordinate(static_cast<float>(FileX), TileSize, HalfMapSize);
	const float CenterY = -GetWorldTileCenterCoordinate(static_cast<float>(FileY), TileSize, HalfMapSize);
	const float PointX = CenterX + LocalPoint.X;
	const float PointY = CenterY + LocalPoint.Y;
	if (Surface.HasRoadSurface())
	{
		return FVector(PointX, PointY, Surface.RoadSurface->Evaluate(FVector2D(PointX, PointY)) + Surface.SurfaceZOffset);
	}

	const float GridX = static_cast<float>(FileX) + 0.5f + LocalPoint.X / TileSize;
	const float GridY = static_cast<float>(FileY) + 0.5f - LocalPoint.Y / TileSize;
	return FVector(
		PointX,
		PointY,
		GetTerrainGridBilinearZ(ConditionedCorners, GridX, GridY, TerrainHeightScale) + Surface.TerrainZOffset);
}

bool IsVehicleRoadSurfaceTile(const uint8 BuildingId)
{
	return (BuildingId >= 0x1d && BuildingId <= 0x2b) ||
		(BuildingId >= 0x3f && BuildingId <= 0x46) ||
		(BuildingId >= 0x49 && BuildingId <= 0x59) ||
		(BuildingId >= 0x5d && BuildingId <= 0x6b);
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
	const FRoadMarkingSurface& Surface,
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

	Section.Vertices.Add(MakeRoadMarkingWorldPoint(ConditionedCorners, FileX, FileY, P0, TileSize, HalfMapSize, TerrainHeightScale, Surface));
	Section.Vertices.Add(MakeRoadMarkingWorldPoint(ConditionedCorners, FileX, FileY, P1, TileSize, HalfMapSize, TerrainHeightScale, Surface));
	Section.Vertices.Add(MakeRoadMarkingWorldPoint(ConditionedCorners, FileX, FileY, P2, TileSize, HalfMapSize, TerrainHeightScale, Surface));
	Section.Vertices.Add(MakeRoadMarkingWorldPoint(ConditionedCorners, FileX, FileY, P3, TileSize, HalfMapSize, TerrainHeightScale, Surface));

	Section.Triangles.Add(VertexStart);
	Section.Triangles.Add(VertexStart + 1);
	Section.Triangles.Add(VertexStart + 2);
	Section.Triangles.Add(VertexStart);
	Section.Triangles.Add(VertexStart + 2);
	Section.Triangles.Add(VertexStart + 3);
	Section.TriangleCount += 2;

	// The ribbon tilts with the asphalt on a ramp, so take its normal and tangent from the emitted
	// quad rather than assuming a flat, world-up dash.
	const FVector Along = Section.Vertices[VertexStart + 1] - Section.Vertices[VertexStart];
	const FVector Across = Section.Vertices[VertexStart + 3] - Section.Vertices[VertexStart];
	FVector Normal = FVector::CrossProduct(Across, Along).GetSafeNormal();
	if (Normal.IsNearlyZero())
	{
		Normal = FVector::UpVector;
	}
	else if (Normal.Z < 0.0f)
	{
		Normal = -Normal;
	}

	const FVector AlongUnit = Along.GetSafeNormal();
	const FProcMeshTangent Tangent(
		AlongUnit.IsNearlyZero() ? FVector(Direction.X, Direction.Y, 0.0f) : AlongUnit,
		false);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Section.Normals.Add(Normal);
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
	const FRoadMarkingSurface& Surface,
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
			Surface,
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
	float TileTerrainSurfaceZ,
	const FSimCopterRoadSurfaceProfile* RoadSurface,
	float TerrainZOffset,
	float SurfaceZOffset,
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
	// SCHOOK: FUN_004c82c0 returns the placed object's road top, not the tmap below it. The asphalt
	// plane extracted from that same placed object is therefore the surface to draw on, and it is
	// what carries the dashes up a ramp instead of leaving them on the terrain wedge beneath it.
	// The TL63..TL66 raised caps keep an explicit fallback: their top face is one altitude step
	// above TileOrigin, which is where the dashes belong if no plane could be extracted.
	FRoadMarkingSurface Surface;
	Surface.RoadSurface = RoadSurface;
	Surface.TerrainZOffset = TerrainZOffset;
	Surface.SurfaceZOffset = SurfaceZOffset;
	FSimCopterRoadSurfaceProfile RaisedDeckFallback;
	if (!Surface.HasRoadSurface() && ASimCity2000CityActor::IsOneStepRaisedRoadDeckTile(BuildingId))
	{
		RaisedDeckFallback.ReferenceZ = TileTerrainSurfaceZ + TerrainHeightScale + TerrainZOffset - SurfaceZOffset;
		RaisedDeckFallback.bValid = true;
		Surface.RoadSurface = &RaisedDeckFallback;
	}
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
		Surface,
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

// Terrain is built as independent per-tile quads with one flat normal each, which reads as blocky
// faceting. Weld the vertices by position across the land sections and average their (face) normals
// so the natural terrain shades smoothly, matching the water. Vertices flagged clamped - the tiles
// under buildings/roads - keep their flat normal so those pads stay crisp. Man-made face normals
// still contribute to a neighbour's average, so natural ground gently eases up to a flat pad.
FIntVector TerrainNormalWeldKey(const FVector& Position)
{
	return FIntVector(FMath::RoundToInt(Position.X), FMath::RoundToInt(Position.Y), FMath::RoundToInt(Position.Z));
}

void AccumulateTerrainCornerNormals(const FOriginalMeshSectionData& Section, TMap<FIntVector, FVector>& CornerNormals)
{
	CornerNormals.Reserve(CornerNormals.Num() + Section.Vertices.Num());
	for (int32 Index = 0; Index < Section.Vertices.Num(); ++Index)
	{
		CornerNormals.FindOrAdd(TerrainNormalWeldKey(Section.Vertices[Index])) += Section.Normals[Index];
	}
}

void ApplySmoothTerrainNormals(FOriginalMeshSectionData& Section, const TArray<uint8>& ClampFlags, const TMap<FIntVector, FVector>& CornerNormals)
{
	for (int32 Index = 0; Index < Section.Normals.Num(); ++Index)
	{
		if (ClampFlags.IsValidIndex(Index) && ClampFlags[Index] != 0)
		{
			continue;
		}

		if (const FVector* Summed = CornerNormals.Find(TerrainNormalWeldKey(Section.Vertices[Index])))
		{
			const FVector Smooth = Summed->GetSafeNormal();
			if (!Smooth.IsNearlyZero())
			{
				Section.Normals[Index] = Smooth;
			}
		}
	}
}

// The original SimCopter city builder (FUN_0047c0c0 in SimCopter.exe) applies NO
// per-tile rotation to road/building/bridge meshes. Each SC2 tile id is dispatched
// to a specific, pre-oriented mesh object; the engine places it at the tile position
// with mesh X->world X, mesh Z->world Y, mesh Y->up. Orientation is therefore baked
// into the chosen mesh, and correctness comes from the grid->world axis mapping.
// The only transform applied here is the global 180-degree yaw (negating the local
// X/Y) that the tile placement also folds in, so the whole city faces the same way
// as the original game.
void Append3DVectorLine(
	FOriginalMeshSectionData& Section,
	const FVector& A,
	const FVector& B,
	const FLinearColor& FaceColor,
	float Width,
	bool bBakedAtlasTexturedFace,
	int32& AddedTriangleCount)
{
	const FVector Dir = (B - A).GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		return;
	}

	FVector Perp1 = FVector::CrossProduct(Dir, FVector::UpVector);
	if (Perp1.IsNearlyZero())
	{
		Perp1 = FVector::CrossProduct(Dir, FVector::RightVector);
	}
	Perp1.Normalize();
	const FVector Perp2 = FVector::CrossProduct(Dir, Perp1).GetSafeNormal();

	const float HalfWidth = Width * 0.5f;

	const FVector Offsets[4] = { Perp1 * HalfWidth, -Perp1 * HalfWidth, Perp2 * HalfWidth, -Perp2 * HalfWidth };
	for (int32 QuadIndex = 0; QuadIndex < 2; ++QuadIndex)
	{
		const FVector& O1 = Offsets[QuadIndex * 2];
		const FVector& O2 = Offsets[QuadIndex * 2 + 1];

		const int32 VStart = Section.Vertices.Num();
		Section.Vertices.Add(A + O1);
		Section.Vertices.Add(B + O1);
		Section.Vertices.Add(B + O2);
		Section.Vertices.Add(A + O2);

		for (int32 i = 0; i < 4; ++i)
		{
			Section.UVs.Add(FVector2D::ZeroVector);
			if (bBakedAtlasTexturedFace)
			{
				Section.UV1.Add(FVector2D::ZeroVector);
			}
			Section.VertexColors.Add(FaceColor);
			Section.Tangents.Add(FProcMeshTangent(Dir.X, Dir.Y, Dir.Z));
			
			FVector N = (QuadIndex == 0) ? Perp1 : Perp2;
			if (i == 2 || i == 3) N = -N;
			Section.Normals.Add(N);
		}

		Section.Triangles.Add(VStart);
		Section.Triangles.Add(VStart + 1);
		Section.Triangles.Add(VStart + 2);
		Section.Triangles.Add(VStart);
		Section.Triangles.Add(VStart + 2);
		Section.Triangles.Add(VStart + 3);
		Section.TriangleCount += 2;
		AddedTriangleCount += 2;

		Section.Triangles.Add(VStart);
		Section.Triangles.Add(VStart + 2);
		Section.Triangles.Add(VStart + 1);
		Section.Triangles.Add(VStart);
		Section.Triangles.Add(VStart + 3);
		Section.Triangles.Add(VStart + 2);
		Section.TriangleCount += 2;
		AddedTriangleCount += 2;
	}
}

FVector ConvertPlacedCityMeshVertex(
	const FMaxisMeshVertex& SourceVertex,
	const FVector& TileOrigin,
	float MeshUnitsPerCentimeter,
	float MeshScale)
{
	const FVector Converted =
		FMaxisMeshReader::ConvertMaxisVertexToUnreal(SourceVertex, MeshUnitsPerCentimeter) * MeshScale;
	return TileOrigin + FVector(-Converted.X, -Converted.Y, Converted.Z);
}

bool TryEvaluateTrianglePlaneAtXY(
	const FVector& A,
	const FVector& B,
	const FVector& C,
	const FVector2D& XY,
	float& OutZ,
	FVector2D& OutGradient)
{
	const float Denominator =
		(B.Y - C.Y) * (A.X - C.X) +
		(C.X - B.X) * (A.Y - C.Y);
	if (FMath::Abs(Denominator) <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const float WeightA =
		((B.Y - C.Y) * (XY.X - C.X) + (C.X - B.X) * (XY.Y - C.Y)) /
		Denominator;
	const float WeightB =
		((C.Y - A.Y) * (XY.X - C.X) + (A.X - C.X) * (XY.Y - C.Y)) /
		Denominator;
	const float WeightC = 1.0f - WeightA - WeightB;
	constexpr float EdgeTolerance = 0.002f;
	if (WeightA < -EdgeTolerance || WeightB < -EdgeTolerance || WeightC < -EdgeTolerance)
	{
		return false;
	}

	const FVector Normal = FVector::CrossProduct(B - A, C - A);
	if (FMath::Abs(Normal.Z) <= UE_SMALL_NUMBER)
	{
		return false;
	}

	OutZ = WeightA * A.Z + WeightB * B.Z + WeightC * C.Z;
	OutGradient = FVector2D(-Normal.X / Normal.Z, -Normal.Y / Normal.Z);
	return true;
}

bool TryBuildPlacedRoadSurfaceProfile(
	const FMaxisMeshObject& MeshObject,
	const FVector& TileOrigin,
	float MeshUnitsPerCentimeter,
	float MeshScale,
	FSimCopterRoadSurfaceProfile& OutProfile)
{
	OutProfile = FSimCopterRoadSurfaceProfile();

	// Face type 20 / palette 112 is the yellow road centre line in every RD/BR/highway object.
	// Its mean XY gives us an unambiguous point over the asphalt, even in composite suspension
	// bridge objects whose tower tops also overlap the cell centre.
	FVector LinePointSum = FVector::ZeroVector;
	int32 LinePointCount = 0;
	for (const FMaxisMeshFace& Face : MeshObject.Faces)
	{
		if (Face.FaceType != 20 || Face.MaterialIndex != 112 || Face.VertexIndices.Num() != 2)
		{
			continue;
		}

		for (const uint16 VertexIndex : Face.VertexIndices)
		{
			if (MeshObject.Vertices.IsValidIndex(VertexIndex))
			{
				LinePointSum += ConvertPlacedCityMeshVertex(
					MeshObject.Vertices[VertexIndex],
					TileOrigin,
					MeshUnitsPerCentimeter,
					MeshScale);
				++LinePointCount;
			}
		}
	}

	const FVector LineReference = LinePointCount > 0
		? LinePointSum / static_cast<float>(LinePointCount)
		: TileOrigin;
	const FVector2D ReferenceXY(LineReference.X, LineReference.Y);
	const float MaxCandidateZ = LinePointCount > 0 ? LineReference.Z + 1.0f : TNumericLimits<float>::Max();
	bool bFoundSurface = false;
	float BestZ = -TNumericLimits<float>::Max();
	FVector2D BestGradient = FVector2D::ZeroVector;

	// Face type 15 / palette 48 is the authored asphalt. Select its highest plane below the yellow
	// line. The upper bound rejects BR86's overhead slab while retaining the deck directly beneath
	// its marking; TL63..TL66 have no line and simply select their highest asphalt face.
	for (const FMaxisMeshFace& Face : MeshObject.Faces)
	{
		if (Face.FaceType != 15 || Face.MaterialIndex != 48 || Face.VertexIndices.Num() < 3)
		{
			continue;
		}

		const uint16 RootIndex = Face.VertexIndices[0];
		if (!MeshObject.Vertices.IsValidIndex(RootIndex))
		{
			continue;
		}
		const FVector A = ConvertPlacedCityMeshVertex(
			MeshObject.Vertices[RootIndex], TileOrigin, MeshUnitsPerCentimeter, MeshScale);
		for (int32 TriangleIndex = 1; TriangleIndex + 1 < Face.VertexIndices.Num(); ++TriangleIndex)
		{
			const uint16 BIndex = Face.VertexIndices[TriangleIndex];
			const uint16 CIndex = Face.VertexIndices[TriangleIndex + 1];
			if (!MeshObject.Vertices.IsValidIndex(BIndex) || !MeshObject.Vertices.IsValidIndex(CIndex))
			{
				continue;
			}

			const FVector B = ConvertPlacedCityMeshVertex(
				MeshObject.Vertices[BIndex], TileOrigin, MeshUnitsPerCentimeter, MeshScale);
			const FVector C = ConvertPlacedCityMeshVertex(
				MeshObject.Vertices[CIndex], TileOrigin, MeshUnitsPerCentimeter, MeshScale);
			float CandidateZ = 0.0f;
			FVector2D CandidateGradient = FVector2D::ZeroVector;
			if (TryEvaluateTrianglePlaneAtXY(A, B, C, ReferenceXY, CandidateZ, CandidateGradient) &&
				CandidateZ <= MaxCandidateZ &&
				CandidateZ > BestZ)
			{
				BestZ = CandidateZ;
				BestGradient = CandidateGradient;
				bFoundSurface = true;
			}
		}
	}

	if (!bFoundSurface)
	{
		return false;
	}

	OutProfile.ReferenceXY = ReferenceXY;
	OutProfile.ReferenceZ = BestZ;
	OutProfile.Gradient = BestGradient;
	OutProfile.bValid = true;
	return true;
}

// Emits a Maxis face-type-2 sprite card as a crossed pair of vertical, double-sided quads.
// CentreVertex is the card centre in Maxis object space (Y up); the card spans +/-HalfWidth
// horizontally and +/-HalfHeight vertically about it, so a tree's card sits on the ground.
void AppendMaxisSpriteCard(
	FOriginalMeshSectionData& Section,
	const FVector& TileOrigin,
	const FMaxisMeshVertex& CentreVertex,
	int32 HalfWidth,
	int32 HalfHeight,
	const FVector2D& UVMin,
	const FVector2D& UVMax,
	const FLinearColor& FaceColor,
	float MeshUnitsPerCentimeter,
	float MeshScale,
	bool bRenderBackfaces,
	int32& AddedTriangleCount,
	int32& OutTexturedTriangleCount,
	bool bTexturedFace)
{
	if (HalfWidth <= 0 || HalfHeight <= 0)
	{
		return;
	}

	const auto ToWorld = [&](int32 MaxisX, int32 MaxisY, int32 MaxisZ) -> FVector
	{
		FMaxisMeshVertex Vertex;
		Vertex.X = MaxisX;
		Vertex.Y = MaxisY;
		Vertex.Z = MaxisZ;
		const FVector Converted = FMaxisMeshReader::ConvertMaxisVertexToUnreal(Vertex, MeshUnitsPerCentimeter) * MeshScale;
		// Global 180-degree yaw about world up, matching the polygon path.
		return TileOrigin + FVector(-Converted.X, -Converted.Y, Converted.Z);
	};

	const int32 BottomY = CentreVertex.Y - HalfHeight;
	const int32 TopY = CentreVertex.Y + HalfHeight;

	for (int32 QuadIndex = 0; QuadIndex < 2; ++QuadIndex)
	{
		const bool bAlongX = QuadIndex == 0;
		const int32 MinX = bAlongX ? CentreVertex.X - HalfWidth : CentreVertex.X;
		const int32 MaxX = bAlongX ? CentreVertex.X + HalfWidth : CentreVertex.X;
		const int32 MinZ = bAlongX ? CentreVertex.Z : CentreVertex.Z - HalfWidth;
		const int32 MaxZ = bAlongX ? CentreVertex.Z : CentreVertex.Z + HalfWidth;

		const int32 VStart = Section.Vertices.Num();
		Section.Vertices.Add(ToWorld(MinX, BottomY, MinZ));
		Section.Vertices.Add(ToWorld(MaxX, BottomY, MaxZ));
		Section.Vertices.Add(ToWorld(MaxX, TopY, MaxZ));
		Section.Vertices.Add(ToWorld(MinX, TopY, MinZ));

		// V=0 is the BOTTOM of the sprite here, not the top: ConvertMaxisUVToUnreal already flips V
		// (1 - raw/65536) and BakeCityAtlas re-orders the composite's bottom-up rows on top of that,
		// so the two inversions leave the card's base at V=0. Corners run bottom-left, bottom-right,
		// top-right, top-left to match the vertex order emitted above.
		Section.UVs.Add(FVector2D(UVMin.X, UVMin.Y));
		Section.UVs.Add(FVector2D(UVMax.X, UVMin.Y));
		Section.UVs.Add(FVector2D(UVMax.X, UVMax.Y));
		Section.UVs.Add(FVector2D(UVMin.X, UVMax.Y));

		const FVector Edge = Section.Vertices[VStart + 1] - Section.Vertices[VStart];
		FVector Normal = FVector::CrossProduct(Edge, FVector::UpVector).GetSafeNormal();
		if (Normal.IsNearlyZero())
		{
			Normal = FVector::UpVector;
		}

		const FVector TangentDir = Edge.GetSafeNormal();
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			Section.VertexColors.Add(FaceColor);
			Section.Tangents.Add(FProcMeshTangent(TangentDir.X, TangentDir.Y, TangentDir.Z));
			Section.Normals.Add(Normal);
		}

		Section.Triangles.Add(VStart);
		Section.Triangles.Add(VStart + 1);
		Section.Triangles.Add(VStart + 2);
		Section.Triangles.Add(VStart);
		Section.Triangles.Add(VStart + 2);
		Section.Triangles.Add(VStart + 3);
		Section.TriangleCount += 2;
		AddedTriangleCount += 2;
		if (bTexturedFace)
		{
			OutTexturedTriangleCount += 2;
		}

		// A sprite card has no back: always emit the reversed winding so the tree is visible from
		// both sides of each quad regardless of the object's backface setting.
		(void)bRenderBackfaces;
		Section.Triangles.Add(VStart);
		Section.Triangles.Add(VStart + 2);
		Section.Triangles.Add(VStart + 1);
		Section.Triangles.Add(VStart);
		Section.Triangles.Add(VStart + 3);
		Section.Triangles.Add(VStart + 2);
		Section.TriangleCount += 2;
		AddedTriangleCount += 2;
		if (bTexturedFace)
		{
			OutTexturedTriangleCount += 2;
		}
	}
}

// Lets a placed object contribute everything except its road half. RD67/RD68 and RD67H/RD68H, the
// power-line-over-road crossings, are the only users: the tile now takes its asphalt from the
// ordinary straight-road piece and its dashes from the procedural marking system, so the crossing
// object is placed for its pylon and wires alone.
//
// The materials are measured, not guessed (Docs/scratchpad/dump_maxis_object.py). Across RD29L,
// RD31..RD34, RD67 and RD67H: face type 15 material 48 is the flat asphalt and material 128 the
// sloped body/kerb that the H and ramp pieces add on top of it; face type 20 material 112 is the
// yellow centre line. What must survive is face type 15 material 208 - the poles, the only thing
// in RD67 that reaches full height - and face type 20 material 50, the wires strung at pole top.
// Dropping only material 48 leaves RD67H's ten material-128 faces standing as a raised block.
struct FPlacedObjectRoadFaceFilter
{
	bool bSkipRoadSurfaceFaces = false;
	bool bSkipCentreLineFaces = false;

	// LAMP35..38 draw their light as GEOMETRY: eighteen face-type-11 quads in three bands under the
	// head, plus a 14-vertex pool on the pavement. That is how a 1996 software renderer with no
	// lighting model faked a street light, and next to a real spot light it is just an opaque grey
	// cone hanging off the lamp and a grey egg painted on the road - which is also what was hiding
	// the spot light, since the cone encloses its apex. The remake throws actual light instead
	// (USimCopterStreetLightsComponent), so the fake is dropped.
	//
	// Scoped to the lamps on purpose. Face type 11 is the light-CARD type generally, and the rest of
	// its users - car headlight beams, rotor discs - are wanted.
	bool bSkipLightConeFaces = false;

	bool ShouldSkipFace(const FMaxisMeshFace& Face) const
	{
		if (bSkipRoadSurfaceFaces && Face.FaceType == 15 && (Face.MaterialIndex == 48 || Face.MaterialIndex == 128))
		{
			return true;
		}
		if (bSkipLightConeFaces && Face.FaceType == SimCopterRoadDecorations::LightCardFaceType)
		{
			return true;
		}
		return bSkipCentreLineFaces && Face.FaceType == 20 && Face.MaterialIndex == 112;
	}
};

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
	bool bBuildVectorLines,
	const FPlacedObjectRoadFaceFilter& RoadFaceFilter,
	TMap<int32, FOriginalMeshSectionData>& Sections,
	int32& OutTexturedTriangleCount)
{
	int32 AddedTriangleCount = 0;

	TArray<FVector> LocalVertexPositions;
	LocalVertexPositions.Reserve(MeshObject.Vertices.Num());
	for (const FMaxisMeshVertex& SourceVertex : MeshObject.Vertices)
	{
		const FVector ConvertedVertex =
			FMaxisMeshReader::ConvertMaxisVertexToUnreal(SourceVertex, MeshUnitsPerCentimeter) * MeshScale;
		LocalVertexPositions.Add(FVector(-ConvertedVertex.X, -ConvertedVertex.Y, ConvertedVertex.Z));
	}

	TArray<TArray<FVector>> AutoSmoothCornerNormals;
	FMaxisProceduralMeshBuilder::BuildAutoSmoothCornerNormals(
		MeshObject,
		LocalVertexPositions,
		true,
		FMaxisProceduralMeshBuilder::DefaultSmoothAngleDegrees,
		AutoSmoothCornerNormals);

	for (int32 FaceIndex = 0; FaceIndex < MeshObject.Faces.Num(); ++FaceIndex)
	{
		const FMaxisMeshFace& Face = MeshObject.Faces[FaceIndex];
		if (Face.VertexIndices.Num() < 2)
		{
			continue;
		}

		if (RoadFaceFilter.ShouldSkipFace(Face))
		{
			continue;
		}

		if (IsDebugPortraitFace(Face))
		{
			continue;
		}

		const int32 TextureKey = GetMaxisFaceTextureKey(Face);
		const bool bAtlasCellInRange = Face.MaterialIndex < FMaxisTextureReader::AtlasColumnCount * FMaxisTextureReader::AtlasColumnCount;
		const bool bBakedAtlasTexturedFace = bUseOriginalTextures && Face.FaceType == 18 && bAtlasCellInRange && AvailableBakedAtlasPageIds.Contains(Face.TextureAtlasIndex);
		const bool bBakedDirectTexturedFace = bUseOriginalTextures && (Face.FaceType == 13 || Face.FaceType == 2) && AvailableBakedDirectImageIds.Contains(Face.MaterialIndex);
		const bool bRuntimeTexturedFace = bUseOriginalTextures && !bBakedAtlasTexturedFace && !bBakedDirectTexturedFace && IsTexturedMaxisFace(Face.FaceType) && AvailableRuntimeTextureKeys.Contains(TextureKey);
		const bool bTexturedFace = bBakedAtlasTexturedFace || bBakedDirectTexturedFace || bRuntimeTexturedFace;

		// Face type 11 is the alpha-blended disc (FMaxisProceduralMeshBuilder::IsTranslucentFaceType):
		// the helicopter's rotor blur, and on city objects the wind power plant's fan wheel (PP200),
		// AR254's glow panels and the TLNS/TLEW signal cards. It carries a palette colour and no
		// texture, so left alone it falls into the opaque INDEX_NONE section and draws as a solid
		// plate - which is what made a windmill a flat teal disc with the tower hidden behind it.
		const bool bTranslucentDiscFace = !bTexturedFace && FMaxisProceduralMeshBuilder::IsTranslucentFaceType(Face.FaceType);
		const int32 SectionKey = bBakedAtlasTexturedFace
			? MakeBakedAtlasPageSectionKey(Face.TextureAtlasIndex)
			: (bBakedDirectTexturedFace ? MakeBakedDirectImageSectionKey(Face.MaterialIndex) : (bRuntimeTexturedFace ? TextureKey : (bTranslucentDiscFace ? TranslucentDiscSectionKeyFlag : INDEX_NONE)));
		FOriginalMeshSectionData& Section = Sections.FindOrAdd(SectionKey);
		const int32 FaceVertexStart = Section.Vertices.Num();
		const FLinearColor FaceColor = bTexturedFace
			? FLinearColor::White
			: ResolveMaxisFaceColor(ColorMap, Face.FaceType, Face.MaterialIndex, TexturedFaceFallbackColor);

		if (IsMaxisSpriteCardFace(Face))
		{
			const uint16 CentreIndex = Face.VertexIndices[0];
			const uint16 CornerIndex = Face.VertexIndices[1];
			if (MeshObject.Vertices.IsValidIndex(CentreIndex) && MeshObject.Vertices.IsValidIndex(CornerIndex))
			{
				const FMaxisMeshVertex& CentreVertex = MeshObject.Vertices[CentreIndex];
				const FMaxisMeshVertex& CornerVertex = MeshObject.Vertices[CornerIndex];
				const int32 HalfWidth = FMath::Abs(CornerVertex.X - CentreVertex.X);
				const int32 HalfHeight = FMath::Abs(CornerVertex.Y - CentreVertex.Y);

				// Every sprite card in the shipped GEO carries the same 0.1/0.9 pair here, so it is a
				// fixed exporter convention rather than a per-card crop rectangle. Mapping it across
				// the quad shaves the outer 10% off all four edges of the sprite - and since the tree
				// artwork runs edge to edge in its image (measured: 0 blank rows at the top, at most 1
				// at the bottom), that clipped the base of the trunk and left every tree drawn 10% of
				// its height above the ground it is actually standing on. The card IS the sprite, so
				// the image maps across it whole.
				const FVector2D UVMin(0.0f, 0.0f);
				const FVector2D UVMax(1.0f, 1.0f);

				// The original draws one camera-facing card. The city mesh is baked once, so the
				// static stand-in is the usual crossed pair of vertical quads: it reads as a tree
				// from any heading the helicopter can approach from, and keeps the exact footprint
				// and height the card encodes.
				AppendMaxisSpriteCard(
					Section,
					TileOrigin,
					CentreVertex,
					HalfWidth,
					HalfHeight,
					UVMin,
					UVMax,
					FaceColor,
					MeshUnitsPerCentimeter,
					MeshScale,
					bRenderBackfaces,
					AddedTriangleCount,
					OutTexturedTriangleCount,
					bTexturedFace);
			}
			continue;
		}

		if (Face.VertexIndices.Num() == 2)
		{
			if (bBuildVectorLines)
			{
				const uint16 IndexA = Face.VertexIndices[0];
				const uint16 IndexB = Face.VertexIndices[1];
				if (MeshObject.Vertices.IsValidIndex(IndexA) && MeshObject.Vertices.IsValidIndex(IndexB))
				{
					const FVector ConvertedA = FMaxisMeshReader::ConvertMaxisVertexToUnreal(MeshObject.Vertices[IndexA], MeshUnitsPerCentimeter) * MeshScale;
					const FVector ConvertedB = FMaxisMeshReader::ConvertMaxisVertexToUnreal(MeshObject.Vertices[IndexB], MeshUnitsPerCentimeter) * MeshScale;
					const FVector LocalA(-ConvertedA.X, -ConvertedA.Y, ConvertedA.Z);
					const FVector LocalB(-ConvertedB.X, -ConvertedB.Y, ConvertedB.Z);
					const FVector A = TileOrigin + LocalA;
					const FVector B = TileOrigin + LocalB;

					Append3DVectorLine(Section, A, B, FaceColor, 5.0f, bBakedAtlasTexturedFace, AddedTriangleCount);
				}
			}
			continue;
		}

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
			Section.Normals.Add(
				AutoSmoothCornerNormals[FaceIndex].IsValidIndex(FaceVertexIndex)
					? AutoSmoothCornerNormals[FaceIndex][FaceVertexIndex]
					: FVector::UpVector);
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
			Section.Normals.SetNum(FaceVertexStart);
			continue;
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

			// The disc is drawn with a two-sided translucent material, so it needs no reversed
			// winding - adding one blends the disc over itself and makes it look solid again,
			// which is the same trap FMaxisProceduralMeshBuilder documents on the rotor path.
			// PP200 already ships its wheel as two fans a few centimetres apart for the two
			// sides, so this face is doubled in the source data before anything here touches it.
			if (bRenderBackfaces && !bTranslucentDiscFace)
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
	// The water surface undulates entirely in its material's vertex shader (World Position Offset),
	// so nothing here needs a tick except the building blink markers: those are camera-facing cards
	// on an 8-step colour phase (FUN_00496c00) and have to be rebuilt when the phase or the view
	// changes. The component early-outs when neither did, and when the city has no lights at all.
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	TerrainMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TerrainMeshComponent"));
	TerrainMeshComponent->SetupAttachment(SceneRoot);
	TerrainMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	TerrainMeshComponent->SetCollisionObjectType(ECC_WorldStatic);
	TerrainMeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
	TerrainMeshComponent->bUseComplexAsSimpleCollision = true;
	TerrainMeshComponent->SetCanEverAffectNavigation(false);
	TerrainMeshComponent->SetCastShadow(false);

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

	// Sits on SceneRoot so its component space is the same one the baked mesh sections and the
	// building instances are placed in - the tile origins can be stored verbatim.
	FlashingLightsComponent = CreateDefaultSubobject<USimCopterFlashingLightsComponent>(TEXT("FlashingLightsComponent"));
	FlashingLightsComponent->SetupAttachment(SceneRoot);
	// A city has hundreds of markers and every lit one gets its own light - MaxPointLights stays 0
	// (uncapped) because MegaLights is what this project renders with. Rooftop beacons on tall
	// buildings want a wide, soft pool of colour.
	FlashingLightsComponent->PointLightAttenuationRadiusCm = 2000.0f;
	FlashingLightsComponent->PointLightIntensity = 20.0f;

	// Both of these are placed in the same component space as the baked sections, for the same
	// reason: their offsets are accumulated tile origins.
	StreetLightsComponent = CreateDefaultSubobject<USimCopterStreetLightsComponent>(TEXT("StreetLightsComponent"));
	StreetLightsComponent->SetupAttachment(SceneRoot);

	SmokeStacksComponent = CreateDefaultSubobject<USimCopterSmokeStacksComponent>(TEXT("SmokeStacksComponent"));
	SmokeStacksComponent->SetupAttachment(SceneRoot);

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

	// Shared with the helicopter's rotor blur on purpose: it is the same face type drawn the same
	// way, and one tuned haze keeps a windmill wheel and a rotor disc reading alike.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BlurDiscMaterialFinder(
		TEXT("/Game/Materials/M_SimCopterRotorDisc.M_SimCopterRotorDisc"));
	if (BlurDiscMaterialFinder.Succeeded())
	{
		BlurDiscMaterial = BlurDiscMaterialFinder.Object;
	}

	// The chimney plumes sample the original effect-selector atlas through the same card material
	// the flames do (USimCopterFireRenderComponent).
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SpriteCardMaterialFinder(
		TEXT("/Game/Materials/M_SimCopterSpriteTexture.M_SimCopterSpriteTexture"));
	if (SpriteCardMaterialFinder.Succeeded())
	{
		SpriteCardMaterial = SpriteCardMaterialFinder.Object;
	}

	// Water uses the same TILED1 texturing as the terrain, but displaces its vertices in the vertex
	// shader (World Position Offset) and computes analytic wave normals. Shoreline verts are pinned
	// via a per-vertex weight baked into vertex-color R (see WaterCornerWeight below).
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> WaterMaterialFinder(TEXT("/Game/Materials/M_SimCopterWater.M_SimCopterWater"));
	if (WaterMaterialFinder.Succeeded())
	{
		WaterMaterial = WaterMaterialFinder.Object;
	}

	// Ground material with three-octave procedural detail-noise normals (see M_SimCopterTerrain). Fed
	// the terrain-low/high texture plus the noise parameters; a per-vertex weight (vertex-color R)
	// fades the noise out near the shoreline and on building/road pads.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> TerrainMaterialFinder(TEXT("/Game/Materials/M_SimCopterTerrain.M_SimCopterTerrain"));
	if (TerrainMaterialFinder.Succeeded())
	{
		TerrainMaterial = TerrainMaterialFinder.Object;
	}

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

FString ASimCity2000CityActor::GetSessionCityFilePath() const
{
	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
	if (GameInstance == nullptr)
	{
		// Editor construction: no game instance, so the actor's own city file is the only source.
		return FString();
	}

	const USimCopterSessionSubsystem* Session = GameInstance->GetSubsystem<USimCopterSessionSubsystem>();
	if (Session == nullptr || !Session->HasPendingSession())
	{
		return FString();
	}

	return Session->GetCityFilePath();
}

void ASimCity2000CityActor::BeginPlay()
{
	Super::BeginPlay();

	// The main menu picked a city, so whatever was baked into this level has to be replaced even
	// when the actor is not configured to load on begin play.
	if (!GetSessionCityFilePath().IsEmpty())
	{
		RebuildCity();
		return;
	}

	if (bLoadOnBeginPlay)
	{
		RebuildCity();
		return;
	}

	// The procedural mesh sections are serialized UPROPERTYs, so terrain, roads and props survive
	// into a duplicated or loaded world untouched. The instanced buildings cannot, which is why
	// they rendered in the editor and vanished in PIE - see AreBuildingInstancesIntact.
	if (bRenderOriginalMeshes && bInstanceBuildingMeshes && !LastLoadedCityName.IsEmpty() && !AreBuildingInstancesIntact())
	{
		RebuildCity();
	}
}

void ASimCity2000CityActor::RebuildCity()
{
	WaterTextureFramesPerSecond = SanitizeWaterTextureFramesPerSecond(WaterTextureFramesPerSecond);
	LastLoadError.Reset();
	LastLoadedCityName.Reset();
	LastOriginalMeshTileCount = 0;
	LastMissingOriginalMeshTileCount = 0;
	LastOriginalMeshTriangleCount = 0;
	LastOriginalTextureCount = 0;
	LastOriginalTexturedTriangleCount = 0;
	BuildingTileFlags.Reset();
	WaterGameplayCornerZ.Reset();
	WaterGameplayTerrainClasses.Reset();
	RoadSurfaceProfiles.Reset();
	OriginalTextureCache.Reset();
	OriginalTextureMaterials.Reset();
	WaterTextureMaterials.Reset();
	ResetBuildingInstances();
	ResetNaturalObjectInstances();

	TerrainMeshComponent->ClearAllMeshSections();
	OriginalMeshComponent->ClearAllMeshSections();
	RoadMarkingMeshComponent->ClearAllMeshSections();
	LastFlashingLightCount = 0;
	if (FlashingLightsComponent != nullptr)
	{
		// Dropped up front so a rebuild that bails out early cannot leave the previous city's
		// lights hanging in the air.
		FlashingLightsComponent->ClearLightPoints();
	}
	TerrainMeshComponent->SetCollisionEnabled(bEnableTerrainCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	OriginalMeshComponent->SetCollisionEnabled(bEnableOriginalMeshCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	RoadMarkingMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

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

	// FUN_0047bb20 runs the tmap conditioning (FUN_004abce0) and only then builds the city's
	// cells (FUN_0047c0c0), which hands the first airport-zoned 4x4 to FUN_004829f0. That
	// demolishes whatever SimCity 2000 zoned there and rebuilds it as SimCopter's own airport -
	// a 2x2 terminal ringed by twelve bare helipads - so it has to happen before anything reads
	// XBLD, and the flatten has to read a corner grid conditioned from the pre-stamp ids.
	TArray<int16> ConditionedTerrainCorners = BuildConditionedTerrainCornerSamples(City);
	{
		const FIntPoint AirportOrigin = SimCopterAirport::BuildAirportIntoCity(City, &ConditionedTerrainCorners);
		UE_LOG(
			LogSimCity2000CityActor,
			Display,
			TEXT("SimCopter airport: %s at (%d, %d); the 4x4 block is now terminal + twelve pads."),
			SimCopterAirport::IsFallbackAirportOrigin(AirportOrigin) ? TEXT("no airport zone, fallback block") : TEXT("block"),
			AirportOrigin.X,
			AirportOrigin.Y);
	}

	BuildingTileFlags.SetNumZeroed(FSimCity2000City::TileCount);
	RoadSurfaceProfiles.SetNum(FSimCity2000City::TileCount);
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
	FBakedCityAtlasMaterials BakedCityAtlasMaterials = bRenderOriginalTextures ? LoadBakedCityAtlasMaterials() : FBakedCityAtlasMaterials();
	// Page-20 pool/pond faces remain on their authored static atlas cells. Only the dedicated
	// terrain-water section uses the five-frame texture animation and its debug FPS input.
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

	const float HalfMapSize = FSimCity2000City::MapSize * TileSize * 0.5f;
	const float OriginalMeshScale = OriginalMeshSourceTileSize > 0.0f ? TileSize / OriginalMeshSourceTileSize : 1.0f;
	const float EffectiveTerrainHeightScale = bUseOriginalTerrainHeightScale ? TileSize * 0.5f : TerrainHeightScale;

	FOriginalMeshSectionData TerrainPage14Section;
	FOriginalMeshSectionData TerrainPage0DSection;
	FOriginalMeshSectionData TerrainWaterSection;
	FOriginalMeshSectionData RoadMarkingSection;
	// Blink markers gathered from every placed object, in SceneRoot space (see the collection site).
	TArray<FSimCopterFlashingLightPoint> CityFlashingLightPoints;
	// The two other marker sets gathered by the same sweep, in the same space: a spot light under
	// every placed LAMP35..38 head, and every face-type-26 chimney puff.
	TArray<USimCopterStreetLightsComponent::FPlacement> CityStreetLightPlacements;
	TArray<USimCopterSmokeStacksComponent::FSmokeMarker> CitySmokeMarkers;
	// Seeds the street-furniture pick. The original rolls the global rand() as it builds, so its
	// choice is different every load; hashing the city's own name instead keeps a given city looking
	// like itself across reloads without making every city identical.
	const int32 RoadDecorationCitySeed = static_cast<int32>(GetTypeHash(City.CityName));
	int32 RoadDecorationCount = 0;
	// Per-vertex "keep flat" flags for the two land terrain sections, index-aligned with their
	// vertices. Set for tiles under buildings/roads so smooth-normal shading does not round the flat
	// pads the conditioning creates - the terrain stays crisp where man-made surfaces sit on it.
	TArray<uint8> TerrainPage14ClampFlags;
	TArray<uint8> TerrainPage0DClampFlags;
	// Per-vertex detail-noise weight (0 near shoreline / on man-made pads, ramping to 1 over open
	// ground), baked into the land vertex colors' R channel for M_SimCopterTerrain.
	TArray<float> TerrainPage14DetailWeights;
	TArray<float> TerrainPage0DDetailWeights;
	TMap<int32, FOriginalMeshSectionData> OriginalMeshSections;
	const bool bUseTexturedTerrainSurface = BakedCityAtlasMaterials.TerrainLowMaterial != nullptr || BakedCityAtlasMaterials.TerrainHighMaterial != nullptr ||
		((TerrainTexture != nullptr || HighTerrainTexture != nullptr) && TexturedMaterial != nullptr);
	// When enabled, water terrain quads go to their own procedural mesh section so Tick can
	// undulate them independently of the static land surface. Gated on a textured surface: the
	// undulation only reads well against the water texture, not the flat vertex-color fallback.
	const bool bAnimateWater = bAnimateWaterSurface && bRenderWater && bUseTexturedTerrainSurface;
	const bool bUseHighTerrainAtlas = BakedCityAtlasMaterials.TerrainHighMaterial != nullptr || (HighTerrainTexture != nullptr && TexturedMaterial != nullptr);

	// ConditionedTerrainCorners was built above, before the airport was stamped, because the
	// original conditions the grid first and levels the airport into it afterwards.

	// SimCopter selects each ground tile's TILED1 atlas cell from a per-tile terrain type code
	// (the type IS the cell index). Reproduce that grid once for the whole map.
	const TArray<uint8> TerrainTypeGrid = BuildTerrainTextureTypeGrid(City, ConditionedTerrainCorners);

	// SCHOOK: SampleWaterGameplaySurface 0x004ae7a0
	// The bucket fill and water-particle landing tests read these conditioned grids too. Keep a
	// runtime copy rather than approximating gameplay height with a physics trace.
	constexpr int32 GameplayCornerGridSize = FSimCity2000City::MapSize + 1;
	WaterGameplayCornerZ.SetNumUninitialized(GameplayCornerGridSize * GameplayCornerGridSize);
	for (int32 GridY = 0; GridY < GameplayCornerGridSize; ++GridY)
	{
		for (int32 GridX = 0; GridX < GameplayCornerGridSize; ++GridX)
		{
			WaterGameplayCornerZ[GridY * GameplayCornerGridSize + GridX] =
				GetTerrainGridVertexZ(ConditionedTerrainCorners, GridX, GridY, EffectiveTerrainHeightScale);
		}
	}
	WaterGameplayTerrainClasses = TerrainTypeGrid;
	MapAltitudeCorners = ConditionedTerrainCorners;

	const FExtendedTerrainData ExtendedTerrain = BuildProceduralExtendedTerrain(
		City,
		ConditionedTerrainCorners,
		TerrainTypeGrid,
		(bRenderTerrain && bRenderProceduralMapExtension) ? ProceduralMapExtensionTiles : 0,
		EffectiveTerrainHeightScale);

	CachedOceanSurfaceZ = ExtendedTerrain.OceanSurfaceZ;
	bHasOceanSurfaceZ = true;

	// WR16-WR19 are the four directional grade-transition poles. The saved XBLD
	// variant is normally right, but terrain conditioning can turn the underlying
	// tile into a diagonal one-corner slope. Resolve the axis from reciprocal
	// neighbors and the high side from the same conditioned corners rendered below.
	auto GetPowerLineOpeningMask = [](uint8 Building) -> uint8
	{
		const uint8 N = static_cast<uint8>(ERoadOpening::North);
		const uint8 E = static_cast<uint8>(ERoadOpening::East);
		const uint8 S = static_cast<uint8>(ERoadOpening::South);
		const uint8 W = static_cast<uint8>(ERoadOpening::West);
		switch (Building)
		{
		case 0x0E: return E | W;
		case 0x0F: return N | S;
		case 0x10: return N | S;
		case 0x11: return E | W;
		case 0x12: return N | S;
		case 0x13: return E | W;
		case 0x14: return S | W;
		case 0x15: return E | S;
		case 0x16: return N | E;
		case 0x17: return N | W;
		case 0x18: return N | S | W;
		case 0x19: return E | S | W;
		case 0x1A: return N | E | S;
		case 0x1B: return N | E | W;
		case 0x1C: return N | E | S | W;
		default: return 0;
		}
	};

	auto GetOppositePowerLineOpening = [](uint8 Opening) -> uint8
	{
		if (Opening == static_cast<uint8>(ERoadOpening::North)) return static_cast<uint8>(ERoadOpening::South);
		if (Opening == static_cast<uint8>(ERoadOpening::South)) return static_cast<uint8>(ERoadOpening::North);
		if (Opening == static_cast<uint8>(ERoadOpening::East)) return static_cast<uint8>(ERoadOpening::West);
		if (Opening == static_cast<uint8>(ERoadOpening::West)) return static_cast<uint8>(ERoadOpening::East);
		return 0;
	};

	TArray<uint8> ResolvedPowerLineMeshIds;
	ResolvedPowerLineMeshIds.SetNumUninitialized(FSimCity2000City::TileCount);
	for (int32 FileY = 0; FileY < FSimCity2000City::MapSize; ++FileY)
	{
		for (int32 FileX = 0; FileX < FSimCity2000City::MapSize; ++FileX)
		{
			const int32 TileIndex = FileY * FSimCity2000City::MapSize + FileX;
			const uint8 SavedBuilding = City.Tiles[TileIndex].Building;
			uint8 ResolvedBuilding = SavedBuilding;
			if (SavedBuilding >= 0x10 && SavedBuilding <= 0x13)
			{
				const uint8 Directions[] = {
					static_cast<uint8>(ERoadOpening::North),
					static_cast<uint8>(ERoadOpening::East),
					static_cast<uint8>(ERoadOpening::South),
					static_cast<uint8>(ERoadOpening::West)
				};
				const int32 DX[] = { 0, 1, 0, -1 };
				const int32 DY[] = { -1, 0, 1, 0 };
				int32 NorthSouthScore = 0;
				int32 EastWestScore = 0;
				for (int32 DirectionIndex = 0; DirectionIndex < UE_ARRAY_COUNT(Directions); ++DirectionIndex)
				{
					const int32 NeighborX = FileX + DX[DirectionIndex];
					const int32 NeighborY = FileY + DY[DirectionIndex];
					if (NeighborX < 0 || NeighborX >= FSimCity2000City::MapSize ||
						NeighborY < 0 || NeighborY >= FSimCity2000City::MapSize)
					{
						continue;
					}

					const uint8 NeighborBuilding =
						City.Tiles[NeighborY * FSimCity2000City::MapSize + NeighborX].Building;
					if ((GetPowerLineOpeningMask(NeighborBuilding) &
						GetOppositePowerLineOpening(Directions[DirectionIndex])) == 0)
					{
						continue;
					}

					if (Directions[DirectionIndex] == static_cast<uint8>(ERoadOpening::North) ||
						Directions[DirectionIndex] == static_cast<uint8>(ERoadOpening::South))
					{
						++NorthSouthScore;
					}
					else
					{
						++EastWestScore;
					}
				}

				const bool bSavedNorthSouth = SavedBuilding == 0x10 || SavedBuilding == 0x12;
				const bool bNorthSouth = NorthSouthScore == EastWestScore
					? bSavedNorthSouth
					: NorthSouthScore > EastWestScore;
				const int32 CornerNW = GetConditionedTerrainCornerSample(ConditionedTerrainCorners, FileX, FileY);
				const int32 CornerNE = GetConditionedTerrainCornerSample(ConditionedTerrainCorners, FileX + 1, FileY);
				const int32 CornerSE = GetConditionedTerrainCornerSample(ConditionedTerrainCorners, FileX + 1, FileY + 1);
				const int32 CornerSW = GetConditionedTerrainCornerSample(ConditionedTerrainCorners, FileX, FileY + 1);

				if (bNorthSouth)
				{
					const int32 NorthHeight = CornerNW + CornerNE;
					const int32 SouthHeight = CornerSW + CornerSE;
					ResolvedBuilding = NorthHeight == SouthHeight
						? (SavedBuilding == 0x10 || SavedBuilding == 0x12 ? SavedBuilding : 0x10)
						: (NorthHeight > SouthHeight ? 0x10 : 0x12);
				}
				else
				{
					const int32 WestHeight = CornerNW + CornerSW;
					const int32 EastHeight = CornerNE + CornerSE;
					ResolvedBuilding = WestHeight == EastHeight
						? (SavedBuilding == 0x11 || SavedBuilding == 0x13 ? SavedBuilding : 0x11)
						: (WestHeight > EastHeight ? 0x11 : 0x13);
				}
			}

			ResolvedPowerLineMeshIds[TileIndex] = ResolvedBuilding;
		}
	}

	// Which tiles were routed into the animated water section (must match the routing used below).
	// Original-map tiles come from the terrain type grid; outside tiles come from the extension.
	auto IsAnimatedWaterTile = [&](int32 X, int32 Y) -> bool
	{
		if (X >= 0 && X < FSimCity2000City::MapSize && Y >= 0 && Y < FSimCity2000City::MapSize)
		{
			return IsWaterTerrainBase(TerrainTypeGrid[Y * FSimCity2000City::MapSize + X]);
		}
		if (ExtendedTerrain.IsEnabled() && ExtendedTerrain.ContainsTile(X, Y) && !ExtendedTerrain.IsOriginalMapTile(X, Y))
		{
			return ExtendedTerrain.IsWaterTile(X, Y) || IsWaterTerrainBase(ExtendedTerrain.GetTerrainType(X, Y));
		}
		return false;
	};

	// Ramp weight for a grid corner based on its distance (in corner-rings; 0 = one of the four tiles
	// touching the corner matches) to the nearest tile satisfying IsTargetTile: 0 at the boundary,
	// ramping linearly to 1 at Ramp tiles away. Used for the water shoreline and the land detail fades.
	auto CornerFadeToNearest = [&](int32 GridX, int32 GridY, int32 Ramp, auto&& IsTargetTile) -> float
	{
		int32 Nearest = Ramp + 1;
		for (int32 TileY = GridY - 1 - Ramp; TileY <= GridY + Ramp && Nearest > 0; ++TileY)
		{
			for (int32 TileX = GridX - 1 - Ramp; TileX <= GridX + Ramp; ++TileX)
			{
				if (IsTargetTile(TileX, TileY))
				{
					const int32 RingX = FMath::Max(0, FMath::Max((GridX - 1) - TileX, TileX - GridX));
					const int32 RingY = FMath::Max(0, FMath::Max((GridY - 1) - TileY, TileY - GridY));
					Nearest = FMath::Min(Nearest, FMath::Max(RingX, RingY));
					if (Nearest == 0)
					{
						break;
					}
				}
			}
		}
		return static_cast<float>(FMath::Min(Nearest, Ramp)) / static_cast<float>(Ramp);
	};

	// Per-vertex undulation weight for the water section (drives both the wave displacement and its
	// normals via vertex-color R). A corner touching land is pinned to 0 so the water stays welded to
	// the static land (no gaps); from there the weight ramps up to 1 over WaterShoreRampTiles tiles so
	// the waves - and their normals - ease in gradually offshore instead of jumping at the shoreline.
	const int32 WaterShoreRamp = FMath::Clamp(WaterShoreRampTiles, 1, 8);
	auto WaterCornerWeight = [&](int32 GridX, int32 GridY) -> float
	{
		return CornerFadeToNearest(GridX, GridY, WaterShoreRamp,
			[&](int32 X, int32 Y) { return !IsAnimatedWaterTile(X, Y); });
	};

	// The same weights again, but addressable by grid corner rather than by vertex index. The
	// shader reads them out of vertex colour; anything floating ON the water (the boats, and the
	// people standing on them) has to evaluate the wave on the CPU instead, and needs the weight at
	// an arbitrary XY to do it. Built here so the two can never disagree about where the shoreline
	// pinning is - see GetWaterWaveOffsetCm.
	{
		constexpr int32 CornerCount = FSimCity2000City::MapSize + 1;
		WaterCornerWeightGrid.SetNumUninitialized(CornerCount * CornerCount);
		for (int32 GridY = 0; GridY < CornerCount; ++GridY)
		{
			for (int32 GridX = 0; GridX < CornerCount; ++GridX)
			{
				WaterCornerWeightGrid[GridY * CornerCount + GridX] =
					CornerFadeToNearest(GridX, GridY, WaterShoreRamp,
						[&](int32 X, int32 Y) { return !IsAnimatedWaterTile(X, Y); });
			}
		}
	}

	// Weights are appended in lockstep with the four V0..V3 corners AppendTerrainTileWithHeights adds
	// for each routed water tile, so this array stays index-aligned with TerrainWaterSection.Vertices.
	TArray<float> WaterVertexWeights;
	auto AppendWaterCornerWeights = [&](int32 FileX, int32 FileY)
	{
		WaterVertexWeights.Add(WaterCornerWeight(FileX, FileY));
		WaterVertexWeights.Add(WaterCornerWeight(FileX + 1, FileY));
		WaterVertexWeights.Add(WaterCornerWeight(FileX + 1, FileY + 1));
		WaterVertexWeights.Add(WaterCornerWeight(FileX, FileY + 1));
	};

	// A tile is "man-made" (a flattened building/road pad) when its XBLD is road- or building-like.
	auto IsManMadeTile = [&](int32 X, int32 Y) -> bool
	{
		if (X >= 0 && X < FSimCity2000City::MapSize && Y >= 0 && Y < FSimCity2000City::MapSize)
		{
			const uint8 Building = City.Tiles[Y * FSimCity2000City::MapSize + X].Building;
			return IsRoadLikeTile(Building) || IsBuildingLikeTile(Building);
		}
		return false;
	};

	// Detail-noise weight for the land: 0 at corners touching water OR a building/road pad, ramping to
	// 1 over TerrainNoiseWaterFadeTiles / TerrainNoisePadFadeTiles respectively (whichever is nearer
	// wins). Fading near water keeps the shoreline normal weld undisturbed; fading near the pads eases
	// the noise in away from those flat surfaces instead of snapping on at the tile edge.
	const int32 DetailWaterFade = FMath::Clamp(TerrainNoiseWaterFadeTiles, 1, 8);
	const int32 DetailPadFade = FMath::Clamp(TerrainNoisePadFadeTiles, 1, 8);
	auto LandCornerDetailWeight = [&](int32 GridX, int32 GridY) -> float
	{
		const float WaterFade = CornerFadeToNearest(GridX, GridY, DetailWaterFade, IsAnimatedWaterTile);
		const float PadFade = CornerFadeToNearest(GridX, GridY, DetailPadFade, IsManMadeTile);
		return FMath::Min(WaterFade, PadFade);
	};

	// Appends the four V0..V3 detail weights for a land tile into the matching page's array, in lockstep
	// with AppendTerrainTile.
	auto AppendLandDetailWeights = [&](int32 FileX, int32 FileY, bool bUseHighPage)
	{
		TArray<float>& Weights = bUseHighPage ? TerrainPage0DDetailWeights : TerrainPage14DetailWeights;
		Weights.Add(LandCornerDetailWeight(FileX, FileY));
		Weights.Add(LandCornerDetailWeight(FileX + 1, FileY));
		Weights.Add(LandCornerDetailWeight(FileX + 1, FileY + 1));
		Weights.Add(LandCornerDetailWeight(FileX, FileY + 1));
	};

	int32 TerrainCount = 0;
	int32 ExtensionTerrainCount = 0;
	int32 OriginalMeshTriangleCount = 0;
	bool bOriginalSpecialE7BuildingPlaced = false;
	// FUN_0047c0c0 owns multi-tile buildings through the scene-cell raster, not XZON. Keeping
	// the claim state across this row-major sweep makes the first cell of each verified XBLD
	// square its owner and suppresses every cell covered by that placement.
	TArray<uint8> OriginalBuildingSceneCellState;
	OriginalBuildingSceneCellState.Init(0, FSimCity2000City::TileCount);

	// --- Instanced buildings -------------------------------------------------------------
	// Buildings are the only city geometry that can be destroyed, so they are placed as
	// instances of a per-model runtime static mesh rather than baked into the shared sections.
	// Every distinct model is built and collision-cooked exactly once here; each placement then
	// costs a transform, and demolishing one costs a RemoveInstance.
	const bool bUseInstancedBuildings = bRenderOriginalMeshes && bOriginalMeshLibraryLoaded && bInstanceBuildingMeshes;
	TileBuildingIds.Init(INDEX_NONE, FSimCity2000City::TileCount);

	// One material per section key, shared by every model that uses it, so a texture is not
	// re-instanced per building.
	TMap<int32, UMaterialInterface*> BuildingSectionMaterials;
	auto ResolveBuildingSectionMaterial = [&](int32 SectionKey) -> UMaterialInterface*
	{
		if (UMaterialInterface** Cached = BuildingSectionMaterials.Find(SectionKey))
		{
			return *Cached;
		}

		UMaterialInterface* Resolved = nullptr;
		if (SectionKey == INDEX_NONE)
		{
			Resolved = VertexColorMaterial;
		}
		else if (IsTranslucentDiscSectionKey(SectionKey))
		{
			Resolved = BlurDiscMaterial;
		}
		else if (IsBakedAtlasPageSectionKey(SectionKey))
		{
			if (UMaterialInterface* const* Baked = BakedCityAtlasMaterials.PageMaterials.Find(GetBakedSectionAssetIndex(SectionKey)))
			{
				Resolved = *Baked;
			}
		}
		else if (IsBakedDirectImageSectionKey(SectionKey))
		{
			if (UMaterialInterface* const* Baked = BakedCityAtlasMaterials.DirectImageMaterials.Find(GetBakedSectionAssetIndex(SectionKey)))
			{
				Resolved = *Baked;
			}
		}
		else if (TexturedMaterial != nullptr)
		{
			if (UTexture2D* const* Texture = OriginalTexturesByKey.Find(SectionKey))
			{
				if (UMaterialInstanceDynamic* TextureMaterial = UMaterialInstanceDynamic::Create(TexturedMaterial, this))
				{
					TextureMaterial->SetTextureParameterValue(TEXT("Texture"), *Texture);
					OriginalTextureMaterials.Add(TextureMaterial);
					Resolved = TextureMaterial;
				}
			}
		}

		BuildingSectionMaterials.Add(SectionKey, Resolved);
		return Resolved;
	};

	TMap<FBuildingModelKey, int32> ModelComponentIndices;
	// Per model triangle counts, so the reported totals still count every placement even though
	// the geometry itself is only built once.
	TArray<int32> ModelTriangleCounts;
	TArray<int32> ModelTexturedTriangleCounts;
	// Builds (once) and returns the instanced component for a model, or INDEX_NONE when the
	// model has no usable geometry - in which case the caller falls back to the merged path.
	auto ResolveBuildingModelComponent = [&](const FBuildingModelKey& Key, const FMaxisMeshObject& PrimaryObject, const TArray<FColor>* PrimaryColorMap) -> int32
	{
		if (const int32* Existing = ModelComponentIndices.Find(Key))
		{
			return *Existing;
		}

		// Build the model at the origin: the merged path's per-vertex work already folds in the
		// global 180-degree city yaw, so the only thing a placement adds is the tile translation.
		TMap<int32, FOriginalMeshSectionData> ModelSections;
		int32 ModelTexturedTriangles = 0;
		int32 ModelTriangles = 0;
		const bool bBuildVectorLines = true; // buildings are never in the line-drawn XBLD ranges
		const FPlacedObjectRoadFaceFilter NoRoadFaceFilter; // and never carry a road half
		ModelTriangles += AppendMaxisMeshObject(
			PrimaryObject,
			PrimaryColorMap,
			FVector::ZeroVector,
			OriginalMeshUnitsPerCentimeter,
			OriginalMeshScale,
			bRenderOriginalMeshBackfaces,
			bOriginalTexturesLoaded,
			AvailableOriginalTextureKeys,
			AvailableBakedAtlasPageIds,
			AvailableBakedDirectImageIds,
			OriginalTexturedFaceFallbackColor,
			bBuildVectorLines,
			NoRoadFaceFilter,
			ModelSections,
			ModelTexturedTriangles);

		if (Key.SecondaryObjectId != INDEX_NONE)
		{
			const TArray<FColor>* SecondaryColorMap = nullptr;
			if (const FMaxisMeshObject* SecondaryObject = MeshLibrary.FindObjectByObjectId(Key.SecondaryObjectId, &SecondaryColorMap))
			{
				ModelTriangles += AppendMaxisMeshObject(
					*SecondaryObject,
					SecondaryColorMap,
					FVector::ZeroVector,
					OriginalMeshUnitsPerCentimeter,
					OriginalMeshScale,
					bRenderOriginalMeshBackfaces,
					bOriginalTexturesLoaded,
					AvailableOriginalTextureKeys,
					AvailableBakedAtlasPageIds,
					AvailableBakedDirectImageIds,
					OriginalTexturedFaceFallbackColor,
					bBuildVectorLines,
					NoRoadFaceFilter,
					ModelSections,
					ModelTexturedTriangles);
			}
		}

		UStaticMesh* ModelMesh = BuildBuildingModelStaticMesh(this, ModelSections, ResolveBuildingSectionMaterial);
		if (ModelMesh == nullptr)
		{
			ModelComponentIndices.Add(Key, INDEX_NONE);
			return INDEX_NONE;
		}

		UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(this);
		Component->SetStaticMesh(ModelMesh);
		for (int32 SlotIndex = 0; SlotIndex < ModelMesh->GetStaticMaterials().Num(); ++SlotIndex)
		{
			UMaterialInterface* SlotMaterial = ModelMesh->GetStaticMaterials()[SlotIndex].MaterialInterface;
			EnsureInstancedStaticMeshUsage(SlotMaterial);
			Component->SetMaterial(SlotIndex, SlotMaterial);
		}
		Component->SetupAttachment(SceneRoot);
		Component->SetCollisionEnabled(bEnableOriginalMeshCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		Component->SetCollisionObjectType(ECC_WorldStatic);
		Component->SetCollisionResponseToAllChannels(ECR_Block);
		Component->SetCanEverAffectNavigation(false);
		// The merged city mesh casts no shadow because it is one unculled ~434k-triangle proxy -
		// shadowing it would mean re-rendering the whole city per light. Instanced buildings are
		// culled and batched per placement, so they can afford to cast, and being lit they receive
		// too. This is what puts buildings in each other's and the terrain's shadow.
		Component->SetCastShadow(true);
		Component->SetMobility(EComponentMobility::Movable);
		// Removal then displaces exactly one instance - the last one into the freed slot - instead
		// of shifting every later index down, which is what keeps demolition O(1) and keeps the
		// per-building records repairable without walking the component.
		Component->SetRemoveSwap();
		Component->RegisterComponent();

		const int32 ComponentIndex = BuildingInstanceComponents.Add(Component);
		BuildingModelMeshes.Add(ModelMesh);
		ComponentInstanceBuildings.AddDefaulted();
		ModelTriangleCounts.Add(ModelTriangles);
		ModelTexturedTriangleCounts.Add(ModelTexturedTriangles);
		check(ComponentInstanceBuildings.Num() == BuildingInstanceComponents.Num());
		ModelComponentIndices.Add(Key, ComponentIndex);
		return ComponentIndex;
	};

	// Trees and the park, on the same instancing idea as buildings but without the bookkeeping: no
	// tree can burn down or be demolished, so there is nothing to keep a per-instance record for.
	// Keyed by object id alone, because a natural tile never carries a secondary object.
	const bool bUseInstancedNaturalObjects =
		bRenderOriginalMeshes && bOriginalMeshLibraryLoaded && bInstanceNaturalObjectMeshes;
	TMap<int32, int32> NaturalObjectComponentIndices;
	auto ResolveNaturalObjectComponent =
		[this, &NaturalObjectComponentIndices, &ResolveBuildingSectionMaterial](
			int32 ObjectId,
			const FMaxisMeshObject& MeshObject,
			const TArray<FColor>* ColorMap,
			bool bRenderBackfaces,
			bool bTexturesLoaded,
			const TSet<int32>& TextureKeys,
			const TSet<int32>& AtlasPageIds,
			const TSet<int32>& DirectImageIds,
			const FLinearColor& FallbackColor,
			float UnitsPerCentimeter,
			float MeshScale,
			bool bCollision) -> int32
	{
		if (const int32* Existing = NaturalObjectComponentIndices.Find(ObjectId))
		{
			return *Existing;
		}

		// Built at the origin: AppendMaxisMeshObject already folds the global 180-degree city yaw
		// into its vertices, so a placement adds only the tile translation - the same contract the
		// building models are built under.
		TMap<int32, FOriginalMeshSectionData> ModelSections;
		int32 ModelTexturedTriangles = 0;
		AppendMaxisMeshObject(
			MeshObject,
			ColorMap,
			FVector::ZeroVector,
			UnitsPerCentimeter,
			MeshScale,
			bRenderBackfaces,
			bTexturesLoaded,
			TextureKeys,
			AtlasPageIds,
			DirectImageIds,
			FallbackColor,
			/*bBuildVectorLines*/ true,
			FPlacedObjectRoadFaceFilter(),
			ModelSections,
			ModelTexturedTriangles);

		UStaticMesh* ModelMesh = BuildBuildingModelStaticMesh(this, ModelSections, ResolveBuildingSectionMaterial);
		if (ModelMesh == nullptr)
		{
			NaturalObjectComponentIndices.Add(ObjectId, INDEX_NONE);
			return INDEX_NONE;
		}

		UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(this);
		Component->SetStaticMesh(ModelMesh);
		for (int32 SlotIndex = 0; SlotIndex < ModelMesh->GetStaticMaterials().Num(); ++SlotIndex)
		{
			UMaterialInterface* SlotMaterial = ModelMesh->GetStaticMaterials()[SlotIndex].MaterialInterface;
			EnsureInstancedStaticMeshUsage(SlotMaterial);
			Component->SetMaterial(SlotIndex, SlotMaterial);
		}
		Component->SetupAttachment(SceneRoot);
		Component->SetCollisionEnabled(bCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		Component->SetCollisionObjectType(ECC_WorldStatic);
		Component->SetCollisionResponseToAllChannels(ECR_Block);
		Component->SetCanEverAffectNavigation(false);
		Component->SetCastShadow(bNaturalObjectsCastShadow);

		// A tree is a MASKED sprite card, and the mask only exists in the RASTER. Every coarse scene
		// representation - the distance fields, the ray tracing scene - carries the card as the solid
		// quad it geometrically is, because none of them run the pixel shader that would cut the
		// leaves out of it. Anything shadowing against those representations therefore casts the
		// whole rectangle.
		//
		// That is what the sun does NOT do (a virtual shadow map is rastered, so it masks correctly)
		// and what a local light CAN do, which is exactly the shape of the report: trees casting a
		// full-card shadow specifically under the helicopter's searchlight. A rectangular shadow is
		// strictly worse than no shadow, so the cards stay out of both representations by default.
		Component->SetAffectDistanceFieldLighting(bNaturalObjectsAffectDistanceFieldLighting);
		Component->SetVisibleInRayTracing(bNaturalObjectsVisibleInRayTracing);

		// Static, unlike the buildings: nothing ever adds or removes a tree after the build, and a
		// static instanced component is what lets the virtual shadow map CACHE its pages instead of
		// re-rendering them. Getting the whole city's foliage out of one movable procedural mesh and
		// into cacheable instances is the entire point of this path.
		Component->SetMobility(EComponentMobility::Static);
		Component->RegisterComponent();

		const int32 ComponentIndex = NaturalObjectInstanceComponents.Add(Component);
		NaturalObjectModelMeshes.Add(ModelMesh);
		NaturalObjectComponentIndices.Add(ObjectId, ComponentIndex);
		return ComponentIndex;
	};

	// Rubble models (original objects 0x14f..0x152, one per footprint size) are built up front so
	// a collapse costs only an AddInstance rather than a model build mid-play.
	if (bUseInstancedBuildings)
	{
		for (int32 FootprintSize = 1; FootprintSize <= 4; ++FootprintSize)
		{
			const int32 RubbleObjectId = 0x14e + FootprintSize;
			const TArray<FColor>* RubbleColorMap = nullptr;
			const FMaxisMeshObject* RubbleObject = MeshLibrary.FindObjectByObjectId(RubbleObjectId, &RubbleColorMap);
			if (RubbleObject == nullptr)
			{
				UE_LOG(LogSimCity2000CityActor, Warning, TEXT("Rubble object 0x%x not found; demolished %dx%d buildings will leave bare ground."), RubbleObjectId, FootprintSize, FootprintSize);
				continue;
			}

			FBuildingModelKey RubbleKey;
			RubbleKey.PrimaryObjectId = RubbleObjectId;
			RubbleComponentIndices[FootprintSize - 1] = ResolveBuildingModelComponent(RubbleKey, *RubbleObject, RubbleColorMap);
		}
	}

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
			// absolute orientation matches the original game. The terrain/mesh append helpers below
			// fold both steps into their per-vertex coordinates.
			const bool bRoadLikeTile = IsRoadLikeTile(Tile.Building);
			const bool bBuildingLikeTile = IsBuildingLikeTile(Tile.Building);
			const bool bNaturalObjectTile = GetOriginalNaturalObjectId(Tile.Building) != INDEX_NONE;
			const bool bRubbleTile = GetOriginalRubbleObjectId(Tile.Building) != INDEX_NONE;
			// Objects on tiles the terrain builder never flattens, so their ground is genuinely sloped:
			// power lines (0x0E-0x1C), trees (0x06-0x0C), and rubble (0x01-0x04). The small park
			// (0x0D) is flattened like a building now, so it takes the pad sample the same way one
			// does - see IsOriginalParkTile.
			const bool bGroundHuggingObjectTile =
				(bNaturalObjectTile && !IsOriginalParkTile(Tile.Building))
				|| bRubbleTile
				|| (Tile.Building >= 0x0E && Tile.Building <= 0x1C);

			if (bRenderTerrain)
			{
				const uint8 TerrainType = TerrainTypeGrid[TileIndex];
				const bool bUseHighPageForTile = TerrainType >= SimCopterHighTerrainTypeBase && bUseHighTerrainAtlas;
				const int32 TerrainAtlasTileIndex = bUseHighPageForTile
					? static_cast<int32>(TerrainType - SimCopterHighTerrainTypeBase)
					: static_cast<int32>(TerrainType & 0x3f);
				// Water is always a low-page (TILED1) cell, so it never conflicts with the high page.
				const bool bWaterTile = bAnimateWater && IsWaterTerrainBase(TerrainType);
				AppendTerrainTile(
					ConditionedTerrainCorners,
					FileX,
					FileY,
					TileSize,
					EffectiveTerrainHeightScale,
					HalfMapSize,
					TerrainAtlasTileIndex,
					bWaterTile ? TerrainWaterSection : (bUseHighPageForTile ? TerrainPage0DSection : TerrainPage14Section));
				if (bWaterTile)
				{
					AppendWaterCornerWeights(FileX, FileY);
				}
				else
				{
					// Clamp (keep flat) the terrain under buildings/roads so smooth shading leaves
					// their flat pads crisp; natural ground (grass/trees/parks) smooths freely.
					const bool bManMade = bRoadLikeTile || bBuildingLikeTile;
					const uint8 ClampFlag = bManMade ? 1 : 0;
					TArray<uint8>& LandClampFlags = bUseHighPageForTile ? TerrainPage0DClampFlags : TerrainPage14ClampFlags;
					LandClampFlags.Add(ClampFlag);
					LandClampFlags.Add(ClampFlag);
					LandClampFlags.Add(ClampFlag);
					LandClampFlags.Add(ClampFlag);
					AppendLandDetailWeights(FileX, FileY, bUseHighPageForTile);
				}
				++TerrainCount;
			}

			if (bRenderOriginalMeshes && bOriginalMeshLibraryLoaded && Tile.Building > 0 && (bRoadLikeTile || bBuildingLikeTile || bNaturalObjectTile || bRubbleTile))
			{
				const FIntPoint Footprint = bBuildingLikeTile
					? FSimCopterCityGeometryRules::ClaimOriginalBuildingFootprint(
						City,
						FileX,
						FileY,
						OriginalBuildingSceneCellState)
					: FIntPoint(1, 1);
				if (Footprint.X > 0 && Footprint.Y > 0)
				{
					const TArray<FColor>* ColorMap = nullptr;
					// Bridges/elevated roads and buildings are dispatched by the original builder to
					// specific object Ids rather than through the heuristic XBLD->mesh table.
					const bool bUseBridgeDispatch = Tile.Building >= 0x3f && Tile.Building <= 0x6b;
					// Bridges and surface roads both pick their mesh from this one test.
					const bool bTileIsFlat = IsOriginalTerrainTileFlat(ConditionedTerrainCorners, FileX, FileY);
					const FOriginalBridgeDispatch BridgeDispatch = bUseBridgeDispatch
						? GetOriginalBridgeDispatch(Tile.Building, bTileIsFlat, Tile.BitFlags)
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
					const int32 NaturalObjectId = bNaturalObjectTile
						? GetOriginalNaturalObjectId(Tile.Building)
						: GetOriginalRubbleObjectId(Tile.Building);
					// The rail band is dispatched by id like the bridges, for the permutation
					// reason spelled out on GetOriginalRailTileObjectId.
					const int32 RailObjectId = (!bUseBridgeDispatch && Tile.Building >= 0x2c && Tile.Building <= 0x3e)
						? GetOriginalRailTileObjectId(Tile.Building)
						: INDEX_NONE;
					// Surface roads are dispatched by id as well, so the flat tiles get their curbless
					// RD*L slab instead of the sloped piece the heuristic table always resolved to.
					const int32 RoadObjectId = !bUseBridgeDispatch
						? GetOriginalRoadTileObjectId(Tile.Building, bTileIsFlat)
						: INDEX_NONE;
					const int32 PrimaryObjectId = BridgeDispatch.PrimaryObjectId != INDEX_NONE
						? BridgeDispatch.PrimaryObjectId
						: (RoadObjectId != INDEX_NONE
							? RoadObjectId
							: (RailObjectId != INDEX_NONE
								? RailObjectId
								: (NaturalObjectId != INDEX_NONE ? NaturalObjectId : BuildingDispatch.PrimaryObjectId)));
					// SCHOOK: RoadDecorations 0x0047c0c0 (its `local_2c == 2` arm)
					// Nine of the builder's road cases hang a SECOND object off the same scene cell:
					// street furniture on straight roads, a lamp at a T junction, a signal at a
					// crossroads. They go at the same tile origin as the slab - the meshes carry
					// their own in-tile offset - which is exactly what the existing secondary-object
					// path does, so the decoration rides it. See SimCopterRoadDecorations.h.
					const int32 RoadDecorationObjectId = (bRenderRoadDecorations && !bUseBridgeDispatch)
						? SimCopterRoadDecorations::GetRoadDecorationObjectId(
							Tile.Building,
							FileX,
							FileY,
							bTileIsFlat,
							SimCopterRoadDecorations::MakeStreetFurnitureRoll(FileX, FileY, RoadDecorationCitySeed))
						: INDEX_NONE;
					const int32 SecondaryObjectId = RoadDecorationObjectId != INDEX_NONE
						? RoadDecorationObjectId
						: (BridgeDispatch.SecondaryObjectId != INDEX_NONE
							? BridgeDispatch.SecondaryObjectId
							: BuildingDispatch.SecondaryObjectId);
					RoadDecorationCount += RoadDecorationObjectId != INDEX_NONE ? 1 : 0;
					const uint8 MeshTileId = Tile.Building >= 0x0E && Tile.Building <= 0x1C
						? ResolvedPowerLineMeshIds[TileIndex]
						: Tile.Building;
					const FMaxisMeshObject* MeshObject = (PrimaryObjectId != INDEX_NONE)
						? MeshLibrary.FindObjectByObjectId(PrimaryObjectId, &ColorMap)
						: MeshLibrary.FindObjectByTileId(MeshTileId, &ColorMap);
					if (MeshObject != nullptr)
					{
						const float MeshWorldX = GetWorldTileCenterCoordinate(static_cast<float>(FileX) + (static_cast<float>(Footprint.X) - 1.0f) * 0.5f, TileSize, HalfMapSize);
						const float MeshWorldY = -GetWorldTileCenterCoordinate(static_cast<float>(FileY) + (static_cast<float>(Footprint.Y) - 1.0f) * 0.5f, TileSize, HalfMapSize);
						// GetAverageTerrainSurfaceZ derives Z from the tile's own ALTM step, but the terrain
						// MESH is built from the conditioned corner grid - AppendTerrainTile feeds it the
						// four GetTerrainGridVertexZ corners. On tiles the builder flattens (buildings and
						// the flat road/rail network) all four corners are forced to that same step, so the
						// two agree and those objects sit correctly. On tiles it does NOT flatten - power
						// lines, trees, the small park, rubble - the step is only one of four differing
						// corner heights, so the object floats above or sinks into the slope by up to the
						// tile's corner spread. Those sample the rendered surface under the footprint
						// centre instead: the same surface the player walks on.
						const float MeshTerrainTopZ = bGroundHuggingObjectTile
							? GetTerrainGridBilinearZ(
								ConditionedTerrainCorners,
								static_cast<float>(FileX) + static_cast<float>(Footprint.X) * 0.5f,
								static_cast<float>(FileY) + static_cast<float>(Footprint.Y) * 0.5f,
								EffectiveTerrainHeightScale)
							: GetAverageTerrainSurfaceZ(City, FileX, FileY, Footprint.X, Footprint.Y, EffectiveTerrainHeightScale);
						const FVector TileOrigin(MeshWorldX, MeshWorldY, MeshTerrainTopZ + OriginalMeshZOffset);

						if (IsVehicleRoadSurfaceTile(Tile.Building) && RoadSurfaceProfiles.IsValidIndex(TileIndex))
						{
							// SCHOOK: FUN_004c82c0 returns the placed object's surface at the mover's
							// exact X/Z. Extract the asphalt plane from the same object FUN_0047c0c0
							// dispatched, so every RD slope, bridge approach and highway ramp shares
							// one continuous authored surface with traffic.
							TryBuildPlacedRoadSurfaceProfile(
								*MeshObject,
								TileOrigin,
								OriginalMeshUnitsPerCentimeter,
								OriginalMeshScale,
								RoadSurfaceProfiles[TileIndex]);
						}

						// Blink markers are face type 25, which AppendMaxisMeshObject drops (a single
						// vertex is neither a polygon nor one of its two-point lines). Collect them here
						// for both the instanced-building and the baked-section paths, since the original
						// draws them for whatever object the tile placed. See
						// FSimCopterFlashingLightSchedule for the colour-phase rule they blink on.
						if (bRenderFlashingLights)
						{
							const int32 FirstLight = CityFlashingLightPoints.Num();
							FSimCopterFlashingLightSchedule::ExtractLightPoints(
								*MeshObject,
								ColorMap,
								OriginalMeshUnitsPerCentimeter,
								OriginalMeshScale,
								/*bApplyCityMeshOrientation*/ true,
								CityFlashingLightPoints);
							if (SecondaryObjectId != INDEX_NONE)
							{
								const TArray<FColor>* SecondaryLightColorMap = nullptr;
								if (const FMaxisMeshObject* SecondaryLightObject =
									MeshLibrary.FindObjectByObjectId(SecondaryObjectId, &SecondaryLightColorMap))
								{
									const int32 FirstSecondaryLight = CityFlashingLightPoints.Num();
									FSimCopterFlashingLightSchedule::ExtractLightPoints(
										*SecondaryLightObject,
										SecondaryLightColorMap,
										OriginalMeshUnitsPerCentimeter,
										OriginalMeshScale,
										/*bApplyCityMeshOrientation*/ true,
										CityFlashingLightPoints);
									// SIGNAL1's six markers keep their cards and lose their lights. The whole
									// city cycles red/yellow/green off one 20 Hz counter, so every signal in
									// view changes together several times a second - as hundreds of coloured
									// point lights that is a strobing street, not a traffic signal.
									if (SimCopterRoadDecorations::IsTrafficSignalObjectId(SecondaryObjectId))
									{
										for (int32 LightIndex = FirstSecondaryLight;
											LightIndex < CityFlashingLightPoints.Num();
											++LightIndex)
										{
											CityFlashingLightPoints[LightIndex].bCastPointLight = false;
										}
									}
								}
							}
							for (int32 LightIndex = FirstLight; LightIndex < CityFlashingLightPoints.Num(); ++LightIndex)
							{
								CityFlashingLightPoints[LightIndex].LocalOffset += TileOrigin;
							}
						}

						// A placed street light gets a real spot light under its head. The apex,
						// throw and spread are read out of the lamp's own face-type-11 cards - the
						// cone the original paints - rather than invented here.
						if (bRenderStreetLightSpotLights &&
							SimCopterRoadDecorations::IsStreetLightObjectId(SecondaryObjectId))
						{
							const TArray<FColor>* LampColorMap = nullptr;
							if (const FMaxisMeshObject* LampObject =
								MeshLibrary.FindObjectByObjectId(SecondaryObjectId, &LampColorMap))
							{
								SimCopterRoadDecorations::FStreetLightEmitter Emitter;
								if (SimCopterRoadDecorations::TryGetStreetLightEmitter(
									*LampObject,
									OriginalMeshUnitsPerCentimeter,
									OriginalMeshScale,
									/*bApplyCityMeshOrientation*/ true,
									Emitter))
								{
									USimCopterStreetLightsComponent::FPlacement Placement;
									Placement.Location = Emitter.LocalOffset + TileOrigin;
									Placement.ConeLengthCm = Emitter.ConeLengthCm;
									Placement.ConeHalfAngleDegrees = Emitter.ConeHalfAngleDegrees;
									CityStreetLightPlacements.Add(Placement);
								}
							}
						}

						// Chimney plumes are face type 26 - the effect-marker type, dropped by the
						// mesh appender for the same single-vertex reason type 25 is. Only eight
						// shipped models carry any, so this costs nothing on the rest of the city.
						if (bRenderSmokeStacks)
						{
							const int32 FirstMarker = CitySmokeMarkers.Num();
							USimCopterSmokeStacksComponent::ExtractSmokeMarkers(
								*MeshObject,
								OriginalMeshUnitsPerCentimeter,
								OriginalMeshScale,
								/*bApplyCityMeshOrientation*/ true,
								CitySmokeMarkers);
							for (int32 MarkerIndex = FirstMarker; MarkerIndex < CitySmokeMarkers.Num(); ++MarkerIndex)
							{
								CitySmokeMarkers[MarkerIndex].LocalOffset += TileOrigin;
							}
						}
						const bool bBuildVectorLines = !((Tile.Building >= 0x1D && Tile.Building <= 0x2B) || (Tile.Building >= 0x3F && Tile.Building <= 0x42) || (Tile.Building >= 0x0E && Tile.Building <= 0x1C));

						// The power-line-over-road crossings take their asphalt from the ordinary
						// straight-road primary and their dashes from the procedural marking table, so
						// the primary's own authored centre line and the crossing object's entire road
						// half are both suppressed. Everything else the crossing owns - the pylon and
						// the two-point wire faces that carry the line across the tile - still renders.
						const bool bPowerLineRoadCrossing = Tile.Building == 0x43 || Tile.Building == 0x44;
						FPlacedObjectRoadFaceFilter PrimaryRoadFaceFilter;
						PrimaryRoadFaceFilter.bSkipCentreLineFaces = bPowerLineRoadCrossing;
						FPlacedObjectRoadFaceFilter SecondaryRoadFaceFilter = PrimaryRoadFaceFilter;
						SecondaryRoadFaceFilter.bSkipRoadSurfaceFaces = bPowerLineRoadCrossing;
						SecondaryRoadFaceFilter.bSkipLightConeFaces =
							SimCopterRoadDecorations::IsStreetLightObjectId(SecondaryObjectId);

						// bBuildVectorLines is off across the whole road band because the SLAB carries an
						// authored centre line the procedural marking system draws instead. A decoration
						// standing on that same tile has face-type-20 lines of its own - the lamp's three
						// at the head, the signal's one at the top of its mast - and they are structure,
						// not road paint. Append3DVectorLine renders them as solid tubes in the face's own
						// palette colour, which is where the missing arm between the pole and the box went.
						const bool bSecondaryVectorLines =
							bBuildVectorLines || RoadDecorationObjectId != INDEX_NONE;

						// Trees and the park instance too, but by their own path: no building record,
						// so a placement is nothing but an AddInstance.
						int32 NaturalComponentIndex = INDEX_NONE;
						if (bUseInstancedNaturalObjects && bNaturalObjectTile && NaturalObjectId != INDEX_NONE)
						{
							NaturalComponentIndex = ResolveNaturalObjectComponent(
								NaturalObjectId,
								*MeshObject,
								ColorMap,
								bRenderOriginalMeshBackfaces,
								bOriginalTexturesLoaded,
								AvailableOriginalTextureKeys,
								AvailableBakedAtlasPageIds,
								AvailableBakedDirectImageIds,
								OriginalTexturedFaceFallbackColor,
								OriginalMeshUnitsPerCentimeter,
								OriginalMeshScale,
								bEnableOriginalMeshCollision);
						}

						// Buildings become instances so a single one can be removed later; roads,
						// bridges and power lines stay baked in the shared sections.
						int32 PlacedComponentIndex = INDEX_NONE;
						if (NaturalComponentIndex == INDEX_NONE && bUseInstancedBuildings && bBuildingLikeTile && !bUseBridgeDispatch)
						{
							FBuildingModelKey ModelKey;
							ModelKey.PrimaryObjectId = PrimaryObjectId;
							ModelKey.SecondaryObjectId = SecondaryObjectId;
							ModelKey.MeshTileId = PrimaryObjectId != INDEX_NONE ? INDEX_NONE : static_cast<int32>(MeshTileId);
							PlacedComponentIndex = ResolveBuildingModelComponent(ModelKey, *MeshObject, ColorMap);
						}

						if (NaturalComponentIndex != INDEX_NONE)
						{
							NaturalObjectInstanceComponents[NaturalComponentIndex]->AddInstance(
								FTransform(TileOrigin), /*bWorldSpace*/ false);
							++LastNaturalObjectInstanceCount;
							++LastOriginalMeshTileCount;
						}
						else if (PlacedComponentIndex != INDEX_NONE)
						{
							const int32 BuildingId = Buildings.AddDefaulted();
							FSimCopterCityBuilding& Building = Buildings[BuildingId];
							Building.OriginTile = FIntPoint(FileX, FileY);
							Building.FootprintTiles = Footprint;
							Building.PlacementOrigin = TileOrigin;
							Building.XbldId = Tile.Building;
							Building.Parts.Add(AddBuildingInstance(PlacedComponentIndex, BuildingId, TileOrigin));

							// Every tile the footprint covers resolves to the one building id, so
							// demolition can be asked for with any of them.
							for (int32 OffsetY = 0; OffsetY < Footprint.Y; ++OffsetY)
							{
								for (int32 OffsetX = 0; OffsetX < Footprint.X; ++OffsetX)
								{
									const int32 CoveredX = FileX + OffsetX;
									const int32 CoveredY = FileY + OffsetY;
									if (CoveredX < FSimCity2000City::MapSize && CoveredY < FSimCity2000City::MapSize)
									{
										TileBuildingIds[CoveredY * FSimCity2000City::MapSize + CoveredX] = BuildingId;
									}
								}
							}

							OriginalMeshTriangleCount += ModelTriangleCounts[PlacedComponentIndex];
							LastOriginalTexturedTriangleCount += ModelTexturedTriangleCounts[PlacedComponentIndex];
							++LastOriginalMeshTileCount;
						}
						else
						{
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
								bBuildVectorLines,
								PrimaryRoadFaceFilter,
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
										bSecondaryVectorLines,
										SecondaryRoadFaceFilter,
										OriginalMeshSections,
										LastOriginalTexturedTriangleCount);
								}
								else
								{
									UE_LOG(LogSimCity2000CityActor, Warning, TEXT("Could not resolve secondary object id 0x%x for XBLD 0x%x."), SecondaryObjectId, Tile.Building);
								}
							}

							++LastOriginalMeshTileCount;
						}
					}
					else
					{
						++LastMissingOriginalMeshTileCount;
					}
				}
			}

			// After the mesh, never before it: the tile's asphalt plane is extracted by the block
			// above, and the dashes have to sit on that plane to climb a ramp with it.
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
					GetTerrainTileCenterZ(City, FileX, FileY, EffectiveTerrainHeightScale),
					RoadSurfaceProfiles.IsValidIndex(TileIndex) ? &RoadSurfaceProfiles[TileIndex] : nullptr,
					OriginalMeshZOffset + RoadMarkingZOffset,
					RoadMarkingZOffset,
					RoadMarkingWidth,
					RoadMarkingColor);
			}
		}
	}

	// The WR14-WR28 meshes already contain two-point wire faces from each pole
	// attachment to the edge of the tile. Use those faces as the source of truth:
	// their pole-side endpoints preserve each model's direction, spacing, and
	// attachment height (including the raised slope variants).
	if (bRenderOriginalMeshes && bOriginalMeshLibraryLoaded)
	{
		struct FPowerLineMeshSpan
		{
			uint8 Opening = 0;
			FVector LocalAnchor = FVector::ZeroVector;
			FVector LocalOuter = FVector::ZeroVector;
			FLinearColor Color = FLinearColor::Black;
		};

		TMap<uint8, TArray<FPowerLineMeshSpan>> PowerLineSpansByBuilding;
		for (int32 BuildingId = 0x0E; BuildingId <= 0x1C; ++BuildingId)
		{
			const TArray<FColor>* ColorMap = nullptr;
			const FMaxisMeshObject* MeshObject = MeshLibrary.FindObjectByTileId(BuildingId, &ColorMap);
			if (MeshObject == nullptr)
			{
				continue;
			}

			TArray<FPowerLineMeshSpan>& Spans = PowerLineSpansByBuilding.FindOrAdd(static_cast<uint8>(BuildingId));
			for (const FMaxisMeshFace& Face : MeshObject->Faces)
			{
				if (Face.VertexIndices.Num() != 2 ||
					!MeshObject->Vertices.IsValidIndex(Face.VertexIndices[0]) ||
					!MeshObject->Vertices.IsValidIndex(Face.VertexIndices[1]))
				{
					continue;
				}

				const FVector ConvertedA = FMaxisMeshReader::ConvertMaxisVertexToUnreal(
					MeshObject->Vertices[Face.VertexIndices[0]],
					OriginalMeshUnitsPerCentimeter) * OriginalMeshScale;
				const FVector ConvertedB = FMaxisMeshReader::ConvertMaxisVertexToUnreal(
					MeshObject->Vertices[Face.VertexIndices[1]],
					OriginalMeshUnitsPerCentimeter) * OriginalMeshScale;
				const FVector LocalA(-ConvertedA.X, -ConvertedA.Y, ConvertedA.Z);
				const FVector LocalB(-ConvertedB.X, -ConvertedB.Y, ConvertedB.Z);

				// Face winding is not consistent across the WR models. The point
				// nearest tile center is always the pole/crossbar attachment.
				const bool bAIsAnchor = FVector2D(LocalA.X, LocalA.Y).SizeSquared() <
					FVector2D(LocalB.X, LocalB.Y).SizeSquared();
				const FVector LocalAnchor = bAIsAnchor ? LocalA : LocalB;
				const FVector LocalOuter = bAIsAnchor ? LocalB : LocalA;

				uint8 Opening = 0;
				if (FMath::Abs(LocalOuter.X) >= FMath::Abs(LocalOuter.Y))
				{
					Opening = LocalOuter.X >= 0.0f
						? static_cast<uint8>(ERoadOpening::East)
						: static_cast<uint8>(ERoadOpening::West);
				}
				else
				{
					Opening = LocalOuter.Y >= 0.0f
						? static_cast<uint8>(ERoadOpening::North)
						: static_cast<uint8>(ERoadOpening::South);
				}

				FPowerLineMeshSpan& Span = Spans.AddDefaulted_GetRef();
				Span.Opening = Opening;
				Span.LocalAnchor = LocalAnchor;
				Span.LocalOuter = LocalOuter;
				Span.Color = ResolveMaxisFaceColor(
					ColorMap,
					Face.FaceType,
					Face.MaterialIndex,
					OriginalTexturedFaceFallbackColor);
			}
		}

		auto GatherOpeningSpans = [](const TArray<FPowerLineMeshSpan>* Spans, uint8 Opening)
		{
			TArray<const FPowerLineMeshSpan*> Result;
			if (Spans != nullptr)
			{
				for (const FPowerLineMeshSpan& Span : *Spans)
				{
					if (Span.Opening == Opening)
					{
						Result.Add(&Span);
					}
				}
			}

			// Keep the two conductors paired by their crossbar position.
			Result.Sort([Opening](const FPowerLineMeshSpan& A, const FPowerLineMeshSpan& B)
			{
				const bool bEastWest = Opening == static_cast<uint8>(ERoadOpening::East) ||
					Opening == static_cast<uint8>(ERoadOpening::West);
				return bEastWest
					? A.LocalAnchor.Y < B.LocalAnchor.Y
					: A.LocalAnchor.X < B.LocalAnchor.X;
			});
			return Result;
		};

		// Must match the pole placement exactly: power line tiles are never flattened, so their Z
		// comes from the conditioned corner grid the terrain mesh renders, not the tile's ALTM step.
		// Anchoring the wire with GetAverageTerrainSurfaceZ while the pole it hangs from used the
		// sampled surface is what left the spans detached on sloped ground. Each end samples its own
		// tile, so a span between poles at different heights already runs at the correct slant - the
		// sag below is applied about the straight line between those two anchors.
		auto GetTileMeshOrigin = [&](int32 FileX, int32 FileY)
		{
			return FVector(
				GetWorldTileCenterCoordinate(static_cast<float>(FileX), TileSize, HalfMapSize),
				-GetWorldTileCenterCoordinate(static_cast<float>(FileY), TileSize, HalfMapSize),
				GetTerrainGridBilinearZ(
					ConditionedTerrainCorners,
					static_cast<float>(FileX) + 0.5f,
					static_cast<float>(FileY) + 0.5f,
					EffectiveTerrainHeightScale) + OriginalMeshZOffset);
		};

		const uint8 Directions[] = {
			static_cast<uint8>(ERoadOpening::North),
			static_cast<uint8>(ERoadOpening::East),
			static_cast<uint8>(ERoadOpening::South),
			static_cast<uint8>(ERoadOpening::West)
		};
		const int32 DX[] = { 0, 1, 0, -1 };
		const int32 DY[] = { -1, 0, 1, 0 };
		FOriginalMeshSectionData& Section = OriginalMeshSections.FindOrAdd(INDEX_NONE);
		int32 AddedPowerLineTriangleCount = 0;

		for (int32 FileY = 0; FileY < FSimCity2000City::MapSize; ++FileY)
		{
			for (int32 FileX = 0; FileX < FSimCity2000City::MapSize; ++FileX)
			{
				const FSimCity2000Tile& Tile = City.Tiles[FileY * FSimCity2000City::MapSize + FileX];
				const uint8 ResolvedBuilding =
					ResolvedPowerLineMeshIds[FileY * FSimCity2000City::MapSize + FileX];
				const TArray<FPowerLineMeshSpan>* TileSpans = PowerLineSpansByBuilding.Find(ResolvedBuilding);
				if (TileSpans == nullptr)
				{
					continue;
				}

				const FVector TileOrigin = GetTileMeshOrigin(FileX, FileY);
				for (int32 DirectionIndex = 0; DirectionIndex < UE_ARRAY_COUNT(Directions); ++DirectionIndex)
				{
					const uint8 Direction = Directions[DirectionIndex];
					const TArray<const FPowerLineMeshSpan*> SourceSpans = GatherOpeningSpans(TileSpans, Direction);
					if (SourceSpans.IsEmpty())
					{
						continue;
					}

					const int32 TargetX = FileX + DX[DirectionIndex];
					const int32 TargetY = FileY + DY[DirectionIndex];
					const bool bTargetInBounds =
						TargetX >= 0 && TargetX < FSimCity2000City::MapSize &&
						TargetY >= 0 && TargetY < FSimCity2000City::MapSize;
					const TArray<FPowerLineMeshSpan>* TargetTileSpans = nullptr;
					if (bTargetInBounds)
					{
						const uint8 TargetResolvedBuilding =
							ResolvedPowerLineMeshIds[TargetY * FSimCity2000City::MapSize + TargetX];
						TargetTileSpans = PowerLineSpansByBuilding.Find(TargetResolvedBuilding);
					}

					const TArray<const FPowerLineMeshSpan*> TargetSpans =
						GatherOpeningSpans(TargetTileSpans, GetOppositePowerLineOpening(Direction));
					if (!TargetSpans.IsEmpty())
					{
						// The opposite tile emits this same span while visiting north/west.
						if (Direction == static_cast<uint8>(ERoadOpening::North) ||
							Direction == static_cast<uint8>(ERoadOpening::West))
						{
							continue;
						}

						const FVector TargetOrigin = GetTileMeshOrigin(TargetX, TargetY);
						const int32 WireCount = FMath::Min(SourceSpans.Num(), TargetSpans.Num());
						for (int32 WireIndex = 0; WireIndex < WireCount; ++WireIndex)
						{
							const FVector Start = TileOrigin + SourceSpans[WireIndex]->LocalAnchor;
							const FVector End = TargetOrigin + TargetSpans[WireIndex]->LocalAnchor;
							const float Distance = FVector::Distance(Start, End);
							const float SagAmount = FMath::Min(Distance * 0.1f, TileSize * 0.25f);
							const float SegmentLength = FMath::Max(TileSize / 24.0f, 1.0f);
							const int32 SegmentCount = FMath::Clamp(FMath::CeilToInt(Distance / SegmentLength), 16, 48);

							FVector Previous = Start;
							for (int32 SegmentIndex = 1; SegmentIndex <= SegmentCount; ++SegmentIndex)
							{
								const float T = static_cast<float>(SegmentIndex) / static_cast<float>(SegmentCount);
								FVector Position = FMath::Lerp(Start, End, T);
								Position.Z -= 4.0f * SagAmount * T * (1.0f - T);
								Append3DVectorLine(
									Section,
									Previous,
									Position,
									SourceSpans[WireIndex]->Color,
									5.0f,
									false,
									AddedPowerLineTriangleCount);
								Previous = Position;
							}
						}
					}
				}
			}
		}

		OriginalMeshTriangleCount += AddedPowerLineTriangleCount;
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
				const bool bWaterTile = bAnimateWater && (ExtendedTerrain.IsWaterTile(FileX, FileY) || IsWaterTerrainBase(TerrainType));
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
					bWaterTile ? TerrainWaterSection : (bUseHighPageForTile ? TerrainPage0DSection : TerrainPage14Section));
				if (bWaterTile)
				{
					AppendWaterCornerWeights(FileX, FileY);
				}
				else
				{
					// The generated outer terrain is all natural ground, so it never clamps.
					TArray<uint8>& LandClampFlags = bUseHighPageForTile ? TerrainPage0DClampFlags : TerrainPage14ClampFlags;
					LandClampFlags.Add(0);
					LandClampFlags.Add(0);
					LandClampFlags.Add(0);
					LandClampFlags.Add(0);
					AppendLandDetailWeights(FileX, FileY, bUseHighPageForTile);
				}
				++TerrainCount;
				++ExtensionTerrainCount;
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

		// Prefer the detail-noise ground material: same TILED1 texture as the baked surface, plus the
		// per-vertex-weighted procedural normal noise. Fall back to the plain baked/runtime materials.
		UMaterialInstanceDynamic* NoiseMID = nullptr;
		if (bEnableTerrainDetailNoise && TerrainMaterial != nullptr)
		{
			UTexture* NoiseTexture = SurfaceTexture;
			if (NoiseTexture == nullptr && SurfaceMaterial != nullptr)
			{
				SurfaceMaterial->GetTextureParameterValue(FMaterialParameterInfo(TEXT("Texture")), NoiseTexture);
			}
			if (NoiseTexture != nullptr)
			{
				NoiseMID = UMaterialInstanceDynamic::Create(TerrainMaterial, this);
			}
			if (NoiseMID != nullptr)
			{
				NoiseMID->SetTextureParameterValue(TEXT("Texture"), NoiseTexture);
				NoiseMID->SetScalarParameterValue(TEXT("NoiseAmpFine"), TerrainNoiseAmpFine);
				NoiseMID->SetScalarParameterValue(TEXT("NoiseScaleFine"), TerrainNoiseScaleFine);
				NoiseMID->SetScalarParameterValue(TEXT("NoiseAmpMed"), TerrainNoiseAmpMed);
				NoiseMID->SetScalarParameterValue(TEXT("NoiseScaleMed"), TerrainNoiseScaleMed);
				NoiseMID->SetScalarParameterValue(TEXT("NoiseAmpLarge"), TerrainNoiseAmpLarge);
				NoiseMID->SetScalarParameterValue(TEXT("NoiseScaleLarge"), TerrainNoiseScaleLarge);
				NoiseMID->SetScalarParameterValue(TEXT("NoiseAmpXLarge"), TerrainNoiseAmpXLarge);
				NoiseMID->SetScalarParameterValue(TEXT("NoiseScaleXLarge"), TerrainNoiseScaleXLarge);
				OriginalTextureMaterials.Add(NoiseMID);
				TerrainMeshComponent->SetMaterial(SectionIndex, NoiseMID);
			}
		}

		if (NoiseMID != nullptr)
		{
			// already assigned
		}
		else if (SurfaceMaterial != nullptr)
		{
			TerrainMeshComponent->SetMaterial(SectionIndex, SurfaceMaterial);
		}
		else if (SurfaceTexture != nullptr && TexturedMaterial != nullptr)
		{
			UMaterialInstanceDynamic* RuntimeTerrainMID = UMaterialInstanceDynamic::Create(TexturedMaterial, this);
			if (RuntimeTerrainMID != nullptr)
			{
				RuntimeTerrainMID->SetTextureParameterValue(TEXT("Texture"), SurfaceTexture);
				OriginalTextureMaterials.Add(RuntimeTerrainMID);
				TerrainMeshComponent->SetMaterial(SectionIndex, RuntimeTerrainMID);
			}
		}
		else if (VertexColorMaterial != nullptr)
		{
			TerrainMeshComponent->SetMaterial(SectionIndex, VertexColorMaterial);
		}
	};

	// Smooth the natural terrain's per-quad flat normals into averaged corner normals so it shades
	// smoothly instead of blocky. All land sections plus the water section are welded together (shared
	// corner positions), so the low/high atlas boundary and the shoreline have no shading seam; tiles
	// under buildings/roads stay flat (clamped). The water section's smoothed rest normal is the base
	// slope M_SimCopterWater rides its waves on, so sloped coastline water matches the land it meets.
	if (bSmoothTerrainShading)
	{
		check(TerrainPage14ClampFlags.Num() == TerrainPage14Section.Vertices.Num());
		check(TerrainPage0DClampFlags.Num() == TerrainPage0DSection.Vertices.Num());
		TMap<FIntVector, FVector> TerrainCornerNormals;
		AccumulateTerrainCornerNormals(TerrainPage14Section, TerrainCornerNormals);
		AccumulateTerrainCornerNormals(TerrainPage0DSection, TerrainCornerNormals);
		AccumulateTerrainCornerNormals(TerrainWaterSection, TerrainCornerNormals);
		ApplySmoothTerrainNormals(TerrainPage14Section, TerrainPage14ClampFlags, TerrainCornerNormals);
		ApplySmoothTerrainNormals(TerrainPage0DSection, TerrainPage0DClampFlags, TerrainCornerNormals);
		ApplySmoothTerrainNormals(TerrainWaterSection, TArray<uint8>(), TerrainCornerNormals);
	}

	// Bake the detail-noise weight into the land vertex colors (R) for M_SimCopterTerrain to read.
	if (bEnableTerrainDetailNoise)
	{
		auto BakeDetailWeights = [](FOriginalMeshSectionData& Section, const TArray<float>& Weights)
		{
			if (Weights.Num() != Section.VertexColors.Num())
			{
				return;
			}
			for (int32 Index = 0; Index < Section.VertexColors.Num(); ++Index)
			{
				const float Weight = Weights[Index];
				Section.VertexColors[Index] = FLinearColor(Weight, Weight, Weight, 1.0f);
			}
		};
		BakeDetailWeights(TerrainPage14Section, TerrainPage14DetailWeights);
		BakeDetailWeights(TerrainPage0DSection, TerrainPage0DDetailWeights);
	}

	CreateTerrainSurfaceSection(TerrainPage14Section, BakedCityAtlasMaterials.TerrainLowMaterial, TerrainTexture);
	CreateTerrainSurfaceSection(TerrainPage0DSection, BakedCityAtlasMaterials.TerrainHighMaterial, HighTerrainTexture);

	// Water gets its own section using M_SimCopterWater: same TILED1 texturing as the terrain, but the
	// vertices undulate in the vertex shader (World Position Offset) and light with analytic wave
	// normals - no per-frame CPU work, and it animates in the editor and in game alike. The shoreline
	// pinning weight (0 = welded to land, 1 = open water) is baked into vertex-color R for the shader.
	if (TerrainWaterSection.Vertices.Num() > 0)
	{
		check(WaterVertexWeights.Num() == TerrainWaterSection.VertexColors.Num());
		for (int32 Index = 0; Index < TerrainWaterSection.VertexColors.Num(); ++Index)
		{
			const float Weight = WaterVertexWeights[Index];
			TerrainWaterSection.VertexColors[Index] = FLinearColor(Weight, Weight, Weight, 1.0f);
		}

		const int32 WaterSectionIndex = TerrainMeshSectionIndex++;
		// Reuse the exact TILED1 atlas the terrain-low surface samples so the water looks identical.
		UTexture* WaterSurfaceTexture = nullptr;
		if (BakedCityAtlasMaterials.TerrainLowMaterial != nullptr)
		{
			BakedCityAtlasMaterials.TerrainLowMaterial->GetTextureParameterValue(FMaterialParameterInfo(TEXT("Texture")), WaterSurfaceTexture);
		}
		if (WaterSurfaceTexture == nullptr)
		{
			WaterSurfaceTexture = TerrainTexture;
		}

		UMaterialInstanceDynamic* WaterMID = (WaterMaterial != nullptr && WaterSurfaceTexture != nullptr)
			? UMaterialInstanceDynamic::Create(WaterMaterial, this)
			: nullptr;
		UMaterialInterface* WaterSectionMaterial = nullptr;
		if (WaterMID != nullptr)
		{
			WaterMID->SetTextureParameterValue(TEXT("Texture"), WaterSurfaceTexture);
			// Amplitude 0 freezes the surface flat when animation is disabled while keeping the texture.
			WaterMID->SetScalarParameterValue(TEXT("WaveAmplitude"), bAnimateWaterSurface ? WaterWaveAmplitude : 0.0f);
			WaterMID->SetScalarParameterValue(TEXT("WaveLength"), WaterWaveLength);
			WaterMID->SetScalarParameterValue(TEXT("WaveSpeed"), WaterWaveSpeed);
			WaterMID->SetScalarParameterValue(TEXT("WaterTextureFramesPerSecond"), WaterTextureFramesPerSecond);
			OriginalTextureMaterials.Add(WaterMID);
			WaterTextureMaterials.Add(WaterMID);
			WaterSectionMaterial = WaterMID;
		}
		else if (BakedCityAtlasMaterials.TerrainLowMaterial != nullptr)
		{
			WaterSectionMaterial = BakedCityAtlasMaterials.TerrainLowMaterial;
		}
		else if (VertexColorMaterial != nullptr)
		{
			WaterSectionMaterial = VertexColorMaterial;
		}

		// Bind a valid material before the section creates its first render state. Creating the mesh
		// first briefly exposed the procedural component's empty/default slot while the water MID was
		// being prepared, which could make the whole water plane appear late on a city load.
		if (WaterSectionMaterial != nullptr)
		{
			TerrainMeshComponent->SetMaterial(WaterSectionIndex, WaterSectionMaterial);
		}
		TerrainMeshComponent->CreateMeshSection_LinearColor(
			WaterSectionIndex,
			TerrainWaterSection.Vertices,
			TerrainWaterSection.Triangles,
			TerrainWaterSection.Normals,
			TerrainWaterSection.UVs,
			TerrainWaterSection.VertexColors,
			TerrainWaterSection.Tangents,
			bEnableTerrainCollision);
	}

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
		if (IsTranslucentDiscSectionKey(TextureKey))
		{
			SectionMaterial = BlurDiscMaterial;
		}
		else if (IsBakedAtlasPageSectionKey(TextureKey))
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
	LastBuildingModelCount = BuildingInstanceComponents.Num();
	LastNaturalObjectModelCount = NaturalObjectInstanceComponents.Num();

	LastFlashingLightCount = CityFlashingLightPoints.Num();
	if (FlashingLightsComponent != nullptr)
	{
		FlashingLightsComponent->SetLightPoints(MoveTemp(CityFlashingLightPoints));
	}

	LastRoadDecorationCount = RoadDecorationCount;
	LastStreetLightCount = CityStreetLightPlacements.Num();
	if (StreetLightsComponent != nullptr)
	{
		StreetLightsComponent->bEnabled = bRenderStreetLightSpotLights;
		StreetLightsComponent->SetStreetLights(MoveTemp(CityStreetLightPlacements));
	}

	LastSmokeStackMarkerCount = CitySmokeMarkers.Num();
	if (SmokeStacksComponent != nullptr)
	{
		SmokeStacksComponent->bEnabled = bRenderSmokeStacks;
		if (!CitySmokeMarkers.IsEmpty())
		{
			// The selector atlas is palette indices, so it must be built from the same shared SIM3D
			// colour map the city meshes were coloured with.
			const TArray<FColor>* SmokePalette = MeshLibrary.GetSharedColorMap();
			FString SmokeError;
			if (SmokePalette == nullptr || SmokePalette->Num() < 256)
			{
				UE_LOG(LogSimCity2000CityActor, Warning,
					TEXT("Smoke stacks: no shared SIM3D palette, so the chimney plumes were skipped."));
			}
			else if (!SmokeStacksComponent->InitSmokeAssets(*SmokePalette, SpriteCardMaterial, SmokeError))
			{
				UE_LOG(LogSimCity2000CityActor, Warning,
					TEXT("Smoke stacks: %s"), *SmokeError);
			}
		}
		SmokeStacksComponent->SetSmokeMarkers(MoveTemp(CitySmokeMarkers));
	}

	UE_LOG(
		LogSimCity2000CityActor,
		Display,
		TEXT("Road decorations: %d placed (%d street lights); smoke stacks: %d markers."),
		LastRoadDecorationCount,
		LastStreetLightCount,
		LastSmokeStackMarkerCount);

	UE_LOG(
		LogSimCity2000CityActor,
		Display,
		TEXT("Instanced buildings: %d distinct models, %d placements (each model's geometry and collision built once)."),
		LastBuildingModelCount,
		LastBuildingInstanceCount);

	UE_LOG(
		LogSimCity2000CityActor,
		Display,
		TEXT("Instanced natural objects: %d distinct models (TREE6..TREE12 + LP13), %d placements."),
		LastNaturalObjectModelCount,
		LastNaturalObjectInstanceCount);

	UE_LOG(
		LogSimCity2000CityActor,
		Display,
		TEXT("Rendered SC2 city '%s' from '%s': terrain=%d extensionTerrain=%d originalMeshTiles=%d missingOriginalMeshTiles=%d originalTriangles=%d roadMarkingTriangles=%d roadSurfaceProfiles=%d texturedTriangles=%d originalTextures=%d chunks=%d rotation=%d waterLevel=%d terrainHeightScale=%.2f"),
		*City.CityName,
		*ResolvedCityPath,
		TerrainCount,
		ExtensionTerrainCount,
		LastOriginalMeshTileCount,
		LastMissingOriginalMeshTileCount,
		LastOriginalMeshTriangleCount,
		RoadMarkingSection.TriangleCount,
		Algo::CountIf(RoadSurfaceProfiles, [](const FSimCopterRoadSurfaceProfile& Profile) { return Profile.bValid; }),
		LastOriginalTexturedTriangleCount,
		LastOriginalTextureCount,
		City.Chunks.Num(),
		City.Rotation,
		City.WaterLevel,
		EffectiveTerrainHeightScale);
}

void ASimCity2000CityActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// The blink phase is global and time-based, so the component decides whether anything actually
	// needs rebuilding; a city with no lit objects never allocates a mesh section at all.
	if (FlashingLightsComponent != nullptr && FlashingLightsComponent->HasLightPoints() && GetWorld() != nullptr)
	{
		FlashingLightsComponent->SyncLightsFromPlayerCamera(GetWorld()->GetTimeSeconds());
	}

	// Same shape: the chimney kernels are camera-facing and re-jittered on the original's own 20 Hz
	// raster frame, so they rebuild against the live camera like the fire does.
	if (SmokeStacksComponent != nullptr && SmokeStacksComponent->GetSmokeMarkerCount() > 0 && GetWorld() != nullptr)
	{
		SmokeStacksComponent->SyncSmokeFromPlayerCamera(GetWorld()->GetTimeSeconds());
	}
}

FString ASimCity2000CityActor::GetResolvedCityPath() const
{
	return ResolveCityPath();
}

FString ASimCity2000CityActor::GetResolvedOriginalGameRoot() const
{
	return ResolveOriginalGameRoot();
}

float ASimCity2000CityActor::GetFlashingLightIntensityScale() const
{
	return FlashingLightsComponent != nullptr ? FlashingLightsComponent->PointLightIntensityScale : 1.0f;
}

void ASimCity2000CityActor::SetFlashingLightIntensityScale(float Scale)
{
	if (FlashingLightsComponent != nullptr)
	{
		FlashingLightsComponent->PointLightIntensityScale = FMath::Max(Scale, 0.0f);
	}
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

bool ASimCity2000CityActor::IsOneStepRaisedRoadDeckTile(const uint8 BuildingId)
{
	// SCHOOK: FUN_0047c0c0 places TL63..TL66 for 0x3f..0x42 and the road bridge objects for
	// 0x49..0x59. Their drivable top is 0x20 original tmap units (one ALTM step) above the
	// scene-cell origin; FUN_004c82c0 returns that object top to ground movers.
	return (BuildingId >= 0x3f && BuildingId <= 0x42) ||
		(BuildingId >= 0x49 && BuildingId <= 0x59);
}

bool ASimCity2000CityActor::TryGetRoadSurfaceWorldZ(
	const FVector& WorldLocation,
	float& OutSurfaceWorldZ) const
{
	OutSurfaceWorldZ = 0.0f;
	if (RoadSurfaceProfiles.Num() != FSimCity2000City::TileCount || TileSize <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const FTransform CityTransform = GetActorTransform();
	const FVector LocalLocation = CityTransform.InverseTransformPosition(WorldLocation);
	const float HalfMapSize = FSimCity2000City::MapSize * TileSize * 0.5f;
	const int32 FileX = FMath::FloorToInt((LocalLocation.X + HalfMapSize) / TileSize);
	const int32 FileY = FMath::FloorToInt((HalfMapSize - LocalLocation.Y) / TileSize);
	if (FileX < 0 || FileX >= FSimCity2000City::MapSize ||
		FileY < 0 || FileY >= FSimCity2000City::MapSize)
	{
		return false;
	}

	const FSimCopterRoadSurfaceProfile& Profile =
		RoadSurfaceProfiles[FileY * FSimCity2000City::MapSize + FileX];
	if (!Profile.bValid)
	{
		return false;
	}

	const float LocalSurfaceZ = Profile.Evaluate(FVector2D(LocalLocation.X, LocalLocation.Y));
	OutSurfaceWorldZ = CityTransform.TransformPosition(
		FVector(LocalLocation.X, LocalLocation.Y, LocalSurfaceZ)).Z;
	return true;
}

bool ASimCity2000CityActor::TryGetWaterGameplaySurface(
	const FVector& WorldLocation,
	float& OutSurfaceWorldZ,
	uint8& OutTerrainClass,
	FIntPoint* OutTile) const
{
	constexpr int32 MapSize = FSimCity2000City::MapSize;
	constexpr int32 CornerGridSize = MapSize + 1;
	if (TileSize <= KINDA_SMALL_NUMBER ||
		WaterGameplayCornerZ.Num() != CornerGridSize * CornerGridSize ||
		WaterGameplayTerrainClasses.Num() != MapSize * MapSize)
	{
		return false;
	}

	const FVector LocalLocation = GetActorTransform().InverseTransformPosition(WorldLocation);
	const float HalfMapSize = static_cast<float>(MapSize) * TileSize * 0.5f;
	const float GridX = (LocalLocation.X + HalfMapSize) / TileSize;
	const float GridY = (HalfMapSize - LocalLocation.Y) / TileSize;
	const int32 FileX = FMath::FloorToInt(GridX);
	const int32 FileY = FMath::FloorToInt(GridY);
	if (FileX < 0 || FileX >= MapSize || FileY < 0 || FileY >= MapSize)
	{
		return false;
	}

	const auto CornerZ = [this](const int32 X, const int32 Y)
	{
		return WaterGameplayCornerZ[Y * (FSimCity2000City::MapSize + 1) + X];
	};
	const float SurfaceLocalZ = SimCopterWaterGameplay::SampleTerrainTriangleHeight(
		GridX - static_cast<float>(FileX),
		GridY - static_cast<float>(FileY),
		CornerZ(FileX, FileY),
		CornerZ(FileX + 1, FileY),
		CornerZ(FileX + 1, FileY + 1),
		CornerZ(FileX, FileY + 1));
	OutSurfaceWorldZ = GetActorTransform().TransformPosition(
		FVector(LocalLocation.X, LocalLocation.Y, SurfaceLocalZ)).Z;
	OutTerrainClass = WaterGameplayTerrainClasses[FileY * MapSize + FileX];
	if (OutTile != nullptr)
	{
		*OutTile = FIntPoint(FileX, FileY);
	}
	return true;
}

float ASimCity2000CityActor::GetWaterWaveOffsetCm(const FVector& WorldLocation) const
{
	// A CPU evaluation of M_SimCopterWater's World Position Offset, kept term-for-term identical to
	// WATER_WAVE_PRELUDE + WATER_WPO_CODE in Tools/Unreal/CreateSimCopterMaterials.py:
	//
	//     K1 = 2*pi / max(WaveLength, 1);   K2 = K1 * 1.7
	//     P1 = K1 * (X*0.7 + Y*0.7) + T*Speed
	//     P2 = K2 * (X*0.3 - Y*0.95) + T*Speed*0.8
	//     h  = Weight * Amplitude * (0.6*sin(P1) + 0.4*sin(P2))
	//
	// The vertices move in the vertex shader and nothing on the CPU knows about it, so anything that
	// is supposed to sit ON the sea - the boats, and the people standing on their decks - has to run
	// the same arithmetic or it floats on the surface's rest plane while the water heaves through it.
	// Two things must match or the boat will not track the crest it is drawn on: the shader's Time
	// node is world time, and Weight is the shoreline-pinning vertex colour, which is why the corner
	// grid is baked next to the vertex weights rather than approximated here.
	//
	// Both of the shader's early-outs are reproduced: Weight 0 (a shoreline vertex welded to the
	// static land) and Low Power Graphics, which returns a flat surface for the whole sea.
	const UWorld* World = GetWorld();
	if (World == nullptr || !bAnimateWaterSurface || WaterWaveAmplitude <= 0.0f)
	{
		return 0.0f;
	}
	// The shader's LowPower input comes from USimCopterDayNightSubsystem::PublishLowPower, which
	// reads exactly this. Ask the same source rather than a second copy of the flag.
	if (SimCopterLowPower::IsEnabled())
	{
		return 0.0f;
	}

	const float Weight = GetWaterWaveWeightAt(WorldLocation);
	if (Weight <= 0.0f)
	{
		return 0.0f;
	}

	const FVector Local = GetActorTransform().InverseTransformPosition(WorldLocation);
	// The shader reads absolute world position; the actor is placed at the origin in the shipped
	// level, but transform through it anyway so a moved city still lines up with its own material.
	const FVector WavePos = GetActorTransform().TransformPosition(FVector(Local.X, Local.Y, 0.0f));

	const float K1 = 2.0f * PI / FMath::Max(WaterWaveLength, 1.0f);
	const float K2 = K1 * 1.7f;
	const float Time = static_cast<float>(World->GetTimeSeconds());
	const float P1 = K1 * (static_cast<float>(WavePos.X) * 0.7f + static_cast<float>(WavePos.Y) * 0.7f) +
		Time * WaterWaveSpeed;
	const float P2 = K2 * (static_cast<float>(WavePos.X) * 0.3f - static_cast<float>(WavePos.Y) * 0.95f) +
		Time * WaterWaveSpeed * 0.8f;
	return Weight * WaterWaveAmplitude * (0.6f * FMath::Sin(P1) + 0.4f * FMath::Sin(P2));
}

float ASimCity2000CityActor::GetWaterWaveWeightAt(const FVector& WorldLocation) const
{
	// Bilinear across the tile's four corner weights - the same interpolation the rasteriser does
	// to vertex colour between the four verts AppendTerrainTileWithHeights emitted for this tile.
	constexpr int32 MapSize = FSimCity2000City::MapSize;
	constexpr int32 CornerGridSize = MapSize + 1;
	if (TileSize <= KINDA_SMALL_NUMBER || WaterCornerWeightGrid.Num() != CornerGridSize * CornerGridSize)
	{
		return 0.0f;
	}

	const FVector LocalLocation = GetActorTransform().InverseTransformPosition(WorldLocation);
	const float HalfMapSize = static_cast<float>(MapSize) * TileSize * 0.5f;
	const float GridX = (static_cast<float>(LocalLocation.X) + HalfMapSize) / TileSize;
	const float GridY = (HalfMapSize - static_cast<float>(LocalLocation.Y)) / TileSize;
	const int32 FileX = FMath::FloorToInt(GridX);
	const int32 FileY = FMath::FloorToInt(GridY);
	if (FileX < 0 || FileX >= MapSize || FileY < 0 || FileY >= MapSize)
	{
		return 0.0f;
	}

	const auto CornerWeight = [this](const int32 X, const int32 Y)
	{
		return WaterCornerWeightGrid[Y * CornerGridSize + X];
	};
	const float FracX = GridX - static_cast<float>(FileX);
	const float FracY = GridY - static_cast<float>(FileY);
	const float Top = FMath::Lerp(CornerWeight(FileX, FileY), CornerWeight(FileX + 1, FileY), FracX);
	const float Bottom = FMath::Lerp(CornerWeight(FileX, FileY + 1), CornerWeight(FileX + 1, FileY + 1), FracX);
	return FMath::Lerp(Top, Bottom, FracY);
}

bool ASimCity2000CityActor::TryGetMapTerrainGrids(
	TArray<uint8>& OutTerrainClasses,
	TArray<uint8>& OutAltitudeShades) const
{
	constexpr int32 MapSize = FSimCity2000City::MapSize;
	constexpr int32 CornerGridSize = MapSize + 1;
	if (WaterGameplayTerrainClasses.Num() != MapSize * MapSize ||
		MapAltitudeCorners.Num() != CornerGridSize * CornerGridSize)
	{
		return false;
	}

	OutTerrainClasses = WaterGameplayTerrainClasses;

	// FUN_004a28e0 shades a tile with `tmap[y][x] >> 6`, clamped to 15 - one shade per two
	// altitude steps, since a step is 0x20 sample units. It samples the tile's own corner, not
	// an interpolated centre.
	OutAltitudeShades.SetNumUninitialized(MapSize * MapSize);
	for (int32 FileY = 0; FileY < MapSize; ++FileY)
	{
		for (int32 FileX = 0; FileX < MapSize; ++FileX)
		{
			const int32 Sample = MapAltitudeCorners[FileY * CornerGridSize + FileX];
			OutAltitudeShades[FileY * MapSize + FileX] =
				static_cast<uint8>(FMath::Clamp(Sample >> 6, 0, 15));
		}
	}

	return true;
}

bool ASimCity2000CityActor::IsTerrainCollisionComponent(
	const UPrimitiveComponent* HitComponent) const
{
	return HitComponent != nullptr && HitComponent == TerrainMeshComponent;
}

bool ASimCity2000CityActor::AreBuildingInstancesIntact() const
{
	if (BuildingInstanceComponents.Num() == 0)
	{
		return false;
	}

	for (const UInstancedStaticMeshComponent* Component : BuildingInstanceComponents)
	{
		if (Component == nullptr || !Component->IsRegistered())
		{
			return false;
		}

		const UStaticMesh* Mesh = Component->GetStaticMesh();
		// A building's static mesh is built at runtime with no committed source description, and
		// its FStaticMeshRenderData is a bare TUniquePtr rather than a UPROPERTY. Such meshes are
		// marked duplicate-transient so starting PIE leaves this component's mesh null instead of
		// constructing an invalid duplicate. The render-data check also covers old/stale worlds.
		if (Mesh == nullptr || !Mesh->HasValidRenderData())
		{
			return false;
		}
	}

	return true;
}

void ASimCity2000CityActor::ResetBuildingInstances()
{
	for (UInstancedStaticMeshComponent* Component : BuildingInstanceComponents)
	{
		if (Component != nullptr)
		{
			Component->DestroyComponent();
		}
	}
	BuildingInstanceComponents.Reset();
	BuildingModelMeshes.Reset();
	Buildings.Reset();
	TileBuildingIds.Reset();
	ComponentInstanceBuildings.Reset();
	for (int32& RubbleComponentIndex : RubbleComponentIndices)
	{
		RubbleComponentIndex = INDEX_NONE;
	}
	LastBuildingModelCount = 0;
	LastBuildingInstanceCount = 0;
}

void ASimCity2000CityActor::ResetNaturalObjectInstances()
{
	for (UInstancedStaticMeshComponent* Component : NaturalObjectInstanceComponents)
	{
		if (Component != nullptr)
		{
			Component->DestroyComponent();
		}
	}
	NaturalObjectInstanceComponents.Reset();
	NaturalObjectModelMeshes.Reset();
	LastNaturalObjectModelCount = 0;
	LastNaturalObjectInstanceCount = 0;
}

FSimCopterBuildingPart* ASimCity2000CityActor::FindBuildingPartInComponent(int32 BuildingId, int32 ComponentIndex)
{
	if (!Buildings.IsValidIndex(BuildingId))
	{
		return nullptr;
	}

	FSimCopterCityBuilding& Building = Buildings[BuildingId];
	for (FSimCopterBuildingPart& Part : Building.Parts)
	{
		if (Part.ComponentIndex == ComponentIndex)
		{
			return &Part;
		}
	}
	if (Building.RubblePart.ComponentIndex == ComponentIndex)
	{
		return &Building.RubblePart;
	}
	return nullptr;
}

FSimCopterBuildingPart ASimCity2000CityActor::AddBuildingInstance(int32 ComponentIndex, int32 BuildingId, const FVector& Origin)
{
	FSimCopterBuildingPart Part;
	if (!BuildingInstanceComponents.IsValidIndex(ComponentIndex))
	{
		return Part;
	}

	UInstancedStaticMeshComponent* Component = BuildingInstanceComponents[ComponentIndex];
	if (Component == nullptr)
	{
		return Part;
	}

	Part.ComponentIndex = ComponentIndex;
	Part.InstanceIndex = Component->AddInstance(FTransform(Origin), /*bWorldSpace*/ false);
	ComponentInstanceBuildings[ComponentIndex].Add(BuildingId);
	checkf(
		ComponentInstanceBuildings[ComponentIndex].Num() == Component->GetInstanceCount(),
		TEXT("Building instance ownership drifted out of lockstep with its component."));
	++LastBuildingInstanceCount;
	return Part;
}

void ASimCity2000CityActor::RemoveBuildingInstance(FSimCopterBuildingPart& Part)
{
	const int32 ComponentIndex = Part.ComponentIndex;
	const int32 InstanceIndex = Part.InstanceIndex;
	Part.Reset();

	if (!BuildingInstanceComponents.IsValidIndex(ComponentIndex) ||
		!ComponentInstanceBuildings.IsValidIndex(ComponentIndex))
	{
		return;
	}

	UInstancedStaticMeshComponent* Component = BuildingInstanceComponents[ComponentIndex];
	TArray<int32>& InstanceBuildings = ComponentInstanceBuildings[ComponentIndex];
	if (Component == nullptr || !InstanceBuildings.IsValidIndex(InstanceIndex))
	{
		return;
	}

	// The instance's collision goes with it: instance bodies are built from the model's one shared
	// body setup, so nothing is re-cooked here and no vertex buffer is rebuilt.
	const int32 LastIndex = InstanceBuildings.Num() - 1;
	const int32 DisplacedBuildingId = InstanceBuildings[LastIndex];

	Component->RemoveInstance(InstanceIndex);
	InstanceBuildings.RemoveAtSwap(InstanceIndex);

	// The components are set to RemoveAtSwap, so exactly one instance moves: the last one drops
	// into the freed slot. Re-point just that one rather than re-deriving the whole component.
	if (InstanceIndex != LastIndex)
	{
		if (FSimCopterBuildingPart* DisplacedPart = FindBuildingPartInComponent(DisplacedBuildingId, ComponentIndex))
		{
			DisplacedPart->InstanceIndex = InstanceIndex;
		}
	}

	--LastBuildingInstanceCount;
}

bool ASimCity2000CityActor::HasStandingBuildingAtTile(int32 FileX, int32 FileY) const
{
	if (FileX < 0 || FileX >= FSimCity2000City::MapSize || FileY < 0 || FileY >= FSimCity2000City::MapSize ||
		TileBuildingIds.Num() != FSimCity2000City::TileCount)
	{
		return false;
	}

	const int32 BuildingId = TileBuildingIds[FileY * FSimCity2000City::MapSize + FileX];
	return Buildings.IsValidIndex(BuildingId) && !Buildings[BuildingId].bDemolished;
}

bool ASimCity2000CityActor::HasRubbleAtTile(int32 FileX, int32 FileY) const
{
	if (FileX < 0 || FileX >= FSimCity2000City::MapSize || FileY < 0 || FileY >= FSimCity2000City::MapSize ||
		TileBuildingIds.Num() != FSimCity2000City::TileCount)
	{
		return false;
	}

	const int32 BuildingId = TileBuildingIds[FileY * FSimCity2000City::MapSize + FileX];
	return Buildings.IsValidIndex(BuildingId) &&
		Buildings[BuildingId].bDemolished &&
		Buildings[BuildingId].RubblePart.IsValid();
}

bool ASimCity2000CityActor::TryGetBuildingBoundsAtTile(int32 FileX, int32 FileY, FBox& OutWorldBounds) const
{
	if (!HasStandingBuildingAtTile(FileX, FileY))
	{
		return false;
	}

	const FSimCopterCityBuilding& Building = Buildings[TileBuildingIds[FileY * FSimCity2000City::MapSize + FileX]];

	OutWorldBounds.Init();
	for (const FSimCopterBuildingPart& Part : Building.Parts)
	{
		if (!Part.IsValid() || !BuildingInstanceComponents.IsValidIndex(Part.ComponentIndex))
		{
			continue;
		}
		const UInstancedStaticMeshComponent* Component = BuildingInstanceComponents[Part.ComponentIndex];
		if (Component == nullptr || Component->GetStaticMesh() == nullptr)
		{
			continue;
		}

		FTransform InstanceTransform;
		if (Component->GetInstanceTransform(Part.InstanceIndex, InstanceTransform, /*bWorldSpace*/ true))
		{
			OutWorldBounds += Component->GetStaticMesh()->GetBoundingBox().TransformBy(InstanceTransform);
		}
	}
	return OutWorldBounds.IsValid != 0;
}

float ASimCity2000CityActor::SanitizeWaterTextureFramesPerSecond(float FramesPerSecond)
{
	return FMath::IsFinite(FramesPerSecond)
		? FMath::Clamp(FramesPerSecond, 0.0f, MaxWaterTextureFramesPerSecond)
		: DefaultWaterTextureFramesPerSecond;
}

void ASimCity2000CityActor::SetWaterTextureFramesPerSecond(float FramesPerSecond)
{
	WaterTextureFramesPerSecond = SanitizeWaterTextureFramesPerSecond(FramesPerSecond);
	for (UMaterialInstanceDynamic* Material : WaterTextureMaterials)
	{
		if (Material != nullptr)
		{
			Material->SetScalarParameterValue(
				TEXT("WaterTextureFramesPerSecond"),
				WaterTextureFramesPerSecond);
		}
	}
}

bool ASimCity2000CityActor::IsInsideStandingBuildingBounds(
	const FVector& WorldLocation,
	const float ClearanceCm) const
{
	if (TileBuildingIds.Num() != FSimCity2000City::TileCount || TileSize <= UE_SMALL_NUMBER)
	{
		return false;
	}

	constexpr int32 MapSize = FSimCity2000City::MapSize;
	const float HalfMapSize = MapSize * TileSize * 0.5f;
	const FVector LocalLocation = GetActorTransform().InverseTransformPosition(WorldLocation);
	const int32 FileX = FMath::FloorToInt((LocalLocation.X + HalfMapSize) / TileSize);
	const int32 FileY = FMath::FloorToInt((HalfMapSize - LocalLocation.Y) / TileSize);
	const float HorizontalClearance = FMath::Max(0.0f, ClearanceCm);
	TSet<int32> CheckedBuildings;

	// TileBuildingIds maps every child tile back to its footprint owner. The one-tile margin also
	// catches authored GEO geometry that overhangs its nominal SC2 footprint.
	for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
	{
		for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
		{
			const int32 TileX = FileX + OffsetX;
			const int32 TileY = FileY + OffsetY;
			if (TileX < 0 || TileX >= MapSize || TileY < 0 || TileY >= MapSize)
			{
				continue;
			}

			const int32 BuildingId = TileBuildingIds[TileY * MapSize + TileX];
			if (!Buildings.IsValidIndex(BuildingId) || Buildings[BuildingId].bDemolished ||
				CheckedBuildings.Contains(BuildingId))
			{
				continue;
			}
			CheckedBuildings.Add(BuildingId);

			FBox Bounds(ForceInit);
			if (!TryGetBuildingBoundsAtTile(TileX, TileY, Bounds))
			{
				continue;
			}

			if (WorldLocation.X >= Bounds.Min.X - HorizontalClearance &&
				WorldLocation.X <= Bounds.Max.X + HorizontalClearance &&
				WorldLocation.Y >= Bounds.Min.Y - HorizontalClearance &&
				WorldLocation.Y <= Bounds.Max.Y + HorizontalClearance &&
				WorldLocation.Z >= Bounds.Min.Z - 10.0f &&
				WorldLocation.Z <= Bounds.Max.Z + 10.0f)
			{
				return true;
			}
		}
	}

	return false;
}

bool ASimCity2000CityActor::DemolishBuildingAtTile(int32 FileX, int32 FileY, TArray<FIntPoint>& OutClearedTiles, bool bLeaveRubble)
{
	OutClearedTiles.Reset();
	if (!HasStandingBuildingAtTile(FileX, FileY))
	{
		return false;
	}

	constexpr int32 MapSize = FSimCity2000City::MapSize;
	const int32 BuildingId = TileBuildingIds[FileY * MapSize + FileX];
	FSimCopterCityBuilding& Building = Buildings[BuildingId];

	for (FSimCopterBuildingPart& Part : Building.Parts)
	{
		RemoveBuildingInstance(Part);
	}
	Building.Parts.Reset();
	Building.bDemolished = true;

	// FUN_004a5fd0 swaps the structure's geometry for the rubble model matching its footprint,
	// so the site is left as a debris pile rather than bare ground.
	const int32 RubbleSlot = FMath::Clamp(FMath::Max(Building.FootprintTiles.X, Building.FootprintTiles.Y), 1, 4) - 1;
	if (bLeaveRubble && RubbleComponentIndices[RubbleSlot] != INDEX_NONE)
	{
		Building.RubblePart = AddBuildingInstance(RubbleComponentIndices[RubbleSlot], BuildingId, Building.PlacementOrigin);
	}

	// The footprint stops being a building - it is rubble now - but the tiles keep pointing at the
	// record so the rubble remains resolvable and a repeat demolition is a no-op.
	for (int32 OffsetY = 0; OffsetY < Building.FootprintTiles.Y; ++OffsetY)
	{
		for (int32 OffsetX = 0; OffsetX < Building.FootprintTiles.X; ++OffsetX)
		{
			const int32 TileX = Building.OriginTile.X + OffsetX;
			const int32 TileY = Building.OriginTile.Y + OffsetY;
			if (TileX >= MapSize || TileY >= MapSize)
			{
				continue;
			}
			const int32 TileIndex = TileY * MapSize + TileX;
			if (BuildingTileFlags.IsValidIndex(TileIndex))
			{
				BuildingTileFlags[TileIndex] = 0;
			}
			OutClearedTiles.Emplace(TileX, TileY);
		}
	}

	return true;
}

void ASimCity2000CityActor::GetDemolishedBuildingOrigins(TArray<FIntPoint>& OutOrigins) const
{
	OutOrigins.Reset();
	for (const FSimCopterCityBuilding& Building : Buildings)
	{
		if (Building.bDemolished)
		{
			OutOrigins.Add(Building.OriginTile);
		}
	}
}

void ASimCity2000CityActor::RestoreDemolishedBuildingOrigins(
	const TArray<FIntPoint>& Origins,
	TArray<FIntPoint>& OutClearedTiles)
{
	OutClearedTiles.Reset();
	for (const FIntPoint& Origin : Origins)
	{
		TArray<FIntPoint> BuildingTiles;
		if (DemolishBuildingAtTile(Origin.X, Origin.Y, BuildingTiles, /*bLeaveRubble=*/true))
		{
			OutClearedTiles.Append(BuildingTiles);
		}
	}
}

bool ASimCity2000CityActor::IsBuildingCollisionHit(
	const UPrimitiveComponent* HitComponent,
	const FVector& WorldLocation) const
{
	if (HitComponent == nullptr)
	{
		return false;
	}

	if (HitComponent == TerrainMeshComponent.Get() ||
		HitComponent == RoadMarkingMeshComponent.Get())
	{
		return false;
	}

	// An instanced building is a building by construction - it only exists while it stands.
	for (const UInstancedStaticMeshComponent* BuildingComponent : BuildingInstanceComponents)
	{
		if (BuildingComponent == HitComponent)
		{
			return true;
		}
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
	// A session started from the main menu names the city to play (cities/career/city<N>.sc2 for a
	// career, any .sc2 for a user game), which wins over whatever this actor was saved with. The
	// traffic system reads the path back through GetResolvedCityPath, so both stay in step.
	if (const FString SessionCityPath = GetSessionCityFilePath(); !SessionCityPath.IsEmpty())
	{
		return SessionCityPath;
	}

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
	// The level authors this to the developer checkout's Reference copy, which is right in the
	// editor and meaningless in a shipped build - so honour it only when it actually points at an
	// install, and otherwise ask where the player's data really is.
	const FString ConfiguredPath = OriginalGameRoot.Path.TrimStartAndEnd();
	if (!ConfiguredPath.IsEmpty())
	{
		const FString Absolute = FPaths::IsRelative(ConfiguredPath)
			? FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), ConfiguredPath))
			: FPaths::ConvertRelativePathToFull(ConfiguredPath);
		if (SimCopterOriginalGame::IsOriginalGameRoot(Absolute))
		{
			return Absolute;
		}
	}

	return SimCopterOriginalGame::ResolveRoot();
}

bool ASimCity2000CityActor::IsRoadLikeTile(uint8 BuildingId)
{
	return BuildingId >= 0x0E && BuildingId <= 0x6F;
}

bool ASimCity2000CityActor::IsBuildingLikeTile(uint8 BuildingId)
{
	return BuildingId >= 0x70;
}
