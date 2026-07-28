// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "City/SimCopterAirport.h"
#include "GameFramework/Actor.h"
#include "Ground/SimCopterDispatch.h"
#include "UObject/ObjectKey.h"
#include "UObject/NoExportTypes.h"
#include "SimCopterTrafficSystemActor.generated.h"

class ASimCity2000CityActor;
class ASimCopterGroundAgent;
class ASimCopterMissionSystemActor;
class UInstancedStaticMeshComponent;
class USimCopterDispatchMarkerComponent;

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

// Runtime state of one emergency-vehicle pool slot. The names mirror the original's
// veh + 0x299 state values (Docs/scratchpad/ghidra/emergency_dispatch_decode_20260725.md
// section 6); FUN_004b9e40 switches on exactly these.
enum class ESimCopterDispatchVehicleState : uint8
{
	// Slot holds no vehicle: the original's "veh[4] & 2 clear", the slot a station spawn
	// takes. Never a dispatch candidate.
	Empty,
	// State 4: driving to a fixed destination tile (F2/F3/F4).
	Responding,
	// State 3: destination re-read from the spotlight every frame (F5 chase dispatch).
	Chasing,
	// State 1 with the on-scene flag 0x04: parked at the scene, doing the job.
	OnScene,
	// State 1 with the recall flag 0x10: driving back to the station road tile.
	Returning,
	// State 2: parked at the station. The only state that makes a spawned vehicle a
	// redispatch candidate (FUN_004bc250).
	Idle
};

// One emergency vehicle. Field comments name the original offsets they stand in for.
struct FSimCopterDispatchVehicle
{
	TWeakObjectPtr<ASimCopterGroundAgent> Agent;
	ESimCopterDispatchVehicleState State = ESimCopterDispatchVehicleState::Empty;
	// +0x12d destination tile / +0x12b home (station road) tile / +0x29d station index.
	FIntPoint DestinationTile = FIntPoint(INDEX_NONE, INDEX_NONE);
	FIntPoint HomeTile = FIntPoint(INDEX_NONE, INDEX_NONE);
	int32 StationIndex = INDEX_NONE;
	// +0x2ad: the give-up / stay timer, and +0x2a5: the gap between on-scene attempts.
	float StayTimerSeconds = 0.0f;
	float ActionTimerSeconds = 0.0f;
	// Gap to the next water-jet droplet. The original sprayed one per game frame; spraying per
	// rendered frame here would swamp the shared 70-slot trajectory pool (which the player's
	// bucket and cannon also draw from) within a second.
	float JetTimerSeconds = 0.0f;
	// Planned road-node route (the original walked FUN_004bef30's back-links instead).
	TArray<int32> RouteNodes;
	int32 RouteCursor = 0;
	// Mission event the vehicle is working on, when it found one.
	int32 TargetEventId = INDEX_NONE;
	// Tile it is working on; INDEX_NONE when it has nothing in range.
	FIntPoint TargetTile = FIntPoint(INDEX_NONE, INDEX_NONE);
	// Where a fire truck's monitor is pointed (FUN_004b9b10's +0x2c0 aim). This is the flame's
	// own position, not its tile centre - see FServiceFireTarget.
	FVector TargetWorld = FVector::ZeroVector;
	bool bHasJetTarget = false;
	// True once the vehicle has acted at the scene at least once (original flag 0x08).
	bool bActedAtScene = false;
	// The waypoint marker hanging over DestinationTile, and the original's "marker is linked"
	// flag +0x2b1 & 0x20. Created on the first dispatch and reused for the slot's lifetime, the
	// way the original keeps one render node per vehicle.
	TWeakObjectPtr<USimCopterDispatchMarkerComponent> Marker;
	FIntPoint MarkerTile = FIntPoint(INDEX_NONE, INDEX_NONE);
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

	// FUN_004ca350: the nearest *other* visible person running the behaviour VM whose loop flag
	// and/or state match (-2 = "any"), by Manhattan distance. A loop-flag-0 search also skips
	// anyone already flagged EBhavAttr::CriminalCaught - that is what makes an arrested criminal
	// stop being a target for every other cop on the map. Backs behaviour opcode 15's object
	// classes 5/6/8/14.
	ASimCopterGroundAgent* FindNearestBehaviorPerson(
		const ASimCopterGroundAgent& From,
		int32 LoopFlagFilter,
		int32 StateFilter) const;
	bool TryGetPeopleFacingStepTarget(
		const FVector& FromWorldLocation,
		int32 Facing,
		float StepDistanceCm,
		FVector& OutWorldLocation,
		int32& OutTileClass) const;
	int32 GetXbldTileId(int32 FileX, int32 FileY) const;
	// Raw XZON byte; its low nibble is the zone type (8 = airport, which is what the airport
	// search in SimCopterAirport looks for).
	int32 GetZoneTileId(int32 FileX, int32 FileY) const;
	int32 GetBuildingFootprintSize(int32 FileX, int32 FileY) const;

	// The airport block this city starts at (FUN_0047c0c0 / FUN_004829f0), cached with the rest
	// of the grid. (128, 128) means the city had no airport zone and the original would have
	// built one just past the map's far corner.
	FIntPoint GetAirportOriginTile() const { return AirportOriginTile; }
	// World location of one of the airport's twelve helipads (FUN_004829f0's pad table order).
	// The height comes from the terminal tile, because FUN_004829f0 flattens the whole block to
	// that one height-map sample before it places anything.
	bool TryGetAirportPadWorldLocation(int32 PadIndex, FVector& OutWorldLocation) const;
	// FUN_004a5fd0 zeroes the XBLD entry of every tile a burned-down building covered, which is
	// what stops the sim treating the cleared ground as a building (and stops fire re-igniting it).
	void ClearXbldTiles(const TArray<FIntPoint>& Tiles);
	// The city actor this traffic system is bound to; owns the building instances.
	ASimCity2000CityActor* GetCityActor() const;
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

	// The mean road speed, in cm/s. FUN_0049dbb0 authors every vehicle's own speed when it is
	// placed - veh[0xc3] = (rand() & 7) + 0x24 and veh[0xc7] = (rand() & 7) + 0x28, i.e. 36..43
	// and 40..47 *original units per second* - and FUN_0049be50 advances the car by
	// speed * frameDelta, so those are per-second figures. At the default 400 cm tile (64 units
	// per tile, 6.25 cm per unit) the range is 225..294 cm/s and the mean is 259.
	//
	// This used to be 720, which is 115 units/s - nearly three times the original. It mattered
	// once speeders arrived: FUN_0049d980's 1.75x fleeing multiplier took them to 201 units/s,
	// past the helicopter's own 192 units/s ceiling (MaxPitch, which the flight model uses
	// directly as airspeed), so nothing could ever catch one.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Movement", meta = (ClampMin = "1.0"))
	float VehicleSpeedCmPerSec = 259.0f;

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
	TArray<uint8> ZoneTileIds;
	// Resolved once per city build; (128, 128) when the city has no airport zone.
	FIntPoint AirportOriginTile = FIntPoint(SimCopterAirport::FallbackOriginTile, SimCopterAirport::FallbackOriginTile);
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

	// --- emergency dispatch (F2-F5) ---
	// Station registries and the five-slot vehicle pool per service, indexed by
	// SimCopterDispatch::EService. Rebuilt with the road graph.
	TArray<SimCopterDispatch::FStation> DispatchStations[static_cast<int32>(SimCopterDispatch::EService::Count)];
	TArray<FSimCopterDispatchVehicle> DispatchVehicles[static_cast<int32>(SimCopterDispatch::EService::Count)];
	// The tile chase-dispatched police re-read every frame (the original read the
	// spotlight node directly; the pawn pushes it here instead).
	FIntPoint SpotlightChaseTile = FIntPoint(INDEX_NONE, INDEX_NONE);

public:
	// Runs one FUN_004bc680 dispatch transaction for a service and commits the result.
	// bChaseSpotlight selects initial state 3 (F5) instead of 4 (F2/F3/F4).
	SimCopterDispatch::EDispatchResult RequestEmergencyDispatch(
		SimCopterDispatch::EService Service,
		const FIntPoint& TargetTile,
		bool bChaseSpotlight);

	// FUN_0049b3f0: release the first vehicle of this service within two rings of the
	// spotlight tile. A vehicle of a different service on the way aborts the scan, as in
	// the original.
	bool ClearEmergencyDispatch(SimCopterDispatch::EService Service, const FIntPoint& SpotlightTile);

	// The pawn feeds the spotlight's ground tile here so chase-dispatched police can
	// follow it.
	void SetSpotlightChaseTile(const FIntPoint& Tile) { SpotlightChaseTile = Tile; }

	int32 GetDispatchStationCount(SimCopterDispatch::EService Service) const;
	int32 GetActiveDispatchCount(SimCopterDispatch::EService Service) const;
	// One-line status for the debug panel: stations, units out, and what they are doing.
	FString GetDispatchStatusLine(SimCopterDispatch::EService Service) const;

	// The pawn publishes the spotlight's ground point and range band here every frame so speeder
	// cars can accumulate their mark (FUN_004a01f0, interaction mode 1). bActive is false
	// whenever the light is off, which resets every mark - the original's DAT_00503aa0 == 3.
	void SetSpotlightMarkSource(const FVector& GroundWorldLocation, int32 Band, bool bActive);

	// Mission system hooks
	bool TryStartTrafficJam(int32 EventId, int32& OutTileX, int32& OutTileY);
	void EndTrafficJam(int32 EventId);
	bool TryStartCarFire(int32 EventId, int32& OutTileX, int32& OutTileY);

	// FUN_004b8540: put the mission's speeder on a road tile near (TileX, TileY). Capped at the
	// original's pool of five.
	bool TryActivateSpeederCar(int32 EventId, int32 TileX, int32 TileY);

	// Live position of a mission's speeder, for the world tag that follows it. OutSpotlightMark
	// is the 0..10 illumination counter and OutStopped is true once it has pulled over, so the
	// tag can tell the player which of the three things they still have to do.
	bool TryGetSpeederCarState(
		int32 EventId,
		FVector& OutWorldLocation,
		int32& OutSpotlightMark,
		bool& OutStopped) const;

	// Speeders that have just been pulled over, for the few seconds their tag stays up after the
	// mission record has already been retired and paid.
	void GetRecentlyStoppedSpeederLocations(TArray<FVector>& OutWorldLocations) const;

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
	// Mission people who are not standing on the ground and must not be snapped to it: the
	// survivors floating beside the capsized boat (FUN_004b1950, spawn mode 1) and the passengers
	// stranded on the roof of a moving train (FUN_004b7fd0, spawn mode 0x13). bFloatOnWaterSurface
	// seats them on the water; otherwise they are left exactly where they are put and the caller
	// drives them. OutSpawned, when supplied, receives the agents so the caller can keep carrying
	// them.
	int32 SpawnMissionSwimmersAtWorldLocation(
		int32 Count,
		const FVector& WorldLocation,
		int32 EventId,
		int32 SpawnMode,
		float SpreadRadiusCm,
		bool bFloatOnWaterSurface = true,
		TArray<ASimCopterGroundAgent*>* OutSpawned = nullptr);
	// FUN_004c3f00: the mission's people go down with the boat / train. Returns how many went.
	int32 RemoveMissionPeople(int32 EventId);
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

	// --- emergency dispatch internals ---
	// FUN_004bcc80: rescan the three station registries from the XBLD grid.
	void RebuildDispatchStations();
	// Per-frame state machines (FUN_004b9e40 and its fire/ambulance siblings).
	void UpdateDispatchVehicles(float DeltaSeconds);
	void UpdateOneDispatchVehicle(SimCopterDispatch::EService Service, int32 SlotIndex, float DeltaSeconds);

	// FUN_004be890 / FUN_004be820 / FUN_004be750, folded into one step: hang the service's
	// waypoint marker over the vehicle's destination tile while it is responding or chasing,
	// re-anchor it when the destination moves, spin it, and drop it in every other state.
	void UpdateDispatchMarker(SimCopterDispatch::EService Service, FSimCopterDispatchVehicle& Vehicle);

	// The marker art is missing or unreadable; say so once rather than every tick per vehicle.
	bool bLoggedDispatchMarkerError = false;

	// --- Speeder cars and police pursuit -----------------------------------------------------
	// FUN_004a01f0's inputs, republished by the pawn each frame.
	FVector SpotlightMarkWorldLocation = FVector::ZeroVector;
	int32 SpotlightMarkBand = INDEX_NONE;
	bool bSpotlightMarkActive = false;

	// The live speeders. The original's pool is fixed at five (FUN_00479bb0).
	TArray<TWeakObjectPtr<ASimCopterGroundAgent>> CriminalCars;

	// FUN_004a01f0 + FUN_0049d980: accumulate each speeder's mark from the spotlight and set the
	// speed multiplier it earns.
	void UpdateCriminalCars(float DeltaSeconds);

	// FUN_004b9e40 case 0's three-ring sweep. Returns the nearest speeder within
	// SimCopterCriminalCar::PursuitMaxTileSteps of FromTile, or null.
	ASimCopterGroundAgent* FindPursuitTarget(const FIntPoint& FromTile) const;

	// FUN_0049df60's occupancy half: another stopped emergency vehicle already holds the tile.
	bool CanVehicleStopOnTile(const FIntPoint& Tile) const;

	// One vehicle's road speed. The original gives each car its own value rather than a shared
	// one, so this reproduces that spread around VehicleSpeedCmPerSec's mean.
	float DrawVehicleSpeedCmPerSec();

	// FUN_004b8b60: siren, officer out, close the mission record, hold, remove.
	void RunCriminalCarArrest(ASimCopterGroundAgent& Car, float DeltaSeconds);
	// Breadth-first road-node route; stands in for FUN_004bef30's Dijkstra + back-links.
	bool TryPlanRoadRoute(const FIntPoint& FromTile, const FIntPoint& ToTile, TArray<int32>& OutNodes) const;
	bool TryRetargetDispatchVehicle(FSimCopterDispatchVehicle& Vehicle, const FIntPoint& DestinationTile);
	ASimCopterGroundAgent* SpawnDispatchVehicleAgent(SimCopterDispatch::EService Service, const FIntPoint& RoadTile);
	void AdvanceDispatchRoute(FSimCopterDispatchVehicle& Vehicle);
	bool HasDispatchVehicleArrived(const FSimCopterDispatchVehicle& Vehicle) const;
	// The on-scene action: FUN_004bd980's service call. Returns true when the vehicle
	// found something to do this attempt.
	bool RunDispatchOnSceneAction(SimCopterDispatch::EService Service, FSimCopterDispatchVehicle& Vehicle);
	// FUN_004bdc70: recall a vehicle to its station.
	void RecallDispatchVehicle(FSimCopterDispatchVehicle& Vehicle);
	// FUN_004bc660 + FUN_0049d5a0 + FUN_004a4340: free the station slot and despawn.
	void ReleaseDispatchVehicle(SimCopterDispatch::EService Service, int32 SlotIndex);
	bool TryGetDispatchVehicleTile(const FSimCopterDispatchVehicle& Vehicle, FIntPoint& OutTile) const;
	ASimCopterMissionSystemActor* ResolveMissionSystem() const;
};
