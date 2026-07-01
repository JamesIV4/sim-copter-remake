// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Pure parser for SimCopter's `X/people.df` - the "global behavior file".
//
// Same Doug container as privanim.df (see SimCopterPrivAnimReader). Two sections, 137 entries
// each: `BHAV` behavior programs and `POSI` (editor graph layout, ignored). Programs are
// authored visual graphs run by the original's walker VM; the remake interprets the same
// records verbatim (FSimCopterBehaviorVM).
//
// BHAV payload: [BE u16 recordCount] + count x 12-byte records at payload+2.
// Record: [BE u16 token][s8 trueNext][s8 falseNext][4 x BE u16 args].
//   token <  0x100 : opcode (dispatched to a handler; see EBhavOp / the VM)
//   token >= 0x100 : CALL the BHAV program with that entry id; the caller resumes at
//                    trueNext/falseNext with the callee's boolean result.
//   next -2 = return TRUE from this program, -1 = return FALSE, else = record index.
// Ground truth: Docs/OriginalGameFileFormats.md ("people.df"), decompiles in
// Docs/scratchpad/ghidra/out_vm_*.txt; e.g. "Idle-5" = [local0 := 5][wait local0].
struct FBhavRecord
{
	uint16 Token = 0;
	int8 TrueNext = 0;
	int8 FalseNext = 0;
	uint16 Args[4] = {0, 0, 0, 0};
};

struct FBhavProgram
{
	int32 Id = 0;
	FString Name;
	TArray<FBhavRecord> Records;
};

struct FPeopleBehaviorModel
{
	TMap<int32, FBhavProgram> ProgramsById;

	const FBhavProgram* FindProgram(int32 ProgramId) const { return ProgramsById.Find(ProgramId); }

	// The original's per-state program table DAT_0058de80 (FUN_004c3010), states 0..0x14.
	static const TArray<int32>& GetStateProgramIds();
	// Per-state anim loop flag (person+0x14a): -2 default, 0 for states 3/10/11/12/13, 1 for 7/8.
	static int16 GetStateLoopFlag(int32 StateIndex);
};

class SIMCOPTERREMAKE_API FSimCopterPeopleReader
{
public:
	static FString ResolvePeoplePath(const FString& OriginalGameRoot);
	static bool LoadFromFile(const FString& FilePath, FPeopleBehaviorModel& OutModel, FString& OutError);
	static bool LoadFromBytes(const TArray<uint8>& FileData, FPeopleBehaviorModel& OutModel, FString& OutError);
};
