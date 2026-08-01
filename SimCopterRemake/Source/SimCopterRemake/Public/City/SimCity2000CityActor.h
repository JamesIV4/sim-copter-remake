// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/NoExportTypes.h"
#include "UObject/SoftObjectPath.h"
#include "SimCity2000CityActor.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPrimitiveComponent;
class UProceduralMeshComponent;
class USceneComponent;
class USimCopterFlashingLightsComponent;
class UStaticMesh;
class UTexture2D;

// One model instance making up a placed building. A building always places its primary object
// and, for some dispatches, a secondary object at the same origin; both come away together when
// it is demolished, and its rubble is placed the same way.
struct FSimCopterBuildingPart
{
	int32 ComponentIndex = INDEX_NONE;
	int32 InstanceIndex = INDEX_NONE;

	bool IsValid() const { return ComponentIndex != INDEX_NONE && InstanceIndex != INDEX_NONE; }
	void Reset() { ComponentIndex = INDEX_NONE; InstanceIndex = INDEX_NONE; }
};

// One placed building. Its identity is its building id - its index in the city's building array -
// which is fixed for the life of a city build. The instance indices underneath it are not: they
// move whenever a neighbouring instance in the same component is removed, so nothing outside this
// record should ever hold one.
struct FSimCopterCityBuilding
{
	// Top-left tile of the footprint, and the footprint it covers.
	FIntPoint OriginTile = FIntPoint::ZeroValue;
	FIntPoint FootprintTiles = FIntPoint(1, 1);
	// Component-space origin the model was placed at; the rubble takes the same spot.
	FVector PlacementOrigin = FVector::ZeroVector;
	uint8 XbldId = 0;
	bool bDemolished = false;
	// At most one part per component - the primary and secondary objects are different models.
	TArray<FSimCopterBuildingPart, TInlineAllocator<2>> Parts;
	// Valid once demolished, if a rubble model existed for this footprint size.
	FSimCopterBuildingPart RubblePart;
};

UCLASS()
class SIMCOPTERREMAKE_API ASimCity2000CityActor : public AActor
{
	GENERATED_BODY()

public:
	ASimCity2000CityActor();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "SimCopter|City")
	void RebuildCity();

	UFUNCTION(BlueprintPure, Category = "SimCopter|City")
	FString GetResolvedCityPath() const;

	UFUNCTION(BlueprintPure, Category = "SimCopter|City")
	FString GetResolvedOriginalGameRoot() const;

	UFUNCTION(BlueprintPure, Category = "SimCopter|City")
	float GetTileSize() const;

	// Debug tuning for the buildings' blink markers. The scale multiplies whatever this city's
	// beacons were tuned to, so it stays in balance with the helicopter's own markers.
	float GetFlashingLightIntensityScale() const;
	void SetFlashingLightIntensityScale(float Scale);

	// FUN_004814c0's threshold is fed by DAT_005039a0 fixed-time units, not milliseconds. The
	// material exposes the resulting five-frame texture cadence so it can be tuned live without
	// rebuilding either the city or the shaders. Zero deliberately freezes frame zero for inspection.
	static constexpr float DefaultWaterTextureFramesPerSecond = 4.0f;
	static constexpr float MaxWaterTextureFramesPerSecond = 120.0f;
	static float SanitizeWaterTextureFramesPerSecond(float FramesPerSecond);
	float GetWaterTextureFramesPerSecond() const { return WaterTextureFramesPerSecond; }
	void SetWaterTextureFramesPerSecond(float FramesPerSecond);

	UFUNCTION(BlueprintPure, Category = "SimCopter|City")
	bool UsesOriginalTerrainHeightScale() const;

	UFUNCTION(BlueprintPure, Category = "SimCopter|City")
	float GetTerrainHeightScale() const;

	UFUNCTION(BlueprintPure, Category = "SimCopter|City")
	float GetEffectiveTerrainHeightScale() const;

	// The raised road caps (TL63..TL66) and road bridge band place their driving plane exactly
	// one original terrain-height step above the owning tile's terrain origin. Kept public so the
	// city markings and traffic graph use the same decoded surface rule.
	static bool IsOneStepRaisedRoadDeckTile(uint8 BuildingId);

	// Samples the same conditioned terrain triangle and terrain-class grid used to build the
	// visible city. Terrain classes below 10 are water in the original water gameplay routines.
	// Returns false outside the original 128x128 gameplay map or before a city has been rebuilt.
	bool TryGetWaterGameplaySurface(
		const FVector& WorldLocation,
		float& OutSurfaceWorldZ,
		uint8& OutTerrainClass,
		FIntPoint* OutTile = nullptr) const;

	// World Z of the ocean surface, which the terrain build averages over every water vertex.
	// False before a city has been rebuilt.
	bool TryGetOceanSurfaceWorldZ(float& OutWorldZ) const
	{
		OutWorldZ = CachedOceanSurfaceZ;
		return bHasOceanSurfaceZ;
	}

	// The two grids the cockpit map shades open ground with (FUN_004a28e0): the conditioned
	// terrain class per tile, and the tmap corner sample reduced to the map's 0..15 shade the way
	// the original does it - one right shift by 6, clamped - because a corner holds
	// (height step + 1) * 0x20. Both come out 128x128, indexed [FileY * MapSize + FileX]. False
	// before a city has been rebuilt.
	bool TryGetMapTerrainGrids(TArray<uint8>& OutTerrainClasses, TArray<uint8>& OutAltitudeShades) const;

	bool IsTerrainCollisionComponent(const UPrimitiveComponent* HitComponent) const;
	bool IsBuildingCollisionHit(const UPrimitiveComponent* HitComponent, const FVector& WorldLocation) const;

	// FUN_004a5fd0: the building covering this tile burned down. Removes its instances - which
	// takes its geometry and its collision with it, since instance bodies come from the model's
	// one shared cook - and clears the footprint's building flags so nothing treats it as a
	// building any more. Any tile of the footprint may be passed, and OutClearedTiles reports the
	// whole footprint. Returns false when the tile has no removable building (already demolished,
	// or the building was baked rather than instanced).
	//
	// bLeaveRubble is the original's behaviour and what a fire wants. Clearing a site to build on
	// - the player's hangar - passes false, because a fresh building standing in a debris pile
	// reads as a bug rather than as history.
	UFUNCTION(BlueprintCallable, Category = "SimCopter|City")
	bool DemolishBuildingAtTile(int32 FileX, int32 FileY, TArray<FIntPoint>& OutClearedTiles, bool bLeaveRubble = true);
	void GetDemolishedBuildingOrigins(TArray<FIntPoint>& OutOrigins) const;
	void RestoreDemolishedBuildingOrigins(const TArray<FIntPoint>& Origins, TArray<FIntPoint>& OutClearedTiles);

	// True while the tile is covered by a building that has not been demolished.
	UFUNCTION(BlueprintPure, Category = "SimCopter|City")
	bool HasStandingBuildingAtTile(int32 FileX, int32 FileY) const;

	// True where a building burned down and left the footprint-sized rubble model in its place.
	UFUNCTION(BlueprintPure, Category = "SimCopter|City")
	bool HasRubbleAtTile(int32 FileX, int32 FileY) const;

	// False when the building instances did not survive into this world - the state a duplicated
	// (PIE) or loaded world starts in. Runtime-built static meshes are deliberately excluded from
	// duplication because their fast-built render data cannot be copied safely; BeginPlay rebuilds
	// when this reports false.
	bool AreBuildingInstancesIntact() const;

	// World-space bounds of the standing building covering this tile, for the demolition's
	// debris and damage sweep. Returns false when there is nothing there.
	bool TryGetBuildingBoundsAtTile(int32 FileX, int32 FileY, FBox& OutWorldBounds) const;

	// Conservative spawn guard backed by the rendered instanced-mesh bounds, including model
	// overhang beyond the SC2 tile footprint. ClearanceCm expands only the horizontal footprint.
	bool IsInsideStandingBuildingBounds(const FVector& WorldLocation, float ClearanceCm = 0.0f) const;

private:
	UPROPERTY(VisibleAnywhere, Category = "SimCopter|City")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "SimCopter|City")
	TObjectPtr<UProceduralMeshComponent> TerrainMeshComponent;

	UPROPERTY(VisibleAnywhere, Category = "SimCopter|City")
	TObjectPtr<UProceduralMeshComponent> OriginalMeshComponent;

	UPROPERTY(VisibleAnywhere, Category = "SimCopter|City")
	TObjectPtr<UProceduralMeshComponent> RoadMarkingMeshComponent;

	// Every placed building's face-type-25 blink markers, gathered during RebuildCity. One
	// component serves the whole city because the original drives them all off a single global
	// phase counter (FUN_00496c00).
	UPROPERTY(VisibleAnywhere, Category = "SimCopter|City")
	TObjectPtr<USimCopterFlashingLightsComponent> FlashingLightsComponent;

	// Buildings, airports, power plants and the traffic signals blink; turn this off to drop them.
	UPROPERTY(EditAnywhere, Category = "SimCopter|City")
	bool bRenderFlashingLights = true;

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

	// Filled by the terrain build; see TryGetOceanSurfaceWorldZ.
	float CachedOceanSurfaceZ = 0.0f;
	bool bHasOceanSurfaceZ = false;

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

	// Playback rate for the five original water texture cells. Independent of the WPO waves above.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Render|Water", meta = (ClampMin = "0.0", ClampMax = "120.0"))
	float WaterTextureFramesPerSecond = DefaultWaterTextureFramesPerSecond;

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

	// Place buildings as instances of a per-model runtime static mesh instead of baking their
	// triangles into the shared OriginalMeshComponent. Each distinct GEO model is built and
	// collision-cooked once and then instanced, which is what lets a single building be removed
	// when it burns down - baked into the merged mesh there is no per-building identity to
	// remove. Turning this off restores the old merged path (and disables demolition).
	UPROPERTY(EditAnywhere, Category = "SimCopter|Original Meshes")
	bool bInstanceBuildingMeshes = true;

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

	UPROPERTY(VisibleInstanceOnly, Category = "SimCopter|Debug")
	int32 LastFlashingLightCount = 0;

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

	// The dedicated terrain-water MID. Mesh-pool water remains on its authored static atlas cell.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> WaterTextureMaterials;

	UPROPERTY(VisibleInstanceOnly, Category = "SimCopter|Debug")
	int32 LastBuildingModelCount = 0;

	UPROPERTY(VisibleInstanceOnly, Category = "SimCopter|Debug")
	int32 LastBuildingInstanceCount = 0;

	// One instanced component per distinct building model, each holding every placement of that
	// model in the city. Kept parallel with BuildingInstanceTiles below.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> BuildingInstanceComponents;

	// The runtime static meshes those components render, held so they survive collection.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMesh>> BuildingModelMeshes;

	TArray<uint8> BuildingTileFlags;

	// Conditioned 129x129 terrain vertices and 128x128 class grid retained for the water bucket
	// and particle collision paths. Rendering used to discard both after RebuildCity.
	TArray<float> WaterGameplayCornerZ;
	TArray<uint8> WaterGameplayTerrainClasses;

	// The same 129x129 grid in the original's own sample units, kept because the cockpit map
	// shades ground by shifting the raw sample rather than by any world height.
	TArray<int16> MapAltitudeCorners;

	// Every placed building, indexed by building id. Demolished entries stay put so their id, tile
	// span and rubble remain resolvable.
	TArray<FSimCopterCityBuilding> Buildings;

	// Every tile a building covers -> that building's id, INDEX_NONE where none was placed. The
	// whole footprint points at the one id, so demolition can be asked for with any of its tiles.
	TArray<int32> TileBuildingIds;

	// Per component, the building id owning each of its instances. Kept in lockstep with the
	// component's instance array so a removal can be mirrored and the one displaced instance
	// re-pointed - see RemoveBuildingInstance.
	TArray<TArray<int32>> ComponentInstanceBuildings;

	// Rubble model component per footprint size 1..4 (original objects 0x14f..0x152), built up
	// front so a collapse costs only an AddInstance.
	int32 RubbleComponentIndices[4] = { INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE };

	void ResetBuildingInstances();
	// The part of this building held in the given component, or null. At most one exists: a
	// building's primary, secondary and rubble models are always distinct models.
	FSimCopterBuildingPart* FindBuildingPartInComponent(int32 BuildingId, int32 ComponentIndex);
	// Removes one instance and repairs the single index the removal displaces. The components use
	// RemoveAtSwap, so exactly one other instance moves - the last one into the freed slot.
	void RemoveBuildingInstance(FSimCopterBuildingPart& Part);
	// Adds an instance of a model component and records which building owns it.
	FSimCopterBuildingPart AddBuildingInstance(int32 ComponentIndex, int32 BuildingId, const FVector& Origin);

	FString ResolveCityPath() const;
	FString ResolveOriginalGameRoot() const;

	// The city the main menu asked for (USimCopterSessionSubsystem), or empty when the level was
	// entered without a session - then the actor's own CityFile is used.
	FString GetSessionCityFilePath() const;

	static bool IsRoadLikeTile(uint8 BuildingId);
	static bool IsBuildingLikeTile(uint8 BuildingId);
};
