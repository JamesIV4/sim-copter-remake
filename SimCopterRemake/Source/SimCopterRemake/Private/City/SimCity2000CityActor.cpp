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

			const FVector LocalVertex = FMaxisMeshReader::ConvertMaxisVertexToUnreal(MeshObject.Vertices[SourceVertexIndex], MeshUnitsPerCentimeter) * MeshScale;
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
	WaterInstances->ClearInstances();
	RoadInstances->ClearInstances();
	BuildingInstances->ClearInstances();
	OriginalMeshComponent->ClearAllMeshSections();
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
	bool bOriginalTexturesLoaded = false;
	if (bRenderOriginalMeshes)
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

	TMap<int32, FOriginalMeshSectionData> OriginalMeshSections;

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

			const float WorldX = (static_cast<float>(FSimCity2000City::MapSize - 1 - FileX) + 0.5f) * TileSize - HalfMapSize;
			const float WorldY = (static_cast<float>(FSimCity2000City::MapSize - 1 - FileY) + 0.5f) * TileSize - HalfMapSize;
			const float TerrainTopZ = (static_cast<float>(Tile.Altitude) + 1.0f) * TerrainHeightScale;
			const bool bRoadLikeTile = IsRoadLikeTile(Tile.Building);
			const bool bBuildingLikeTile = IsBuildingLikeTile(Tile.Building);
			bool bRenderedOriginalMesh = false;

			if (bRenderTerrain)
			{
				const FVector TerrainScale(TileSize * CubeToUnrealScale, TileSize * CubeToUnrealScale, TerrainTopZ * CubeToUnrealScale);
				const FTransform TerrainTransform(FRotator::ZeroRotator, FVector(WorldX, WorldY, TerrainTopZ * 0.5f), TerrainScale);
				TerrainInstances->AddInstance(TerrainTransform);
				++TerrainCount;
			}

			if (bRenderWater && Tile.bWater)
			{
				const float WaterThickness = FMath::Max(RoadPlateHeight, 6.0f);
				const FVector WaterScale(TileSize * CubeToUnrealScale, TileSize * CubeToUnrealScale, WaterThickness * CubeToUnrealScale);
				const FTransform WaterTransform(FRotator::ZeroRotator, FVector(WorldX, WorldY, TerrainTopZ + WaterThickness * 0.5f + 1.0f), WaterScale);
				WaterInstances->AddInstance(WaterTransform);
				++WaterCount;
			}

			if (bOriginalMeshLibraryLoaded && Tile.Building > 0 && (bRoadLikeTile || bBuildingLikeTile))
			{
				const TArray<FColor>* ColorMap = nullptr;
				const FMaxisMeshObject* MeshObject = MeshLibrary.FindObjectByTileId(Tile.Building, &ColorMap);
				if (MeshObject != nullptr)
				{
					const FVector TileOrigin(WorldX, WorldY, TerrainTopZ + OriginalMeshZOffset);
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

			const bool bOriginalMeshAttemptedForThisTile = bRenderOriginalMeshes && (bRoadLikeTile || bBuildingLikeTile);
			const bool bRenderPlaceholderForThisTile = !bOriginalMeshAttemptedForThisTile || (!bRenderedOriginalMesh && bRenderPlaceholderForMissingOriginalMeshes);
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
		TEXT("Rendered SC2 city '%s' from '%s': terrain=%d water=%d roads=%d buildings=%d originalMeshTiles=%d missingOriginalMeshTiles=%d originalTriangles=%d texturedTriangles=%d originalTextures=%d chunks=%d rotation=%d waterLevel=%d"),
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
		City.WaterLevel);
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
