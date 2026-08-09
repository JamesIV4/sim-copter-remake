// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Burglar getaway cars, recurring robberies, and the police pursuit that catches them.
//
// Decoded from SimCopter.exe 2026-07-26. The loop the original builds is:
//
//   1. A CARROBBR getaway car drives the road network at 1.75x normal traffic speed.
//   2. The player holds the searchlight on it. Every tick inside the cone the car's "marked"
//      counter climbs by 2 to a cap of 10, and while it is above zero the car's speed multiplier
//      collapses to 1.05x / 1.32x / 1.52x by spotlight band - the tighter the beam, the slower
//      the car. Letting the beam off resets the counter outright.
//   3. The player dispatches police. An arrived police car sweeps three rings around itself for
//      a speeder and orders the nearest one to pull over.
//   4. The order only sticks if the car has been marked. A marked car halts, an unmarked one
//      keeps going - which is what makes the spotlight the actual gameplay verb rather than
//      decoration.
//   5. Once stopped, the burglar gets out and runs BHAV 1303 -> 1079 to commit a burglary.
//      If uncaught, they message the same starting car through opcode 61 and the cycle repeats.
//      If they do not return inside 120 seconds, FUN_004b8c90 posts CriminalCaught and ends it.
//
// Functions:
//
//   FUN_004b8470  the criminal car class. Message id 0x11e, GEO object 0x11e = CARROBBR
//                 ("badguy"). FUN_00479bb0 preallocates the pool five at a time into
//                 DAT_00582b08, so a city holds at most five.
//   FUN_004b8540  the spawner: take a free pool slot, place it with FUN_0049cf10 (a radius-5
//                 road search that refuses bridge decks for this id), record the mission event
//                 id at +0x113 and arm the cruise timer at +0x137.
//   FUN_004b8630  the per-tick state machine (vtable[4]).
//   FUN_004b89a0  vtable[1] - "pull over", the call a police car makes on its target.
//   FUN_0049e0c0  the shared halt the above defers to: set the stop distance, raise the
//                 stopping flag, clear the moving flags.
//   FUN_004b8b60  stop sequence: door sounds, deploy behavior class 0xf / person state 0xd,
//                 then wait 120 s for that burglar to return.
//   FUN_004b8c90  timeout = caught; nonzero opcode-61 message = drive away and burgle again.
//   FUN_0049dab0  the police target filter.
//   FUN_004a01f0  the spotlight mark accumulator (interaction mode 1).
//   FUN_0049d980  the speed multiplier the mark feeds.
//   FUN_004b9e40  case 0 - the arrived police car's three-ring sweep and stop order.
//   FUN_0049df60  the "may I stop here" gate (already ported as
//                 SimCopterDispatch::IsIntersectionTileId plus the occupancy test).
namespace SimCopterCriminalCar
{
// Runtime message ids. These double as GEO object ids - the body mesh IS the message id.
constexpr int32 CriminalCarMessageId = 0x11e; // CARROBBR "badguy"
constexpr int32 PoliceCarMessageId = 0x11d;   // CARPOLIC "popo"

// FUN_00479bb0 allocates the criminal-car pool in one run of five.
constexpr int32 PoolCapacity = 5;

// FUN_004a01f0: +2 per tick, and the accumulator stops climbing at 10.
constexpr int32 SpotlightMarkStep = 2;
constexpr int32 SpotlightMarkMax = 10;

// FUN_004b9e40 case 0: FUN_004beda0(3) rings, and the hit is only taken when
// FUN_0049b000(policeTile, targetTile) reports fewer than 3 steps.
constexpr int32 PursuitScanRings = 3;
constexpr int32 PursuitMaxTileSteps = 3;

// Initialized-data values read directly from the retail Ghidra project.
constexpr int32 CruiseBaseDelay1616 = 0x640000;     // DAT_00506360, 100.0 s
constexpr int32 CruiseDelayModulo1616 = 0x2580000;  // DAT_00506364, 600.0 s divisor
constexpr float SpotlightLostCooldownSeconds = 20.0f; // DAT_00506368 = 0x140000
constexpr float BurglarOutsideSeconds = 120.0f;       // veh[0x10] = 0x780000

// FUN_0049dbb0 authors every vehicle's road speed when it is placed:
//   veh[0xc3] = (rand() & 7) + 0x24  -> 36..43
//   veh[0xc7] = (rand() & 7) + 0x28  -> 40..47
// FUN_0049be50 advances the car by speed * frameDelta, so these are original units per second.
// FUN_0049d980 picks between the two per tick and applies the fleeing multiplier on top.
constexpr int32 RoadSpeedMinUnitsPerSecond = 36;
constexpr int32 RoadSpeedMaxUnitsPerSecond = 47;

// The helicopter's airspeed ceiling, for the relationship that makes a pursuit winnable: the
// flight model uses tenth-degrees of pitch directly as units/s, so the default airframe's
// MaxPitch of 192.3 is also its top speed. A speeder at the fastest base and the full 1.75x
// multiplier has to stay under this or nothing can ever follow one.
constexpr float HelicopterTopSpeedUnitsPerSecond = 192.3f;

// FUN_004b8b60 calls FUN_0049bd00(behavior class 0xf, person state 0xd). TrySpawnMissionPerson's
// public order is state first, class second, so keep the names explicit and do not swap them.
constexpr int32 BurglarPersonState = 0xd;
constexpr int32 BurglarBehaviorClass = 0xf;

// FUN_004b9e40 calls FUN_004bd980(0xe, state): 0xe is the behavior class, not a spawn mode.
// Class 14 carries the cop's walk-surface rules while the state below selects its BHAV.
constexpr int32 OfficerBehaviorClass = 0xe;
// Person states, i.e. BHAV 1401 "Cop foot" and BHAV 1402 "Cop speeder".
constexpr int32 OfficerStateDefault = 8;
constexpr int32 OfficerStateAgainstFleeing = 0xe;
// privanim figure "Kopp" - the uniformed officer in the shipped figure set.
inline const TCHAR* OfficerFigureName = TEXT("Kopp");

// FUN_004b8630's switch, in its own numbering.
enum class EState : uint8
{
	// 0: cruising. Watches for the mark and for a stop it can make.
	Cruising = 0,
	// 1: the cruise timer expired; pull over at the next valid road position to burgle.
	SeekingBurglaryStop = 1,
	// 2: marked - the siren is up and the car is running from the spotlight.
	Fleeing = 2,
	// 3: the burglar is outside; wait up to BurglarOutsideSeconds for opcode 61.
	BurglarOutside = 3,
	// 4: person placement failed; leave and retire the mission silently.
	Leaving = 4,
	// 5: pulling over.
	Stopping = 5,
};

// FUN_004b8b60 / FUN_004b8c90 gate person deployment and a successful return on the shared
// aDrOpen/aDrClose sound slots. The executable stores these as flags at +0x133..+0x136; keeping a
// compact phase makes the same asynchronous path explicit on the Unreal actor.
enum class EDoorPhase : uint8
{
	None = 0,
	OpeningForBurglary,
	ClosingForBurglary,
	OpeningForReturn,
	ClosingForReturn,
};

// FUN_0049dab0. A police car hunts the criminal car by id, or anything flying the fleeing flag -
// which is how a speeder *person* is a valid target for the same code.
SIMCOPTERREMAKE_API bool IsPursuitTarget(int32 MessageId, bool bFleeing);

// FUN_004a01f0. bLit is "inside the cone this tick"; the counter climbs by 2 to the cap, and a
// tick with the spotlight off (DAT_00503aa0 == 3) resets it to zero rather than decaying it.
SIMCOPTERREMAKE_API int32 AccumulateSpotlightMark(int32 Current, bool bLit, bool bSpotlightActive);

// FUN_004a01f0's range test: the horizontal distance from the car to the beam's ground point,
// against the band's own radius. Band is SimCopterSpotlight's 0..2; anything else is out of range.
SIMCOPTERREMAKE_API float GetSpotlightMarkRadiusOriginalUnits(int32 Band);

// FUN_0049d980. A fleeing car runs at 1.75x normal, and every tick it is marked that drops to
// the band's own multiplier - the tightest beam slows it most. Cars that are not fleeing are
// unaffected and return 1.
SIMCOPTERREMAKE_API float GetFleeingSpeedMultiplier(bool bFleeing, int32 SpotlightMark, int32 Band);

// FUN_004b89a0, the pull-over. States 3 and 4 ignore the order outright. A police caller carries
// it when the car has been marked, whatever state it is in. Otherwise only a cruising-paused or
// already-pulling-over car takes it, and only if it is not stopping already.
SIMCOPTERREMAKE_API bool AcceptsStopOrder(
	EState State,
	int32 SpotlightMark,
	int32 CallerMessageId,
	bool bAlreadyStopping);

// FUN_004b9e40: the officer's state is 0xe when the car it just stopped was fleeing, 8 otherwise.
SIMCOPTERREMAKE_API int32 GetOfficerPersonState(bool bHasTarget, bool bTargetFleeing);

// FUN_0049b000's octile step count, which the pursuit compares against PursuitMaxTileSteps.
SIMCOPTERREMAKE_API int32 GetTileStepDistance(const FIntPoint& A, const FIntPoint& B);
}
