// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Formats/SimCopterPeopleReader.h"

// Interpreter for SimCopter's people.df BHAV behavior programs - a faithful port of the
// original's walker VM (FUN_004ce7b0 + the 88-handler table at 0x4c84e0; advance semantics
// from FUN_004ce8f0). The remake executes the *shipped* programs verbatim; opcode semantics
// come from the decompiled handlers (Docs/scratchpad/ghidra/out_vm_ops_*.txt).
//
// Execution model per original:
//  * A person runs one program per state (FPeopleBehaviorModel::GetStateProgramIds).
//  * The walk stack holds up to 12 frames; a record token >= 0x100 CALLS that BHAV id.
//  * Each frame carries 10 u16 locals; person attributes are a u16 block (original +0x140..).
//  * Handler results: False/True pick the record's false/true edge; Yield re-runs the same
//    record next tick (waits/movement); Stop ends behavior (despawn paths).
//  * Edge -2 returns TRUE from the program, -1 returns FALSE (pops the stack; the caller
//    resumes at its call record's edges).

enum class EBhavStepResult : uint8
{
	Ran,        // consumed the tick (yield) - resume same spot next tick
	Completed,  // top-level program returned; will restart next tick
	Stopped,    // behavior asked to stop (e.g. despawn)
	Failed      // missing program / runaway loop - caller should fall back
};

// Well-known person attribute slots (u16 block at original person+0x140; resolver scope 3).
namespace EBhavAttr
{
	constexpr int32 Facing = 0;      // +0x140, 0..7 (45-degree steps)
	constexpr int32 BehaviorClass = 3; // +0x146 (indexes the allowed-tile-class table)
	constexpr int32 State = 4;       // +0x148
	constexpr int32 LoopFlag = 5;    // +0x14a
	constexpr int32 Frame = 6;       // +0x14c
	constexpr int32 Speed = 8;       // +0x150, 0..10
	constexpr int32 Visible = 9;     // +0x152
	constexpr int32 Count = 0x30;
}

struct FSimCopterPersonContext
{
	static constexpr int32 MaxStackDepth = 12;   // FUN_004ce630(0xc,...)
	static constexpr int32 LocalsPerFrame = 10;  // handler local addressing (slot+cursor*10)

	struct FFrame
	{
		int32 ProgramId = 0;
		int32 RecordIndex = 0;
		uint16 Locals[LocalsPerFrame] = {};
	};

	TArray<FFrame> Stack;
	uint16 Attributes[EBhavAttr::Count] = {};
	uint16 Lfsr = 0x2a2a; // people PRNG state (FUN_004ce9d0, 16-bit LFSR tap 0x1bf5)

	// Outputs the world/agent consumes after each tick:
	FString PendingAnimMnemonic; // op1 bind requests ("1Wal", "NoMo", "Wave", ...)
	bool bRequestDespawn = false;

	int32 GetStateIndex() const { return Attributes[EBhavAttr::State]; }
	void ResetToState(int32 StateIndex);

	// The original's dedicated behavior PRNG: LFSR with tap 0x1bf5 (FUN_004ce9d0), modulo
	// helper FUN_004cea00.
	uint16 RandomRaw();
	uint16 RandomBounded(uint16 Bound);
};

// World queries/actions the opcode handlers need; implemented by the owning agent.
class ISimCopterBehaviorWorld
{
public:
	virtual ~ISimCopterBehaviorWorld() = default;

	// Original tile class of the tile the person stands on (FUN_004c9220 classes; 7 = the
	// walkable road/sidewalk class, see FUN_004c3010's per-state allowed lists).
	virtual int32 GetCurrentTileClass() const = 0;
	// Membership of a class in the state's allowed list (DAT_0058d750 rows).
	virtual bool IsTileClassAllowedForState(int32 StateIndex, int32 TileClass) const = 0;
	// Advance one movement step along Attributes[Facing] at Attributes[Speed].
	// Returns false when blocked (walker follows the false edge / retries).
	virtual bool MoveStep(FSimCopterPersonContext& Context) = 0;
	// The original FUN_004c9bc0: is the player helicopter (or a threat) close/low?
	virtual bool IsThreatNearby(const FSimCopterPersonContext& Context) const = 0;

	virtual void OnUnknownOpcode(int32 Opcode) {}
};

class SIMCOPTERREMAKE_API FSimCopterBehaviorVM
{
public:
	// The per-state allowed tile classes (FUN_004c3010's DAT_0058d750 rows; 10 entries each,
	// 0-terminated). Default row = {13,11,10,12,7}; state 1/15 = {2}; state 2 = {13,11,10,12};
	// state 3 = {7}.
	static const TArray<int32>& GetAllowedTileClasses(int32 StateIndex);

	// Runs one behavior tick (the original ran this once per person per sim tick, LOD-gated).
	static EBhavStepResult Tick(
		FSimCopterPersonContext& Context,
		const FPeopleBehaviorModel& Model,
		ISimCopterBehaviorWorld& World);
};
