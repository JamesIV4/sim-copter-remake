// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ground/SimCopterBehaviorVM.h"

#include "Formats/SimCopterPeopleCityRules.h"
#include "Ground/SimCopterInteraction.h"

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
	case 4: // timed move: walk along facing while local counter runs (FUN_004ca7d0)
	{
		uint16& Counter = Local(Record.Args[0]);
		if (Counter == 0)
		{
			// FUN_004ca7d0's exhausted branch still runs the post-move selector with
			// result 8 / speed 0, which binds NoMo (FUN_004c6970 case 0/8). Without this
			// the walk clip keeps playing while the person stands at the burst's end.
			Context.PendingAnimMnemonic = TEXT("NoMo");
			return EOpResult::True;
		}
		--Counter;
		return World.MoveStep(Context) ? EOpResult::Yield : EOpResult::False;
	}
	case 6: // bind figure model by id (FUN_004ca860) - the remake picks figures visually
		return EOpResult::True;
	case 7: // local[arg0] := random(resolved arg1) (FUN_004ca8e0)
	{
		const uint16 Bound = ResolveOperand(Context, Record.Args[2], Record.Args[1], nullptr);
		Local(Record.Args[0]) = Context.RandomBounded(FMath::Max<uint16>(Bound, 1));
		return EOpResult::True;
	}
	case 13: // mission-event side effect (FUN_004caac0 -> FUN_004ccf50)
		World.PostMissionOutcome(Context, int32(int16(Record.Args[0])));
		return EOpResult::True;
	case 15: // nearest object of a class (FUN_004cac70); arg3 names the local the range lands in
	{
		const int32 ObjectClass = int32(int16(Record.Args[0]));
		const int32 Radius = int32(int16(ResolveOperand(Context, Record.Args[2], Record.Args[1], nullptr)));
		int32 TileDistance = 0;
		const bool bFound = World.SelectObjectOfClass(Context, ObjectClass, TileDistance);
		// The original stores 2000 in the local when the search comes up empty, and only commits
		// the object to the selected slot when it is inside the range (0x4cb0e5).
		Local(Record.Args[3]) = uint16(bFound ? TileDistance : 2000);
		if (!bFound || TileDistance >= Radius)
		{
			Context.ClearSelection();
			return EOpResult::False;
		}
		return EOpResult::True;
	}
	case 14: // proximity tests against the player and the person's carrier (FUN_004caaf0)
		return World.EvaluateProximityTest(Context, int32(int16(Record.Args[0])))
			? EOpResult::True
			: EOpResult::False;
	case 16: // deactivate person (FUN_004cb180 -> FUN_004c4e40; result 3 = stop)
		Context.bRequestDespawn = true;
		return EOpResult::Stop;
	case 17: // "may I stand here?" - and if so, get off whatever I am on (FUN_004cb190)
		return World.TryAlightHere() ? EOpResult::True : EOpResult::False;
	case 18: // face toward the selected object; absent object is a true no-op (FUN_004cb270)
		if (!Context.bHasSelection)
		{
			return EOpResult::True; // FUN_004cb270's "no object" early-out returns 1
		}
		return World.FaceSelectedObject(Context) ? EOpResult::True : EOpResult::False;
	case 19: // tile class == arg (FUN_004cb2c0 -> FUN_004c9220)
		return World.GetCurrentTileClass() == int32(Record.Args[0]) ? EOpResult::True : EOpResult::False;
	case 20: // tile class allowed for my behavior class (FUN_004cb300, DAT_0058ec00 rows)
		return FSimCopterPeopleCityRules::GetAmbientStateTileClasses(Context.Attributes[EBhavAttr::BehaviorClass])
			.Contains(World.GetCurrentTileClass())
			? EOpResult::True : EOpResult::False;
	case 21: // may I stand here? (FUN_004cb360 -> FUN_004c9bc0, the test without the side effect)
		return World.CanAlightHere() ? EOpResult::True : EOpResult::False;
	case 22: // same tile as player/camera globals; writes player speed + facing locals (FUN_004cb370)
	{
		int32 CurrentFileX = INDEX_NONE;
		int32 CurrentFileY = INDEX_NONE;
		FSimCopterBehaviorPlayerTileProbe Probe;
		if (!World.TryGetCurrentTileCoordinate(CurrentFileX, CurrentFileY) ||
			!World.TryGetPlayerTileProbe(Context, Probe) ||
			CurrentFileX != Probe.FileX ||
			CurrentFileY != Probe.FileY)
		{
			return EOpResult::False;
		}

		Local(Record.Args[0]) = Probe.Speed;
		Local(Record.Args[1]) = Probe.Facing;
		return EOpResult::True;
	}
	case 23: // speed += arg, clamped 0..10 (FUN_004cb420; person+0x150)
	{
		Context.Attributes[EBhavAttr::PreviousSpeed] = Context.Attributes[EBhavAttr::Speed];
		const int32 NewSpeed = FMath::Clamp<int32>(
			int32(int16(Context.Attributes[EBhavAttr::Speed])) + int16(Record.Args[0]), 0, 10);
		Context.Attributes[EBhavAttr::Speed] = uint16(NewSpeed);
		return EOpResult::True;
	}
	case 24: // compute bearing/distance to a selected runtime object (FUN_004cb480)
		return EOpResult::False;
	case 25: // XBLD id at my tile == arg0 (FUN_004cb550; BHAV 801 tests 209 = the hospital)
		return World.GetCurrentTileBuildingId() == int32(int16(Record.Args[0]))
			? EOpResult::True : EOpResult::False;
	case 26: // same shape as 25 in the original, one tile-map lookup apart; unported.
		World.OnUnknownOpcode(Record.Token);
		return EOpResult::False;
	case 27: // reaction-force side effect (FUN_004cb630); not needed by the remake movement.
		return EOpResult::True;
	case 28: // maybe create a rioter from a carried/context object (FUN_004cb680)
		return EOpResult::False;
	case 29: // facing := local[arg0] & 7 (FUN_004cb6d0)
		Context.Attributes[EBhavAttr::Facing] = uint16(Local(Record.Args[0]) & 7);
		return EOpResult::True;
	case 31: // face away from a linked runtime object; no object means success (FUN_004cc240)
		return EOpResult::True;
	case 32: // face away from whatever last interacted with me (FUN_004cc290 -> FUN_004cc2b0)
	case 33: // ...and token 0x21 is the same handler facing toward it
		// person+0x1a4 is the object that caused the last interaction, which the remake does not
		// keep a handle to. That lands on FUN_004cc2b0's own no-bearing arm: take a random facing
		// and report failure, which is what the shipped programs branch on.
		Context.Attributes[EBhavAttr::Facing] = Context.RandomBounded(8);
		return EOpResult::False;
	case 34: // wander out of road/invalid pedestrian tile (FUN_004cc330)
	{
		const int32 TileClass = World.GetCurrentTileClass();
		if (TileClass != 7 && TileClass != 8 && TileClass != 6 && TileClass != 9 &&
			TileClass != INDEX_NONE && TileClass != 1)
		{
			return EOpResult::True;
		}

		uint16& Counter = Local(Record.Args[0]);
		if (Counter == 0)
		{
			return EOpResult::False;
		}
		--Counter;
		return World.MoveStep(Context) ? EOpResult::Yield : EOpResult::False;
	}
	case 36: // face toward a runtime object class (FUN_004cc470); no matching object in this world.
		return EOpResult::False;
	// Both are FUN_004ca940; op 12 is the arm that also gets on the thing when it arrives
	// (`*param_3 == 0xc`), which is how every passenger in the game boards anything.
	case 12:
	case 38:
	{
		if (!Context.bHasSelection)
		{
			return EOpResult::False;
		}
		uint16& Counter = Local(Record.Args[0]);
		if (Counter == 0)
		{
			// Ran out of tries. As with op 4's exhausted branch, the post-move selector binds
			// NoMo so the walk clip stops with the walk.
			Context.PendingAnimMnemonic = TEXT("NoMo");
			return EOpResult::False;
		}
		--Counter;
		switch (World.StepTowardSelectedObject(Context))
		{
		case ESimCopterBehaviorStepResult::Arrived:
			if (Record.Token == 12 && !World.BoardSelection(Context))
			{
				return EOpResult::False;
			}
			return EOpResult::True;
		case ESimCopterBehaviorStepResult::Moving:  return EOpResult::Yield;
		default:                                    return EOpResult::False;
		}
	}
	case 39: // push a BHAV onto the selected person's stack (FUN_004cc560); always succeeds
		World.PushReactionOnSelectedObject(Context, int32(Record.Args[0]));
		return EOpResult::True;
	case 40: // tear the person down (FUN_004cc5d0 clears person+0x142 and unhooks the render node)
		// Both edges of every shipped op-40 record are -3, so the walker never resumes; treating
		// it as opcode 16's despawn is what stops a finished cop standing in the road forever.
		Context.bRequestDespawn = true;
		return EOpResult::Stop;
	case 37: // FUN_004cc530: FUN_004ca4b0() then result 3 - leave the map
		Context.bRequestDespawn = true;
		return EOpResult::Stop;
	case 44: // pick the selected person up (FUN_004cc6a0)
		World.PutSelectedPersonOnMe(Context);
		return EOpResult::True; // the original returns 1 whether or not anything was selected
	case 46: // select the person I am carrying (FUN_004cc7d0 -> FUN_004ca650)
		return World.SelectCarriedPerson(Context, /*bAlsoDropThem*/ false) ? EOpResult::True : EOpResult::False;
	case 47: // put the selected person down (FUN_004cc8d0)
		World.DropSelectedPerson(Context);
		return EOpResult::True;
	case 48: // make the selection my carrier (FUN_004cc900)
		return World.BoardSelection(Context) ? EOpResult::True : EOpResult::False;
	case 51: // set down whoever I am carrying and select them (FUN_004cca00 -> FUN_004ca570)
		return World.SelectCarriedPerson(Context, /*bAlsoDropThem*/ true) ? EOpResult::True : EOpResult::False;
	case 53: // select the player's helicopter when it is within 24 units (FUN_004cca60)
	{
		int32 Distance = 0;
		if (!World.SelectObjectOfClass(Context, EBhavObjectClass::PlayerHelicopter, Distance))
		{
			return EOpResult::False;
		}
		// FUN_004cca60's own gate is a 3D Manhattan sum of 0x18 original units, not a tile count.
		if (!World.IsSelectionWithinUnits(Context, 24))
		{
			Context.ClearSelection();
			return EOpResult::False;
		}
		return EOpResult::True;
	}
	case 56: // may an emergency crew work on the tile I am on? (FUN_004ccc40)
		return World.IsCurrentTileServiceable() ? EOpResult::True : EOpResult::False;
	case 57: // sound/side-effect trigger (FUN_004ccca0 -> FUN_004c5210)
		return EOpResult::True;
	case 62: // select the vehicle I belong to, else the player's helicopter (FUN_004ca700)
		World.SelectOwningVehicle(Context);
		return EOpResult::True; // the original returns 1 on both arms
	case 63: // am I riding something? (FUN_004ca6f0: person+0x1a0 != 0)
		return World.IsRidingCarrier(Context) ? EOpResult::True : EOpResult::False;
	// FUN_004cbb80 / FUN_004cb730 copy the selected object between this frame and its caller's
	// (walkFrame[-2] + 4). The remake keeps one selection slot on the person rather than one per
	// frame, so callee and caller already share it and the copy is implicit - but the opcodes
	// still have to succeed, because both are on the hot path of the medevac programs.
	case 67:
	case 68:
		return EOpResult::True;
	case 69: // is my selection the player's helicopter? (FUN_004cbb60)
		return World.IsSelectionPlayerHelicopter(Context) ? EOpResult::True : EOpResult::False;
	case 71: // am I carrying anyone? (FUN_004cbaa0 -> FUN_004ca650)
		return World.IsCarryingPerson() ? EOpResult::True : EOpResult::False;
	case 72: // clear the selection (FUN_004cb770)
		Context.ClearSelection();
		return EOpResult::True;
	case 74: // local[arg0] := the difficulty tier (FUN_004cba70 reads DAT_004f9740)
		Local(Record.Args[0]) = uint16(World.GetDifficultyTier());
		return EOpResult::True;
	case 75: // local[arg0] := my tile X (FUN_004cba10 reads person+0x12a)
	case 76: // local[arg0] := my tile Y (FUN_004cba40 reads person+0x12c)
	{
		int32 FileX = INDEX_NONE;
		int32 FileY = INDEX_NONE;
		if (!World.TryGetCurrentTileCoordinate(FileX, FileY))
		{
			return EOpResult::True; // the original writes whatever is cached; never fails
		}
		Local(Record.Args[0]) = uint16(Record.Token == 75 ? FileX : FileY);
		return EOpResult::True;
	}
	case 77: // local[arg0] := abs(local[arg0]) (FUN_004cb9e0)
	{
		uint16& Slot = Local(Record.Args[0]);
		Slot = uint16(FMath::Abs(int32(int16(Slot))));
		return EOpResult::True;
	}
	case 82: // is my selection within 25 units? (FUN_004ccad0)
		return World.IsSelectionWithinUnits(Context, 25) ? EOpResult::True : EOpResult::False;
	case 58: // get on the heli if the harness is raised (FUN_004cccd0)
		return World.GetOnHelicopterIfHarnessRaised(Context) ? EOpResult::True : EOpResult::False;
	case 59: // is my carrier the player's helicopter? (FUN_004cce30)
		return World.IsCarrierPlayerHelicopter() ? EOpResult::True : EOpResult::False;
	case 61: // tell the vehicle I belong to something (FUN_004ccef0)
		World.MessageOwningVehicle(int32(int16(Record.Args[0])));
		return EOpResult::True;
	case 70: // snap/update vertical position from ground/carried object (FUN_004cbab0)
		return EOpResult::True;
	case 85: // ambient audio/side-effect kick (FUN_004cc110 -> FUN_004c5210)
		return EOpResult::True;
	case 84: // select a medevac victim already aboard the player's helicopter (FUN_004cc830)
		return World.SelectMedevacVictimAboardPlayer(Context) ? EOpResult::True : EOpResult::False;
	case 86: // is my carrier the harness? (FUN_004cceb0)
		return World.IsCarrierHarness() ? EOpResult::True : EOpResult::False;
	case 87: // am I back on the tile I was placed on? (FUN_004cce50)
		return World.IsOnHomeTile() ? EOpResult::True : EOpResult::False;
	case 30: // bind "Thro" and launch a projectile along my facing (FUN_004cbfd0)
	case 60: // the same handler (FUN_004cced0 forwards to it)
		World.ThrowProjectileAtSelection(Context, /*bAtSelection*/ false);
		return EOpResult::True;
	case 83: // face the selection, bind "Thro", throw at it (FUN_004cc130)
		World.ThrowProjectileAtSelection(Context, /*bAtSelection*/ true);
		return EOpResult::True;
	case 66: // fall and die (FUN_004cbbc0)
		World.BeginFallAndDie(Context);
		Context.bRequestDespawn = true;
		return EOpResult::Stop;
	case 35: // conditional despawn: local[arg0] over a tuned threshold (FUN_004cc410)
	{
		// FUN_004abb00(0x20) is a tuning read the remake does not have; the original compares the
		// local against it and only leaves when the local is larger. With no threshold to read,
		// stay - the alternative is people vanishing for no visible reason.
		if (int32(int16(Record.Args[0])) == -1)
		{
			Context.bRequestDespawn = true;
			return EOpResult::Stop;
		}
		return EOpResult::False;
	}
	case 54: // set the face this person shows in the seat window (FUN_004ccb40)
		World.SetSeatPortraitMood(int32(int16(Record.Args[0])));
		return EOpResult::True;
	case 55: // local[arg0] := the player helicopter's speed (FUN_004ccb80)
		// BHAV 264 "Face vs. speed/health" reads it and picks face 2 over 250, face 1 over 125,
		// face 0 otherwise - a passenger looking progressively less happy the faster you fly.
		Local(Record.Args[0]) = uint16(FMath::Clamp(World.GetPlayerHelicopterSpeed(), 0, 0xffff));
		return EOpResult::True;
	case 73: // is there a paramedic on the map? (FUN_004cb9c0 -> FUN_004ca4f0(state 5, hidden))
		return World.HasHiddenPersonInState(5) ? EOpResult::True : EOpResult::False;
	case 79: // local[arg0] := DAT_00506448 (FUN_004cb7d0); a frame counter the remake has no use for
		Local(Record.Args[0]) = 0;
		return EOpResult::True;
	default:
		// Not yet ported (semantics in out_vm_ops_*.txt). Follow the failure edge so a missing
		// object/mission handler does not silently invent successful behavior.
		World.OnUnknownOpcode(Record.Token);
		return EOpResult::False;
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

// SCHOOK: PersonReactionPush 0x004c1050
bool FSimCopterPersonContext::PushReactionProgram(int32 ProgramId)
{
	if (ProgramId == INDEX_NONE)
	{
		return false;
	}
	if (!SimCopterInteraction::ReactionCanInterrupt(ProgramId, ActiveReactionProgramId))
	{
		return false;
	}

	// The original drops the deepest frame when the stack is nearly full
	// (person[0x3d] > frames - 2) before pushing the reaction.
	if (Stack.Num() >= MaxStackDepth - 1 && Stack.Num() > 1)
	{
		Stack.Pop(EAllowShrinking::No);
	}
	if (Stack.Num() >= MaxStackDepth)
	{
		return false;
	}

	FFrame Frame;
	Frame.ProgramId = ProgramId;
	Stack.Add(Frame);
	ActiveReactionProgramId = ProgramId;
	return true;
}

uint16 FSimCopterPersonContext::RandomRaw()
{
	return FSimCopterPeopleCityRules::NextPeopleRandomRaw(Lfsr);
}

uint16 FSimCopterPersonContext::RandomBounded(uint16 Bound)
{
	return FSimCopterPeopleCityRules::NextPeopleRandomBounded(Lfsr, Bound);
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
