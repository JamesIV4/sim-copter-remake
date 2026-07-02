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
- Historical note: the in-tile placement, `FUN_004c2450`, and ambient opcode gaps listed here were
  closed by the follow-up passes below; keep this checkpoint only as the pre-fix state.

**Update 2026-07-01 (Fable 5 follow-up / city logic gap fill):**
- Corrected an opcode-numbering trap: `DAT_0058ef78` is sparse, so the early VM handlers are not
  the old shifted cases. Implemented/relocated the ambient-critical handlers against the exact
  table: op4 timed move, op6 figure bind no-op, op7 random local, op13 side-effect no-op, op15
  threat/object-distance probe, op16 deactivate, op17 threat response, op19 tile-class equality,
  op20 `DAT_0058ec00` behavior-class membership, op21 threat nearby, op23 speed clamp, op24
  object bearing probe, op27 reaction side effect, op28 riot create probe, op29 facing from local,
  op31 linked-object facing, op34 wander out of road/bad tile, op36 runtime-object facing, op57/85
  sound/side-effect triggers, op59/86 carried-object/player probes. The 500-tick ambient VM test
  now logs **377 move steps and 0 unported opcodes**.
- Important spawn-driver correction: `FUN_004c2450` chooses a **behavior class** (`person+0x146`,
  spawn param_1), not the BHAV state. Ambient spawn mode/state remains 0 and runs BHAV 600. The
  chosen behavior class is then used by VM op20 against `DAT_0058ec00`.
- Traffic spawning now uses the original `DAT_0058ec00` ambient spawn classes `{12,13,11,10,5,4,3}`
  instead of starting people on road class 7. Road class 7 remains a movement-allowed class via
  `DAT_0058d750`, but is no longer the primary spawn fallback. Spawned pedestrians get an initial
  behavior class chosen with the visible `FUN_004c2450` loop: special rolls 10/17, otherwise common
  random 0..9, accepted only if the row contains the tile class.
- Verified after this pass: editor build, `SimCopter.Behavior.VM.Reference`,
  `SimCopter.City.PeopleRules.ClassMap`, and `SimCopter.Formats.PrivAnim.Reference`.
- Historical note: `FUN_004c02a0`, the rare `FUN_004c7190 -> FUN_004c7170` branch, and the ambient
  unknown-opcode list were closed by the Codex follow-up below. Non-ambient runtime object stack
  semantics remain separate from city ambient pedestrian logic.

**Update 2026-07-01 (Codex follow-up; city-side gaps closed without guessing):**
- Exact people PRNG is now ported from `FUN_004ce9d0`/`FUN_004cea00`: raw step left-shifts the
  low 16 bits, XORs tap `0x1bf5` only when old AH has bit 7 set, then stores `state ^= step`;
  bounded random returns the 16-bit division remainder. This replaces the earlier right-shift LFSR.
- Exact building footprint size is ported from `FUN_004e4f80`: road/bridge ids use the original
  special cases, ids `<0x70` are size 1, and ids `>=0x70` read the decoded `DAT_004fad30` table.
  Pedestrian spawn nodes are now original scene-cell owners/centers, with XZON high-nibble owner
  filtering and same-XBLD footprint validation, not per-tile sidewalk jitter.
- Exact local spawn sampler from `FUN_004c02a0` is ported for placement modes 0/1/2/3 before the
  original collision-height rejection layer: mode 0 perimeter-biased edge sampling, mode 1 interior
  full-footprint sampling, mode 2 half-footprint sampling, mode 3 full-footprint flag-gated sampling.
  World placement follows `FUN_004c7020`: scene-cell center plus original-unit local offset.
- Ambient behavior-class choice now includes the exact `FUN_004c7190 -> FUN_004c7170` rare branch:
  initial global `DAT_0058dc3a = 65000`, threshold `>> 2`, common fallback random 0..9, and
  rare class roll 20/11 with 12/8 weighting. `FUN_004c2450` is applied as behavior-class selection,
  not BHAV state selection.
- VM opcode 22 is ported exactly at the branch level from `FUN_004cb370`: compare current tile
  against `FUN_00489610`'s tile globals (`DAT_005d70f0/f4`), then write player speed/facing into
  local slots arg0/arg1. The facing math is the exact `FUN_004c8230` octant result with `(facing-2)&7`.
  The Unreal pawn speed value is a remake bridge for the original `DAT_005040d0+0x150` scalar.
- VM op18 (no runtime-object parameter => true no-op) and op70 (snap/update vertical position,
  true) are named for ambient coverage. Unknown VM opcodes no longer pass true; they follow the
  false edge and log, so unsupported non-ambient mission/object handlers cannot silently invent
  success.
- Verification after this pass: editor build succeeded; automation passed for
  `SimCopter.Behavior.VM.Opcode22PlayerTileProbe`, `SimCopter.Behavior.VM.Reference`,
  `SimCopter.City.PeopleRules.ClassMap`, and `SimCopter.Formats.PrivAnim.Reference`. The VM
  reference test now also runs all 21 behavior classes across ambient tile classes
  `{12,13,11,10,5,4,3,7}` and asserts zero unknown opcodes.
- Remaining limitation is outside city ambient pedestrian logic: non-ambient mission/runtime-object
  stack semantics (carried victims, helicopters, emergency vehicles, criminal/cop object chasing)
  still need object graph integration before those scenarios can be called fully ported.

**Update 2026-07-01 (Fable 5 pass 2: figures, anims, ambient driver fully decoded):**
- **FUN_004c71c0 = behavior class -> figure/head/voice, applied at spawn (NOT in bytecode;
  people.df has zero op6 figure binds).** Classes 0-9 street mix (Blonde, Woman, 2woman, Child,
  5man, fatman, "BLUE", SUIT, 5.5man, SHADES); **10 = 2DOGG (dog), 17 = Coww, 20 = Elvis,
  16 = Nessie, 11 = 2blonde**, 12 Medik, 13 Fireman, 14 Kopp, 15 Badguy, 18 TubaExpert,
  19 pilot, 21 swimmer. Class 6 key "BLUE" matches no privanim figure (original binds none).
  Also sets head-image index (+0x18e), voice set (+0x18c; dogs/cows/anyone get Elvis voices
  0x2f-0x36 at 1/200 or 1/65000), clothes recolor rand(14) at +0x160, and a 1-in-65000^2
  re-roll to the celebrity classes (FUN_004c7170: 60% Elvis / 40% 2blonde).
- **FUN_004c68f0 clip remap (exact): figures keyed 2DOG/Coww play DgRn for {1Wal,1Run,Tote}
  and DgSt for everything else.** Ported into RebuildFigureClip.
- **FUN_004c6970 = post-move anim/sound selector - walking anims come from the MOVE, not op1**:
  result 0/8 -> speed 0 NoMo, 1-6 1Wal, 7+ 1Run; 1 -> FaCl (fell); 2/6/4 -> Whoa (+bump sounds);
  **5 -> met another person: face them (FUN_004c8430) then 50/50 2Gab/HipH + a voice line (the
  street conversations)**; 7/10/0xb -> NoMo. Ported: MoveStep now binds NoMo/1Wal/1Run/Whoa.
- **FUN_004c9300 move core**: heading = (facing+0x140 + 2) & 7 into direction table
  DAT_0058da98/daa0 (12-byte stride), scaled by speed; per-step mover FUN_004c9470 returns the
  result codes above; **auto-turns clockwise up to 8 retries when blocked, gated by person
  +0x16a** (so Codex's facing-scan MoveStep is faithful).
- **Ambient driver chain**: FUN_004c2ba0 spawns along the LEADING-EDGE strip (radius
  DAT_0058dc2e tiles) of a square around the camera person whenever it crosses a tile, in the
  camera velocity direction; caps: skip all if count DAT_0058dc1c > DAT_0058dc2a, per-tile spawn
  only while < DAT_0058dc22 -> FUN_004c2550 (detail gate rand(0xd - DAT_0058dc18) < 3, dc18
  init 0xc) -> FUN_004c25b0 scripted building sids (**0xd7 baseball: batter BHAV 0x4b5 + 7
  fielders BHAV 0x4b6 at fixed 16.16 offsets, all sharing ONE rand(10) team color at +0x160;
  0xd1 -> spawn class 0xc state 5; 0xd2 -> class 0xe state 7; 0xdb -> BHAV 0x4b2**) then
  FUN_004c2450 generic ambient per tile.
- **Ambient tile gate FUN_004c9cc0(x,y,0)**: requires density byte grid DAT_005bde80[x*256+y]
  > 9 (per-tile population density, written by FUN_004abce0) AND scene-cell flag 0x20 clear.
  NOT yet ported - remake spawns without the density gate.
- **figure.twk [Figure Parms] binds via FUN_004c8120** in order: dc22=Max random ambient(55),
  dc2a=Max ambient period(76), 506428=Far limit(1305), 50642c=Far boundary(716), 506430=Med(111),
  506434=Near(7), dc2e=Beaming rect radius(8), dc32=master-slow(0.1), dc26=Dont sim past dist(8),
  506bf0=feet adjust(-18), dc3a=Consider this large(4; FUN_004c3010 also writes 65000 - check
  call order before trusting either).
- **FUN_004a7a10 = 30-slot mission/EVENT creator** (name table + per-type spawn: riot 0x1000 =
  16 + rand&7*(N-2) people mode 3 (min 11 or abort); building-crowd 0x40 mode 4 with XBLD in
  0x70..0xdb; crime 0x20 mode 6; single-person events 0x200/0x2000/0x20000 state 9 modes
  10/11/12). FUN_004c1050 = reaction broadcaster: DAT_0058d728[type] -> 900-series interrupt
  BHAV pushed on people stacks with priority rules (903/909/912/915 outrank).
- Remake fixes this pass: spawner class -> figure via SetInitialBehaviorClass (dogs/cows/Elvis
  now spawn), dog/cow clip remap, post-move anim binding (fixes "no walk animations" and the
  car-like look), facing left to the VM (removed velocity-facing overwrite that used a different
  octant basis).
- Still open: density-gate port (DAT_005bde80 source - likely computable from .sc2 XPOP/density),
  bump-into-person result 5 (street chats), event/mission spawner, and the TRAFFIC (vehicle) AI
  port which is the next goal.
