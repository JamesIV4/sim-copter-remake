// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/NoExportTypes.h"
#include "UObject/SoftObjectPath.h"
#include "SimCity2000CityActor.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UProceduralMeshComponent;
class USceneComponent;
class UStaticMesh;
class UTexture2D;

UCLASS()
class SIMCOPTERREMAKE_API ASimCity2000CityActor : public AActor
{
	GENERATED_BODY()

public:
	ASimCity2000CityActor();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "SimCopter|City")
	void RebuildCity();

private:
	UPROPERTY(VisibleAnywhere, Category = "SimCopter|City")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "SimCopter|City")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> TerrainInstances;

	UPROPERTY(VisibleAnywhere, Category = "SimCopter|City")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> WaterInstances;

	UPROPERTY(VisibleAnywhere, Category = "SimCopter|City")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> RoadInstances;

	UPROPERTY(VisibleAnywhere, Category = "SimCopter|City")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> BuildingInstances;

	UPROPERTY(VisibleAnywhere, Category = "SimCopter|City")
	TObjectPtr<UProceduralMeshComponent> OriginalMeshComponent;

	UPROPERTY(EditAnywhere, Category = "SimCopter|City", meta = (FilePathFilter = "sc2"))
	FFilePath CityFile;

	UPROPERTY(EditAnywhere, Category = "SimCopter|City")
	FDirectoryPath OriginalGameRoot;

	UPROPERTY(EditAnywhere, Category = "SimCopter|City")
	bool bLoadOnConstruction = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|City")
	bool bLoadOnBeginPlay = false;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render", meta = (ClampMin = "10.0"))
	float TileSize = 400.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render", meta = (ClampMin = "1.0"))
	float TerrainHeightScale = 60.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render", meta = (ClampMin = "1.0"))
	float BuildingHeightScale = 150.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render", meta = (ClampMin = "1.0"))
	float RoadPlateHeight = 8.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render")
	bool bRenderTerrain = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render")
	bool bRenderWater = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render")
	bool bRenderRoads = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render")
	bool bRenderBuildings = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Original Meshes")
	bool bRenderOriginalMeshes = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Original Meshes")
	bool bRenderOriginalTextures = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Original Meshes")
	bool bRenderPlaceholderForMissingOriginalMeshes = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Original Meshes")
	bool bRenderOriginalMeshBackfaces = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Original Meshes", meta = (ClampMin = "1.0"))
	float OriginalMeshUnitsPerCentimeter = 2621.44f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Original Meshes", meta = (ClampMin = "1.0"))
	float OriginalMeshSourceTileSize = 1600.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Original Meshes")
	float OriginalMeshZOffset = 2.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Original Meshes")
	FLinearColor OriginalTexturedFaceFallbackColor = FLinearColor(0.62f, 0.61f, 0.57f);

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render")
	FLinearColor TerrainColor = FLinearColor(0.24f, 0.38f, 0.20f);

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render")
	FLinearColor WaterColor = FLinearColor(0.05f, 0.23f, 0.55f);

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render")
	FLinearColor RoadColor = FLinearColor(0.05f, 0.05f, 0.045f);

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render")
	FLinearColor BuildingColor = FLinearColor(0.55f, 0.53f, 0.47f);

	UPROPERTY(VisibleInstanceOnly, Category = "SimCopter|Debug")
	FString LastLoadedCityName;

	UPROPERTY(VisibleInstanceOnly, Category = "SimCopter|Debug")
	FString LastLoadError;

	UPROPERTY(VisibleInstanceOnly, Category = "SimCopter|Debug")
	int32 LastOriginalMeshTileCount = 0;

	UPROPERTY(VisibleInstanceOnly, Category = "SimCopter|Debug")
	int32 LastMissingOriginalMeshTileCount = 0;

	UPROPERTY(VisibleInstanceOnly, Category = "SimCopter|Debug")
	int32 LastOriginalMeshTriangleCount = 0;

	UPROPERTY(VisibleInstanceOnly, Category = "SimCopter|Debug")
	int32 LastOriginalTextureCount = 0;

	UPROPERTY(VisibleInstanceOnly, Category = "SimCopter|Debug")
	int32 LastOriginalTexturedTriangleCount = 0;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> SharedCubeMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> SharedBaseMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> VertexColorMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> TexturedMaterial;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> OriginalTextureCache;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> OriginalTextureMaterials;

	FString ResolveCityPath() const;
	FString ResolveOriginalGameRoot() const;
	void ConfigureInstanceComponent(UHierarchicalInstancedStaticMeshComponent* Component) const;
	void ApplyComponentMaterial(UHierarchicalInstancedStaticMeshComponent* Component, const FLinearColor& Color);

	static bool IsRoadLikeTile(uint8 BuildingId);
	static bool IsBuildingLikeTile(uint8 BuildingId);
	static float EstimateBuildingFloors(uint8 BuildingId);
};
