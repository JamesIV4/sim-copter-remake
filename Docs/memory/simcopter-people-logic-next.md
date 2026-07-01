---
name: simcopter-people-logic-next
description: "Next major RE goal: clean decode of SimCopter people/pedestrian logic (behavior VM + spawn rules) for faithful spawning and original behaviors"
metadata: 
  node_type: memory
  type: project
  originSessionId: ac7bbbd4-fa8e-4787-8737-b11dd7d4b850
---

**Next major reverse-engineering goal (set 2026-06-26):** a clean decode of the SimCopter **people logic** so the remake spawns pedestrians correctly and they carry **all the original behaviors** (idle/walk/panic/return-to-car/flee-helicopter-spotlight/get-picked-up/decommission-when-far). The `privanim.df` figure/animation container is already fully decoded (see [[simcopter-population-rendering]] and `Docs/OriginalGameFileFormats.md` "Faithful Extraction Method" + `Tools/privanim_extract.py`); the behavior VM is the remaining piece.

**Why:** the remake currently uses placeholder waypoint wandering; the user wants faithful original behavior + spawn rules, not an approximation.

**How to apply / entry points (all Ghidra/static - live code reads are unreliable under SimCopterX, see [[simcopter-live-memory-rip]]):**
- Spawn config `FUN_004c4190` (free-slot among 500 person objects `DAT_0058e030`; tile/object/world spawn modes; per-state anim defaulting). Init `FUN_004c2f30`.
- Per-frame driver `FUN_004c6450`: runs behavior + advances animation frame (wraps at clip ARPP rows), LOD-gated by `DAT_0058dc26`.
- **88-handler behavior bytecode VM**: dispatch table `DAT_0058ef78`, entered via `FUN_004ccf20` (`(&DAT_0058ef78)[op]()`), executed by walking each agent's `BHAV`/"VAHB" (`0x42484156`) resource with the IFF walker `FUN_004ce7b0` (proven this pass to be a *behavior interpreter*, not a geometry drawer). `BHAV` accessor `FUN_004d0100`. People behavior PRNG `FUN_004ce9d0` (16-bit LFSR tap `0x1bf5`).
- Per-state table `DAT_0058de80` (anim ids per state; state set by `FUN_004c7090` at person `+0x148`, anim at `+0x17a`, loop flags at `+0x14a`). Person record fields validated live: alive`+0x142`, state`+0x148`, anim`+0x17a`, pos(16.16)`+0x1cc/+0x1d0/+0x1d4`, carrier`+0x1a0`.
- Per-city spawn weighting is in `career.twk` (Fire/Crime/Rescue/Riot/Traffic/MedEvac/Transport weights; see `Docs/MissionsAndTweakSystem.md`).
- Decode needed: the 88 opcode handlers + bytecode grammar, state-transition triggers, and how `people.df` `BHAV` scripts bind to states. Use `Tools/Ghidra/ReverseExplore.java` (`decompileforce`/`scan` commands added this pass).

**Update 2026-07-01 (Fable 5 pass; see [[simcopter-privanim-decoded]]):**
- The VM is fully located: walker `FUN_004ce7b0` reads 16-bit tokens (<0x100 = opcode, >=0x100 = 4-char node link push `FUN_004ce700`, 12-deep stack); opcodes dispatch via person vtable[0] `FUN_004ccf20` -> thunk table at `0x4c84e0 + 0x20*n` -> real handlers. **Full opcode->function map: `Docs/scratchpad/ghidra/out_vm_handlers.txt`.** First handlers decoded (`out_vm_ops0-6.txt`): op0 wait-counter (per-state counters at person `-8 + (arg + stackCursor*10)*2`), op1 `FUN_004c68f0(arg)`, op3 move-step, op4 binds a privanim FIGURE node by u32 arg -> person `+0x21c`/`+0x218`, op6 walk-toward-object (uses facing `+0x140`, pos `+0x1cc..`, tile `+0x12a/+0x12c`).
- `FUN_004c6450` fully decoded: carried-object follow (`+0x1a0`), LOD gate vs camera tile (`DAT_0061a618/c` vs person `+0x12a/+0x12c`, radius `DAT_0058dc26`), run walker, then frame advance: `person+0x14c += 1`, wrapping at `*(u16*)(clipNode->arppArray(+0x28) + 0x14)` = the clip's ARPP row count. **person+0x224 = bound ANIP clip node; +0x21c = bound BODC figure node; +0x14c = current frame.**
- `people.df` is the SAME DF container format as privanim (one reader class) - enumerate its BHAV/sections with the `DougFile` parser in `Tools/privanim_extract.py`.
- Next concrete step: find which opcode(s) write person `+0x224` (clip bind) to resolve the `DAT_0058de80` anim-id (600/750/850) -> clip-name/mnemonic mapping, then decode all 88 handlers.

**Update 2026-07-01 (evening): the BHAV breakthrough.**
- **people.df parsed with the Doug-container model: 2 sections, `BHAV` + `POSI`, 137 entries each.**
  BHAV = named behavior programs ('Random Turn', 'Idle-5..80', 'Walk-30', 'Check for Cops or
  Heli', 'cop - return to copter', 'crim - do robbery', 'Criminal Robber', 'Run a base
  (baseball)', 'Park (ambient sid-213)', 'Disappear', 'update: picked up', ... ids 256..1499).
  **CONFIRMED: DAT_0058de80 state-table values (600/700/750/800/850) are BHAV ENTRY IDS** - states
  bind programs, programs bind anims by mnemonic (clip bind = op1 -> FUN_004c68f0, person+0x224).
- **BHAV payload = [BE u16 recordCount] + count x 12-byte records; record = [u16 opcode][5 x u16
  args]**; anim mnemonics appear inline in args. POSI = editor graph layout (visual tool).
- Full opcode->handler map: `Docs/scratchpad/ghidra/out_vm_handlers.txt`; ALL ~81 handler
  decompiles saved: `out_vm_ops_00-39.txt`, `out_vm_ops_40-81.txt`; spawn config/state setup/init
  decompiles: `out_spawnrules.txt` (FUN_004c4190/FUN_004c7090/FUN_004c3010). Parse script
  pattern: scratchpad people_parse.py (reuse Tools/privanim_extract.py DougFile).
- Remaining for the port: (1) semantic map of each opcode from the saved decompiles (args,
  result codes 0/1/2/3/-1 = walker advance/loop/yield semantics), (2) state-transition triggers
  (who calls FUN_004c7090 with which state), (3) spawn modes/weights from out_spawnrules.txt +
  career.twk, (4) implement interpreter (C++ FSimCopterBehaviorVM) executing shipped BHAV records
  against a person context struct mirroring the decoded person fields.

**SESSION STATE 2026-07-01 (IMPORTANT - code written but BUILD WAS INTERRUPTED, never compiled):**
New files, all written but unverified: `Formats/SimCopterDougContainerInternal.h` +
`SimCopterDougContainer.cpp` (shared Doug parsing; privanim reader refactored to use it),
`Formats/SimCopterPeopleReader.{h,cpp}` (BHAV programs + state table GetStateProgramIds),
`Ground/SimCopterBehaviorVM.{h,cpp}` (interpreter: ops 0,1,2,3,4,5,10,11,13,14,15,17 exact;
others log + pass true), `Tests/SimCopterBehaviorVMTests.cpp` (Idle-40 = 40 yields; state
programs exist; ambient 600 runs 500 ticks). Agent: implements ISimCopterBehaviorWorld,
StartOriginalBehavior/UpdateOriginalBehavior wired into BeginPlay/ConfigureAgent/Tick;
UpdateFigureAnimation skips speed-based clip switch when bBehaviorActive. OnFoot pawn: pilot
figure via LoadOriginalBodyFigure/RebuildPlayerFigureClip + shared
FSimCopterPopulationSprite::CreateTextureFromImage (moved from anonymous helper).
**FIRST ACTION NEXT SESSION: run the editor build, fix compile errors, run the two automation
tests (SimCopter.Formats.PrivAnim + SimCopter.Behavior.VM).**

**USER-REPORTED GAP (the reason for the next decode pass):** pedestrians currently walk the
remake's route graph with no original behavior/animations - the v1 "blend" (VM gates speed,
route graph steers) is a stopgap. **Decode the CITY-SIDE data properly:**
1. `FUN_004c9220(tileX, tileY)` = tile -> tile-class (classes 2/7/10/11/12/13 per allowed
   tables); decompile it + its data (likely reads XBLD/terrain grids DAT_005910b0/DAT_005bde80).
2. `DAT_0058d6d0` (per-class 6-byte table used by spawn FUN_004c4190 via `tileClass*6`).
3. `DAT_0058e800` = 256-entry XBLD building-id -> behavior class map (who lives/works where);
   ambient programs are keyed to building sids ('Park (ambient sid-213)', 'Baseball ... sid-215').
4. Ambient spawn driver: who calls FUN_004c4190 with which mode/state per tile (find callers).
5. Then replace the route-graph blend: facing-based movement with real per-tile class checks
   (VM ops 13/14 become meaningful), spawn people at original tiles/states/figures.
Note: user saw "no animations" on walking peds in the CURRENT (pre-VM) build - verify figure
clip switching after rebuild (SpeedAlpha threshold / RebuildFigureClip) while testing.

**VM core decoded (out_vm_core.txt):** BHAV record = [u16 op][s8 trueNext][s8 falseNext]
[4 x u16 args]; advance FUN_004ce8f0: next -2 = return-TRUE (pop stack, re-dispatch in caller),
-1 = return-FALSE; handler result 0/1 = advance false/true, 2 = yield (same record next tick),
3 = stop. 10 u16 LOCALS per stack frame (person - 8 + (slot + cursor*10)*2). Op2 = expression
engine FUN_004cd0d0: operands via resolver FUN_004cd2e0 (scope 7=literal, 9=frame local,
3=person attr @+0x140+id*2), operator 0..7 = > < == += -= := *= /=. Op0=wait local counter,
op1=bind anim mnemonic, op4=bind figure, op18/19=tile-class tests (per-state list DAT_0058ec00),
op23=speed(+0x150)+=arg clamp 0..10. Head sprites: SIM3D.BMP heads are 52x25 PANORAMAS (wrap
around the head; UE uses an 8-side cylinder, HeadFaceU param tunes which U faces forward).

**Update 2026-07-01 (city-side data wired):**
- Build checkpoint from the previous interrupted pass is clean. Verified editor build plus
  `SimCopter.Formats.PrivAnim.Reference`, `SimCopter.Behavior.VM.Reference`, and new
  `SimCopter.City.PeopleRules.ClassMap` automation tests.
- `FUN_004c9220` is now ported as `FSimCopterPeopleCityRules`: XBLD byte -> initialized
  `DAT_0058e800` people tile class. `DAT_0058d6d0` was not an asset blob; it is initialized in
  `FUN_004c3010`: classes 2/3/5/7 => `{PlacementMode=1, SurfaceMode=4}`, class 4 =>
  `{1,2}`, all others => `{0,4}`.
- Traffic system now builds a `PeopleTileClasses` grid from the active city and makes pedestrian
  spawn candidates from original ambient state-0 classes `{13,11,10,12,7}` instead of road-only
  sidewalk nodes. Vehicle road graph is unchanged.
- The old pedestrian route-graph fallback is removed: traffic no longer assigns next pedestrian
  graph targets, and `ASimCopterGroundAgent::MoveStep` now uses original facing (`(facing+2)&7`)
  plus the city tile-class grid to choose short movement targets only when the destination class is
  allowed by the decoded table.
- Remaining true gaps: exact `FUN_004c02a0` in-tile spawn offset/placement semantics, ambient
  state selection from `DAT_0058ec00`/`FUN_004c2450` instead of always starting state 0, and the
  still-unported VM opcodes (VM test currently logs: ambient run 0 move steps; 8 unported opcodes).
