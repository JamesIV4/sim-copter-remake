// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// The original's one object-interaction router, ported from
//   FUN_0048ae70  square-spiral tile scan (mode 1 = 3 rings, mode 2 = 5 rings)
//   FUN_0049a4f0  object-class dispatch on obj[0xc]
//   FUN_004c1050  person reaction: DAT_0058d728[mode] -> a 900-series BHAV push
//   FUN_004c3010  the reaction table itself
//
// Evidence and the full table: Docs/scratchpad/ghidra/heli_tools_models_decode_20260724.md.
//
// Every tool that touches the world (spotlight, megaphone, water, tear gas, Apache weapons,
// and later emergency dispatch) goes through this one path instead of adding a bespoke
// radius query per tool (plan section 5.3).

class AActor;

// Interaction mode = the index into DAT_0058d728. Values are the executable's, not ours.
namespace ESimCopterInteractionMode
{
constexpr int32 Debris = 0;
constexpr int32 Spotlight = 1;
constexpr int32 Megaphone = 2;
constexpr int32 Missile = 3;
constexpr int32 Water = 4;
constexpr int32 TearGasCloud = 5;
constexpr int32 FireSparks = 6;
constexpr int32 MachineGun = 7;
constexpr int32 BoatHit = 8;
constexpr int32 VehicleHit = 9;
constexpr int32 PersonNeutral = 13;
constexpr int32 TearGasCanister = 14;
constexpr int32 Fall = 16;
constexpr int32 Count = 20;
}

// One routed interaction. Carries everything FUN_0049a4f0 passes down plus the remake's
// world handles, so a single struct serves every tool.
struct SIMCOPTERREMAKE_API FSimCopterInteractionEvent
{
	// Index into the reaction table.
	int32 Mode = ESimCopterInteractionMode::Spotlight;

	// FUN_0049a4f0 param_2: the object that caused the interaction (helicopter body,
	// spotlight node, projectile node). Used to reject self-interactions.
	AActor* Source = nullptr;

	// Tile the scan is centred on and the world point it corresponds to.
	FIntPoint TargetTile = FIntPoint(INDEX_NONE, INDEX_NONE);
	FVector TargetWorldLocation = FVector::ZeroVector;

	// FUN_0049a4f0 param_5: the megaphone message index, or the spotlight range band.
	// Stored at person+0x15a for mode 2 (this is how the shipped BHAV 901 picks a branch).
	int32 MessageIndex = 0;

	// FUN_0049a4f0 param_4: mission/scoring context (-1 when there is none).
	int32 MissionEventId = INDEX_NONE;

	// Water/projectile impact strength where the mode carries one.
	float ImpactStrength = 0.0f;
};

namespace SimCopterInteraction
{
// FUN_0048ae70: run length reached before the walk stops.
constexpr int32 SpotlightRings = 3;
constexpr int32 MegaphoneRings = 5;

// DAT_0058d728[Mode]; INDEX_NONE for the 0xffff entries.
SIMCOPTERREMAKE_API int32 GetPersonReactionProgram(int32 Mode);

// FUN_004c1050 mode 1 hard-codes BHAV 950 instead of using the table.
constexpr int32 SpotlightReactionProgram = 950;

// BHAV 904 "Rxn: Run away (dir already set)" - runs on whatever facing the caller set first.
// Used by the remake to clear people out from under a descending helicopter.
constexpr int32 RunAwayReactionProgram = 904;

// FUN_004c1050's interrupt priority: 903 (Die), 915 (Missile/bullet), 912 (Large fast
// vehicle hit) and 909 (Fall) cannot be displaced by a lesser reaction while one is active.
SIMCOPTERREMAKE_API bool IsHighPriorityReaction(int32 ProgramId);
SIMCOPTERREMAKE_API bool ReactionCanInterrupt(int32 NewProgramId, int32 CurrentProgramId);

// Exact port of FUN_0048ae70's square spiral. Visit order and the tiles it skips are the
// original's, quirks included: the walk stops mid-leg on the ring where the run length
// reaches Rings, so mode 1 covers 8 tiles and mode 2 covers 24.
SIMCOPTERREMAKE_API void BuildSpiralTiles(
	const FIntPoint& CenterTile,
	int32 Rings,
	TArray<FIntPoint>& OutTiles);

// Rings for a given mode; 0 when the mode does not spiral (FUN_0048ae70 returns early for
// anything other than 1 and 2).
SIMCOPTERREMAKE_API int32 GetSpiralRingsForMode(int32 Mode);
}
