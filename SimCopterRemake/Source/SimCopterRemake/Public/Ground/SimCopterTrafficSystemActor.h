// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/ObjectKey.h"
#include "UObject/NoExportTypes.h"
#include "SimCopterTrafficSystemActor.generated.h"

class ASimCity2000CityActor;
class ASimCopterGroundAgent;
class UInstancedStaticMeshComponent;

struct FSimCopterGroundRouteNode
{
	FVector LocalLocation = FVector::ZeroVector;
	FVector Location = FVector::ZeroVector;
	int32 FileX = 0;
	int32 FileY = 0;
	uint8 BuildingId = 0;
	int32 PeopleTileClass = INDEX_NONE;
	int32 PeopleFootprintSize = 1;
	int32 PeoplePlacementMode = 0;
	TArray<int32> Neighbors;
};

UENUM(BlueprintType)
enum class ESimCopterTrafficFlowMode : uint8
{
	Normal,
	TrafficJam
};

// Which car AI drives the traffic. Original = the decoded SimCopter behavior: cars wander the
// road graph with random turns, queue behind blockers (jams form naturally, and the player's
// landed helicopter blocks tiles), no traffic lights. Modernized = the remake's traffic-light +
// blockage-recovery system, kept for comparison/backup.
UENUM(BlueprintType)
enum class ESimCopterTrafficAiMode : uint8
{
	Original,
	Modernized
};

// Lightweight whole-map population entry: the entire city is simulated as records; full agents
// ("beamed" figures, in the original's terms) only exist near the camera. Far records render as
// two instanced-mesh batches.
struct FSimCopterWholeMapRecord
{
	FVector Location = FVector::ZeroVector;
	int32 Facing = 0;             // stored facing 0..7 (pedestrians)
	int32 BehaviorClass = 0;      // pedestrians: original behavior class (figure identity)
	int32 RouteNodeIndex = INDEX_NONE; // vehicles: previous road node
	int32 RouteNextIndex = INDEX_NONE; // vehicles: node being driven toward
};

struct FSimCopterVehicleTrafficState
{
	FVector LastLocation = FVector::ZeroVector;
	float BlockedSeconds = 0.0f;
	float RecentCollisionSeconds = 0.0f;
	float RecoveryCooldownSeconds = 0.0f;
	float TrafficLightLineGraceSeconds = 0.0f;
	float RecoveryBypassSeconds = 0.0f;
	float RecoveryRejoinSeconds = 0.0f;
	float IntersectionCommitSeconds = 0.0f;
	bool bInitialized = false;
	bool bInTrafficLightLine = false;
	bool bMissionJammed = false;
	// A car-fire mission (event mask 0x408) set this car alight. The car stays stopped and shows
	// flame visuals (rendered by the mission actor's fire component) until doused.
	bool bMissionOnFire = false;
	int32 MissionEventId = INDEX_NONE;
};

// One burning car reported to the fire renderer: a stable key + its world location.
struct FSimCopterBurningVehicle
{
	int32 Key = 0;
	int32 EventId = INDEX_NONE;
	FVector World = FVector::ZeroVector;
};

UCLASS()
class SIMCOPTERREMAKE_API ASimCopterTrafficSystemActor : public AActor
{
	GENERATED_BODY()

public:
	ASimCopterTrafficSystemActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "SimCopter|Traffic")
	bool RebuildSpawnData();

	bool TryGetPeopleTileCoordinateAtWorldLocation(
		const FVector& WorldLocation,
		int32& OutFileX,
		int32& OutFileY) const;
	int32 GetPeopleTileClassAtWorldLocation(const FVector& WorldLocation) const;
	bool TryGetTerrainWorldZAtWorldLocation(const FVector& WorldLocation, float& OutTerrainWorldZ) const;
	bool IsWaterTile(int32 FileX, int32 FileY) const;
	bool TryFindNearestTransportLandTile(int32 OriginX, int32 OriginY, int32& OutX, int32& OutY);
	int32 GetPeopleStoredFacingFromWorldLocations(
		const FVector& FromWorldLocation,
		const FVector& ToWorldLocation) const;
	bool TryGetPeopleFacingStepTarget(
		const FVector& FromWorldLocation,
		int32 Facing,
		float StepDistanceCm,
		FVector& OutWorldLocation,
		int32& OutTileClass) const;
	int32 GetXbldTileId(int32 FileX, int32 FileY) const;
	int32 GetBuildingFootprintSize(int32 FileX, int32 FileY) const;
	bool TryGetTileCenterWorldLocation(int32 FileX, int32 FileY, FVector& OutWorldLocation) const;
	// Convert a source-runtime (X, Y-up, Z) 16.16 offset with the same axis mapping,
	// city yaw, and actor transform used by the rendered city geometry.
	FVector ConvertOriginalOffsetToWorld(int32 X1616, int32 Y1616, int32 Z1616) const;
	// Inverse of ConvertOriginalOffsetToWorld: a world-space delta back to a source-runtime
	// (X, Y-up, Z) 16.16 offset.
	void ConvertWorldOffsetToOriginal(const FVector& WorldOffset, int32& OutX1616, int32& OutY1616, int32& OutZ1616) const;
	// One original person-unit (1/64 tile) in world centimeters - the people mover's scale
	// (original positions are 16.16 with 64 units per tile).
	float GetPeopleWorldCmPerOriginalUnit() const;
	// World-space unit direction for a stored people facing octant (movement heading =
	// (facing + 2) & 7 into the FUN_004c3010 compass table).
	FVector GetPeopleFacingWorldDirection(int32 Facing) const;

protected:
	UPROPERTY(EditInstanceOnly, Category = "SimCopter|City")
	TObjectPtr<ASimCity2000CityActor> SourceCityActor;

	UPROPERTY(EditAnywhere, Category = "SimCopter|City")
	bool bUseActiveCityActor = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|City", meta = (FilePathFilter = "sc2"))
	FFilePath CityFile;

	UPROPERTY(EditAnywhere, Category = "SimCopter|City")
	FDirectoryPath OriginalGameRoot;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Population")
	TSubclassOf<ASimCopterGroundAgent> GroundAgentClass;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Population")
	bool bSpawnOnBeginPlay = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Population", meta = (ClampMin = "0"))
	int32 MaxVehicleAgents = 160;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Population", meta = (ClampMin = "0"))
	int32 MaxPedestrianAgents = 280;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Population|Original", meta = (ClampMin = "0"))
	int32 OriginalAmbientRandomCap = 55;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Population|Original", meta = (ClampMin = "0"))
	int32 OriginalAmbientPeriodCap = 76;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Population|Original", meta = (ClampMin = "1", ClampMax = "128"))
	int32 OriginalAmbientScanRadiusTiles = 8;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Population|Original", meta = (ClampMin = "1", ClampMax = "128"))
	int32 OriginalAmbientDespawnRadiusTiles = 12;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Population", meta = (ClampMin = "1000.0"))
	float SpawnRadiusCm = 26000.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Population", meta = (ClampMin = "1000.0"))
	float DespawnRadiusCm = 34000.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Population", meta = (ClampMin = "0.0"))
	float MinSpawnDistanceCm = 1200.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Population", meta = (ClampMin = "0.01"))
	float SpawnThinkIntervalSeconds = 0.1f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Population", meta = (ClampMin = "1"))
	int32 MaxSpawnAttemptsPerThink = 8;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Population")
	int32 RandomSeed = 1996;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Population")
	TArray<FString> VehicleMeshNames;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Population")
	TArray<FString> PedestrianMeshNames;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Population")
	bool bRequireOriginalPopulationMeshes = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Movement", meta = (ClampMin = "1.0"))
	float VehicleSpeedCmPerSec = 720.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Movement", meta = (ClampMin = "1.0"))
	float PedestrianSpeedCmPerSec = 230.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Movement", meta = (ClampMin = "0.0", ClampMax = "0.45"))
	float VehicleLaneOffsetTileFraction = 0.20f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Movement", meta = (ClampMin = "0.0", ClampMax = "0.35"))
	float VehicleCornerClipTileFraction = 0.12f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Movement", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float VehicleRightTurnEarlyClipTileFraction = 0.12f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Movement", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float VehicleRightTurnCornerClipTileFraction = 0.14f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic")
	ESimCopterTrafficAiMode TrafficAiMode = ESimCopterTrafficAiMode::Original;

	// Original mode: a car stops when the player (helicopter or on foot) blocks the lane ahead,
	// like the original's "You Blocked Traffic!" behavior.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic", meta = (ClampMin = "0.0"))
	float PlayerRoadBlockLookAheadCm = 700.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic", meta = (ClampMin = "0.0"))
	float PlayerRoadBlockLaneWidthCm = 420.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic")
	ESimCopterTrafficFlowMode TrafficFlowMode = ESimCopterTrafficFlowMode::Normal;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "1.0"))
	float NormalVehicleFollowLookAheadCm = 560.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "1.0"))
	float NormalVehicleStopDistanceCm = 82.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "1.0"))
	float NormalVehicleMinimumFollowDistanceCm = 128.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "1.0"))
	float NormalVehicleSlowDistanceCm = 305.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "0.0"))
	float NormalTrafficBrakeRate = 5.5f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "0.5"))
	float TrafficLightPhaseSeconds = 10.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal")
	bool bStaggerTrafficLightPhases = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "1.0"))
	float TrafficLightStopDistanceCm = 205.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "1.0"))
	float TrafficLightSlowDistanceCm = 430.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "0.0"))
	float TrafficLightLineGraceDurationSeconds = 2.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "1.0"))
	float TrafficLightQueueStopDistanceCm = 230.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "1.0"))
	float TrafficLightQueueSlotSpacingCm = 255.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "1.0"))
	float TrafficLightQueueSlowDistanceCm = 780.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "0.0"))
	float TrafficLightQueueBrakeRate = 12.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "1.0"))
	float TrafficLightQueueLaneWidthCm = 520.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "0.0"))
	float TrafficLightIntersectionCommitDurationSeconds = 4.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "0.0"))
	float VehicleBlockedSpeedThresholdCmPerSec = 45.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "0.0"))
	float VehicleBlockedSecondsBeforeRecovery = 2.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "0.0"))
	float VehicleCollisionMemorySeconds = 5.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "0.0"))
	float VehicleRecoveryCooldownSeconds = 6.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "0.0"))
	float VehicleRecoveryReverseImpulseCmPerSec = 220.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "0.0"))
	float VehicleRecoveryBackUpDistanceCm = 120.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "0.0"))
	float VehicleRecoveryBypassOffsetCm = 185.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "0.0"))
	float VehicleRecoveryBypassDurationSeconds = 3.2f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "0.1"))
	float VehicleRecoveryBypassSpeedMultiplier = 1.1f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "0.0"))
	float VehicleRecoveryBlockerLookAheadCm = 560.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "0.0"))
	float VehicleRecoveryRejoinDurationSeconds = 2.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "1.0"))
	float VehicleRecoveryRejoinStopDistanceCm = 220.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "1.0"))
	float VehicleRecoveryRejoinSlowDistanceCm = 620.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "1.0"))
	float VehicleRecoveryRejoinLaneWidthCm = 520.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "1.0"))
	float VehicleRoadContainmentDistanceCm = 300.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "1.0"))
	float VehicleLaneGuidanceLookAheadCm = 260.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "0.05"))
	float VehicleLaneGuidanceDurationSeconds = 0.35f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic Avoidance", meta = (ClampMin = "0.0"))
	float VehicleOverlapPaddingCm = 18.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic Avoidance", meta = (ClampMin = "0.0"))
	float VehicleBumpImpulseCmPerSec = 180.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic Avoidance", meta = (ClampMin = "1.0"))
	float VehicleFollowLookAheadCm = 520.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic Avoidance", meta = (ClampMin = "1.0"))
	float VehicleStopDistanceCm = 145.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic Avoidance", meta = (ClampMin = "1.0"))
	float VehicleSlowDistanceCm = 430.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic Avoidance", meta = (ClampMin = "1.0"))
	float VehicleIntersectionSlowDistanceCm = 620.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic Avoidance", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float VehicleIntersectionCruiseSpeedScale = 0.72f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic Avoidance", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float VehicleIntersectionTurnSpeedScale = 0.44f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic Avoidance", meta = (ClampMin = "0.0"))
	float VehicleIntersectionBrakeRate = 4.5f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic Avoidance", meta = (ClampMin = "0.0"))
	float PedestrianCarLookAheadCm = 700.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic Avoidance", meta = (ClampMin = "0.0"))
	float PedestrianRoadEscapeDistanceCm = 115.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic Avoidance", meta = (ClampMin = "0.0"))
	float PedestrianAvoidanceDurationSeconds = 1.6f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic Avoidance", meta = (ClampMin = "0.1"))
	float PedestrianAvoidanceSpeedMultiplier = 1.25f;

	// --- Whole-map population (remake divergence: people/traffic visible across the map) ---

	// Simulate the entire city as lightweight records; near the camera the normal agent pool
	// still provides full-detail figures/cars, far records render as instanced boxes.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Whole Map")
	bool bSimulateWholeMap = true;

	// Sector edge in tiles; budgets are computed per sector from its spawnable tile content
	// (the decoded ambient tile classes), so dense districts get crowds and empty land none.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Whole Map", meta = (ClampMin = "4", ClampMax = "64"))
	int32 WholeMapSectorTiles = 16;

	// Pedestrians per fully built-up sector (the original ambient cap is 55 for one camera
	// area of comparable size, from figure.twk [Figure Parms] Max random ambient).
	UPROPERTY(EditAnywhere, Category = "SimCopter|Whole Map", meta = (ClampMin = "0"))
	int32 WholeMapPedestriansPerFullSector = 55;

	// Vehicles per road tile within a sector.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Whole Map", meta = (ClampMin = "0.0"))
	float WholeMapVehiclesPerRoadTile = 0.12f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Whole Map", meta = (ClampMin = "0"))
	int32 WholeMapMaxPedestrians = 4000;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Whole Map", meta = (ClampMin = "0"))
	int32 WholeMapMaxVehicles = 1500;

	// Far-record simulation cadence. Movement is advanced and instances updated at this rate.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Whole Map", meta = (ClampMin = "0.05"))
	float WholeMapSimTickIntervalSeconds = 0.2f;

	// Far instances inside this radius of the camera are hidden; the full-detail agent pool
	// covers that zone so people/cars are not doubled up.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Whole Map", meta = (ClampMin = "0.0"))
	float WholeMapHideRadiusCm = 24000.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Whole Map")
	TObjectPtr<UInstancedStaticMeshComponent> FarPedestrianInstances;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Whole Map")
	TObjectPtr<UInstancedStaticMeshComponent> FarVehicleInstances;

	UPROPERTY(VisibleInstanceOnly, Category = "SimCopter|Debug")
	int32 WholeMapPedestrianRecordCount = 0;

	UPROPERTY(VisibleInstanceOnly, Category = "SimCopter|Debug")
	int32 WholeMapVehicleRecordCount = 0;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render", meta = (ClampMin = "10.0"))
	float TileSize = 400.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render")
	bool bUseOriginalTerrainHeightScale = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render", meta = (ClampMin = "1.0"))
	float TerrainHeightScale = 200.0f;

	UPROPERTY(VisibleInstanceOnly, Category = "SimCopter|Debug")
	FString LastLoadError;

	UPROPERTY(VisibleInstanceOnly, Category = "SimCopter|Debug")
	FString LastCitySource;

	UPROPERTY(VisibleInstanceOnly, Category = "SimCopter|Debug")
	int32 RoadNodeCount = 0;

	UPROPERTY(VisibleInstanceOnly, Category = "SimCopter|Debug")
	int32 PedestrianNodeCount = 0;

	UPROPERTY(VisibleInstanceOnly, Category = "SimCopter|Debug")
	int32 ActiveVehicleCount = 0;

	UPROPERTY(VisibleInstanceOnly, Category = "SimCopter|Debug")
	int32 ActivePedestrianCount = 0;

private:
	TArray<FSimCopterGroundRouteNode> RoadNodes;
	TArray<FSimCopterGroundRouteNode> PedestrianNodes;
	TMap<FIntPoint, int32> RoadNodeIndexByTile;
	TMap<FIntPoint, int32> PedestrianNodeIndexByTile;
	TArray<uint8> XbldTileIds;
	TArray<uint8> PeopleTileClasses;
	TArray<uint8> PeopleTerrainTypes;
	TArray<uint8> WaterTileFlags;
	TArray<float> TileCenterWorldZ;
	TArray<FSimCopterWholeMapRecord> WholeMapPedestrianRecords;
	TArray<FSimCopterWholeMapRecord> WholeMapVehicleRecords;
	float WholeMapSimAccumulatorSeconds = 0.0f;
	TArray<TWeakObjectPtr<ASimCopterGroundAgent>> VehicleAgents;
	TArray<TWeakObjectPtr<ASimCopterGroundAgent>> PedestrianAgents;
	TMap<TObjectKey<ASimCopterGroundAgent>, FSimCopterVehicleTrafficState> VehicleTrafficStates;
	FRandomStream RandomStream;
	uint16 PeopleRandomState = 1;
	FTransform ActiveCityToWorldTransform = FTransform::Identity;
	FString ActiveOriginalGameRootPath;
	float ActiveTileSize = 400.0f;
	float SpawnThinkAccumulatorSeconds = 0.0f;
	int32 LastAmbientScanTileX = INDEX_NONE;
	int32 LastAmbientScanTileY = INDEX_NONE;
	bool bLoggedMissingPedestrianMeshes = false;

public:
	// Mission system hooks
	bool TryStartTrafficJam(int32 EventId, int32& OutTileX, int32& OutTileY);
	void EndTrafficJam(int32 EventId);
	bool TryStartCarFire(int32 EventId, int32& OutTileX, int32& OutTileY);

	// Report every car currently on fire (for the mission actor's fire renderer).
	void GetBurningVehicles(TArray<FSimCopterBurningVehicle>& Out) const;
	// Extinguish burning cars within RadiusCm of WorldLocation (helicopter bucket dump). Appends
	// the event ids of the cars that were put out so the caller can post the douse/scoring events.
	void DouseBurningVehiclesNear(const FVector& WorldLocation, float RadiusCm, TArray<int32>& OutExtinguishedEventIds);
	bool TrySpawnMissionPerson(int32 SpawnMode, int32 PersonState, int32 TileX, int32 TileY, int32 EventId);
	int32 PickUpMissionPeopleNear(
		int32 EventId,
		const FVector& WorldLocation,
		int32 MaxCount,
		float RadiusCm,
		float MaxVerticalDeltaCm,
		int32* OutNewPickupCreditCount = nullptr);
	int32 GuideMissionPeopleToLocation(
		int32 EventId,
		const FVector& SearchLocation,
		const FVector& TargetLocation,
		int32 MaxCount,
		float SearchRadiusCm,
		float MaxVerticalDeltaCm,
		float GuidanceSeconds);
	int32 BoardMissionPeopleTouching(
		int32 EventId,
		const FVector& WorldLocation,
		int32 MaxCount,
		float TouchRadiusCm,
		float MaxVerticalDeltaCm,
		int32* OutNewPickupCreditCount = nullptr);
	ASimCopterGroundAgent* FindMissionPersonNear(int32 EventId, const FVector& WorldLocation, float RadiusCm, float MaxVerticalDeltaCm);
	int32 SpawnMissionPeopleAtWorldLocation(
		int32 Count,
		const FVector& WorldLocation,
		int32 EventId,
		int32 SpawnMode,
		int32 PersonState,
		float SpreadRadiusCm);
	ASimCopterGroundAgent* SpawnFallingMissionPassengerAtWorldLocation(
		const FVector& WorldLocation,
		int32 EventId,
		int32 SpawnMode,
		int32 PersonState,
		float FallInjuryDistanceCm);
	int32 ReleaseMissionPeopleNear(int32 EventId, const FVector& WorldLocation, int32 MaxCount, float RadiusCm, float MaxVerticalDeltaCm);

	// Spawns a script-driven mission agent (e.g. the hospital EMT or a patient it carries) with its
	// feet on FeetWorldLocation and an optional privanim figure. Not added to the ambient pool -
	// the caller owns and drives it. Returns nullptr when it could not be created.
	ASimCopterGroundAgent* SpawnScriptedMissionAgent(
		const FVector& FeetWorldLocation,
		int32 EventId,
		const FString& FigureName,
		bool bInjuredPose,
		float MovementSpeedScale = 1.0f);

	ASimCity2000CityActor* ResolveSourceCityActor() const;
	FString ResolveCityPath() const;
	FString ResolveOriginalGameRoot() const;
	FVector GetPopulationFocusLocation() const;
	void UpdateAgentPool(float DeltaSeconds);
	void PruneAgentArray(TArray<TWeakObjectPtr<ASimCopterGroundAgent>>& Agents, const FVector& FocusLocation);
	void UpdateTrafficInteractions(float DeltaSeconds);
	void ApplyPlayerRoadBlocking();
	void SyncVehicleTrafficStates(float DeltaSeconds);
	void ApplyTrafficLights(float DeltaSeconds);
	void ApplyVehicleFollowing(float LookAheadCm, float StopDistanceCm, float SlowDistanceCm, bool bUseNormalBraking, float DeltaSeconds);
	void ApplyIntersectionApproachSlowdown(float DeltaSeconds);
	void ResolveVehicleOverlaps();
	void UpdateVehicleBlockageRecovery();
	void ApplyVehicleLaneGuidance(float DeltaSeconds);
	void UpdatePedestrianAvoidance();
	bool IsVehicleSpawnLocationClear(const FVector& SpawnLocation) const;
	bool IsPedestrianSpawnLocationOpen(const FVector& SpawnLocation) const;
	// A mission victim may stand here only if it is not buried inside a building mesh, unless the
	// tile is a road (a car-accident victim can legitimately lie on the road surface).
	bool IsMissionGroundSpawnValid(const FVector& SpawnLocation) const;
	bool TryFindPedestrianEscapeTarget(const FVector& PedestrianLocation, const FVector& EscapeDirection, FVector& OutTarget) const;
	bool TryGetPedestrianAwayFromRoadCenterDirection(const ASimCopterGroundAgent& Pedestrian, FVector& OutAwayDirection) const;
	bool IsTrafficLightIntersectionNode(int32 NodeIndex) const;
	bool IsTrafficLightGreenForApproach(int32 IntersectionNodeIndex, int32 PreviousNodeIndex) const;
	void MarkVehicleInTrafficLightLine(ASimCopterGroundAgent& Vehicle);
	void MarkVehicleCommittedToIntersection(ASimCopterGroundAgent& Vehicle);
	void MarkVehicleCollision(ASimCopterGroundAgent& Vehicle);
	bool TryStartVehicleRecovery(ASimCopterGroundAgent& Vehicle, FSimCopterVehicleTrafficState& State);
	ASimCopterGroundAgent* FindClosestBlockingVehicle(const ASimCopterGroundAgent& Vehicle, const FVector& ForwardDirection) const;
	FVector ChooseVehicleBypassDirection(const ASimCopterGroundAgent& Vehicle, const ASimCopterGroundAgent* BlockingVehicle, const FVector& ForwardDirection) const;
	bool TryMakeVehicleLaneGuidanceTarget(const ASimCopterGroundAgent& Vehicle, FVector& OutTarget, float& OutDistanceFromLane, bool& bOutTraversingDiagonalRoad) const;
	bool IsVehicleTraversingDiagonalRoadTile(const ASimCopterGroundAgent& Vehicle) const;
	bool DoesVehicleRouteTouchDiagonalRoadTile(int32 TargetIndex, int32 PreviousIndex, int32 NextIndex) const;
	FVector ClampVehicleLocationToRoadNetwork(const FVector& Location) const;
	FVector MakeVehicleRoadSafePathOffset(const FVector& BaseLocation, const FVector& DesiredOffset) const;
	void AssignNextTarget(ASimCopterGroundAgent& Agent, const TArray<FSimCopterGroundRouteNode>& Nodes);
	void BuildWholeMapPopulation();
	void UpdateWholeMapPopulation(float DeltaSeconds);
	bool TrySpawnAgent(bool bVehicle, const FVector& FocusLocation);
	int32 CountAmbientPedestrians() const;
	bool TryRunOriginalAmbientPedestrianScan(const FVector& FocusLocation, int32 MaxSpawnAttempts);
	bool TryRunAmbientTileSpawn(int32 TileX, int32 TileY, int32 SpawnAttemptCount, int32& AttemptsRemaining);
	bool TryGenericAmbientSpawnAtTile(int32 TileX, int32 TileY);
	int32 TrySpawnSpecialBuildingPeople(int32 TileX, int32 TileY, int32& AttemptsRemaining);
	bool TrySpawnOriginalPersonAtTile(
		int32 TileX,
		int32 TileY,
		int32 BehaviorClass,
		int32 InitialState,
		int32 InitialProgramId,
		const FVector2D* ExplicitOriginalOffset,
		int32 ClothesOffset);
	bool TryResolvePedestrianNodeForTile(int32 TileX, int32 TileY, int32& OutNodeIndex) const;
	bool IsOriginalAmbientTileGateOpen(int32 TileX, int32 TileY) const;
	bool HasAmbientPedestrianNearTile(int32 TileX, int32 TileY, float RadiusTiles) const;
	int32 ChooseNodeNearFocus(const TArray<FSimCopterGroundRouteNode>& Nodes, const FVector& FocusLocation);
	FVector MakeVehicleRouteTargetLocation(const TArray<FSimCopterGroundRouteNode>& Nodes, int32 TargetIndex, int32 PreviousIndex, int32 ApproachIndex, int32 LookAheadIndex) const;
	FVector MakeRoutePointLocation(const TArray<FSimCopterGroundRouteNode>& Nodes, int32 PointIndex, int32 PreviousIndex, int32 NextIndex, bool bVehicle) const;
};
