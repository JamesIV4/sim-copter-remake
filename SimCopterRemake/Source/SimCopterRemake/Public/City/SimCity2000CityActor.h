// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/NoExportTypes.h"
#include "UObject/SoftObjectPath.h"
#include "SimCity2000CityActor.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPrimitiveComponent;
class UProceduralMeshComponent;
class USceneComponent;
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

	UFUNCTION(BlueprintPure, Category = "SimCopter|City")
	FString GetResolvedCityPath() const;

	UFUNCTION(BlueprintPure, Category = "SimCopter|City")
	FString GetResolvedOriginalGameRoot() const;

	UFUNCTION(BlueprintPure, Category = "SimCopter|City")
	float GetTileSize() const;

	UFUNCTION(BlueprintPure, Category = "SimCopter|City")
	bool UsesOriginalTerrainHeightScale() const;

	UFUNCTION(BlueprintPure, Category = "SimCopter|City")
	float GetTerrainHeightScale() const;

	UFUNCTION(BlueprintPure, Category = "SimCopter|City")
	float GetEffectiveTerrainHeightScale() const;

	bool IsBuildingCollisionHit(const UPrimitiveComponent* HitComponent, const FVector& WorldLocation) const;

private:
	UPROPERTY(VisibleAnywhere, Category = "SimCopter|City")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "SimCopter|City")
	TObjectPtr<UProceduralMeshComponent> TerrainMeshComponent;

	UPROPERTY(VisibleAnywhere, Category = "SimCopter|City")
	TObjectPtr<UProceduralMeshComponent> OriginalMeshComponent;

	UPROPERTY(VisibleAnywhere, Category = "SimCopter|City")
	TObjectPtr<UProceduralMeshComponent> RoadMarkingMeshComponent;

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

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render")
	bool bUseOriginalTerrainHeightScale = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render", meta = (ClampMin = "1.0"))
	float TerrainHeightScale = 200.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render")
	bool bRenderTerrain = true;

	// Weld and average the natural terrain's per-tile flat normals into smooth corner normals so the
	// ground shades smoothly instead of blocky. Tiles under buildings/roads stay flat (crisp pads).
	UPROPERTY(EditAnywhere, Category = "SimCopter|Render")
	bool bSmoothTerrainShading = true;

	// Perturb the ground's shading normal with three octaves of procedural noise (M_SimCopterTerrain)
	// for subtle organic relief. Faded to zero near the shoreline and on building/road pads so it does
	// not disturb the water weld or the flat pads. Amplitudes are ~bump height, scales ~bump size (uu).
	UPROPERTY(EditAnywhere, Category = "SimCopter|Render|Terrain Detail")
	bool bEnableTerrainDetailNoise = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render|Terrain Detail", meta = (ClampMin = "0.0"))
	float TerrainNoiseAmpFine = 12.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render|Terrain Detail", meta = (ClampMin = "1.0"))
	float TerrainNoiseScaleFine = 350.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render|Terrain Detail", meta = (ClampMin = "0.0"))
	float TerrainNoiseAmpMed = 45.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render|Terrain Detail", meta = (ClampMin = "1.0"))
	float TerrainNoiseScaleMed = 1000.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render|Terrain Detail", meta = (ClampMin = "0.0"))
	float TerrainNoiseAmpLarge = 150.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render|Terrain Detail", meta = (ClampMin = "1.0"))
	float TerrainNoiseScaleLarge = 3000.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render|Terrain Detail", meta = (ClampMin = "0.0"))
	float TerrainNoiseAmpXLarge = 400.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render|Terrain Detail", meta = (ClampMin = "1.0"))
	float TerrainNoiseScaleXLarge = 8000.0f;

	// How many tiles inland the detail noise ramps from 0 (at the shoreline) up to full strength.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Render|Terrain Detail", meta = (ClampMin = "1", ClampMax = "8"))
	int32 TerrainNoiseWaterFadeTiles = 3;

	// How many tiles the detail noise ramps from 0 (on building/road pads) up to full strength, so the
	// noise eases in away from the flat pads instead of snapping on at the tile edge.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Render|Terrain Detail", meta = (ClampMin = "1", ClampMax = "8"))
	int32 TerrainNoisePadFadeTiles = 2;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render")
	bool bRenderProceduralMapExtension = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render", meta = (ClampMin = "0", ClampMax = "256"))
	int32 ProceduralMapExtensionTiles = 96;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render")
	bool bRenderWater = true;

	// The original SimCopter water surface bobs up and down. When enabled (and the textured terrain
	// surface is in use) the water tiles get their own mesh section drawn with M_SimCopterWater, which
	// displaces and lights them in the vertex shader (see Docs/ReverseEngineering.md).
	UPROPERTY(EditAnywhere, Category = "SimCopter|Render|Water")
	bool bAnimateWaterSurface = true;

	// How many tiles it takes for the waves to ramp from calm (welded to the shore) to full open-water
	// amplitude. Higher = gentler transition, less of a lighting seam where water meets land.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Render|Water", meta = (ClampMin = "1", ClampMax = "8"))
	int32 WaterShoreRampTiles = 3;

	// Peak vertical displacement of the undulating water surface, in world units.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Render|Water", meta = (ClampMin = "0.0"))
	float WaterWaveAmplitude = 28.0f;

	// Distance between wave crests, in world units.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Render|Water", meta = (ClampMin = "1.0"))
	float WaterWaveLength = 1100.0f;

	// Wave travel speed multiplier (radians per second at the primary frequency).
	UPROPERTY(EditAnywhere, Category = "SimCopter|Render|Water", meta = (ClampMin = "0.0"))
	float WaterWaveSpeed = 1.1f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render")
	bool bRenderRoads = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render")
	bool bRenderRoadMarkings = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render", meta = (ClampMin = "1.0"))
	float RoadMarkingWidth = 5.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render", meta = (ClampMin = "0.0"))
	float RoadMarkingZOffset = 8.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Collision")
	bool bEnableTerrainCollision = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Collision")
	bool bEnableOriginalMeshCollision = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Original Meshes")
	bool bRenderOriginalMeshes = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Original Meshes")
	bool bRenderOriginalTextures = true;

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
	FLinearColor RoadMarkingColor = FLinearColor(1.0f, 0.82f, 0.22f);

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
	TObjectPtr<UMaterialInterface> VertexColorMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> TexturedMaterial;

	// Base material for the undulating water surface (M_SimCopterWater); a dynamic instance is created
	// per rebuild to feed it the terrain water texture and the wave parameters below.
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> WaterMaterial;

	// Base material for the ground with procedural detail-noise normals (M_SimCopterTerrain); dynamic
	// instances are created per rebuild to feed the terrain texture and the noise parameters.
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> TerrainMaterial;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> OriginalTextureCache;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> OriginalTextureMaterials;

	TArray<uint8> BuildingTileFlags;

	FString ResolveCityPath() const;
	FString ResolveOriginalGameRoot() const;

	static bool IsRoadLikeTile(uint8 BuildingId);
	static bool IsBuildingLikeTile(uint8 BuildingId);
};
