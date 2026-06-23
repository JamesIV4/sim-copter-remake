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
constexpr int32 SimCopterTerrainTextureNameIndex = 100000;

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

FVector2D GetMaxisAtlasCellUV(int32 TileIndex, float LocalU, float LocalV)
{
	const int32 ClampedTileIndex = FMath::Clamp(TileIndex, 0, 63);
	const int32 Column = ClampedTileIndex % FMaxisTextureReader::AtlasColumnCount;
	const int32 RowFromBottom = ClampedTileIndex / FMaxisTextureReader::AtlasColumnCount;
	const float CellScale = 1.0f / static_cast<float>(FMaxisTextureReader::AtlasColumnCount);
	constexpr float LocalPixelInset = 0.5f / static_cast<float>(FMaxisTextureReader::AtlasTileSize);
	const float InsetLocalU = FMath::Lerp(LocalPixelInset, 1.0f - LocalPixelInset, LocalU);
	const float InsetLocalV = FMath::Lerp(LocalPixelInset, 1.0f - LocalPixelInset, LocalV);

	return FVector2D(
		(static_cast<float>(Column) + InsetLocalU) * CellScale,
		(static_cast<float>(FMaxisTextureReader::AtlasColumnCount - 1 - RowFromBottom) + (1.0f - InsetLocalV)) * CellScale);
}

int32 ResolveTerrainAtlasTileIndex(const FSimCity2000Tile& Tile)
{
	const int32 TerrainCode = static_cast<int32>(Tile.Terrain);
	if (TerrainCode < 0x10)
	{
		return FMath::Clamp(0x20 + TerrainCode, 0, 63);
	}

	return FMath::Clamp(TerrainCode - 0x10, 0, 63);
}

void AppendTerrainTile(
	const FSimCity2000City& City,
	int32 FileX,
	int32 FileY,
	float TileSize,
	float TerrainHeightScale,
	float HalfMapSize,
	FOriginalMeshSectionData& Section)
{
	const int32 TileIndex = FileY * FSimCity2000City::MapSize + FileX;
	const FSimCity2000Tile& Tile = City.Tiles[TileIndex];
	const int32 VertexStart = Section.Vertices.Num();
	const int32 AtlasTileIndex = ResolveTerrainAtlasTileIndex(Tile);
	const float TerrainZ = GetTerrainSurfaceZ(Tile, TerrainHeightScale);

	const FVector V0(GetWorldGridCoordinate(FileX, TileSize, HalfMapSize), GetWorldGridCoordinate(FileY, TileSize, HalfMapSize), TerrainZ);
	const FVector V1(GetWorldGridCoordinate(FileX + 1, TileSize, HalfMapSize), GetWorldGridCoordinate(FileY, TileSize, HalfMapSize), TerrainZ);
	const FVector V2(GetWorldGridCoordinate(FileX + 1, TileSize, HalfMapSize), GetWorldGridCoordinate(FileY + 1, TileSize, HalfMapSize), TerrainZ);
	const FVector V3(GetWorldGridCoordinate(FileX, TileSize, HalfMapSize), GetWorldGridCoordinate(FileY + 1, TileSize, HalfMapSize), TerrainZ);

	Section.Vertices.Add(V0);
	Section.Vertices.Add(V1);
	Section.Vertices.Add(V2);
	Section.Vertices.Add(V3);

	Section.UVs.Add(GetMaxisAtlasCellUV(AtlasTileIndex, 0.0f, 1.0f));
	Section.UVs.Add(GetMaxisAtlasCellUV(AtlasTileIndex, 1.0f, 1.0f));
	Section.UVs.Add(GetMaxisAtlasCellUV(AtlasTileIndex, 1.0f, 0.0f));
	Section.UVs.Add(GetMaxisAtlasCellUV(AtlasTileIndex, 0.0f, 0.0f));

	FVector Normal = FVector::CrossProduct(V1 - V0, V2 - V0).GetSafeNormal();
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

	Section.Triangles.Add(VertexStart + 2);
	Section.Triangles.Add(VertexStart);
	Section.Triangles.Add(VertexStart + 1);
	Section.Triangles.Add(VertexStart);
	Section.Triangles.Add(VertexStart + 3);
	Section.Triangles.Add(VertexStart + 2);
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

FVector RotateCityLocalVertex(const FVector& LocalVertex, int32 QuarterTurns)
{
	const int32 NormalizedQuarterTurns = ((QuarterTurns % 4) + 4) % 4;
	switch (NormalizedQuarterTurns)
	{
	case 1:
		return FVector(-LocalVertex.Y, LocalVertex.X, LocalVertex.Z);
	case 2:
		return FVector(-LocalVertex.X, -LocalVertex.Y, LocalVertex.Z);
	case 3:
		return FVector(LocalVertex.Y, -LocalVertex.X, LocalVertex.Z);
	default:
		return LocalVertex;
	}
}

int32 AppendMaxisMeshObject(
	const FMaxisMeshObject& MeshObject,
	const TArray<FColor>* ColorMap,
	const FVector& TileOrigin,
	float MeshUnitsPerCentimeter,
	float MeshScale,
	int32 MeshQuarterTurns,
	bool bRenderBackfaces,
	bool bUseOriginalTextures,
	const TSet<int32>& AvailableTextureKeys,
	const FLinearColor& TexturedFaceFallbackColor,
	TMap<int32, FOriginalMeshSectionData>& Sections,
	int32& OutTexturedTriangleCount)
{
	int32 AddedTriangleCount = 0;
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

			const FVector LocalVertex = RotateCityLocalVertex(FMaxisMeshReader::ConvertMaxisVertexToUnreal(MeshObject.Vertices[SourceVertexIndex], MeshUnitsPerCentimeter) * MeshScale, MeshQuarterTurns);
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

		const FVector FaceNormal = FVector::CrossProduct(
			Section.Vertices[FaceVertexStart + 1] - Section.Vertices[FaceVertexStart],
			Section.Vertices[FaceVertexStart + 2] - Section.Vertices[FaceVertexStart]).GetSafeNormal();

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
	const int32 CityMeshQuarterTurns = (4 - (City.Rotation & 0x3)) & 0x3;

	FOriginalMeshSectionData TerrainSection;
	TMap<int32, FOriginalMeshSectionData> OriginalMeshSections;
	const bool bUseTexturedTerrainSurface = TerrainTexture != nullptr && TexturedMaterial != nullptr;

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

			const float WorldX = GetWorldTileCenterCoordinate(static_cast<float>(FileX), TileSize, HalfMapSize);
			const float WorldY = GetWorldTileCenterCoordinate(static_cast<float>(FileY), TileSize, HalfMapSize);
			const float TerrainTopZ = GetTerrainTileCenterZ(City, FileX, FileY, EffectiveTerrainHeightScale);
			const bool bRoadLikeTile = IsRoadLikeTile(Tile.Building);
			const bool bBuildingLikeTile = IsBuildingLikeTile(Tile.Building);
			bool bRenderedOriginalMesh = false;
			bool bSuppressPlaceholderForFootprintChild = false;

			if (bRenderTerrain)
			{
				AppendTerrainTile(City, FileX, FileY, TileSize, EffectiveTerrainHeightScale, HalfMapSize, TerrainSection);
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
					const FMaxisMeshObject* MeshObject = MeshLibrary.FindObjectByTileId(Tile.Building, &ColorMap);
					if (MeshObject != nullptr)
					{
						const float MeshWorldX = GetWorldTileCenterCoordinate(static_cast<float>(FileX) + (static_cast<float>(Footprint.Width) - 1.0f) * 0.5f, TileSize, HalfMapSize);
						const float MeshWorldY = GetWorldTileCenterCoordinate(static_cast<float>(FileY) + (static_cast<float>(Footprint.Height) - 1.0f) * 0.5f, TileSize, HalfMapSize);
						const float MeshTerrainTopZ = GetAverageTerrainSurfaceZ(City, FileX, FileY, Footprint.Width, Footprint.Height, EffectiveTerrainHeightScale);
						const FVector TileOrigin(MeshWorldX, MeshWorldY, MeshTerrainTopZ + OriginalMeshZOffset);
						OriginalMeshTriangleCount += AppendMaxisMeshObject(
							*MeshObject,
							ColorMap,
							TileOrigin,
							OriginalMeshUnitsPerCentimeter,
							OriginalMeshScale,
							CityMeshQuarterTurns,
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

	if (TerrainSection.Vertices.Num() > 0)
	{
		TerrainMeshComponent->CreateMeshSection_LinearColor(
			0,
			TerrainSection.Vertices,
			TerrainSection.Triangles,
			TerrainSection.Normals,
			TerrainSection.UVs,
			TerrainSection.VertexColors,
			TerrainSection.Tangents,
			false);

		if (bUseTexturedTerrainSurface)
		{
			UMaterialInstanceDynamic* TerrainMaterial = UMaterialInstanceDynamic::Create(TexturedMaterial, this);
			if (TerrainMaterial != nullptr)
			{
				TerrainMaterial->SetTextureParameterValue(TEXT("Texture"), TerrainTexture);
				OriginalTextureMaterials.Add(TerrainMaterial);
				TerrainMeshComponent->SetMaterial(0, TerrainMaterial);
			}
		}
		else if (VertexColorMaterial != nullptr)
		{
			TerrainMeshComponent->SetMaterial(0, VertexColorMaterial);
		}
	}

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
