// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Flight/SimCopterWaterGameplay.h"
#include "Input/Reply.h"
#include "Missions/SimCopterMissionSystem.h"
#include "UI/SimCopterMissionMarkerLayout.h"
#include "SimCopterMissionSystemActor.generated.h"

class ASimCopterTrafficSystemActor;
class ASimCopterHelicopterPawn;
class ASimCopterGroundAgent;
class USimCopterFireRenderComponent;
class USimCopterParticleFXComponent;
enum class ESimCopterMissionPassengerKind : uint8;
class UMaterialInterface;
class SConstraintCanvas;
class STextBlock;
class SVerticalBox;
class SWidget;

// One monitored medevac unload at a hospital. The state-5 Medik's shipped BHAV 263 owns the
// visible interaction: select the real cabin patient, alight them through the carrier service,
// tote them laterally, and set them down on XBLD D1 for BHAV 282 to finish. This record is only a
// bounded recovery guard around that behavior; it does not animate a parallel handoff.
struct FSimCopterMedevacHandoff
{
	int32 EventId = INDEX_NONE;
	TWeakObjectPtr<ASimCopterHelicopterPawn> Helicopter;
	TWeakObjectPtr<ASimCopterGroundAgent> Emt;
	int32 LastOnboardCount = 0;
	float SecondsWithoutProgress = 0.0f;
};

// How the mission layer is running the current city. The original shell offered exactly two session
// kinds through DAT_00518d50: 1 = user city (FUN_004080c0, weights copied from career City0 and
// optionally overridden by the city file's own 9-dword 0x5eeeeee record) and 2 = career
// (FUN_00407f30, per-city record + cities\career\city<N>.sc2). Both always ran the scheduler, so
// there is no original "no jobs" mode; free roam is expressed the one way the original data can
// express it - a city whose seven weights sum to zero, which FUN_004a6d20 collapses into an
// all-zero cumulative table so FUN_004a6e60 can never pick a bucket.
UENUM()
enum class ESimCopterMissionSessionMode : uint8
{
	// Nothing chosen yet. The mission system is not ticked while the main menu decides.
	Pending,
	// No scheduled jobs (zero-weight city). Fires/jams can still be started by hand.
	FreeRoam,
	// The city's difficulty tier and weight vector drive the scheduler, as in both original modes.
	CityJobs,
	// One mission started on demand; scheduled spawning stays off so it runs alone.
	SingleMission,
};

struct FSimCopterMissionLogEntry
{
	FString Text;
	FLinearColor Color = FLinearColor::White;
	float RemainingSeconds = 0.0f;
};

struct FSimCopterMissionWorldMarkerEntry
{
	FVector WorldLocation = FVector::ZeroVector;
	FString Label;
	FString Detail;
	FLinearColor Color = FLinearColor::White;
};

UCLASS()
class SIMCOPTERREMAKE_API ASimCopterMissionSystemActor : public AActor, public SimCopterMissions::ISimCopterMissionWorld
{
	GENERATED_BODY()
	
public:	
	ASimCopterMissionSystemActor();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:	
	virtual void Tick(float DeltaTime) override;

	// ~Begin ISimCopterMissionWorld Interface
	virtual int32 GetXbldTileId(int32 TileX, int32 TileY) const override;
	virtual int32 GetBuildingFootprintSize(int32 TileX, int32 TileY) const override;
	virtual int32 GetBuildingTopHeight1616(int32 TileX, int32 TileY) const override;
	virtual bool GetCameraTile(int32& OutTileX, int32& OutTileY) const override;
	virtual bool GetPlayerTile(int32& OutTileX, int32& OutTileY) const override;
	virtual bool IsModalUiActive() const override;

	virtual void OnBuildingFireIgnited(int32 TileX, int32 TileY, int32 EventId) override;
	virtual void OnBuildingBurnedDown(int32 TileX, int32 TileY, int32 FootprintSize) override;

	virtual void PlayRadioVoice(int32 VoiceId, int32 Volume) override;
	virtual void PlayUiSound(int32 SoundId) override;
	virtual void OnUiMessage(const SimCopterMissions::FSimCopterMissionUiMessage& Message) override;

	virtual bool TryActivatePlaneCrash(int32 EventId) override;
	virtual bool TryActivateTrainCrash(int32 EventId) override;
	virtual bool TryActivateBoatRescue(int32 EventId, int32 Timer1616, int32 TileX, int32 TileY, int32& OutTileX, int32& OutTileY) override;
	virtual bool TryActivateTrainRescue(int32 EventId, int32 Timer1616, int32& OutTileX, int32& OutTileY) override;

	// --- ambient vehicle callbacks (ASimCopterAmbientVehiclesActor) ---
	// A crashing plane/train has to be able to open a mission of its own: FUN_004b2cd0 creates a
	// fire or a boat rescue at the impact tile, and FUN_004b49b0 promotes the train's own record.
	int32 CreateMissionAt(int32 TileX, int32 TileY, int32 TypeMask);
	void PostMissionEvent(int32 Code, int32 EventId, int32 Value, bool bSilent);
	void PostMissionEventAt(int32 Code, int32 EventId, int32 X, int32 Y, int32 Value, bool bSilent);
	bool CanIgniteCrashSite(int32 TileX, int32 TileY) const { return MissionSystem.CanIgniteCrashSite(TileX, TileY); }
	// True while the mission layer still holds a live record for this event - what a crash wreck
	// or a rescued boat watches to know when to clear itself away.
	bool IsMissionEventActive(int32 EventId) const
	{
		const SimCopterMissions::FSimCopterMissionRecord* Record = MissionSystem.FindRecord(EventId);
		return Record != nullptr && Record->bActive;
	}
	// FUN_004a88e0 returns the live record's +0x30 coordinate pair. Object class 0 uses this
	// destination in BHAV 292 before a transport passenger requests opcode 17 (alight).
	bool TryGetMissionDestinationTile(int32 EventId, int32& OutTileX, int32& OutTileY) const;
	// The whole 30-slot record table (DAT_0057f9dc). The cockpit map draws every live record and
	// cycles its selection through them in slot order, so it needs the table, not a lookup.
	const TArray<SimCopterMissions::FSimCopterMissionRecord>& GetMissionRecords() const
	{
		return MissionSystem.GetRecords();
	}
	// FUN_004a9230(mask): the event id of the first live record carrying every bit of TypeMask, or
	// INDEX_NONE. Behaviour opcodes 24 and 28 use it to find the running riot.
	int32 FindActiveMissionOfType(int32 TypeMask) const;
	// FUN_004abb00(mask): how many live records carry every bit of TypeMask. Behaviour opcode 35
	// compares against the MedEvac count.
	int32 CountActiveMissionsOfType(int32 TypeMask) const;
	// OR a type bit onto a running record, the way EVT_DebrisCreated/MedevacVictimAdded do.
	void PromoteMissionType(int32 EventId, int32 TypeBits) { MissionSystem.PromoteRecordType(EventId, TypeBits); }
	int32 GetMissionDifficultyTier() const { return MissionSystem.GetDifficultyTier(); }
	// FUN_004c3f00: the mission's people go with the vehicle when it sinks or blows up.
	void RemoveMissionPeople(int32 EventId);
	// Finds (or spawns) the actor that owns the plane/boat/train pools.
	class ASimCopterAmbientVehiclesActor* ResolveAmbientVehicles();


	virtual bool TryStartTrafficJam(int32 EventId, int32& OutTileX, int32& OutTileY) override;
	virtual void EndTrafficJam(int32 EventId) override;
	virtual bool TryStartCarFire(int32 EventId, int32& OutTileX, int32& OutTileY) override;
	virtual bool TryActivateSpeederCar(int32 EventId, int32 TileX, int32 TileY) override;
	virtual bool TryResolveTransportSpawnTile(int32 OriginX, int32 OriginY, int32& OutTileX, int32& OutTileY) override;
	virtual bool TrySpawnMissionPerson(int32 Mode, int32 SubState, int32 TileX, int32 TileY, int32 EventId) override;
	// Stable action boundary used by both the decoded VM and engine-side recovery paths. A real
	// person owns idempotency; these methods are the only place passenger outcomes reach mission
	// counters.
	bool NotifyMissionPersonBoarded(ASimCopterGroundAgent* Person);
	bool NotifyMissionPersonDelivered(ASimCopterGroundAgent* Person);
	bool NotifyMissionPersonDied(ASimCopterGroundAgent* Person);
	void NotifyPassengerDroppedFromHelicopter(int32 EventId, ESimCopterMissionPassengerKind Kind, int32 Count);
	// BHAV 263 only reaches BHAV 269's "get on starting object" arm after it finds no medevac
	// victim aboard. The mission layer adds the missing temporal context: the helper ride is useful
	// only while an active medevac still has a patient waiting to be retrieved.
	bool CanHospitalParamedicBoardPlayerHelicopter(const ASimCopterHelicopterPawn* Helicopter) const;
	bool CreatePlayerCausedMedevacForVictim(ASimCopterGroundAgent* Victim);
	bool CreatePlayerCausedCarFireForVehicle(ASimCopterGroundAgent* Vehicle);
	bool ConvertDroppedTransportPassengerToMedevac(ASimCopterGroundAgent* Victim, int32 SourceTransportEventId);

	// --- Emergency dispatch resolution hooks ---
	// These are what an arrived fire truck / police car / ambulance asks of the mission
	// layer. See Docs/scratchpad/ghidra/emergency_dispatch_decode_20260725.md section 7:
	// the original's fire truck scans a five-ring spiral for a burning cell and sprays
	// emitter type 6 at it (the water then douses through the ordinary impact path), and
	// its police car scans three rings for a target before acting.

	// What a parked fire truck is currently hosing.
	struct FServiceFireTarget
	{
		// Where the water has to land. This is the flame's OWN position, not its anchor
		// cell's origin: IgniteBuilding puts a large building's flames up to +/-0x700000
		// out from that origin while Fire Radius is only ~0x2beb99, so aiming at the cell
		// centre reaches nothing on any building bigger than 1x1.
		FVector World = FVector::ZeroVector;
		FIntPoint Tile = FIntPoint(INDEX_NONE, INDEX_NONE);
		int32 EventId = INDEX_NONE;
	};

	// SCHOOK: FireTruckAcquireTarget 0x004b9890 0x004b9b10
	// Walk a RadiusTiles-ring spiral out from FromTile for something alight. Within a burning
	// cell the original reservoir-samples that cell's flames, so which one gets hosed varies
	// shot to shot; a burning vehicle on the tile wins outright. Nothing found -> false.
	bool TryAcquireServiceFireTarget(
		const FIntPoint& FromTile,
		int32 RadiusTiles,
		FServiceFireTarget& OutTarget) const;

	// Nearest active traffic-jam mission tile within RadiusTiles of FromTile.
	bool TryFindNearestJamTile(const FIntPoint& FromTile, int32 RadiusTiles, FIntPoint& OutTile, int32& OutEventId) const;

	// Nearest active medevac/rescue mission tile within RadiusTiles of FromTile.
	bool TryFindNearestMedicalTile(const FIntPoint& FromTile, int32 RadiusTiles, FIntPoint& OutTile, int32& OutEventId) const;

	// Police clearing a jam they have driven to.
	bool ClearTrafficJamEvent(int32 EventId);

	// SCHOOK: VehicleMegaphoneReaction 0x0049d7e0. Message 0 clears one jammed car and posts
	// EVT_CarCleared; the mission closes only after every car counted by EVT_JamCarAdded clears.
	bool ReportTrafficJamCarCleared(int32 EventId);

	// FUN_004b8c90: the arrest has run its course and the car is being taken away. Posts
	// EVT_CriminalCaught, which takes the record's CriminalsCaught to its TargetCount of 1 and
	// so completes the mission - notification and payout included.
	void ReportSpeederCarCaught(int32 EventId);

	// FUN_004b8b60's failure branch: the arrest could not put anyone on the ground, so the record
	// is retired with EVT_SetCategory value 4 (CAT_ExpireSilently) - no completion, no payout.
	void ReportSpeederCarUnresolved(int32 EventId);

	// SCHOOK: FireTruckSpray 0x004a5ca0
	// One shot from a fire truck's monitor: emitter type 6, launched from 30 units above the
	// truck along the swept elevation. Nothing is extinguished here - the droplet douses where
	// it lands, through the same impact path as the helicopter's water. The sweep state lives
	// on this actor because the original keeps it in DAT_00505f84/DAT_00505f88, one pair shared
	// by every truck.
	void SpawnServiceWaterJet(const FVector& TruckWorld, const FVector& TargetWorld);

	// Distinct anchor tiles that currently have at least one active flame, with that tile's
	// flame count. Debug/diagnostic use - a dispatched fire truck's scan works in these
	// tiles, so this is what to compare its position against when it appears idle.
	void GetActiveFlameTiles(TArray<TPair<FIntPoint, int32>>& OutTiles) const;

	// Debug: force-spawn a building fire (or car fire) near the camera so fire visuals and the
	// bucket douse can be exercised without waiting for the scheduler. Invoked via the helicopter
	// pawn's `SimForceFire` / `SimForceCarFire` console commands (the player pawn routes Exec).
	void SimForceFire();
	void SimForceCarFire();

	// --- Session control (driven by the main menu through ASimCopterGameMode) ---

	// Holds the mission layer before its first tick so nothing spawns while the session is being
	// set up. Without this the actor starts city 0's jobs on its first tick, which is what a map
	// entered directly (PIE straight into the city level) gets.
	void HoldSessionForMenu();

	// Free roam: adopt the city's difficulty/day-night but zero its seven scheduler weights.
	void StartFreeRoamSession(int32 CareerCityIndex);

	// Jobs arrive on the city's own schedule, as in both original session kinds. CareerCityIndex
	// supplies the tuning record; a user city uses City0, the way FUN_004080c0 seeds mode 1.
	// bFirstJobImmediately rolls the opening job now instead of after the original's 180s countdown.
	void StartCityJobsSession(int32 CareerCityIndex, bool bFirstJobImmediately = false);

	// Start one mission of TypeMask right now through the original placement path
	// (FUN_004a92f0 -> FUN_004a7a10) and leave scheduled spawning off so it runs alone.
	// Returns the created event id, or INDEX_NONE when the placer found no suitable tile (or the
	// type's world hook is not ported yet).
	int32 StartSingleMissionSession(int32 CareerCityIndex, int32 TypeMask);

	// Adds one mission to the session that is already running, without touching score or cash.
	// Returns the created event id or INDEX_NONE.
	int32 StartMissionNow(int32 TypeMask);

	ESimCopterMissionSessionMode GetSessionMode() const { return SessionMode; }
	int32 GetCareerCityCount() const { return MissionSystem.GetCareerCityCount(); }
	bool GetCareerCityInfo(int32 Index, SimCopterMissions::FSimCopterCareerCity& OutCity) const;
	int32 GetSessionScore() const { return MissionSystem.GetScore(); }
	int32 GetSessionCash() const { return MissionSystem.GetCash(); }
	int32 GetSessionDifficultyTier() const { return MissionSystem.GetDifficultyTier(); }
	// Which career city the session adopted, so the dashboard can read its points requirement.
	int32 GetSessionCareerCityIndex() const { return MissionSystem.GetCareerCityIndex(); }
	int32 GetActiveMissionCount() const { return MissionSystem.GetActiveMissionCount(); }

	// FUN_00407a90 through the session record: the hangar shop's till. Negative spends; the
	// original clamps the balance at zero rather than refusing, and so does this.
	void AddSessionCash(int32 Delta) { MissionSystem.AddCash(Delta); }

	// Save loading: restores the durable session record after StartCityJobsSession has performed
	// the ordinary city setup. Transient jobs start fresh because their original BOMB payload has
	// no remake serializer yet.
	void RestoreSavedSessionState(
		int32 Score,
		int32 Cash,
		const SimCopterMissions::FSimCopterCareerCity& City,
		float ElapsedSeconds)
	{
		MissionSystem.RestoreSessionState(Score, Cash, City);
		SessionElapsedSeconds = FMath::Max(0.0f, ElapsedSeconds);
	}

	// The record the running session is scheduling from - the eight values the Settings screen's
	// City Settings dialog edits. FUN_00440ec0 writes them straight back into the live block, and
	// so does this: the change takes effect on the next scheduler tick, with no restart.
	const SimCopterMissions::FSimCopterCareerCity& GetSessionCareerCity() const
	{
		return MissionSystem.GetCareerCity();
	}
	void SetSessionCareerCity(const SimCopterMissions::FSimCopterCareerCity& City)
	{
		MissionSystem.SetCareerCity(City);
	}

	// Seconds since the session opened, for stamping career log lines.
	float GetSessionElapsedSeconds() const { return SessionElapsedSeconds; }

	// Complete live BOMB-side mission state (records, scheduler, fires and actor-owned mission
	// bookkeeping). The byte array contains no UObject pointers and is safe across level travel.
	bool CaptureRuntimeSaveState(TArray<uint8>& OutData);
	bool RestoreRuntimeSaveState(const TArray<uint8>& Data);

	// Called only when a type-5/type-6 water trajectory hits land or geometry. Remaining particle
	// life is the douse strength (bucket particles arrive already divided by four); water-surface
	// impacts are rejected by the particle updater before this boundary.
	int32 ApplyWaterParticleImpact(const FVector& ImpactWorldLocation, int32 Strength1616);

	// SCHOOK: FireProximityProbe 0x004a5c10
	// How far a point sits above the top of the nearest flame below it, in original 16.16 units,
	// or 0 when there is no flame within reach. This is the helicopter's fire-damage and
	// fire-shake input (FSimCopterFlightEnvironment::FireHeightDelta): the flight model burns hit
	// points while the delta is inside [MinFireAlt, MaxFireAlt] and shakes harder the smaller it
	// is. Negative means the point is below the top of the flame.
	//
	// The original walks only the flames on the caller's own tile and box-tests them; the remake
	// walks the live flame list and does the same test, which is the same set.
	int32 GetFireHeightDelta1616(const FVector& WorldLocation) const;

	// ~End ISimCopterMissionWorld Interface

private:
	UPROPERTY(EditInstanceOnly, Category = "SimCopter|Traffic")
	TObjectPtr<ASimCopterTrafficSystemActor> SourceTrafficSystem;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic")
	bool bUseActiveTrafficSystem = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|UI")
	bool bShowMissionMessageLog = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|UI", meta = (ClampMin = "1", ClampMax = "12"))
	int32 MaxMessageLogEntries = 6;

	UPROPERTY(EditAnywhere, Category = "SimCopter|UI", meta = (ClampMin = "0.5", ClampMax = "30.0"))
	float MessageLogDurationSeconds = 8.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|UI")
	FVector2D MessageLogScreenPadding = FVector2D(18.0f, 18.0f);

	UPROPERTY(EditAnywhere, Category = "SimCopter|UI")
	bool bShowMissionWorldMarkers = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|UI")
	FVector2D MissionMarkerSize = FVector2D(110.0f, 63.0f);

	// Lift the panel above the exact projected objective point in screen space. A world-space
	// lift makes the tag slide sideways when the helicopter camera banks, especially nearby.
	UPROPERTY(EditAnywhere, Category = "SimCopter|UI", meta = (ClampMin = "0.0", ClampMax = "64.0"))
	float MissionMarkerScreenOffset = 4.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|UI", meta = (ClampMin = "0.0", ClampMax = "128.0"))
	float MissionMarkerEdgePadding = 9.0f;

	// Clearance around each visible cockpit/HUD panel. Panel geometry is sampled from Slate, so
	// this remains correct when the HUD scale or viewport size changes.
	UPROPERTY(EditAnywhere, Category = "SimCopter|UI", meta = (ClampMin = "0.0", ClampMax = "128.0"))
	float MissionMarkerUiPadding = 12.0f;

	// Marker panels may cover half of one another on either screen axis, but no more. This keeps
	// coincident objectives readable without spreading a dense cluster across the HUD.
	UPROPERTY(EditAnywhere, Category = "SimCopter|UI", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MissionMarkerAllowedOverlap = 0.5f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Missions", meta = (ClampMin = "50.0"))
	float PassengerPickupRadiusCm = 780.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Missions", meta = (ClampMin = "50.0"))
	float PassengerDropoffRadiusCm = 820.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Missions", meta = (ClampMin = "50.0"))
	float PassengerTransferMaxVerticalDeltaCm = 420.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Missions", meta = (ClampMin = "50.0"))
	float PassengerBoardTouchRadiusCm = 130.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Missions", meta = (ClampMin = "0.05"))
	float PassengerBoardGuidanceSeconds = 0.45f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Missions", meta = (ClampMin = "25.0"))
	float MedevacOnFootPickupRadiusCm = 95.0f;

	// Rescue victims (spawn modes 1, 2 and 0x13 - water, roof and train) are winched aboard where
	// they are, not from a pickup tile, and are set down on any dry land: FUN_004a7a10 leaves a
	// rescue record's Secondary tile at -1, so there is no delivery point to fly to.
	//
	// How close counts depends on which way they come aboard. With the harness out, the reach is
	// measured from the rope END; climbing straight in needs them against the airframe, i.e. the
	// helicopter's own collision extent plus this margin.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Missions", meta = (ClampMin = "0.0"))
	float RescueBoardTouchMarginCm = 70.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Missions", meta = (ClampMin = "20.0"))
	float RescueHarnessReachCm = 220.0f;

	// How low the helicopter has to be over dry land before the survivors get out.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Missions", meta = (ClampMin = "50.0"))
	float RescueDropoffHeightCm = 600.0f;

	// How close the helicopter (with medevac patients aboard) must be to the hospital drop-off tile
	// before the EMT comes out to unload it.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Missions", meta = (ClampMin = "100.0"))
	float MedevacHospitalHandoffRadiusCm = 1500.0f;

	// Recovery only: if BHAV 263 makes no cabin progress for this long while the helicopter
	// remains landed at D1, resolve the remaining seat(s) through the established mission service.
	// Normal unloads never reach this timer.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Missions", meta = (ClampMin = "5.0"))
	float MedevacBehaviorRecoverySeconds = 45.0f;

	SimCopterMissions::FSimCopterMissionSystem MissionSystem;

	ESimCopterMissionSessionMode SessionMode = ESimCopterMissionSessionMode::Pending;

	// Set by HoldSessionForMenu: something is choosing the session, so the first tick must not fall
	// back to city 0's jobs.
	bool bSessionSelectionHeld = false;

	// Wall clock since BeginSession, used only to stamp the career mission log.
	float SessionElapsedSeconds = 0.0f;

	// Shared session entry: adopt career city CareerCityIndex (with its scheduler weights zeroed
	// when bAllowScheduledMissions is false) and open the session with the original's start
	// money/score.
	void BeginSession(ESimCopterMissionSessionMode Mode, int32 CareerCityIndex, bool bAllowScheduledMissions);

	// Renders the cloned FIREPTS fire/smoke marker effects for every active building flame; driven
	// each tick from MissionSystem.GetFlames().
	UPROPERTY(Transient)
	TObjectPtr<USimCopterFireRenderComponent> FireRenderComponent;

	// Material for fire/smoke point sprites (defaults to M_SimCopterParticleFXSoft).
	UPROPERTY(EditAnywhere, Category = "SimCopter|Fire")
	TObjectPtr<UMaterialInterface> FlameMaterial;

	// Rising smoke + embers above each fire from the typed effect pool.
	UPROPERTY(Transient)
	TObjectPtr<USimCopterParticleFXComponent> FireSmokeComponent;

	// How close the bucket water must be to a burning car to put it out.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Fire", meta = (ClampMin = "50.0"))
	float CarDouseRadiusCm = 600.0f;

	// Fire-truck jet elevation sweep (original DAT_00505f84 / DAT_00505f88): the aim rises by
	// the step each shot until it reaches 4.0 units, then falls back to 0 - the stream arcs up
	// over the fire and back down again about every five seconds.
	SimCopterWaterGameplay::FFireTruckJetSweep ServiceJetSweep;
	bool bLoggedServiceJetSpawnFailure = false;

	// Cached resolved city actor (for the rendered-surface trace that seats flames on rooftops)
	// and the original game root (for loading the flame GEO meshes once).
	TWeakObjectPtr<AActor> ResolvedCityActor;
	bool bFireAssetsInitialized = false;

	// Build the per-frame flame draw list from the mission system and push it to the fire
	// component. Converts each flame's tile to a rooftop-traced world point + growth/flicker.
	void UpdateFireVisuals(float DeltaSeconds);
	// Emit rising smoke + fire embers above a burning point (origin = flame base, Scale = flame size).
	void SpawnFirePlume(const FVector& FlameBaseWorld, float Scale, float DeltaSeconds);
	FString ResolveOriginalGameRootDir() const;
	// Trace the rendered surface (building roof / terrain) at a world XY; returns the top Z.
	bool TraceSurfaceTopZ(const FVector& WorldXY, float& OutTopZ) const;
	// Where one flame is drawn: tile centre plus its local offset, seated on the traced roof and
	// lifted by the storeys it has climbed. Shared by the renderer and the fire-damage probe so
	// the band the helicopter burns in is the fire the player can see.
	bool TryGetFlameWorldLocation(
		const struct SimCopterMissions::FSimCopterFlame& Flame,
		FVector& OutWorld) const;

	TArray<FSimCopterMissionLogEntry> MissionMessageLog;
	TSharedPtr<SWidget> MessageLogWidget;
	TSharedPtr<SWidget> MessageLogPanel;
	TSharedPtr<SVerticalBox> MessageLogBox;
	TSharedPtr<SWidget> MissionMarkerWidget;
	TSharedPtr<SConstraintCanvas> MissionMarkerCanvas;
	TArray<FSimCopterMedevacHandoff> MedevacHandoffs;
	// Mission records clear their type when the casualty counter completes them. Keep the hospital
	// tile until every real medevac seat (including a deceased patient) has been unloaded.
	TMap<int32, FIntPoint> MedevacHospitalTiles;

	// The burning-building loop (id 0x0d) is one voice for the whole city: FUN_004a4ac0 keeps
	// picking the nearest fire and calling SetPosition on that single slot, so a second fire
	// does not double it. Re-run every tick from UpdateFireVisuals.
	void UpdateFireAudio();

	// SCHOOK: EmergencySirenMixer 0x004a1d50
	// Four looping voices - ambulance, fire, police, hose - each driven by the distance to the
	// NEAREST vehicle of that kind and nothing else. The original stores those four distances in
	// DAT_0057f750..764 and runs an identical block per sound.
	void UpdateEmergencySirenAudio();

	// Where and when a fire truck last fired its monitor, so the hose loop knows whether any
	// truck is spraying and how far away it is.
	FVector ServiceJetWorld = FVector::ZeroVector;
	double ServiceJetLastSeconds = -1000.0;

	ASimCopterTrafficSystemActor* ResolveTrafficSystem() const;
	void ProcessPassengerTransfers();
	// The rescue half of the transfer loop: winch water/roof/train survivors aboard and set them
	// down on dry land (FUN_004ccf50 action 1 posts EVT_RescueDelivered for spawn modes 1/2/0x13).
	void ProcessRescueTransfers();
	int32 PostPassengerDelivery(
		int32 EventId,
		ESimCopterMissionPassengerKind Kind,
		int32 RequestedCount,
		bool bSilent = false);
	int32 ReleaseMissionPassengersFromHelicopter(
		ASimCopterHelicopterPawn* Helicopter,
		int32 EventId,
		ESimCopterMissionPassengerKind Kind,
		int32 MaxCount,
		const FVector& DropLocation,
		bool bRemoveAfterDelivery);

	TWeakObjectPtr<class ASimCopterAmbientVehiclesActor> CachedAmbientVehicles;
	// Guarantees the decoded hospital worker and monitors its BHAV-driven unload (called each Tick,
	// after ProcessPassengerTransfers).
	void ProcessMedevacHospitalHandoffs(float DeltaSeconds);
	FSimCopterMedevacHandoff* FindMedevacHandoff(int32 EventId);
	void BeginMedevacHandoff(int32 EventId, ASimCopterHelicopterPawn* Helicopter, const FVector& HospitalCenter);
	// Returns false when the handoff is finished/aborted and should be cleaned up.
	bool AdvanceMedevacHandoff(FSimCopterMedevacHandoff& Handoff, float DeltaSeconds);
	void EndMedevacHandoff(FSimCopterMedevacHandoff& Handoff, bool bResolvePatients = false);
	void DeliverMedevacDirectly(int32 EventId, ASimCopterHelicopterPawn* Helicopter);
	void GetTransferReadyHelicopters(TArray<ASimCopterHelicopterPawn*>& OutHelicopters) const;
	void EnsureMessageLogWidget();
	void RemoveMessageLogWidget();
	void RefreshMessageLogWidget();
	void PushMissionLogMessage(const FString& Text, const FLinearColor& Color);
	FString FormatMissionUiMessage(const SimCopterMissions::FSimCopterMissionUiMessage& Message, FLinearColor& OutColor) const;
	// Mirrors the HUD line into the career log the hangar's Mission Log page prints.
	void WriteCareerLogEntry(const SimCopterMissions::FSimCopterMissionUiMessage& Message);
	void EnsureMissionMarkerWidget();
	void RemoveMissionMarkerWidget();
	void RefreshMissionMarkerWidget();
	void BuildMissionWorldMarkers(TArray<FSimCopterMissionWorldMarkerEntry>& OutMarkers) const;
	void BuildMissionMarkerUiObstacles(
		TArray<SimCopterMissionMarkerLayout::FUiObstacle>& OutObstacles) const;
	bool TryMakeMissionMarkerWorldLocation(int32 TileX, int32 TileY, FVector& OutWorldLocation) const;
	bool ProjectMissionMarkerToScreen(const FVector& WorldLocation, FVector2D& OutScreenPosition, bool& bOutClamped) const;
};
