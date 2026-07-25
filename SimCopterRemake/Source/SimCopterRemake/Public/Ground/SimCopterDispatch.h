// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Decompiled SimCopter emergency-vehicle dispatch (the original's F2-F5 keys).
//
// Everything in this header is a direct port of SimCopter.exe; the evidence lives in
// Docs/scratchpad/ghidra/emergency_dispatch_decode_20260725.md and the raw dumps it cites.
// The functions this file mirrors:
//
//   FUN_0048a580  the four key handlers; target tile read from the spotlight node
//   FUN_004be910  service type -> manager/pool routing
//   FUN_004bc680  the dispatch transaction (result codes 0 / 2 / 4)
//   FUN_004bc250  candidate min-heap (idle vehicles + stations with a free slot)
//   FUN_004bc530  heap pop
//   FUN_004bc660  station slot release
//   FUN_004bcc80  station scan (XBLD id, 3x3 footprint, centre tile, road access)
//   FUN_004bc110  adjacent-road search around a station
//   FUN_004beda0  spiral walker init  /  FUN_004bedd0 step
//   FUN_004bb900  road / intersection tile predicates
//   FUN_0049b3f0  Shift+F<n> clear-dispatch scan
//   FUN_004bdc70  recall a vehicle
//
// The original's routing ran over its own 0x38-byte intersection graph
// (FUN_004bef30 Dijkstra). The remake already owns an equivalent road-tile graph in
// ASimCopterTrafficSystemActor, so reachability is asked of the caller through
// ISimCopterDispatchWorld rather than re-implementing the intersection records.

namespace SimCopterDispatch
{
// ---------------------------------------------------------------------------
// Tile predicates (FUN_004bc110 / FUN_004bc680 / FUN_004bb900 use these ranges
// byte-for-byte).
// ---------------------------------------------------------------------------

// XBLD ids the dispatcher accepts as drivable road.
SIMCOPTERREMAKE_API bool IsRoadTileId(int32 XbldId);

// FUN_004bb900: 0x27..0x2b are intersections; 0x69 is explicitly not one.
SIMCOPTERREMAKE_API bool IsIntersectionTileId(int32 XbldId);

// The octile metric this whole subsystem sorts by (FUN_004bc250, FUN_004bf2c0,
// FUN_0049b060): larger + (smaller >> 1).
SIMCOPTERREMAKE_API int32 TileCost(const FIntPoint& A, const FIntPoint& B);

// City grid is 128x128 everywhere in the original.
constexpr int32 MapTiles = 128;
SIMCOPTERREMAKE_API bool IsTileInBounds(const FIntPoint& Tile);

// ---------------------------------------------------------------------------
// Square-spiral walker, FUN_004beda0 / FUN_004bedd0.
//
// Init takes a "radius"; the walk stops once the leg length passes radius * 2.
// Step mutates the tile in place and returns false when the walk is finished.
// Callers test the starting tile themselves before the first Step, exactly as
// the original does.
// ---------------------------------------------------------------------------
struct SIMCOPTERREMAKE_API FSpiralWalker
{
	int32 StepInLeg = 0;
	int32 Direction = 0;
	int32 LegLength = 1;
	int32 MaxLegLength = 0;

	explicit FSpiralWalker(int32 Radius) : MaxLegLength(Radius * 2) {}

	bool Step(FIntPoint& Tile);
};

// FUN_004bc680 step 2 / FUN_004b9e40 case 2: walk out from Tile and return the first
// road tile found, or false. Radius 4 is the dispatch snap; the police chase retarget
// uses the same call.
SIMCOPTERREMAKE_API bool TryFindNearestRoadTile(
	const TFunctionRef<int32(int32, int32)>& GetXbldTileId,
	const FIntPoint& Origin,
	int32 Radius,
	FIntPoint& OutTile);

// ---------------------------------------------------------------------------
// Services
// ---------------------------------------------------------------------------

// The three service identities. The original keys them off the vehicle's message id
// (veh + 0x14): 0x11c fire, 0x11d police, 0x11f hospital.
enum class EService : uint8
{
	FireTruck,
	Police,
	Ambulance,
	Count
};

// Original message ids, used by the clear-dispatch scan to decide whether a parked
// vehicle belongs to the service being released.
constexpr int32 MessageIdFire = 0x11c;
constexpr int32 MessageIdPolice = 0x11d;
constexpr int32 MessageIdHospital = 0x11f;
SIMCOPTERREMAKE_API int32 GetServiceMessageId(EService Service);

// XBLD building ids passed to FUN_004bcc80.
constexpr int32 XbldHospital = 0xd1;
constexpr int32 XbldPoliceStation = 0xd2;
constexpr int32 XbldFireStation = 0xd3;
SIMCOPTERREMAKE_API int32 GetServiceStationXbldId(EService Service);

// FUN_004bcc80 writes manager[0x2c] = 5 for all three services.
constexpr int32 VehiclesPerService = 5;

// Station footprint the scan clears around a match (FUN_004bcc80 zeroes a 3x3 and
// records the centre at +1/+1).
constexpr int32 StationFootprintTiles = 3;

// FUN_004bc110 searches ring distances 2..4 only.
constexpr int32 StationRoadSearchMinDistance = 2;
constexpr int32 StationRoadSearchMaxDistance = 4;

// FUN_004bc680 snaps the requested target to a road tile within this spiral radius.
constexpr int32 TargetRoadSnapRadius = 4;

// FUN_0049b3f0 releases a vehicle found within this spiral radius of the spotlight.
constexpr int32 ClearDispatchRadius = 2;

// FUN_004b9e40: the on-scene target scan is 3 rings; FUN_004b9890 (fire) uses 5.
constexpr int32 OnSceneScanRadius = 3;
constexpr int32 FireTargetScanRadius = 5;

// FUN_004bd980 arms the stay timer at 0xb40000 (180.0s) after the vehicle acts;
// FUN_004b9e40 uses 0x780000 (120.0s) for the post-action hold and 0x1e0000 (30.0s)
// for the retry gap.
constexpr float OnSceneStaySeconds = 180.0f;
constexpr float OnSceneHoldSeconds = 120.0f;
constexpr float OnSceneRetrySeconds = 30.0f;

// ---------------------------------------------------------------------------
// Station registry (FUN_004bcc80 / FUN_004bc110)
// ---------------------------------------------------------------------------

// One 0x10-byte station record.
struct SIMCOPTERREMAKE_API FStation
{
	EService Service = EService::FireTruck;
	// +0x04/+0x05: the centre of the 3x3 building.
	FIntPoint Tile = FIntPoint(INDEX_NONE, INDEX_NONE);
	// +0x06/+0x07: the road tile vehicles enter and leave from.
	FIntPoint RoadTile = FIntPoint(INDEX_NONE, INDEX_NONE);
	// +0x00: the approach direction, 0..3 = N, E, S, W.
	int32 Direction = 0;
	// +0x08: dispatches currently outstanding from this station. A station is only a
	// candidate while this is <= 0, and it is decremented when a vehicle gets home
	// (FUN_004bc660). Persisted in the original save as NTSF/NTSP/NTSH.
	int32 Outstanding = 0;
};

// FUN_004bc110: the deterministic ring search for a road tile beside a station.
// Returns the direction (0..3) and fills OutRoadTile, or -1 (the original's 0xff).
SIMCOPTERREMAKE_API int32 FindStationRoadAccess(
	const TFunctionRef<int32(int32, int32)>& GetXbldTileId,
	const FIntPoint& StationTile,
	FIntPoint& OutRoadTile);

// FUN_004bcc80's two scan passes, collapsed into one: find every 3x3 building of the
// service's XBLD id, take its centre, and keep it only when it has road access.
SIMCOPTERREMAKE_API void ScanStations(
	const TFunctionRef<int32(int32, int32)>& GetXbldTileId,
	EService Service,
	TArray<FStation>& OutStations);

// ---------------------------------------------------------------------------
// Candidate selection (FUN_004bc250 / FUN_004bc530)
// ---------------------------------------------------------------------------

enum class ECandidateKind : uint8
{
	// Heap kind 1: a vehicle already out in the city and idle.
	Vehicle,
	// Heap kind 2: spawn a fresh vehicle from a station.
	Station
};

struct SIMCOPTERREMAKE_API FCandidate
{
	int32 Cost = 0;
	ECandidateKind Kind = ECandidateKind::Station;
	int32 Index = INDEX_NONE;
};

// The runtime state of one pool slot, as the candidate builder sees it.
struct SIMCOPTERREMAKE_API FVehicleSlotView
{
	// veh[4] & 2: the slot currently holds a spawned vehicle.
	bool bSpawned = false;
	// veh + 0x299 == 2: parked and available for redirection.
	bool bIdle = false;
	FIntPoint Tile = FIntPoint(INDEX_NONE, INDEX_NONE);
};

// FUN_004bc250. Fills OutHeap (unordered; pop with PopCheapestCandidate) and reports
// the free pool slot a station spawn would use (manager[0x30], INDEX_NONE = none).
// Returns true when at least one candidate exists.
SIMCOPTERREMAKE_API bool BuildCandidates(
	const TArray<FStation>& Stations,
	const TArray<FVehicleSlotView>& Slots,
	const FIntPoint& TargetTile,
	TArray<FCandidate>& OutCandidates,
	int32& OutFreeSlotIndex);

// FUN_004bc530: remove and return the cheapest candidate.
SIMCOPTERREMAKE_API bool PopCheapestCandidate(TArray<FCandidate>& Candidates, FCandidate& OutCandidate);

// ---------------------------------------------------------------------------
// The dispatch transaction (FUN_004bc680)
// ---------------------------------------------------------------------------

// FUN_004bc680's return values, kept as the original numbers because the voice clip
// each one plays is part of the decoded behaviour.
enum class EDispatchResult : int32
{
	// manager[0x04] clip: every station is busy and no vehicle is idle.
	NoUnitAvailable = 0,
	// manager[0x0c] clip: no road tile near the target, or nothing can route to it.
	CannotReach = 2,
	// manager[0x08] clip: a unit is on its way.
	Dispatched = 4,
	// Not an original code: the requested tile was off the map, so FUN_004be910
	// returned without touching the manager and no clip played.
	InvalidTarget = -1
};

// What the caller must supply for the routing half of the transaction.
class SIMCOPTERREMAKE_API ISimCopterDispatchWorld
{
public:
	virtual ~ISimCopterDispatchWorld() = default;

	// XBLD id at a tile, 0 when out of bounds.
	virtual int32 GetXbldTileId(int32 TileX, int32 TileY) const = 0;

	// Stands in for FUN_00492240 + FUN_004bef30: is there a drivable route between two
	// road tiles? The original ran Dijkstra over its intersection graph; the remake
	// answers from its own road-tile graph.
	virtual bool CanRouteBetween(const FIntPoint& FromRoadTile, const FIntPoint& ToRoadTile) const = 0;
};

struct SIMCOPTERREMAKE_API FDispatchOutcome
{
	EDispatchResult Result = EDispatchResult::InvalidTarget;
	// The pool slot that was committed (Dispatched only).
	int32 SlotIndex = INDEX_NONE;
	// The station the vehicle came from, or INDEX_NONE when an already-driving vehicle
	// was redirected.
	int32 StationIndex = INDEX_NONE;
	// The road tile the vehicle drives to: the requested tile snapped by the radius-4
	// spiral.
	FIntPoint DestinationTile = FIntPoint(INDEX_NONE, INDEX_NONE);
	// True when an idle vehicle was redirected rather than a station spawning one.
	bool bRedirectedExistingVehicle = false;
	// Which half of CannotReach fired: no road tile within the radius-4 snap (true), or a
	// road was found but nothing could route to it (false). The original plays the same
	// clip for both; the split is here so debug output can tell them apart.
	bool bNoRoadNearTarget = false;
};

// The full FUN_004bc680 transaction. Stations is mutated on success (the chosen
// station's Outstanding is incremented, matching `INC dword ptr [EAX + 0x8]`).
SIMCOPTERREMAKE_API FDispatchOutcome Dispatch(
	const ISimCopterDispatchWorld& World,
	TArray<FStation>& Stations,
	const TArray<FVehicleSlotView>& Slots,
	const FIntPoint& RequestedTile);

} // namespace SimCopterDispatch
