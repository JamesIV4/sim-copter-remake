// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Speeder / criminal cars and the police pursuit that catches them.
//
// Decoded from SimCopter.exe 2026-07-26. The loop the original builds is:
//
//   1. A speeder drives the road network at 1.75x the normal traffic speed.
//   2. The player holds the searchlight on it. Every tick inside the cone the car's "marked"
//      counter climbs by 2 to a cap of 10, and while it is above zero the car's speed multiplier
//      collapses to 1.05x / 1.32x / 1.52x by spotlight band - the tighter the beam, the slower
//      the car. Letting the beam off resets the counter outright.
//   3. The player dispatches police. An arrived police car sweeps three rings around itself for
//      a speeder and orders the nearest one to pull over.
//   4. The order only sticks if the car has been marked. A marked car halts, an unmarked one
//      keeps going - which is what makes the spotlight the actual gameplay verb rather than
//      decoration.
//   5. Once stopped, an officer gets out, the mission record is closed, and the car is removed.
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
//   FUN_004b8b60  the arrest: siren, deploy a person (mode 0xf state 0xd), close the mission
//                 record with EVT_SetCategory value 4, hold 120 s, despawn.
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

// FUN_004b8b60: veh[0x10] = 0x780000 before the car is removed - and FUN_004b8c90, which runs
// when that expires, is what posts the criminal-caught event. So the payout lands at the end of
// this hold, not the moment the car stops.
//
// FUN_004b8630 case 3 has a second exit, veh[8] != 0, and it is confirmed unreachable for a
// speeder: veh[8]'s only writer anywhere in the binary is FUN_0049aed0, which is reached only
// from people-VM opcode 60, and across all 137 shipped BHAV programs that opcode appears twice -
// in "Rioter maybe throw" and "crim - arsonist unspotted", neither of which the arrest's
// deployed person runs. So the full hold really is the only path here.
//
// The remake pays the mission out when the car stops rather than when this expires, so the hold
// is only how long the stopped car stays parked before it is cleared away.
constexpr float ArrestHoldSeconds = 120.0f;

// Paying out immediately retires the mission record, which would take its world tag with it in
// the same frame. Keep the green tag up this long afterwards so the stop actually registers.
constexpr float ArrestTagLingerSeconds = 3.0f;

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

// FUN_004b8b60's stopped-car person: FUN_0049bd00(0xf, 0xd). This pair is specific to the
// criminal car; the ambulance vtable method FUN_004b8f60 deploys class 0x0c / state 5.
constexpr int32 ArrestPersonSpawnMode = 0xf;
constexpr int32 ArrestPersonState = 0xd;

// FUN_004b9e40's officer deploy: spawn mode 0xe, and the state depends on the target.
constexpr int32 OfficerSpawnMode = 0xe;
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
	// 1: the pause the cruise timer drops it into.
	Idling = 1,
	// 2: marked - the siren is up and the car is running from the spotlight.
	Fleeing = 2,
	// 3: arrested, holding for ArrestHoldSeconds before it is removed.
	Arrested = 3,
	// 4: leaving (the arrest could not place an officer, so the record is closed anyway).
	Leaving = 4,
	// 5: pulling over.
	Stopping = 5,
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
