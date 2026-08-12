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
	// +0x180. Which reaction BHAV *other* people get when this person walks into them: 0 means the
	// mode-13 table entry (914 "Rxn: Person--civil, neutral", the street conversation), and the cop
	// and paramedic programs set 916 "don't gawk, maybe run" so nobody chats with them.
	// FUN_004c9470 passes it as FUN_004c1050's param_5.
	constexpr int32 BumpReaction = 32;
	// +0x182. Set by BHAV 916 and by the megaphone reaction, and tested by BHAV 600 rec[14]: while it
	// is 1 the ambient loop keeps clear of the gawk hooks until a 1-in-6 roll clears it.
	constexpr int32 RecentlySpooked = 33;
	// +0x184. BHAV 281 drains it; BHAV 280 rec[11] kills the victim once it falls below 1.
	constexpr int32 MedevacHealth = 34;
	// +0x178. FUN_004c71c0 writes the class's voice pitch here and FUN_004c5210 hands it to
	// AddFrequency, so it is a *signed* offset stored in a u16 slot.
	constexpr int32 VoicePitch = 28;
	// +0x15e "written off": FUN_004c0ba0 sets it on everyone aboard when the helicopter is
	// destroyed. BHAV 264 rec[9] and BHAV 302 rec[10] both stop caring about a person once it is
	// set - a face frozen at 2 and no more sound.
	constexpr int32 WrittenOff = 15;
	// +0x18c. The person's own looping voice event: FUN_004c71c0 seeds it with one of the three
	// footstep clips (0x0e/0x28/0x29) or an Elvis noise, and BHAV 800 rec[4] assigns 58 so a
	// medevac victim owns the EKG. FUN_004c5210 only re-tunes a sound that matches this.
	constexpr int32 VoiceSet = 38;
	// +0x17a. The program id this person's STATE started them on. BHAV 444 rec[25]/rec[27] is the
	// only shipped reader: `attr29 == 444` is how a marching-band member tells itself apart from
	// the leader, whose state (17) starts on 443 instead. Members mill about and toot single
	// instrument notes (sound 37); the leader plays march.wav (sound 38) and keeps following you.
	constexpr int32 StateProgramId = 29;
	// +0x18e. Which head this person wears - FUN_004c71c0 sets it from the behavior class and
	// FUN_004c7090 overwrites it with 10 for state 6. BHAV 264 rec[8] tests it against 10 to
	// decide whether the seat portrait follows the victim's health or the helicopter's speed.
	constexpr int32 HeadImageIndex = 39;
	constexpr int32 Count = 0x30;
}

// FUN_004cac70's object-class table: what op 15 will search for. Values are the executable's.
// Full derivation in Docs/scratchpad/ghidra/criminal_ai_decode_20260728.md.
namespace EBhavObjectClass
{
	constexpr int32 MyMissionCoords = 0;  // FUN_004a88e0(person+0x10a) -> record +0x30, SecondaryX/Y
	constexpr int32 AlreadySelected = 1;  // whatever is already in the selected slot
	constexpr int32 PlayerHelicopter = 2; // DAT_005040d0+0xa4
	// DAT_005040d0+0xbc. Named by opcode 58's own assert text ("master is neither harness nor
	// heli"): this is the rescue harness on the end of the rope.
	constexpr int32 Harness = 3;
	constexpr int32 InteractionSource = 4;// person+0x1a4 - what last interacted with me
	constexpr int32 MedevacVictim = 5;    // FUN_004ca350 state == 6
	constexpr int32 UncaughtCriminal = 6; // FUN_004ca350 loopflag == 0 and CriminalCaught == 0
	constexpr int32 PoliceOfficer = 8;    // FUN_004ca350 loopflag == 1 (states 7 and 8)
	// DAT_00506444, the fixed person record - the player's own avatar on foot. BHAV 291 looks for
	// it so a waiting passenger walks up to you and waves.
	constexpr int32 PlayerAvatar = 9;
	// FUN_004cac70's jump-table cases 10..12 pass 0..2 straight through to
	// FUN_0049b060. Those pools are DAT_00582b20 (ambulance), DAT_00582b50
	// (police), and DAT_00582b38 (fire), respectively. The earlier names had
	// cases 10 and 12 reversed, which sent BHAV 262's medic to a fire truck.
	constexpr int32 Ambulance = 10;       // FUN_0049b060(0, tile)
	constexpr int32 PoliceCar = 11;       // FUN_0049b060(1, tile)
	constexpr int32 FireTruck = 12;       // FUN_0049b060(2, tile)
	// "Service 3" is the CARROBBR burglar-car pool, not a fourth emergency service: BHAV 1402
	// "Cop speeder" probes it at rec[3] to find the car it was sent to arrest.
	constexpr int32 BurglarCar = 13;      // FUN_0049b060(3, tile)
	constexpr int32 Civilian = 14;        // FUN_004ca350 state == 0
	// DAT_005040d0+0xc0, the third object hanging off the player record. Taken to be the
	// searchlight: the only program that asks for it is 1151 "copf - follow heli", and pointing
	// the light at what you want the police to deal with is how SimCopter directs them - the
	// dispatcher itself reads the spotlight node for its chase target (FUN_0049b3f0).
	constexpr int32 PlayerSpotlight = 16;
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
	//
	// Not every object class is an actor: class 16 is the helicopter's spotlight, which in the
	// remake is a place rather than a thing. SelectedLocation always holds the point to walk to
	// and face; SelectedObject is null for those, which only op 39 (push a reaction onto a
	// person) cares about.
	TWeakObjectPtr<AActor> SelectedObject;
	FVector SelectedLocation = FVector::ZeroVector;
	bool bHasSelection = false;
	// Object class 3 selects the harness, which lives on the helicopter's rope rather than being
	// an actor: SelectedObject is the helicopter and this says which of the two was meant.
	bool bSelectionIsHarness = false;

	void ClearSelection()
	{
		SelectedObject.Reset();
		SelectedLocation = FVector::ZeroVector;
		bHasSelection = false;
		bSelectionIsHarness = false;
	}

	// Interrupt reaction state, mirroring FUN_004c1050's writes:
	//   person+0x17c  the 900-series reaction id last accepted - a RECORD, not a latch. It is
	//                 read back only by the cabin-passenger arm of the acceptance test
	//                 (SimCopterInteraction::CanAcceptReaction), and nothing ever clears it.
	//   person+0x158  the interaction's param_5
	//   person+0x15a  the megaphone message index (mode 2 only)
	//   person+0x1a4  the object that caused the interaction, kept as a plain id/handle here
	int32 LastReactionProgramId = INDEX_NONE;
	int32 ReactionParameter = 0;
	int32 MegaphoneMessageIndex = INDEX_NONE;

	int32 GetStateIndex() const { return Attributes[EBhavAttr::State]; }
	void ResetToState(int32 StateIndex);

	// FUN_004c1050's tail: push a reaction BHAV onto the walk stack so it runs before the
	// person's normal program resumes. Returns false when the acceptance test refuses it, when
	// it is already the frame on top of the stack, or when the stack is full (the original pops
	// a frame first). bCabinPassenger is person+0x15c - see CanAcceptReaction.
	bool PushReactionProgram(int32 ProgramId, bool bCabinPassenger = false);

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
	// Context.SelectedObject/SelectedLocation and report its Chebyshev tile distance. False when
	// the class is not supported here or nothing matched - the opcode then writes the original's
	// 2000 sentinel.
	virtual bool SelectObjectOfClass(FSimCopterPersonContext& Context, int32 ObjectClass, int32& OutTileDistance)
	{
		return false;
	}

	// Op 14, FUN_004caaf0: a small family of proximity tests picked by the record's first
	// argument. Case 1 - the one the criminal programs use - is "is the player's helicopter
	// within 4 original units of the ground", i.e. hovering just off the deck.
	virtual bool EvaluateProximityTest(const FSimCopterPersonContext& Context, int32 TestIndex) const
	{
		return false;
	}

	// Op 18, FUN_004cb270: snap facing to the selected object's octant (bearing - 2 & 7).
	virtual bool FaceSelectedObject(FSimCopterPersonContext& Context) { return false; }

	// Op 31, FUN_004cc240: the same bearing, turned 180 degrees (bearing + 2 & 7) - face *away*
	// from the selection. Every flee program selects the thing to run from and then calls this, so
	// without it a criminal runs from a cop in whatever direction they happened to be facing.
	// Nothing selected is a successful no-op; a selection with no bearing (same position) fails.
	virtual bool FaceAwayFromSelectedObject(FSimCopterPersonContext& Context) { return true; }

	// Ops 32/33, FUN_004cc290 -> FUN_004cc2b0: the same two turns against person+0x1a4, the object
	// that last interacted with me. No source at all returns true *without* changing the facing;
	// a source with no bearing takes a random facing and returns false.
	virtual bool FaceInteractionSource(FSimCopterPersonContext& Context, bool bFaceToward) { return true; }

	// Op 80, FUN_004cb790: re-run the post-move selector against person+0x1a4 with result 5 when the
	// source is a person (obj+0xc & 8) and 4 otherwise - the street conversation ("2Gab"/"HipH" plus
	// a voice line) or a "Whoa". Always succeeds.
	virtual void ReactToInteractionSource(FSimCopterPersonContext& Context) {}

	// Op 24, FUN_004cb480 -> FUN_004c9f10: the riot measurement. False when no riot mission is live
	// (FUN_004a9230(0x1000)). Otherwise counts the other people within RadiusTiles, averages their
	// agitation (the op-23 "logic" speed at +0x150) and reports the facing octant toward the crowd's
	// mean position. BHAV 852 turns this into riotValue = count * mean / 15.
	virtual bool MeasureRiotCrowd(
		const FSimCopterPersonContext& Context,
		int32 RadiusTiles,
		int32& OutFacingOctant,
		int32& OutAverageAgitation,
		int32& OutCount) const
	{
		return false;
	}

	// Op 27, FUN_004cb630: person+0x1c4, this person's own radius - 3 original units normally, 1.5
	// once their agitation passes 5, which is how a mob packs twice as tight. The same field is the
	// radius FUN_004c9000 collides people with, so it is also the bump radius.
	virtual void SetBodyRadiusOriginalUnits(float RadiusUnits) {}

	// Op 28, FUN_004cb680 -> FUN_004c4e60: join the live riot - post EVT_RiotPersonAdded and become
	// a state-3 rioter owned by that record. False when no riot is running.
	virtual bool JoinLiveRiot(FSimCopterPersonContext& Context) { return false; }

	// Op 36, FUN_004cc470 -> FUN_004ca190: the nearest cell within RadiusTiles carrying scene-cell
	// flag 0x20. Faces toward it and reports its tile distance, which BHAV 274 branches on to decide
	// between running to watch a fire, gawking at it, and fleeing.
	virtual bool FaceNearestFireWithin(FSimCopterPersonContext& Context, int32 RadiusTiles, int32& OutTileDistance)
	{
		return false;
	}

	// Op 35, FUN_004cc410 -> FUN_004abb00(0x20): how many mission records of type MedEvac are live.
	virtual int32 GetActiveMedevacMissionCount() const { return 0; }

	// Op 35's action, FUN_004c9b50: the person collapses. Their old record is told they died, a fresh
	// MedEvac record is created at their tile, and they become its state-6 victim.
	virtual bool CollapseIntoMedevacVictim(FSimCopterPersonContext& Context) { return false; }

	// Op 50, FUN_004cc980: how much room the selection has for another passenger. The player's
	// helicopter answers its free seat count (manifest +4 minus +8); an emergency vehicle answers the
	// original's 0x1721 "always room" constant; anything else answers INDEX_NONE, which means the
	// handler writes nothing and the local keeps its old value.
	static constexpr int32 EmergencyVehicleRoomId = 0x1721;
	virtual int32 GetSelectionRoomForBoarding(const FSimCopterPersonContext& Context) const { return INDEX_NONE; }

	// Op 78, FUN_004cb830: one step of the abduction flight - normalise the 3D delta to
	// person+0x1a8 and teleport MoveSpeed whole units along it, with no move check or climb gate.
	// True while still travelling (the opcode yields), false when it has arrived, drifted more than
	// 0x15 tiles from the camera, or there is no beam target at all.
	virtual bool AdvanceBeamAbduction(FSimCopterPersonContext& Context) { return false; }

	// Op 79, FUN_004cb7d0: DAT_00506448, the people system's behaviour-tick counter. BHAV 444 reads
	// it twice and subtracts, which is how the tuba player times its notes.
	virtual int32 GetBehaviorTickCounter() const { return 0; }

	// Op 38, FUN_004ca940: face the selected object and take one move step toward it.
	virtual ESimCopterBehaviorStepResult StepTowardSelectedObject(FSimCopterPersonContext& Context)
	{
		return ESimCopterBehaviorStepResult::NoTarget;
	}

	// Op 39, FUN_004cc560: push a BHAV onto the *selected person's* walk stack. This is how a cop
	// arrests a criminal - it pushes BHAV 1060 "Rx: criminal-caught" onto them.
	virtual bool PushReactionOnSelectedObject(FSimCopterPersonContext& Context, int32 ProgramId) { return false; }

	// Op 25, FUN_004cb550: the XBLD building id of the tile the person stands on. INDEX_NONE when
	// the tile is off the map (the original answers 0xffff, or 0xf6 inside the airport patch).
	virtual int32 GetCurrentTileBuildingId() const { return INDEX_NONE; }

	// Op 56, FUN_004ccc40: is the tile under the person one an emergency crew may work on -
	// FUN_004c9cc0 (occupied?) plus FUN_004c9dc0 on its tile class.
	virtual bool IsCurrentTileServiceable() const { return false; }

	// Op 63, FUN_004ca6f0 / op 71, FUN_004cbaa0: person+0x1a0, the thing this person is riding.
	virtual bool IsRidingCarrier(const FSimCopterPersonContext& Context) const { return false; }

	// Ops 21 and 17, both via FUN_004c9bc0: may this person stand where they are right now - the
	// tile is one people may occupy, they are still close to whatever is carrying them, and they
	// are within 6 original units of the ground. Op 17 (FUN_004cb190) additionally *does* it:
	// clears the carrier and re-seats them on the terrain. This is the drop-off test for every
	// passenger type; it is emphatically not a "is the player nearby" probe.
	virtual bool CanAlightHere() const { return false; }
	virtual bool TryAlightHere() { return false; }

	// Op 48, FUN_004cc900 (and op 12's tail): make the selection my carrier and snap to it.
	virtual bool BoardSelection(FSimCopterPersonContext& Context) { return false; }

	// Op 44, FUN_004cc6a0: pick the selected person up - move them onto me and become their
	// carrier. This is the paramedic collecting a victim.
	virtual bool PutSelectedPersonOnMe(FSimCopterPersonContext& Context) { return false; }

	// Op 47, FUN_004cc8d0: clear the selected person's carrier - put them down.
	virtual bool DropSelectedPerson(FSimCopterPersonContext& Context) { return false; }

	// Op 46, FUN_004cc7d0 / op 51, FUN_004ca570: FUN_004ca650 finds the person whose carrier is
	// me - the one I am toting - and selects them. Op 51 also sets them down first.
	virtual bool SelectCarriedPerson(FSimCopterPersonContext& Context, bool bAlsoDropThem) { return false; }

	// Op 71, FUN_004cbaa0: am I carrying anyone.
	virtual bool IsCarryingPerson() const { return false; }

	// Op 58, FUN_004cccd0 "GetOnHeliIfHarnessRaised": true unless I am on the harness with the
	// bucket raised, in which case climb into the cabin first.
	virtual bool GetOnHelicopterIfHarnessRaised(FSimCopterPersonContext& Context) { return true; }

	// Op 59, FUN_004cce30: is my carrier the player's helicopter (DAT_005040d0+0xa4).
	virtual bool IsCarrierPlayerHelicopter() const { return false; }

	// Op 86, FUN_004cceb0: is my carrier the harness (DAT_005040d0+0xbc).
	virtual bool IsCarrierHarness() const { return false; }

	// Op 87, FUN_004cce50: is the scene cell at person+0x188/+0x18a the one I am standing on -
	// i.e. am I back where I was placed.
	virtual bool IsOnHomeTile() const { return false; }

	// Op 84, FUN_004cc830: walk the player's passenger list and select the first medevac victim
	// aboard.
	virtual bool SelectMedevacVictimAboardPlayer(FSimCopterPersonContext& Context) { return false; }

	// Op 54, FUN_004ccb40: which of the three faces this person shows in the seat window. BHAV 264
	// "Face vs. speed/health" sets it from the helicopter's speed and the victim's health. The
	// original writes it into the seat manifest with FUN_0048c0e0 and marks the window dirty.
	virtual void SetSeatPortraitMood(int32 Mood) {}

	// Op 57, FUN_004ccca0 -> FUN_004c5210: play one of the 62 people-voice events out of this
	// person's own bank slot. The arguments are the handler's, in order:
	//   VoiceEvent    which clip family (13 = achdie, 58 = the medevac EKG, 59 = spray, ...)
	//   bAllocateSlot claim a voice-bank slot when this person has none; without it the call is
	//                 dropped rather than stealing one
	//   bNonPositional play 2D instead of 3D at the person's position - and open the audibility
	//                 gate, which is how the EKG is heard from the cockpit
	//   bForce        open the gate regardless of where the person and the player are
	virtual void PlayPersonVoiceEvent(int32 VoiceEvent, bool bAllocateSlot, bool bNonPositional, bool bForce) {}

	// Op 85, FUN_004cc110: the same handler called as FUN_004c5210(-1, 1, 1, 1) - stop whatever
	// this person is saying and give the bank slot back.
	virtual void StopPersonVoice() {}

	// Op 55, FUN_004ccb80: the player helicopter's speed in the units BHAV 264 compares against
	// 250 and 125 - which is the raw forward speed scaled by MaxDamage / remaining hit points, so
	// the thresholds arrive sooner the more battered the machine is.
	virtual int32 GetPlayerHelicopterSpeed() const { return 0; }

	// Op 61, FUN_004ccef0: FUN_0049aed0(person+0x170, arg0) - tell the emergency vehicle I belong
	// to something. A boarded crew member uses it to release its car.
	virtual void MessageOwningVehicle(int32 MessageId) {}

	// Ops 30/60/83, FUN_004cbfd0 / FUN_004cc130: bind "Thro" and launch a projectile, either on
	// the person's facing or straight at the selection. FUN_004cbfd0 reads the record's own opcode
	// to choose the type - `*param_3 == 0x3c` (op 60) asks FUN_0048e0b0 for type 4, the arsonist's
	// firebomb; ops 30 and 83 ask for type 10, the rioter's rock. bIncendiary is that distinction.
	virtual void ThrowProjectileAtSelection(
		FSimCopterPersonContext& Context,
		bool bAtSelection,
		bool bIncendiary) {}

	// Op 66, FUN_004cbbc0: the fall-and-die handler - detach, drop to the ground, post
	// EVT_PersonDied and bind "Dead". True when the person has finished dying.
	virtual bool BeginFallAndDie(FSimCopterPersonContext& Context) { return false; }

	// Op 62, FUN_004ca700: select the emergency vehicle this person belongs to, or the player's
	// helicopter when they belong to none.
	virtual bool SelectOwningVehicle(FSimCopterPersonContext& Context) { return false; }

	// Op 69, FUN_004cbb60: is the selection the player's helicopter?
	virtual bool IsSelectionPlayerHelicopter(const FSimCopterPersonContext& Context) const { return false; }

	// Op 82, FUN_004ccad0: is the selection within 25 original units (Manhattan, 3D)?
	virtual bool IsSelectionWithinUnits(const FSimCopterPersonContext& Context, int32 Units) const { return false; }

	// Op 74, FUN_004cba70: DAT_004f9740, the difficulty tier the mission layer scales counts by.
	virtual int32 GetDifficultyTier() const { return 1; }

	// Op 73, FUN_004cb9c0 -> FUN_004ca4f0(State, 0): true when a person in State exists but is
	// hidden from ordinary object searches (person+0x152 == 0), normally because they are riding.
	virtual bool HasHiddenPersonInState(int32 State) const { return false; }

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
