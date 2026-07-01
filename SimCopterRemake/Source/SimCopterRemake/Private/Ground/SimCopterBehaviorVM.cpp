// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ground/SimCopterBehaviorVM.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCopterBehaviorVM, Log, All);

namespace
{
// Handler result codes, matching the original walker's dispatch (FUN_004ce7b0 switch).
enum class EOpResult : uint8
{
	False = 0,
	True = 1,
	Yield = 2,
	Stop = 3
};

// Operand resolver, port of FUN_004cd2e0. Returns the value; optionally exposes a writable
// slot for assignment operators.
uint16 ResolveOperand(FSimCopterPersonContext& Context, int32 Scope, uint16 Id, uint16** OutSlot)
{
	if (OutSlot != nullptr)
	{
		*OutSlot = nullptr;
	}
	switch (Scope)
	{
	case 7: // literal
		return Id;
	case 9: // current-frame local
		if (Context.Stack.Num() > 0 && Id < FSimCopterPersonContext::LocalsPerFrame)
		{
			uint16* Slot = &Context.Stack.Last().Locals[Id];
			if (OutSlot != nullptr)
			{
				*OutSlot = Slot;
			}
			return *Slot;
		}
		return 0;
	case 3: // person attribute block (original person+0x140 + id*2)
		if (Id < EBhavAttr::Count)
		{
			uint16* Slot = &Context.Attributes[Id];
			if (OutSlot != nullptr)
			{
				*OutSlot = Slot;
			}
			return *Slot;
		}
		return 0;
	default: // FUN_004cd2e0 cases 0/1/4/8/10/0xb return 0
		return 0;
	}
}

// Opcode 2 - the expression engine, port of FUN_004cd0d0.
// args[0] = target id, args[1] = source id/literal, args[2] = operator, args[3] = scope pair
// (hi byte = target scope, lo byte = source scope). Verified against the shipped programs:
// "Idle-5" record 0 is args {0, 5, 5(:=), 0x0907} = local0 := 5.
EOpResult ExecExpression(FSimCopterPersonContext& Context, const FBhavRecord& Record)
{
	const int32 TargetScope = Record.Args[3] >> 8;
	const int32 SourceScope = Record.Args[3] & 0xff;
	uint16* TargetSlot = nullptr;
	const uint16 TargetValue = ResolveOperand(Context, TargetScope, Record.Args[0], &TargetSlot);
	const uint16 SourceValue = ResolveOperand(Context, SourceScope, Record.Args[1], nullptr);

	switch (Record.Args[2])
	{
	case 0: return int16(TargetValue) > int16(SourceValue) ? EOpResult::True : EOpResult::False;
	case 1: return int16(TargetValue) < int16(SourceValue) ? EOpResult::True : EOpResult::False;
	case 2: return TargetValue == SourceValue ? EOpResult::True : EOpResult::False;
	case 3: if (TargetSlot) { *TargetSlot = TargetValue + SourceValue; } return EOpResult::True;
	case 4: if (TargetSlot) { *TargetSlot = TargetValue - SourceValue; } return EOpResult::True;
	case 5: if (TargetSlot) { *TargetSlot = SourceValue; } return EOpResult::True;
	case 6: if (TargetSlot) { *TargetSlot = TargetValue * SourceValue; } return EOpResult::True;
	case 7:
		if (TargetSlot && SourceValue != 0) { *TargetSlot = uint16(int16(TargetValue) / int16(SourceValue)); }
		return EOpResult::True;
	case 8: // := random(source) (the tail cases of FUN_004cd0d0 use the people PRNG)
		if (TargetSlot) { *TargetSlot = Context.RandomBounded(FMath::Max<uint16>(SourceValue, 1)); }
		return EOpResult::True;
	default:
		if (TargetSlot) { *TargetSlot = SourceValue; }
		return EOpResult::True;
	}
}

FString MnemonicFromArgs(const FBhavRecord& Record)
{
	// Record bytes +4..+7 hold a 4-char anim mnemonic ("NoMo", "2Gab", ...); args are BE u16s,
	// so each arg contributes its high then low byte.
	FString Out;
	for (int32 ArgIndex = 0; ArgIndex < 2; ++ArgIndex)
	{
		Out.AppendChar(TCHAR(Record.Args[ArgIndex] >> 8));
		Out.AppendChar(TCHAR(Record.Args[ArgIndex] & 0xff));
	}
	return Out;
}

EOpResult ExecOpcode(
	FSimCopterPersonContext& Context,
	const FBhavRecord& Record,
	ISimCopterBehaviorWorld& World)
{
	FSimCopterPersonContext::FFrame& Frame = Context.Stack.Last();
	auto Local = [&Frame](uint16 Slot) -> uint16& {
		return Frame.Locals[FMath::Min<uint16>(Slot, FSimCopterPersonContext::LocalsPerFrame - 1)];
	};

	switch (Record.Token)
	{
	case 0: // wait: local counter ticks down; True when exhausted (FUN_004ca750)
	{
		uint16& Counter = Local(Record.Args[0]);
		if (Counter == 0)
		{
			return EOpResult::True;
		}
		--Counter;
		return EOpResult::Yield;
	}
	case 1: // bind animation by mnemonic (FUN_004ca7a0 -> FUN_004c68f0)
		Context.PendingAnimMnemonic = MnemonicFromArgs(Record);
		return EOpResult::True;
	case 2: // expression engine (FUN_004ca7c0 -> FUN_004cd0d0)
		return ExecExpression(Context, Record);
	case 3: // timed move: walk along facing while local counter runs (FUN_004ca7d0)
	{
		uint16& Counter = Local(Record.Args[0]);
		if (Counter == 0)
		{
			return EOpResult::True;
		}
		--Counter;
		return World.MoveStep(Context) ? EOpResult::Yield : EOpResult::False;
	}
	case 4: // bind figure model by id (FUN_004ca860) - the remake picks figures visually
		return EOpResult::True;
	case 5: // local0 := random(resolved operand) (FUN_004ca8e0)
	{
		const uint16 Bound = ResolveOperand(Context, Record.Args[3] & 0xff, Record.Args[1], nullptr);
		Local(0) = Context.RandomBounded(FMath::Max<uint16>(Bound, 1));
		return EOpResult::True;
	}
	case 10: // deactivate person (FUN_004cb180 -> FUN_004c4e40; result 3 = stop)
		Context.bRequestDespawn = true;
		return EOpResult::Stop;
	case 11: // threat response probe (FUN_004cb190)
		return World.IsThreatNearby(Context) ? EOpResult::True : EOpResult::False;
	case 13: // tile class == arg (FUN_004cb2c0 -> FUN_004c9220)
		return World.GetCurrentTileClass() == int32(Record.Args[0]) ? EOpResult::True : EOpResult::False;
	case 14: // tile class allowed for my behavior class (FUN_004cb300, DAT_0058d750 rows)
		return World.IsTileClassAllowedForState(
			Context.Attributes[EBhavAttr::BehaviorClass], World.GetCurrentTileClass())
			? EOpResult::True : EOpResult::False;
	case 15: // threat nearby? (FUN_004cb360 -> FUN_004c9bc0)
		return World.IsThreatNearby(Context) ? EOpResult::True : EOpResult::False;
	case 17: // speed += arg, clamped 0..10 (FUN_004cb420; person+0x150)
	{
		const int32 NewSpeed = FMath::Clamp<int32>(
			int32(int16(Context.Attributes[EBhavAttr::Speed])) + int16(Record.Args[0]), 0, 10);
		Context.Attributes[EBhavAttr::Speed] = uint16(NewSpeed);
		return EOpResult::True;
	}
	default:
		// Not yet ported (semantics in out_vm_ops_*.txt). Follow the true edge so shipped
		// programs keep flowing; log once per opcode so the gap list is visible.
		World.OnUnknownOpcode(Record.Token);
		return EOpResult::True;
	}
}
} // namespace

void FSimCopterPersonContext::ResetToState(int32 StateIndex)
{
	Attributes[EBhavAttr::State] = uint16(StateIndex);
	Attributes[EBhavAttr::LoopFlag] = uint16(FPeopleBehaviorModel::GetStateLoopFlag(StateIndex));
	Stack.Reset();
	const TArray<int32>& StatePrograms = FPeopleBehaviorModel::GetStateProgramIds();
	FFrame Frame;
	Frame.ProgramId = StatePrograms.IsValidIndex(StateIndex) ? StatePrograms[StateIndex] : StatePrograms[0];
	Stack.Add(Frame);
}

uint16 FSimCopterPersonContext::RandomRaw()
{
	// FUN_004ce9d0: 16-bit Galois LFSR, tap mask 0x1bf5.
	const uint16 Bit = Lfsr & 1;
	Lfsr >>= 1;
	if (Bit)
	{
		Lfsr ^= 0x1bf5;
	}
	if (Lfsr == 0)
	{
		Lfsr = 0x2a2a;
	}
	return Lfsr;
}

uint16 FSimCopterPersonContext::RandomBounded(uint16 Bound)
{
	return Bound != 0 ? RandomRaw() % Bound : 0; // FUN_004cea00
}

const TArray<int32>& FSimCopterBehaviorVM::GetAllowedTileClasses(int32 StateIndex)
{
	// FUN_004c3010's DAT_0058d750 rows.
	static const TArray<int32> DefaultRow = {13, 11, 10, 12, 7};
	static const TArray<int32> VehicleRow = {2};
	static const TArray<int32> OffRoadRow = {13, 11, 10, 12};
	static const TArray<int32> RoadOnlyRow = {7};
	switch (StateIndex)
	{
	case 1: case 15: return VehicleRow;
	case 2: return OffRoadRow;
	case 3: return RoadOnlyRow;
	default: return DefaultRow;
	}
}

EBhavStepResult FSimCopterBehaviorVM::Tick(
	FSimCopterPersonContext& Context,
	const FPeopleBehaviorModel& Model,
	ISimCopterBehaviorWorld& World)
{
	if (Context.Stack.Num() == 0)
	{
		Context.ResetToState(Context.GetStateIndex());
	}

	// Depth-style runaway guard, mirroring the original's 0x80 dispatch cap per walk.
	for (int32 Guard = 0; Guard < 0x80; ++Guard)
	{
		FSimCopterPersonContext::FFrame& Frame = Context.Stack.Last();
		const FBhavProgram* Program = Model.FindProgram(Frame.ProgramId);
		if (Program == nullptr || !Program->Records.IsValidIndex(Frame.RecordIndex))
		{
			// Treat as "returned false" so callers can handle missing/ended programs.
			Context.Stack.Pop();
			if (Context.Stack.Num() == 0)
			{
				return EBhavStepResult::Completed;
			}
			continue;
		}

		const FBhavRecord& Record = Program->Records[Frame.RecordIndex];

		// Token >= 0x100: call that BHAV id as a subprogram (walker link case).
		if (Record.Token >= 0x100)
		{
			if (Context.Stack.Num() >= FSimCopterPersonContext::MaxStackDepth)
			{
				UE_LOG(LogSimCopterBehaviorVM, Warning, TEXT("BHAV stack overflow in program %d."), Frame.ProgramId);
				Context.ResetToState(Context.GetStateIndex());
				return EBhavStepResult::Failed;
			}
			FSimCopterPersonContext::FFrame Callee;
			Callee.ProgramId = Record.Token;
			Context.Stack.Add(Callee);
			continue;
		}

		const EOpResult Result = ExecOpcode(Context, Record, World);
		if (Result == EOpResult::Yield)
		{
			return EBhavStepResult::Ran;
		}
		if (Result == EOpResult::Stop)
		{
			return EBhavStepResult::Stopped;
		}

		// Advance along the true/false edge, popping returns (port of FUN_004ce8f0).
		bool bEdge = Result == EOpResult::True;
		for (;;)
		{
			FSimCopterPersonContext::FFrame& Top = Context.Stack.Last();
			const FBhavProgram* TopProgram = Model.FindProgram(Top.ProgramId);
			const FBhavRecord* TopRecord = TopProgram != nullptr && TopProgram->Records.IsValidIndex(Top.RecordIndex)
				? &TopProgram->Records[Top.RecordIndex] : nullptr;
			const int8 Next = TopRecord != nullptr
				? (bEdge ? TopRecord->TrueNext : TopRecord->FalseNext)
				: int8(-1);
			if (Next >= 0)
			{
				Top.RecordIndex = Next;
				break;
			}
			bEdge = Next == -2; // -2 = return TRUE, -1 (and anything else) = return FALSE
			Context.Stack.Pop();
			if (Context.Stack.Num() == 0)
			{
				return EBhavStepResult::Completed;
			}
		}
	}

	UE_LOG(LogSimCopterBehaviorVM, Warning, TEXT("BHAV runaway guard hit (state %d)."), Context.GetStateIndex());
	return EBhavStepResult::Failed;
}
