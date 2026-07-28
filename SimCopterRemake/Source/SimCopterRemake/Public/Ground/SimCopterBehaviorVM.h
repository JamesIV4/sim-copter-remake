// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Formats/SimCopterPeopleReader.h"
#include "UObject/WeakObjectPtrTemplates.h"

class AActor;

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
	constexpr int32 Speed = 8;       // +0x150, 0..10 (op23's "logic" speed; compared by programs)
	constexpr int32 Visible = 9;     // +0x152
	constexpr int32 PreviousSpeed = 10; // +0x154, written by op23 before speed changes
	// The actual per-tick move magnitude: FUN_004ca7d0 (op4) passes +0x164 into the move core,
	// and the shipped programs assign it directly via expressions ("movespeed := 6/8/12/16/25").
	// One tick's displacement is octantDir * MoveSpeed / 12 original units (FUN_004c3010 builds
	// the direction table as unit vectors fixed-divided by 0xc0000).
	constexpr int32 MoveSpeed = 18;  // +0x164
	constexpr int32 AmbientFlag = 20; // +0x168; gates the DAT_0058ec00 class-row move check
	constexpr int32 AutoTurn = 21;   // +0x16a; enables the move core's 8-facing retry loop
	constexpr int32 MoveThroughWalls = 40; // +0x190; BHAV 308 sets it after repeated move fails
	constexpr int32 MoveFailCounter = 41;  // +0x192
	// +0x16e. Set by BHAV 1060 "Rx: criminal-caught" and cleared by the criminal root programs.
	// FUN_004ca350 refuses to return a person with this set when it is asked for object class 6,
	// which is how an arrested criminal stops being a target for every other cop on the map.
	constexpr int32 CriminalCaught = 23;
	constexpr int32 Count = 0x30;
}

// FUN_004cac70's object-class table: what op 15 will search for. Values are the executable's.
// Full derivation in Docs/scratchpad/ghidra/criminal_ai_decode_20260728.md.
namespace EBhavObjectClass
{
	constexpr int32 MyMissionCoords = 0;  // FUN_004a88e0(person+0x10a) - the mission's tile
	constexpr int32 AlreadySelected = 1;  // whatever is already in the selected slot
	constexpr int32 PlayerHelicopter = 2; // DAT_005040d0+0xa4
	constexpr int32 PlayerObjectB = 3;    // DAT_005040d0+0xbc
	constexpr int32 InteractionSource = 4;// person+0x1a4 - what last interacted with me
	constexpr int32 MedevacVictim = 5;    // FUN_004ca350 state == 6
	constexpr int32 UncaughtCriminal = 6; // FUN_004ca350 loopflag == 0 and CriminalCaught == 0
	constexpr int32 PoliceOfficer = 8;    // FUN_004ca350 loopflag == 1 (states 7 and 8)
	constexpr int32 FireTruck = 10;       // FUN_0049b060(0, tile)
	constexpr int32 PoliceCar = 11;       // FUN_0049b060(1, tile)
	constexpr int32 Ambulance = 12;       // FUN_0049b060(2, tile)
	constexpr int32 Civilian = 14;        // FUN_004ca350 state == 0
	constexpr int32 PlayerObjectC = 16;   // DAT_005040d0+0xc0
}

// FUN_004ca940's outcome, as op 38 reads it.
enum class ESimCopterBehaviorStepResult : uint8
{
	NoTarget, // nothing selected, or no bearing to it
	Moving,   // took a step; not there yet
	Arrived   // standing on/next to the target
};

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
	uint16 Lfsr = 0x2a2a; // people PRNG state (FUN_004ce9d0, left-shift xor tap 0x1bf5)

	// Outputs the world/agent consumes after each tick:
	FString PendingAnimMnemonic; // op1 bind requests ("1Wal", "NoMo", "Wave", ...)
	bool bRequestDespawn = false;

	// The walker's "current runtime object" slot (original walker record +0x04). Op 15 selects
	// into it; ops 18/38/39 act on whatever is in it. Kept as an actor handle so the VM needs no
	// knowledge of what kind of thing it found.
	TWeakObjectPtr<AActor> SelectedObject;

	// Interrupt reaction state, mirroring FUN_004c1050's writes:
	//   person+0x17c  the 900-series reaction id currently running (INDEX_NONE = none)
	//   person+0x158  the interaction's param_5
	//   person+0x15a  the megaphone message index (mode 2 only)
	//   person+0x1a4  the object that caused the interaction, kept as a plain id/handle here
	int32 ActiveReactionProgramId = INDEX_NONE;
	int32 ReactionParameter = 0;
	int32 MegaphoneMessageIndex = INDEX_NONE;

	int32 GetStateIndex() const { return Attributes[EBhavAttr::State]; }
	void ResetToState(int32 StateIndex);

	// FUN_004c1050's tail: push a reaction BHAV onto the walk stack so it runs before the
	// person's normal program resumes. Returns false when the stack is full (the original
	// pops a frame first) or when the reaction is refused by the priority rules.
	bool PushReactionProgram(int32 ProgramId);

	// Called when a pushed reaction has run to completion.
	void ClearActiveReaction() { ActiveReactionProgramId = INDEX_NONE; }

	// The original's dedicated people PRNG: FUN_004ce9d0 plus modulo helper FUN_004cea00.
	uint16 RandomRaw();
	uint16 RandomBounded(uint16 Bound);
};

struct FSimCopterBehaviorPlayerTileProbe
{
	int32 FileX = INDEX_NONE;
	int32 FileY = INDEX_NONE;
	uint16 Speed = 0;
	uint16 Facing = 0;
};

// World queries/actions the opcode handlers need; implemented by the owning agent.
class ISimCopterBehaviorWorld
{
public:
	virtual ~ISimCopterBehaviorWorld() = default;

	// Original tile class of the tile the person stands on (FUN_004c9220 classes; 7 = the
	// walkable road/sidewalk class, see FUN_004c3010's per-state allowed lists).
	virtual int32 GetCurrentTileClass() const = 0;
	virtual bool TryGetCurrentTileCoordinate(int32& OutFileX, int32& OutFileY) const { return false; }
	// Membership of a class in the movement/spawn-mode allowed list (DAT_0058d750 rows).
	virtual bool IsTileClassAllowedForState(int32 StateIndex, int32 TileClass) const = 0;
	// Advance one movement step along Attributes[Facing] at Attributes[Speed].
	// Returns false when blocked (walker follows the false edge / retries).
	virtual bool MoveStep(FSimCopterPersonContext& Context) = 0;
	// The original FUN_004c9bc0: is the player helicopter (or a threat) close/low?
	virtual bool IsThreatNearby(const FSimCopterPersonContext& Context) const = 0;
	// Original opcode 22 probe: FUN_00489610 tile globals plus DAT_005040d0 speed/facing fields.
	virtual bool TryGetPlayerTileProbe(
		const FSimCopterPersonContext& Context,
		FSimCopterBehaviorPlayerTileProbe& OutProbe) const { return false; }

	// Op 15, FUN_004cac70: find the nearest object of EBhavObjectClass, put it in
	// Context.SelectedObject and report its Chebyshev tile distance. False when the class is not
	// supported here or nothing matched - the opcode then writes the original's 2000 sentinel.
	virtual bool SelectObjectOfClass(FSimCopterPersonContext& Context, int32 ObjectClass, int32& OutTileDistance)
	{
		return false;
	}

	// Op 18, FUN_004cb270: snap facing to the selected object's octant (bearing - 2 & 7).
	virtual bool FaceSelectedObject(FSimCopterPersonContext& Context) { return false; }

	// Op 38, FUN_004ca940: face the selected object and take one move step toward it.
	virtual ESimCopterBehaviorStepResult StepTowardSelectedObject(FSimCopterPersonContext& Context)
	{
		return ESimCopterBehaviorStepResult::NoTarget;
	}

	// Op 39, FUN_004cc560: push a BHAV onto the *selected person's* walk stack. This is how a cop
	// arrests a criminal - it pushes BHAV 1060 "Rx: criminal-caught" onto them.
	virtual bool PushReactionOnSelectedObject(FSimCopterPersonContext& Context, int32 ProgramId) { return false; }

	// Op 13, FUN_004caac0 -> FUN_004ccf50: turn a program outcome code into a mission event on
	// the record this person belongs to. Outcome 6 re-posts the person's own tile as the mission
	// coordinates (which is what makes a criminal's marker follow them); outcome 9 is the arrest.
	virtual void PostMissionOutcome(FSimCopterPersonContext& Context, int32 OutcomeCode) {}

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
