// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Ground/SimCopterTrafficSystemActor.h"

#include "SimCopterAmbientVehicles.generated.h"

class ASimCity2000CityActor;
class ASimCopterGroundAgent;
class ASimCopterMissionSystemActor;
class UMaterialInterface;
class UProceduralMeshComponent;
class USimCopterParticleFXComponent;
class FMaxisMeshLibrary;

// SimCopter's three ambient vehicle pools - planes, boats and the train - and the four mission
// types that hang off them: plane crash (0x4), train crash (0x100), boat rescue (0x90) and train
// rescue (0x110).
//
// Decoded from SimCopter.exe; full notes in
// Docs/scratchpad/ghidra/planes_trains_boats_decode_20260727.md. The pools:
//
//   DAT_00582910  2 planes,  'PLAN' record 0xbc,  tick FUN_004b3b80 -> FUN_004b2330
//   DAT_00582840  3 boats,   'BOAT' record 0xe3,  tick FUN_004b1800
//   DAT_00582afc  1 train,   'TRAN' record 0x1d9, tick FUN_004b7f40 -> FUN_004b4440
//
// The GEO model ids below are confirmed against SIM3D2.MAX's object table, which is how we know
// plane slot 1 is not a second aircraft but the UFO, and boat slot 0 is CAPBOAT1 - the capsized
// boat the rescue mission places.
namespace SimCopterAmbientVehicles
{
// GEO object ids (FMaxisMeshLibrary::FindObjectByObjectId, the remake's FUN_00470571).
constexpr int32 PlaneObjectId = 0x12e;         // PLANE1  - the only crashable plane
constexpr int32 UfoObjectId = 0x17c;           // UFO     - plane slot 1
constexpr int32 BoatObjectId = 0x12f;          // BOAT1   - the ambient boats
constexpr int32 CapsizedBoatObjectId = 0x163;  // CAPBOAT1 - the boat-rescue boat
constexpr int32 TrainLocoObjectId = 0x12d;     // TRAIN1
constexpr int32 TrainCarObjectIds[2] = { 0x14c, 0x14d }; // TRAIN2 / TRAIN3

// FUN_004b3a10 / FUN_004af6f0 / FUN_004b4250 pool sizes.
constexpr int32 PlaneSlots = 2;
constexpr int32 BoatSlots = 3;
constexpr int32 TrainCarCount = 2;

// One original world unit is 1/64 of a tile, the same scale the people mover uses.
constexpr float OriginalUnitsPerTile = 64.0f;

// The original applies several per-frame (not per-second) steps at its own simulation cadence.
// The remake normalises them through this rate, which is the same 15 Hz the ported people
// behaviour VM runs at (ASimCopterGroundAgent::BehaviorTickRate).
constexpr float OriginalSimHz = 15.0f;

// FUN_004b7890's rail-tile test - where the train may be *placed*: xbldId | ((xzon & 2) << 14) in
// 0x2c-0x2d, 0x32-0x3a, 0x45-0x48, 0x4d-0x4e, 0x5a-0x5b (+ the 0x8000 raised variants). ZoneId is
// the raw XZON byte.
SIMCOPTERREMAKE_API bool IsRailTile(int32 XbldId, int32 ZoneId);

// FUN_004b5290's tail - where the train may *travel*. This is a strictly wider band: it also takes
// 0x2e-0x31 (the four rail slope pieces) and 0x3b-0x3e (rail crossing under a road), which the
// placement test deliberately excludes because the train should not materialise on a grade or in a
// crossing. Using the placement set for routing is what makes a train stop dead at every slope.
SIMCOPTERREMAKE_API bool IsTraversableRailTile(int32 XbldId, int32 ZoneId);
}

// One ambient plane. Field comments name the original record offsets.
struct FSimCopterAmbientPlane
{
	int32 ObjectId = SimCopterAmbientVehicles::PlaneObjectId; // +0x54
	bool bVisible = false;          // +0x05 (linked into a scene cell)
	bool bCrashRequested = false;   // +0x06
	bool bCrashing = false;         // +0x07
	FVector Direction = FVector::ForwardVector; // +0x08/+0x0c/+0x10, unit
	float SegmentRemainingCm = 0.0f;            // +0x14
	float SpeedCmPerSec = 0.0f;                 // +0x18 (cruise +0x1c = 120 units/s)
	FIntPoint Tile = FIntPoint(INDEX_NONE, INDEX_NONE); // +0x20/+0x24
	float RespawnAccumSeconds = 0.0f;           // +0x34, armed at the +0x30 delay
	float VerticalSpeedCmPerSec = 0.0f;         // +0x38, normalised from the per-frame step
	int32 EventId = INDEX_NONE;                 // +0x3c
	float EffectTimerSeconds = 180.0f;          // +0x48
	int32 HitCount = 0;                         // +0x50 (UFO only)
	FVector World = FVector::ZeroVector;        // +0x70/+0x74/+0x78
	TObjectPtr<UProceduralMeshComponent> Mesh;
};

// One ambient boat.
struct FSimCopterAmbientBoat
{
	int32 ObjectId = SimCopterAmbientVehicles::BoatObjectId; // +0x7b
	bool bVisible = false;          // +0x05
	FVector Direction = FVector::ForwardVector; // +0x13/+0x17/+0x1b
	float DistanceToTargetCm = 0.0f;            // +0x1f
	float SpeedCmPerSec = 0.0f;                 // +0x2b (current)
	float BaseSpeedCmPerSec = 0.0f;             // +0x2f
	FIntPoint Tile = FIntPoint(INDEX_NONE, INDEX_NONE);       // +0x33/+0x37
	FIntPoint TargetTile = FIntPoint(INDEX_NONE, INDEX_NONE); // +0x43/+0x47
	FIntPoint PreviousTile = FIntPoint(INDEX_NONE, INDEX_NONE);
	float RespawnAccumSeconds = 0.0f;           // +0x4f (delay +0x4b = 10 s, ambient only)
	int32 EventId = INDEX_NONE;                 // +0x53
	float MissionTimerSeconds = 0.0f;           // +0x57
	float WakeTimerSeconds = 0.9f;              // +0x0b (0xe666)
	FVector World = FVector::ZeroVector;        // +0x97/+0x9b/+0x9f
	TObjectPtr<UProceduralMeshComponent> Mesh;
};

// The single train: a locomotive plus two cars that trail it down the rails.
struct FSimCopterAmbientTrain
{
	bool bVisible = false;          // +0x05
	bool bCrashRequested = false;   // +0x0a
	bool bDerailing = false;        // +0x0b
	bool bRescueActive = false;     // +0x0c
	FIntPoint Tile = FIntPoint(INDEX_NONE, INDEX_NONE);     // +0x35/+0x39 (loco)
	FIntPoint NextTile = FIntPoint(INDEX_NONE, INDEX_NONE); // +0x55/+0x59
	FIntPoint PreviousTile = FIntPoint(INDEX_NONE, INDEX_NONE);
	float SpeedCmPerSec = 0.0f;                 // +0x2d
	float BaseSpeedCmPerSec = 0.0f;             // +0x31
	float RespawnAccumSeconds = 0.0f;           // +0x61 (delay +0x5d = 30 s)
	int32 EventId = INDEX_NONE;                 // +0x69
	float MissionTimerSeconds = 0.0f;           // +0x71
	float DerailTimerSeconds = 0.0f;            // +0x6d (0x20000 = 2 s)
	float DerailSpinDegrees = 0.0f;             // driven by the +0x65 spin counter
	FVector World = FVector::ZeroVector;        // +0xb9.. (loco)
	FVector Direction = FVector::ForwardVector; // +0x15/+0x19/+0x1d (kept flat - the step is
	                                            // measured along the ground)
	// Grade of the leg the loco is on, nose-up positive. Remake-only: the original draws the
	// train flat on the tile height and lets it cut through slopes.
	float PitchDegrees = 0.0f;
	// Tile-centre waypoints the loco has passed, newest first. The original keeps the same idea
	// as three explicit car tiles at +0x3d/+0x45 plus the +0x4d/+0x55 history pair.
	TArray<FVector> PathHistory;
	TObjectPtr<UProceduralMeshComponent> LocoMesh;
	TObjectPtr<UProceduralMeshComponent> CarMeshes[SimCopterAmbientVehicles::TrainCarCount];
};

// What a crash leaves behind: the airframe where the plane went in, or the three derailed cars
// where the train stopped. A wreck is inert scenery that happens to be on fire - it holds the
// mission open until the player puts it out (or the clock beats them), then it is cleared away
// with the mission that owned it.
struct FSimCopterVehicleWreck
{
	TObjectPtr<UProceduralMeshComponent> Mesh;
	int32 ObjectId = INDEX_NONE;
	int32 EventId = INDEX_NONE;
	// Stable key for the fire renderer; distinct from the flame-slot and burning-car key spaces.
	int32 Key = 0;
	bool bBurning = false;
	// Counts down while it burns; running out is a failure to douse, not a completion.
	float BurnTimeoutSeconds = 0.0f;
	FVector World = FVector::ZeroVector;
	FVector Direction = FVector::ForwardVector;
	float ExtraYawDegrees = 0.0f;
};

UCLASS()
class SIMCOPTERREMAKE_API ASimCopterAmbientVehiclesActor : public AActor
{
	GENERATED_BODY()

public:
	ASimCopterAmbientVehiclesActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// --- ISimCopterMissionWorld hooks, forwarded by ASimCopterMissionSystemActor ---

	// FUN_004b3aa0: mark the idle PLANE1 as crashing. Fails when it is already going down.
	bool TryActivatePlaneCrash(int32 EventId);

	// FUN_004b7f60: mark the train as crashing.
	bool TryActivateTrainCrash(int32 EventId);

	// FUN_004b1950: place CAPBOAT1 in open water near the tile, spawn 3..5 survivors and report
	// the boat's tile so the mission marker moves onto the water.
	bool TryActivateBoatRescue(int32 EventId, float TimerSeconds, int32 TileX, int32 TileY, int32& OutTileX, int32& OutTileY);

	// FUN_004b7fd0: put the train on the rails (at a random map tile, as the original does),
	// spawn 1..3 trapped passengers and report the train's tile.
	bool TryActivateTrainRescue(int32 EventId, float TimerSeconds, int32& OutTileX, int32& OutTileY);

	// --- crash wreckage ---
	// Every wreck still alight, in the same shape the traffic system reports burning cars, so the
	// mission actor's fire renderer draws them without knowing where they came from.
	void GetBurningWrecks(TArray<FSimCopterBurningVehicle>& Out) const;
	// A bucket load landing near a wreck puts it out. Appends the owning event ids.
	void DouseBurningWrecksNear(const FVector& WorldLocation, float RadiusCm, TArray<int32>& OutExtinguishedEventIds);

	// One line for the mission debug panel.
	FString GetStatusLine() const;

	// Debug: where one of the pools currently is, so a console command can put the camera beside
	// it without the round-trip lag of reading a position out of the log. 0 = train, 1 = capsized
	// boat, 2 = a flying plane, 3 = the nearest burning wreck.
	bool TryGetDebugViewTarget(int32 Which, FVector& OutWorld) const;

	bool CaptureRuntimeSaveState(TArray<uint8>& OutData);
	bool RestoreRuntimeSaveState(const TArray<uint8>& Data);
	// Stable component owned by the fixed UFO pool. Restored abductees relink to it after traffic
	// actors are recreated; the ambient restore then moves that same component to its saved pose.
	USceneComponent* GetUfoBeamTargetComponent() const;

	// The UFO only flies when this is on; the original gated it on DAT_00504084.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Ambient Vehicles")
	bool bEnableUfo = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Ambient Vehicles")
	bool bEnablePlanes = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Ambient Vehicles")
	bool bEnableBoats = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Ambient Vehicles")
	bool bEnableTrain = true;

	// Stands in for the original's draw distance DAT_005a25a0: a vehicle respawns half this many
	// tiles from the camera and is dropped past (this / 2 + 4). The original's value came from the
	// video settings and was small (its world faded out within a dozen tiles); 24 keeps ambient
	// traffic within the remake's much longer draw distance without spreading the respawn probe so
	// thin that it can never find rail or water.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Ambient Vehicles", meta = (ClampMin = "8", ClampMax = "128"))
	int32 ViewRangeTiles = 24;

	// How long burning wreckage stays alight before it counts as burned out and the mission can
	// close without it. Long enough to fly back with a full bucket, short enough that a crash the
	// player ignores does not pin a mission slot open for the rest of the session.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Ambient Vehicles", meta = (ClampMin = "10.0"))
	float WreckBurnTimeoutSeconds = 180.0f;

private:
	// Crash debris and boat wakes go through the same typed effect pools the fire/water FX use.
	UPROPERTY(VisibleAnywhere, Category = "SimCopter|Ambient Vehicles")
	TObjectPtr<USimCopterParticleFXComponent> EffectComponent;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> VertexColorMaterial;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UProceduralMeshComponent>> OwnedMeshes;

	FSimCopterAmbientPlane Planes[SimCopterAmbientVehicles::PlaneSlots];
	FSimCopterAmbientBoat Boats[SimCopterAmbientVehicles::BoatSlots];
	FSimCopterAmbientTrain Train;

	// Crash leftovers, and the people riding the train's first car during a rescue.
	TArray<FSimCopterVehicleWreck> Wrecks;
	int32 NextWreckKey = 1;
	TArray<TWeakObjectPtr<ASimCopterGroundAgent>> TrainRoofRiders;

	// Local-space top of each model, taken from the built geometry so riders sit on the roof
	// rather than at the pivot.
	TMap<int32, float> ModelTopHeightCm;

	// Loaded once and kept: a crash builds fresh mesh components for the wreckage, and reparsing
	// three .MAX packs per car would hitch the frame the train derails on.
	TSharedPtr<FMaxisMeshLibrary> MeshLibrary;

	FRandomStream RandomStream;
	// FUN_004c0d10 is a per-simulation-tick roll, so the UFO's abduction attempt is metered to
	// OriginalSimHz instead of firing once per rendered frame.
	float UfoBeamTickAccumSeconds = 0.0f;
	bool bPoolsInitialized = false;
	bool bLoggedMeshError = false;

	// Rail tiles resolved from the XBLD/XZON grids once the city is up (FUN_004b7890's id set),
	// and the water tiles the boats live on, both cached so a respawn can fall back to the nearest
	// usable tile instead of giving up when the view-edge probe lands on dry land.
	TArray<FIntPoint> RailTiles;
	TSet<FIntPoint> RailTileSet;
	TArray<FIntPoint> WaterTiles;
	bool bRailScanned = false;

	// --- resolution helpers ---
	ASimCopterTrafficSystemActor* ResolveTrafficSystem() const;
	ASimCity2000CityActor* ResolveCityActor() const;
	ASimCopterMissionSystemActor* ResolveMissionSystem() const;
	FString ResolveOriginalGameRoot() const;
	float GetTileSizeCm() const;
	float GetCmPerOriginalUnit() const;
	bool TryGetCameraTile(FIntPoint& OutTile) const;
	bool TryGetTileCenter(const FIntPoint& Tile, FVector& OutWorld) const;
	bool IsWaterTile(const FIntPoint& Tile) const;
	// The 3x3 open-water test FUN_004b10a0 applies to CAPBOAT1 only.
	bool IsOpenWaterTile(const FIntPoint& Tile) const;
	bool TryGetWaterSurfaceZ(const FVector& WorldXY, float& OutZ) const;
	// bForTravel picks FUN_004b5290's wider traversal band over FUN_004b7890's placement band.
	bool IsRailTileAt(const FIntPoint& Tile, bool bForTravel) const;
	// Midpoint of the edge between two tiles - the point the train actually drives to.
	bool TryGetTrainWaypoint(const FIntPoint& FromTile, const FIntPoint& ToTile, FVector& OutWorld) const;
	void EnsureRailScan();
	int32 TileDistanceToCamera(const FIntPoint& Tile) const;

	// --- mesh plumbing ---
	UProceduralMeshComponent* CreateVehicleMesh(int32 ObjectId, const TCHAR* Name);
	void EnsurePools();
	float GetModelTopHeightCm(int32 ObjectId) const;

	// --- wreckage ---
	FSimCopterVehicleWreck* AddWreck(int32 ObjectId, const FVector& World, const FVector& Direction, float ExtraYawDegrees, int32 EventId, bool bBurning, float BurnTimeoutSeconds);
	// Puts the mission into the burning-vehicle accounting so it stays open while the wrecks burn.
	void BeginWreckFire(int32 EventId, int32 WreckCount);
	void UpdateWrecks(float DeltaSeconds);
	void ClearWrecksForEvent(int32 EventId);
	void DestroyWreck(FSimCopterVehicleWreck& Wreck);
	// True while the mission layer still has a live record for this event.
	bool IsMissionEventActive(int32 EventId) const;

	// --- train rescue riders ---
	void UpdateTrainRoofRiders();
	void ClearTrainRoofRiders();
	void SetMeshTransform(
		UProceduralMeshComponent* Mesh,
		const FVector& World,
		const FVector& Direction,
		float ExtraYawDegrees = 0.0f,
		float ExtraPitchDegrees = 0.0f);

	// --- planes (FUN_004b2330 / FUN_004b2630 / FUN_004b3420 / FUN_004b3530 / FUN_004b2910) ---
	void UpdatePlane(FSimCopterAmbientPlane& Plane, float DeltaSeconds);
	bool RespawnPlaneAtViewEdge(FSimCopterAmbientPlane& Plane);
	void StartPlaneSegment(FSimCopterAmbientPlane& Plane);
	void BeginPlaneCrash(FSimCopterAmbientPlane& Plane);
	void UpdatePlaneCrash(FSimCopterAmbientPlane& Plane, float DeltaSeconds);
	// FUN_004b2cd0's tail: water -> boat rescue, burnable tile -> building fire, otherwise
	// promote the plane's own background record into a live mission.
	void ResolvePlaneImpact(FSimCopterAmbientPlane& Plane, const FIntPoint& ImpactTile);
	void HidePlane(FSimCopterAmbientPlane& Plane);

	// --- boats (FUN_004af770 / FUN_004b10a0 / FUN_004b0cf0 / FUN_004b2150) ---
	void UpdateBoat(FSimCopterAmbientBoat& Boat, float DeltaSeconds);
	bool PlaceBoatNearTile(FSimCopterAmbientBoat& Boat, const FIntPoint& Origin, int32 MaxRings);
	bool RespawnBoatAtViewEdge(FSimCopterAmbientBoat& Boat);
	void ChooseBoatTarget(FSimCopterAmbientBoat& Boat);
	void SinkBoat(FSimCopterAmbientBoat& Boat);
	void HideBoat(FSimCopterAmbientBoat& Boat);
	void DrawBoatSpeed(FSimCopterAmbientBoat& Boat);

	// --- train (FUN_004b4440 / FUN_004b4660 / FUN_004b7890 / FUN_004b49b0) ---
	void UpdateTrain(float DeltaSeconds);
	bool PlaceTrainNearTile(const FIntPoint& Origin, int32 MaxRings);
	bool RespawnTrainNearCamera();
	bool AdvanceTrainTile();
	void UpdateTrainDerail(float DeltaSeconds);
	void HideTrain();
	void UpdateTrainCarTransforms();

	// Shared debris burst: FUN_0048e0b0 type 4 x3 plus the sound-0x1a explosion the plane and the
	// train both play at the crash site.
	void SpawnCrashDebris(const FVector& World, int32 EventId, int32 Count);

	// One looping voice per vehicle kind - plane engine, plane dive, UFO, train - each gated on
	// the 1920-unit radius and volume-driven by distance. See the .cpp for the CESSLP1/DIVE1
	// split, which is not what their names suggest.
	void UpdateAmbientVehicleAudio();
	int32 GetDifficultyTier() const;
};
