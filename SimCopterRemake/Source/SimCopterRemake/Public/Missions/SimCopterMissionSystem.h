// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FArchive;

// Decompiled SimCopter mission/event system, ported from SimCopter.exe.
//
// This is an exact port of the original mission layer, decoded from these
// executable functions (see Docs/scratchpad/ghidra/out_m5_*.txt and
// Docs/Milestone5SimulationPlan.md):
//
//   FUN_0047a760  master simulation tick (fps EMA, subsystem order)
//   FUN_004a6e60  mission scheduler (cadence, concurrency cap, weight roll)
//   FUN_004a6d20  career weight vector -> cumulative percentage table
//   FUN_00407bb0  current career city record selector
//   FUN_004a92f0  event placer (per-mask tile selection near the camera)
//   FUN_004abb30  random tile chooser (fail-count widened radius)
//   FUN_004a7a10  event creator (30-slot record table, per-mask spawn config)
//   FUN_004a73e0  mission lifecycle walker (goals, nags, expiry, completion)
//   FUN_004a89c0  mission event sink (counter mutation + type promotion)
//   FUN_004aa150  incremental scoring at event receipt (score/cash + HUD)
//   FUN_004aabf0  final mission scoring (per-type-bit money/points)
//   FUN_004ab480  radio announcement voice sequence composer
//   FUN_004ab170 / FUN_004a5f10  tweak control -> global bindings
//   FUN_004a4ac0 / FUN_004a48e0 / FUN_004a5340 / FUN_004a4fb0  fire simulation
//   FUN_0049a4d0  XBLD id -> property record (flag bit 4 = mission building)
//
// The mission "type" is a BITMASK and scoring sums per-bit contributions
// (e.g. 0x408 = base car-fire event 0x400 plus debris bit 0x8; a car catching
// fire during a building fire ORs bit 0x8 into the running mission's type).
// The scheduler buckets map to the seven career.twk weights in order:
//   Fire(1,4,0x100,0x408) Crime(0x200,0x2000,0x20000,0x4000)
//   Rescue(0x80010,0x90,0x110) Riot(0x1000) Traffic(0x800)
//   MedEvac(0x20) Transport(0x40)
// This layer uses MSVC rand() (LCG), NOT the people-behavior LFSR.
// Time values are 16.16 fixed-point seconds, like the flight model.

namespace SimCopterMissions
{
// Event type mask bits (record +0x50). Names are pinned by the per-mask
// creation code, the scoring globals each bit reads, and the announcement
// voice ids - not guesses. See Docs/Milestone5SimulationPlan.md.
enum EType : int32
{
	TYPE_BuildingFire   = 0x1,      // FUN_004a5080/FUN_004a5340 flame object on a building
	TYPE_Speeder        = 0x2,      // scoring-only bit ([Speeder Miss]); set by the car code
	TYPE_PlaneCrash     = 0x4,      // marks an idle plane object (DAT_00582910)
	TYPE_Debris         = 0x8,      // debris fires; promoted onto fire missions by event 7
	TYPE_RescuePeople   = 0x10,     // rescue-victim component ([Rescue Miss] pp scoring)
	TYPE_Medevac        = 0x20,     // injured people, mode-6 spawns ([Medevac Miss])
	TYPE_Transport      = 0x40,     // building crowd pickup, mode-4 spawns ([Transport Miss])
	TYPE_WaterRescue    = 0x80,     // voice-variant bit on 0x90 (boat rescue)
	TYPE_BoatRescue     = 0x90,     // sinking-boat object (DAT_00582840) + mode-1 victims
	TYPE_TrainCrash     = 0x100,    // marks the train object (DAT_00582afc)
	TYPE_TrainRescue    = 0x110,    // places the train + mode-0x13 victims
	TYPE_Robber         = 0x200,    // single person, person state 10, behavior class 9 (BHAV 1300)
	TYPE_CarFire        = 0x400,    // burning car family bit ([Fire Miss] car controls)
	TYPE_CarFireEvent   = 0x408,    // scheduler-created car fire (FUN_0049fd00 on a car)
	TYPE_TrafficJam     = 0x800,    // jam flag 0x200 on a car (FUN_0049fe30)
	TYPE_Riot           = 0x1000,   // 9..30 requested by difficulty; at least 11 mode-3 people or creation fails
	TYPE_Arsonist       = 0x2000,   // single person, person state 11, behavior class 9 (BHAV 1301)
	TYPE_Burglar        = 0x4000,   // CARROBBR getaway car and recurring burglar (BHAV 1303)
	TYPE_Mugger         = 0x20000,  // single person, person state 12, behavior class 9 (BHAV 1302)
	TYPE_RooftopRescue  = 0x80010,  // people trapped on a mission building roof (person state 2)
	TYPE_Ufo            = 0x100000, // the UFO event ([General Miss] UFO Money/Points)
};

// SCHOOK: RioterSpawn 0x004c4190, spawn-mode-3 arm.
// The original writes 7 directly to person + 0x150 before placing the rioter. BHAV 852 then
// servos that agitation toward `nearby-count * nearby-mean / 15`, while BHAV 311 only leaves the
// riot below 3. The earlier value 3 sat on that cliff and decayed immediately whenever collision
// placement spread even a few people outside the one-tile crowd scan.
static constexpr int32 RioterSpawnAgitation = 7;

// Mission event codes accepted by PostEvent (FUN_004a89c0 switch cases).
enum EEvent : int32
{
	EVT_SetPrimaryCoords   = 0x00, // record +0x28/+0x2c
	EVT_FlameCreated       = 0x01, // +0x58; docks Flame points immediately
	EVT_FlameDoused        = 0x02, // +0x60; pays Flame cash immediately
	EVT_FlameExpired       = 0x03, // +0x64 (burned out on its own)
	EVT_CellBurnedOut      = 0x04, // +0x68 (whole building cell burned; end penalty)
	EVT_StructureIgnited   = 0x05, // +0x5c
	EVT_ObjectCaughtFire   = 0x06, // +0x6c; pays Bldg Saved cash immediately
	EVT_DebrisCreated      = 0x07, // +0x70; promotes type bit 0x8 on 0x505-family fires
	EVT_DebrisExpired      = 0x08, // +0x78
	EVT_DebrisDoused       = 0x09, // +0x74; pays Debris Doused cash
	EVT_SetSecondaryCoords = 0x0a, // record +0x30/+0x34
	EVT_RiotPersonAdded    = 0x0b, // +0x80 (riot size)
	EVT_MedevacVictimAdded = 0x0c, // +0x84; promotes type bit 0x20
	EVT_TransportPassengerAdded = 0x0d, // +0x88; promotes type bit 0x40
	EVT_RescueVictimAdded  = 0x0e, // +0x8c
	EVT_Unknown0F          = 0x0f, // +0x90
	EVT_RescueDelivered    = 0x10, // +0x98; pays Resc Inc cash
	EVT_TransportDelivered = 0x11, // +0x9c; pays Inc Trans cash
	EVT_MedevacDelivered   = 0x12, // +0xa0; pays Inc Medevac cash
	EVT_VictimPickedUp     = 0x13, // +0xa4; pays Inc Pickup cash
	EVT_RioterDispersed    = 0x14, // +0xa8; pays +10 cash +10 score
	EVT_RioterCalmed       = 0x15, // +0xac
	EVT_Unknown16          = 0x16, // +0xb0
	EVT_PersonDied         = 0x17, // +0xb4; unowned deaths dock score
	EVT_CarCrashed         = 0x18, // +0xc0
	EVT_JamCarAdded        = 0x19, // +0xc4; >=3 promotes a background jam to active
	EVT_CarDoused          = 0x1a, // +0xc8; pays Car Doused cash
	EVT_CarCleared         = 0x1b, // +0xd0; pays Car Fire cash
	EVT_CarBurned          = 0x1c, // +0xcc; docks Car Burned points
	EVT_SetCategory        = 0x1d, // record +0x54; 0 on a 0x104-family promotes to active
	EVT_SetTertiaryCoords  = 0x1e, // record +0x38/+0x3c
	EVT_PassengerLost      = 0x1f, // +0xbc
	EVT_DebrisCleared      = 0x20, // +0x7c
	EVT_SpeederPursuit     = 0x21, // pays Incmtl Points value as cash (no counter)
	EVT_SetEndPointsScaled = 0x22, // record +0x48 (size-scaled fire end points)
	EVT_SetEndMoneyScaled  = 0x23, // record +0x44 (size-scaled fire end money)
	EVT_AdjustTargetCount  = 0x24, // +0x94
	EVT_CriminalCaught     = 0x25, // +0xb8
	EVT_SpeederCaught      = 0x26, // pays Speeder End money+points (no counter)
	EVT_UfoResolved        = 0x27, // pays UFO Money/Points
	EVT_NagMugging         = 0x28, // -10 points, STRINGTABLE 0x3b2 "Sim Mugged!"
	EVT_NagSos             = 0x29, // -10 points, or -20 for riot; riot also advances +0x94
	EVT_NagBurglary        = 0x2a, // -10 points, STRINGTABLE 0x3b4 "Burglary Committed!"
	EVT_NagArsonist        = 0x2b, // -10 points, STRINGTABLE 0x3b5 "Arsonist On Loose!"
	EVT_NagPeopleWaiting   = 0x2c, // -10 points, STRINGTABLE 0x3b6 "Sims Waiting!"
	EVT_NagCarsWaiting     = 0x2d, // -10 points, STRINGTABLE 0x3b7 "Cars Waiting!"
	EVT_CrashPenaltyA      = 0x2e, // fixed penalties (heli crash variants):
	EVT_CrashPenaltyB      = 0x2f, //   -100 pts / -300 cash
	EVT_CrashPenaltyC      = 0x30, //   -100 pts / -200 cash
	EVT_CrashPenaltyD      = 0x31, //   -100 pts / -100 cash
	EVT_CrashPenaltyE      = 0x32, //   -50 pts / -50 cash
	EVT_CrashPenaltyF      = 0x33, //   -100 pts / -150 cash
	EVT_CrashPenaltyG      = 0x34, //   -100 pts / -75 cash
	EVT_CrashPenaltyH      = 0x35, //   -200 pts / -200 cash
};

// Record categories (+0x54): 0 = active mission (counted vs Max Easy), 2 =
// background/easy (plane/train/jam start here; converts to active on
// promotion), 4 = expire silently, 8 = complete immediately.
enum ECategory : int32
{
	CAT_Active = 0,
	CAT_Background = 2,
	CAT_ExpireSilently = 4,
	CAT_CompleteNow = 8,
};

// MSVC rand(): the mission layer's PRNG (NOT the people-behavior LFSR).
// state = state * 214013 + 2531011; result = (state >> 16) & 0x7fff.
struct SIMCOPTERREMAKE_API FSimCopterMsvcRand
{
	uint32 State = 1;

	void Seed(uint32 NewSeed) { State = NewSeed; }
	int32 Rand()
	{
		State = State * 214013u + 2531011u;
		return static_cast<int32>((State >> 16) & 0x7fff);
	}
};

// One career city record (career.twk City0..City29; original memory layout
// DAT_00518dcc + city * 0x50, read by FUN_004a6d20).
struct SIMCOPTERREMAKE_API FSimCopterCareerCity
{
	int32 Difficulty = 0;      // Ctrl0 "Difficulty (0-3)"; tier = Difficulty + 1
	float Weights[7] = { 0, 0, 0, 0, 0, 0, 0 }; // Fire Crime Rescue Riot Traffic MedEvac Transport
	int32 DayOrNight = 0;      // Ctrl8
	int32 PointsNeeded = 0;    // Ctrl9
	int32 MoneyEarned = 0;     // Ctrl10
};

// All mission tuning globals with their original addresses and shipped
// defaults. Values marked (16.16) are fixed-point; the tweak fxpt controls
// land here as value * 65536 (matching the original loader).
struct SIMCOPTERREMAKE_API FSimCopterMissionTuning
{
	// [General Miss] (FUN_004ab170 -> 0x505fa8/0x505fa4/0x505fb0/0x506048/0x50604c)
	int32 MaxEasy = 2;
	int32 EasyInterval = 380 << 16;      // (16.16 seconds)
	int32 IntervalAdj = 13107;           // 0.2 (16.16)
	int32 UfoMoney = 2000;
	int32 UfoPoints = 1000;

	// Base mission timer: initialized .data constant DAT_00505fc4 = 0x2580000
	// (600.0s); the scheduler derives the difficulty-scaled timer and the
	// reminder interval (timer >> 3) from it every pass.
	int32 BaseMissionTimer = 0x2580000;  // (16.16 seconds)

	// [Riot Miss] (0x505fcc..0x505fdc). End Money/Points feed FUN_004aabf0. The two
	// "Penalty" controls and Riot Timer are bound by the tweak loader but have no runtime xrefs in
	// the retail executable; they are retained as decoded data, not applied as invented rules.
	int32 RiotEndMoney = 725;
	int32 RiotEndPoints = 505;
	int32 RiotEndMoneyPenalty = 0;
	int32 RiotEndPointsPenalty = 250;
	int32 RiotTimer = static_cast<int32>(197.8 * 65536.0); // (16.16 seconds)

	// [Rescue Miss] (0x505fe0/0x505fe4/0x506090)
	int32 RescueEndMoneyPerPerson = 200;
	int32 RescueEndPointsPerPerson = 100;
	int32 RescueIncMoneyPerPerson = 50;

	// [Transport Miss] (0x505ff0/0x505ff4/0x506094/0x50609c)
	int32 TransportEndMoneyPerPerson = 100;
	int32 TransportEndPointsPerPerson = 50;
	int32 TransportIncMoneyPerPerson = 20;
	int32 PickupIncMoneyPerPerson = 10;

	// [Medevac Miss] (0x505fe8/0x505fec/0x506098)
	int32 MedevacEndMoneyPerPerson = 200;
	int32 MedevacEndPointsPerPerson = 100;
	int32 MedevacIncMoneyPerPerson = 30;

	// [Fire Miss] Ctrl0..19 (see FUN_004ab170 pointer list). NOTE the engine's
	// use of some controls differs from their labels: "Flame($)" (0x506078) is
	// the per-new-flame SCORE penalty and "Flame(neg pts)" (0x50607c) is the
	// per-doused-flame CASH award (verified in FUN_004aa150 cases 1/2).
	int32 FireEndMoneyPerSize = 150;     // 0x505ff8
	int32 FireEndPointsPerSize = 100;    // 0x505ffc
	int32 FireEndMoneyPenalty = 0;       // 0x506000
	int32 FireEndPointsPenalty = 50;     // 0x506004
	int32 PlaneCrashMoney = 200;         // 0x506008
	int32 PlaneCrashPoints = 100;        // 0x50600c
	int32 TrainCrashMoney = 100;         // 0x506010
	int32 TrainCrashPoints = 100;        // 0x506014
	int32 CarFireMoney = 100;            // 0x506038
	int32 CarFirePoints = 50;            // 0x50603c
	int32 DebrisFireMoney = 50;          // 0x506040
	int32 DebrisFirePoints = 50;         // 0x506044
	int32 FlamePointsPenalty = 20;       // 0x506078 (label "Flame($)")
	int32 FlameDousedMoney = 50;         // 0x50607c (label "Flame(neg pts)")
	int32 BldgDestPointsPenalty = 200;   // 0x506080
	int32 BldgSavedMoney = 150;          // 0x506084
	int32 NewDebrisPointsPenalty = 50;   // 0x506088
	int32 DebrisDousedMoney = 20;        // 0x50608c
	int32 CarDousedMoney = 30;           // 0x5060a0
	int32 CarBurnedPointsPenalty = 20;   // 0x5060a8; 0x5060a4 is paid by EVT_CarCleared

	// The EVT_CarCleared multiplier global 0x5060a4 sits between the two bound
	// car controls and is paid per cleared jam car (FUN_004aa150 case 0x1b).
	int32 CarClearedMoney = 0;

	// [Criminal Miss] (0x506018/0x50601c)
	int32 CriminalEndMoney = 500;
	int32 CriminalEndPoints = 300;

	// [Speeder Miss] (0x506020/0x506024/0x506028)
	int32 SpeederEndMoney = 200;
	int32 SpeederEndPoints = 100;
	int32 SpeederIncPoints = 5;

	// [Traffic Miss] (0x50602c/0x506030/0x506034)
	int32 JamEndMoney = 100;
	int32 JamEndPoints = 50;
	int32 JamTimer = 60 << 16;           // (16.16 seconds)

	// Unowned-death score penalty multiplier (0x506050; binder not yet located,
	// used by FUN_004aa150 case 0x17 when the dead person has no mission).
	int32 PersonDiedPointsPenalty = 0;

	// [Fire Parms] from fire.twk (FUN_004a5f10 -> 0x505f40..0x505f54).
	int32 FireDousePoints = static_cast<int32>(37.3 * 65536.0);   // (16.16)
	int32 FireDouseMult = 21;
	int32 FireTimeToLive = static_cast<int32>(190.3 * 65536.0);   // (16.16 seconds)
	int32 FireSpreadInterval = static_cast<int32>(34.7 * 65536.0);// (16.16 seconds)
	int32 FireSpreadProb = 224;          // spread roll: rand % ((1-tier)*10 + this) == 0
	int32 FireRadius = static_cast<int32>(43.9 * 65536.0);        // (16.16 world units)
};

// One mission/event record: exact mirror of the original 0xd4-byte record at
// DAT_0057f9dc + slot * 0xd4 (30 slots). Field names come from the event
// codes that write them and the scoring that reads them.
struct SIMCOPTERREMAKE_API FSimCopterMissionRecord
{
	FString Name;                 // +0x00 sprintf "<retail title> <event id>"
	int32 TypeSerial = 0;         // +0x20 per-type sequence number
	int32 EventId = -1;           // +0x24 unique id (global serial)
	int32 TileX = -1;             // +0x28
	int32 TileY = -1;             // +0x2c
	int32 SecondaryX = -1;        // +0x30
	int32 SecondaryY = -1;        // +0x34
	int32 TertiaryX = -1;         // +0x38
	int32 TertiaryY = -1;         // +0x3c
	int32 TimeAccum = 0;          // +0x40 (16.16 seconds toward the nag interval)
	int32 EndMoneyScaled = 0;     // +0x44 (EVT_SetEndMoneyScaled)
	int32 EndPointsScaled = 0;    // +0x48 (EVT_SetEndPointsScaled)
	bool bActive = false;         // +0x4c bit 0
	int32 TypeMask = 0;           // +0x50
	int32 Category = 0;           // +0x54
	int32 FlamesCreated = 0;      // +0x58
	int32 StructuresIgnited = 0;  // +0x5c
	int32 FlamesDoused = 0;       // +0x60
	int32 FlamesExpired = 0;      // +0x64
	int32 CellsBurnedOut = 0;     // +0x68
	int32 ObjectsCaughtFire = 0;  // +0x6c
	int32 DebrisCreated = 0;      // +0x70
	int32 DebrisDoused = 0;       // +0x74
	int32 DebrisExpired = 0;      // +0x78
	int32 DebrisCleared = 0;      // +0x7c
	int32 RiotSize = 0;           // +0x80
	int32 MedevacVictims = 0;     // +0x84
	int32 TransportPassengers = 0;// +0x88
	int32 RescueVictims = 0;      // +0x8c
	int32 Counter90 = 0;          // +0x90 (EVT_Unknown0F)
	int32 TargetCount = 0;        // +0x94 (crime: 1; riot: elapsed nag periods)
	int32 RescueDelivered = 0;    // +0x98
	int32 TransportDelivered = 0; // +0x9c
	int32 MedevacDelivered = 0;   // +0xa0
	int32 VictimsPickedUp = 0;    // +0xa4
	int32 RiotersDispersed = 0;   // +0xa8
	int32 RiotersCalmed = 0;      // +0xac
	int32 CounterB0 = 0;          // +0xb0 (EVT_Unknown16)
	int32 Casualties = 0;         // +0xb4
	int32 CriminalsCaught = 0;    // +0xb8
	int32 PassengersLost = 0;     // +0xbc
	int32 CarsCrashed = 0;        // +0xc0
	int32 JamCarCount = 0;        // +0xc4
	int32 CarsDoused = 0;         // +0xc8
	int32 CarsBurned = 0;         // +0xcc
	int32 CarsCleared = 0;        // +0xd0
	bool bSuppressCompletionRewards = false;
};

// Message posted through FUN_004a89c0: {code, event id, x, y, value, silent}.
struct SIMCOPTERREMAKE_API FSimCopterMissionEvent
{
	int32 Code = 0;
	int32 EventId = -1;
	int32 X = 0;
	int32 Y = 0;
	int32 Value = 0;
	bool bSilent = false; // original byte +0x14 bit 0: skip the incremental payment
};

// HUD/radio message emitted by the mission layer (FUN_0048c4a0 payloads at
// DAT_00506058: kind 5 = announced, 6 = completed, 8 = score delta, 9 = cash
// delta; TextId is the original string-resource id 0x23b..0x24b / 0x3a2..0x3c1).
struct SIMCOPTERREMAKE_API FSimCopterMissionUiMessage
{
	int32 Kind = 0;
	int32 EventId = -1;
	int32 TextId = 0;
	int32 ValueA = 0;  // completion/deltas: points or primary delta amount
	int32 ValueB = 0;  // completion: money
	int32 TypeMask = 0;
	FString MissionName;
	bool bNegative = false;
};

// One flame record: exact mirror of the original 0xa0-byte record at
// DAT_005ce0a0 (0x8c slots). Positions are 16.16 world units.
//
// A flame is NOT a fire-and-forget puff. FUN_004a48e0 gives it a full
// "TimeToLive (secs)" burn, and FUN_004a4ac0 spends that time growing the
// flame one storey up the wall (footprint - 1 times, each step re-arming the
// full timer) before it finally burns out. Only when the last flame of a
// building burns out does FUN_004a5fd0 demolish it.
struct SIMCOPTERREMAKE_API FSimCopterFlame
{
	bool bActive = false;         // +0x00 bit 0
	int32 GrowthAxisFlags = 0;    // +0x00 bits 2/4/8/0x10 (which wall the flame climbs)
	int32 BurnCountdown = 0;      // +0x04 (16.16 seconds to the next growth step / burn-out)
	int32 DouseHealth1616 = 0;    // +0x08 (tier*0x14 + "Douse Points"; drained by FUN_004a50c0)
	int32 PosX = 0, PosY = 0, PosZ = 0;          // +0x10/+0x14/+0x18 local offset
	int32 GrowthStepsRemaining = 0;              // +0x1c (footprint - 1)
	// +0x0c: one growth step, i.e. one storey. FUN_004a48e0 uses a flat 32.0
	// units on single-cell buildings and buildingTop/footprint on larger ones,
	// so (footprint - 1) steps climb the structure. This is NOT the flame's
	// render size - FUN_004a47c0 gives every flame the same 0x100000 scale.
	int32 GrowthStep1616 = 0;
	int32 DamageCountdown = 0x8000;              // +0x88 (16.16s between FUN_004a6370 sweeps)
	int32 WorldX = 0, WorldY = 0, WorldZ = 0;    // +0x3c/+0x40/+0x44 world position
	int32 TileX = 0, TileY = 0;   // +0x8c/+0x90
	// +0x94: the cell object flagged 4 (the building structure) that the flame
	// climbs. Zero means there is nothing to climb and the flame simply expires
	// at the end of its first burn.
	int32 ClimbTargetObject = 0;
	int32 FireObjectIndex = -1;   // +0x98 (owning fire-object slot)
	int32 EventId = -1;           // +0x9c
};

// One fire object (FUN_004a5080 slots at DAT_005d3820, 3 dwords each): a
// burning building; groups that building's flames.
struct SIMCOPTERREMAKE_API FSimCopterFireObject
{
	bool bActive = false;       // +0 (cell pointer in the original)
	int32 TileX = -1, TileY = -1;
	int32 FlameCount = 0;       // +4
	bool bRescueSpawned = false;// +8 (one trapped-people rescue per building)
};

// World interface: everything the mission core asks of the game world. The
// original called directly into the car/people/train/plane/boat/fire
// subsystems; the remake implements these on the city/traffic actors.
class SIMCOPTERREMAKE_API ISimCopterMissionWorld
{
public:
	virtual ~ISimCopterMissionWorld() = default;

	// --- map/tile queries ---
	virtual int32 GetXbldTileId(int32 TileX, int32 TileY) const = 0;       // DAT_005910b0 grid
	virtual int32 GetBuildingFootprintSize(int32 TileX, int32 TileY) const = 0; // scene cell +8
	// FUN_004a48e0 walks the cell's display list and keeps the tallest object's
	// top (object +0x30) so it can split the structure into footprint storeys.
	// Return the roof height above the cell floor in 16.16 original units, and 0
	// when the cell has no structure to climb.
	virtual int32 GetBuildingTopHeight1616(int32 TileX, int32 TileY) const { return 0; }
	virtual bool GetCameraTile(int32& OutTileX, int32& OutTileY) const = 0;    // DAT_0061a618/c
	virtual bool GetPlayerTile(int32& OutTileX, int32& OutTileY) const = 0;    // DAT_005040d0+0x18
	// DAT_00503aa0 == 3: view mode 3 is the on-foot player. Crime timers advance nearby only in
	// this mode; while flying they pause inside the original's 12-step weighted distance.
	virtual bool IsPlayerOnFoot() const { return false; }
	// FUN_00408c30, the scheduler's only spawn gate (DAT_005812b4). Despite the name it is not a
	// UI test: it returns 1 only in career mode (DAT_00518d50 == 2) once the session score has
	// reached the current city's "Points Needed", i.e. new missions stop arriving as soon as the
	// city is won. Nothing in the original suppressed spawning for a menu - the shell ran in its
	// own screen loop with the simulation stopped, which the remake does by not ticking the
	// mission system at all (ASimCopterMissionSystemActor's debug session hold).
	virtual bool IsModalUiActive() const { return false; }

	// --- per-type creation hooks (return false = creation fails, slot freed,
	//     exactly like the original's early-out paths) ---
	// Mask 1: ignite a building; the core has already validated the tile and
	// allocated the fire object. Implementations place visuals.
	virtual void OnBuildingFireIgnited(int32 TileX, int32 TileY, int32 EventId) {}
	// Mask 4 / 0x100: mark an idle plane / the train as crashing.
	virtual bool TryActivatePlaneCrash(int32 EventId) { return false; }
	virtual bool TryActivateTrainCrash(int32 EventId) { return false; }
	// Mask 0x90 (FUN_004b1aa0): place the capsized boat in open water near the placer's tile,
	// spawn its survivors, and report the boat's own tile - the original does the same thing by
	// posting EVT_SetPrimaryCoords from inside the boat code.
	virtual bool TryActivateBoatRescue(int32 EventId, int32 Timer1616, int32 TileX, int32 TileY, int32& OutTileX, int32& OutTileY) { return false; }
	// Mask 0x110 (FUN_004b7fd0): put the train on the rails - seeded from a random map tile, not
	// the placer's - spawn its trapped passengers, and report the train's tile.
	virtual bool TryActivateTrainRescue(int32 EventId, int32 Timer1616, int32& OutTileX, int32& OutTileY) { return false; }
	// Mask 0x4000: activate the burglar's CARROBBR getaway car. CruiseDelay1616 is the exact
	// FUN_004b8540 draw, made by the mission PRNG before the vehicle subsystem takes ownership.
	virtual bool TryActivateBurglarCar(int32 EventId, int32 TileX, int32 TileY, int32 CruiseDelay1616) { return false; }
	// Mask 0x800 / 0x408: find an eligible ambient car (flag bit 1 set, bits
	// 0x300 clear) and jam it / set it on fire; report its tile.
	virtual bool TryStartTrafficJam(int32 EventId, int32& OutTileX, int32& OutTileY) { return false; }
	virtual void EndTrafficJam(int32 EventId) {}
	virtual bool TryStartCarFire(int32 EventId, int32& OutTileX, int32& OutTileY) { return false; }
	virtual bool TryResolveTransportSpawnTile(int32 OriginX, int32 OriginY, int32& OutTileX, int32& OutTileY)
	{
		OutTileX = OriginX;
		OutTileY = OriginY;
		return true;
	}
	// Person spawns (FUN_004c3eb0 -> FUN_004c4190). The original call order is behavior class,
	// person state; this interface deliberately presents the state first because it drives the
	// placement/lifecycle and keeps the class override optional (-1). Returns false when no person
	// slot/placement is available (original -1).
	virtual bool TrySpawnMissionPerson(int32 PersonState, int32 BehaviorClass, int32 TileX, int32 TileY, int32 EventId) { return false; }
	// Riot centroid (FUN_004c9e40): speed-weighted average tile of the event's
	// people. Return false when none exist.
	virtual bool TryGetRiotCentroid(int32 EventId, int32& OutTileX, int32& OutTileY) const { return false; }
	// Traffic jam expiry (FUN_004a01a0): release cars jammed by this event.
	virtual void OnTrafficJamExpired(int32 EventId) {}

	// --- fire hooks ---
	virtual void OnFlameSpawned(const FSimCopterFlame& Flame, int32 FlameIndex) {}
	virtual void OnFlameRemoved(int32 FlameIndex) {}
	virtual void OnFlameGrown(const FSimCopterFlame& Flame, int32 FlameIndex) {}
	// FUN_004a5fd0: the building at the tile burned down (footprint cleared,
	// density set to 10 in the original grid).
	virtual void OnBuildingBurnedDown(int32 TileX, int32 TileY, int32 FootprintSize) {}
	// FUN_004a6370(rec, 6): the flame damages people/objects in its bounds.
	virtual void DamageInFlameBounds(const FSimCopterFlame& Flame, int32 EventId) {}

	// --- presentation sinks ---
	virtual void OnUiMessage(const FSimCopterMissionUiMessage& Message) {}
	// FUN_0042a3b0(0, voiceId, volume): radio voice phrase queue.
	virtual void PlayRadioVoice(int32 VoiceId, int32 Volume) {}
	// FUN_0042a2a0: one-shot UI sounds (0x1e = cash register, 0x7f/0x21 etc).
	virtual void PlayUiSound(int32 SoundId) {}
};

// The mission system core. Deterministic; call Tick once per simulation frame
// with the frame delta, exactly where FUN_0047a760 ran FUN_004a6e60 and
// FUN_004a4ac0.

class SIMCOPTERREMAKE_API FSimCopterMissionSystem
{
public:
	static constexpr int32 MaxRecords = 30;   // DAT_0057f9dc table size
	static constexpr int32 MaxFlames = 0x8c;  // DAT_005ce0a0 table size
	static constexpr int32 MaxFireObjects = 0x31; // DAT_005d3820 slots (0x94 bytes / 12)

	FSimCopterMissionTuning Tuning;

	void Initialize(ISimCopterMissionWorld* InWorld, uint32 RandSeed);
	void SetCareerCity(const FSimCopterCareerCity& City);
	const FSimCopterCareerCity& GetCareerCity() const { return CareerCity; }
	// SCHOOK: StartUserCity 0x004080c0. Mode 1 owns a separate nine-dword settings record instead
	// of scheduling directly from career City0. These are the original user-city defaults shown by
	// the City Settings panel; the non-settings tail stays attached to the remake's base record.
	static FSimCopterCareerCity MakeUserCityDefaults(const FSimCopterCareerCity& BaseCity);

	bool LoadCareerData(const FString& TweakFilePath);
	void AdvanceCareerCity();
	void AdvanceCareerIfComplete();
	bool IsLevelComplete() const { return bLevelComplete; }
	bool CheckLevelCompletion();

	// FUN_004080c0 (single city, DAT_00518d50 = 1) and FUN_00407f30 (career, = 2) both open a
	// session with $1000 and 0 points. (Both also write two still-unidentified session fields,
	// block +0x44 = 0x10 and +0x48 = 3, which nothing ported reads yet.)
	static constexpr int32 SessionStartingCash = 1000;

	// REMAKE DIVERGENCE, no original counterpart: what it costs you to put a civilian in hospital.
	// Paired with FSimCopterMissionRecord::bSuppressCompletionRewards on player-caused medevacs,
	// so injuring someone is a straight loss and letting them die then costs you again - the
	// original paid full price for delivering someone you had run over yourself.
	static constexpr int32 PlayerCausedInjuryFine = 250;

	// Opens a fresh session: score 0, cash SessionStartingCash.
	void BeginSession();

	// Restores the serializable CINF/UINF + CSET portion after a normal city session has opened.
	// Live mission records and fires belong to the original's separate BOMB world payload and are
	// deliberately not synthesized here.
	void RestoreSessionState(int32 InScore, int32 InCash, const FSimCopterCareerCity& InCareerCity);

	// The career city table (career.twk City0..City29, in file order). The original also keeps a
	// hardcoded per-city map name and successor list in FUN_00408370 - see
	// Docs/scratchpad/ghidra/session_modes_and_menu_20260724.md; only the tweak half is ported.
	int32 GetCareerCityCount() const { return CareerCities.Num(); }
	int32 GetCareerCityIndex() const { return CurrentCityIndex; }
	const FSimCopterCareerCity* GetCareerCityByIndex(int32 Index) const;

	// FUN_00408210's tuning half: adopt career city Index (0..29) and reset the city score. The
	// original also swapped in that city's cities\career\city<N>.sc2 map, which the remake's
	// city actor owns.
	bool SelectCareerCity(int32 Index);

	static const TCHAR* GetTypeDisplayName(int32 TypeMask);
	static int32 GetLocationVoiceId(int32 TileX, int32 TileY); // FUN_004aba30
	// FUN_004b8540 / FUN_004b8630: DAT_00506360 + (short)rand() % DAT_00506364.
	int32 DrawBurglarCruiseDelay1616();


	// Master per-frame update: advances the fps EMA (FUN_0047a760), then runs
	// the scheduler (FUN_004a6e60), the fire update (FUN_004a4ac0) and the
	// lifecycle walker (FUN_004a73e0) in the original order.
	void Tick(float DeltaSeconds);

	// FUN_004a92f0/FUN_004a7a10: place and create an event of the given mask
	// near the camera (or at the explicit tile). Returns the event id or -1.
	int32 CreateEventOfType(int32 TypeMask);
	int32 CreateEventAt(int32 TileX, int32 TileY, int32 TypeMask);

	// Debug: force-spawn a fire mission even when the mission-record pool is full. Grows the pool
	// on demand (normal play never fills it), so this always succeeds if a suitable tile exists.
	int32 DebugForceBuildingFire();
	int32 DebugForceCarFire();

	// FUN_004a89c0: post a mission event (also pays incremental score/cash).
	void PostEvent(const FSimCopterMissionEvent& Event);

	// Convenience for the common {code, id, value} shape.
	void PostEvent(int32 Code, int32 EventId, int32 Value, bool bSilent = false);

	// Creates a medevac record for an already-spawned injured victim. Used when the player causes
	// the injury, so completion pays no end reward.
	int32 CreatePlayerCausedMedevacAt(int32 TileX, int32 TileY);

	void AdjustVictimsPickedUp(int32 EventId, int32 Delta);

	// ORs type bits onto a live record. The original's masks are not fixed at creation either -
	// EVT_DebrisCreated ORs 0x8, EVT_MedevacVictimAdded ORs 0x20, EVT_TransportPassengerAdded ORs
	// 0x40 - so a crash that leaves burning wreckage joins the 0x400 family the same way.
	void PromoteRecordType(int32 EventId, int32 TypeBits);

	// FUN_004a50c0: one water particle landing on a burning cell. Every flame in the
	// cell has its burn countdown pushed out by 3.0s (water stalls the climb), and any
	// flame whose local offset is within Fire Radius of the impact loses
	// ((1 - tier) * 3 + "Douse Mult") * Strength1616 douse health; a flame driven
	// below zero is removed with EVT_FlameDoused. Strength1616 is the original's
	// (0x50000 - particleSize) / 0x50000 term - pass 0x10000 for a full-strength hit.
	// Returns the number of active flames the water was in range of.
	int32 DouseAtLocalOffset(int32 TileX, int32 TileY, int32 LocalX1616, int32 LocalZ1616, int32 Strength1616);

	// World-space entry point (16.16 units, tile = 0x400000).
	int32 DouseAt(int32 WorldX1616, int32 WorldY1616, int32 WorldZ1616);

	// Whole-cell douse: a full-strength hit centred on the cell, used where the caller
	// has no sub-cell impact point. Returns flames in range.
	int32 DouseAtTile(int32 TileX, int32 TileY);

	// Megaphone: clears an active traffic-jam mission - releases the jammed cars, scores it and
	// completes it. Returns false when the event isn't an active jam.
	bool ClearTrafficJam(int32 EventId);

	// --- state accessors ---
	int32 GetScore() const { return Score; }
	int32 GetCash() const { return Cash; }
	void AddScore(int32 Delta);  // FUN_00407b00 (clamps at 0)
	void AddCash(int32 Delta);   // FUN_00407a90 (clamps at 0)
	int32 GetActiveMissionCount() const { return ActiveCount; }
	int32 GetBackgroundMissionCount() const { return BackgroundCount; }
	int32 GetDifficultyTier() const { return DifficultyTier; }
	const TArray<FSimCopterMissionRecord>& GetRecords() const { return Records; }
	const FSimCopterMissionRecord* FindRecord(int32 EventId) const;
	const TArray<FSimCopterFlame>& GetFlames() const { return Flames; }
	int32 GetActiveFlameCount() const { return ActiveFlameCount; }
	const int32* GetCumulativeWeightTable() const { return CumulativeWeights; }

	// Exposed for parity tests.
	FSimCopterMsvcRand& GetRand() { return Rand; }
	void RebuildCumulativeWeights();                 // FUN_004a6d20
	static bool IsFireSuitableTile(int32 XbldId);    // FUN_004a5f60 tile test
	// FUN_004b2cd0's crash-site test: the tile will take a fire and there is none already burning
	// inside FUN_004a6860's spiral. A plane that hits a tile passing this starts a building fire
	// instead of becoming a mission itself.
	bool CanIgniteCrashSite(int32 TileX, int32 TileY) const;
	// FUN_004a92f0 LAB_004a95ff: the tile carries a building a mission can be attached to.
	// Medevac, Transport and every on-foot criminal are placed through it.
	static bool IsMissionBuildingTile(int32 XbldId);
	// FUN_004a92f0's param_1 == 1 loop: may a scheduled building fire start in this tile's
	// building, given the current difficulty? Tier 1 allows only 1x1 buildings and each tier up
	// admits bigger, occupied ones - the escalation the career's fire missions are built on.
	// Consumes a roll from Rand on tiers 2..4, exactly where the original does.
	bool IsBuildingFireTargetAllowedByDifficulty(int32 TileX, int32 TileY);
	static uint8 GetXbldPropertyFlags(int32 XbldId); // FUN_0049a4d0 record byte 0
	void RunSchedulerOnce();                         // FUN_004a6e60 body (post-cadence)

	// FUN_004a6e60's weighted roll with its countdown skipped: creates one mission from the current
	// city's weight vector right now. The original only reached the roll once DAT_00505fb4 had
	// counted down (180 seconds at session start, then the scaled Easy Interval), so this is how the
	// main menu hands the player a first job without the opening wait. Returns true when a mission
	// was created; a zero-weight city can never create one.
	bool RollScheduledMissionNow();

	// BOMB-side save payload. The original writes the live 30-record mission table, scheduler
	// globals and fire pools separately from CINF/CSET. Keep this explicit and pointer-free so a
	// city can be rebuilt first, then resume the exact simulation tick it was saved on.
	bool SerializeRuntimeState(FArchive& Archive);

private:
	ISimCopterMissionWorld* World = nullptr;
	FSimCopterMsvcRand Rand;
	FSimCopterCareerCity CareerCity;


	TArray<FSimCopterCareerCity> CareerCities;
	int32 CurrentCityIndex = 0;


	TArray<FSimCopterMissionRecord> Records;
	TArray<FSimCopterFlame> Flames;
	TArray<FSimCopterFireObject> FireObjects;

	// Career/session state (original globals).
	int32 Score = 0;                  // career base +0x50
	int32 Cash = 0;                   // career base +0x40
	bool bLevelComplete = false;
	bool bLevelCompleteSoundPlayed = false;
	int32 DifficultyTier = 1;         // DAT_004f9740 (city difficulty + 1)
	int32 CumulativeWeights[8] = {0}; // DAT_00581738[0..7]

	// Scheduler globals.
	int32 FrameDeltaEma = 0;          // DAT_005039a8 (16.16 seconds, *7+x>>3)
	int32 SpawnCountdown = 0xb40000;  // DAT_00505fb4 (.data initial 180.0s)
	int32 EasyIntervalCache = 0;      // DAT_00505fac
	int32 MaxEasyWithDifficulty = 0;  // DAT_00505fb8
	int32 ScaledMissionTimer = 0;     // DAT_00505fc8
	int32 NagInterval = 0;            // DAT_0057f998 (ScaledMissionTimer >> 3)
	int32 PercentRoll = 0;            // DAT_0057f9d4 (rand % 100, persists)
	int32 bRerollRequested = 1;       // DAT_00505fc0 (.data initial 1)
	int32 ConsecutivePlaceFailures = 0; // DAT_00506074
	int32 ActiveCount = 0;            // DAT_0057f9c8
	int32 BackgroundCount = 0;        // DAT_0057f9cc
	int32 NextEventId = 0;            // DAT_0057f9d0
	int32 TypeSerials[16] = {0};      // DAT_0057f9a0..DAT_0057f9c4 family counters (crimes/rescues share)
	int32 LifecyclePassCounter = 0;   // riot recenter cadence (every 13th pass)
	int32 FocusRecordIndex = INDEX_NONE; // DAT_0057f9d8 (current mission pointer)

	// Fire globals.
	int32 ActiveFlameCount = 0;       // DAT_00505f58
	int32 SpreadAccumulator = 0;      // DAT_00505f80

	// FUN_004a6e60 helpers.
	void UpdateSchedulerCadence();
	// The roll itself: reroll the percentage when asked, then pick the bucket it lands in.
	void DispatchWeightedRoll();
	void DispatchScheduledType(int32 Bucket);

	// FUN_004a92f0 helpers.
	bool TryPickRandomTileNearCamera(int32& OutX, int32& OutY); // FUN_004abb30
	void NoteCreationResult(bool bCreated);

	// FUN_004a7a10 helpers.
	int32 AllocateRecord();
	void DebugEnsureFreeRecordSlot();
	void ReleaseFailedRecord(int32 RecordIndex);
	int32 FindRecordIndex(int32 EventId) const;   // FUN_004a8890 walk
	void AnnounceCreated(const FSimCopterMissionRecord& Record); // trailer of FUN_004a7a10
	void PostAnnouncementVoice(const FSimCopterMissionRecord& Record); // FUN_004ab480
	static int32 GetTypeTextId(int32 TypeMask);   // shared 0x23b..0x24b switch
	bool FindDefaultDestinationTile(int32 OriginX, int32 OriginY, int32& OutX, int32& OutY) const;
	// Nearest hospital tile (XBLD id 0xD1 / HO209) to the origin - the medevac drop-off.
	bool FindNearestHospitalTile(int32 OriginX, int32 OriginY, int32& OutX, int32& OutY) const;

	// FUN_004a73e0 / FUN_004aabf0.
	void UpdateLifecycle();
	void CompleteMission(FSimCopterMissionRecord& Record); // FUN_004aabf0
	void DeactivateRecord(int32 RecordIndex);
	void PostNag(FSimCopterMissionRecord& Record, int32 NagCode);
	void PostTypedUiMessage(int32 Kind, const FSimCopterMissionRecord* Record, int32 EventId, int32 TextId, int32 ValueA, int32 ValueB, bool bNegative);

	// FUN_004aa150 incremental scoring (invoked from PostEvent).
	void PayIncremental(const FSimCopterMissionEvent& Event, int32 RecordIndex);

	// Fire simulation (FUN_004a4ac0 / FUN_004a48e0 / FUN_004a5340 / FUN_004a4fb0).
	int32 AllocateFireObject(int32 TileX, int32 TileY); // FUN_004a5080
	bool IgniteBuilding(int32 FireObjectIndex, int32 TileX, int32 TileY, int32 EventId, int32 Flags); // FUN_004a5340
	bool SpawnFlame(int32 FireObjectIndex, int32 TileX, int32 TileY, int32 OffsetX, int32 OffsetZ, int32 AxisFlag, int32 EventId, int32 Flags); // FUN_004a48e0
	void UpdateFires();                                  // FUN_004a4ac0
	void SpreadFireFrom(const FSimCopterFlame& Flame);   // FUN_004a4fb0
	void GrowFlame(int32 FlameIndex);                    // FUN_004a4ac0 growth branch
	void RemoveFlame(int32 FlameIndex, bool bDoused);
	// Shared tail of the expiry (FUN_004a4ac0) and douse (FUN_004a50c0) paths: drop the
	// flame from its fire object and, when that empties the building, move the mission
	// marker to a surviving flame of the same event.
	void RetireFlame(int32 FlameIndex, int32 EmptyEventCode);
	// FUN_004a48e0 / FUN_004a4ac0 spawn values, both difficulty-shifted.
	int32 GetFlameBurnTime() const { return (1 - DifficultyTier) * 0x140000 + Tuning.FireTimeToLive; }
	int32 GetFlameDouseHealth() const { return DifficultyTier * 0x14 + Tuning.FireDousePoints; }
	bool HasFlameOnTile(int32 TileX, int32 TileY) const;
	bool IsAnyFireNear(int32 TileX, int32 TileY) const;  // FUN_004a6860 spiral
};

} // namespace SimCopterMissions
