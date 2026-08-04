# SimCopter people logic — decoded + ported

*The behavior VM, spawn rules and walked-surface handling for pedestrians. The longest note here;
the filename keeps its original "next" suffix from when this was still an open goal.*

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

**Update 2026-07-02 (overnight: ORIGINAL TRAFFIC/CAR SYSTEM decoded + ported; bridge fix):**
- **The original precomputes a RoadGraph** (debug dumper FUN_00495700 writes dump_bm.txt):
  per-column intersection lists at DAT_0051ac80 (count bytes at +0x200), intersection struct
  0x38 bytes: +0 x, +1 y, +2 turnFlags (low nibble = NESW connects, high nibble = deadEnd),
  +3 = highway-crossover flag (XBLD 0x69), +4 = altitude>>4, then 4 exits x 0xb bytes:
  [0]=target intersection idx, [1]=target column, [2]=bearing, [3..4]=u16 travel cost T,
  [5..6]=segment count, [7..a]=ptr to 3-byte segments ([0]=dir bits+flags, [1]=length,
  [2]=extra). Builder driver FUN_00491d10 (intersection = FUN_004bb900(x,y) && (highway id or
  both coords odd... highway 2x2 rule), exit tracing FUN_004932d0/FUN_00494550, link
  FUN_00492fc0).
- **Emergency routing = Dijkstra over intersections** (FUN_004bef30, binary heap at
  DAT_0051af08, cost = summed exit T, back-pointers at +0x31/+0x32, exit dir +0x33).
  Hospital/police/fire stations have building->nearest-road records (DAT_0051ac7c /
  DAT_0051ac50 / DAT_0051af00, 0x10 bytes: dir, loc x/y, road x/y).
- **Ambient cars wander with UNIFORM random turns** per tile-type transition choosers (the
  decoded rail twin FUN_004b5290 shows the pattern: switch on XBLD, rand()%2 / %3 / &3 among
  exits, direction bitmask at obj+0x11, next tile at +0x55/+0x59; the 0x8000 bit on the tile
  id = raised/bridge deck flag from DAT_00590800 bit 1).
- **Road family for cars (exact, from FUN_00495700 range checks): [0x1d,0x2b] roads,
  [0x3f,0x46] sloped roads/power crossings, [0x49,0x59] BRIDGES, [0x5d,0x6b] onramps +
  highways.** Bridge decks sit on bWater tiles - THE bridge bug in the remake was
  (a) bWater rejection of road nodes and (b) bridge ids missing from the road family.
- SC2K overlay grids all live in the sim state (FUN_00467070 allocator): TrafficMap(64x64),
  PolluteMap, ValueMap, CrimeMap, PoliceMap/FireMap/PopMap/ROGMap(32x32), XBLD grid auxPtr1 =
  DAT_005910b0, BitsMap = DAT_00590800 (bit 1 = raised/deck), AltMap u16 = DAT_00590d70
  (bits 0-4 base alt, 5-9 water alt).
- **Remake port (SimCopterTrafficSystemActor): ESimCopterTrafficAiMode {Original (default),
  Modernized}.** Original mode = no traffic lights, no blockage recovery, uniform random
  turns (bPreferStraight=false in ChooseNextRouteNode), queue-behind-blockers (jams), and
  ApplyPlayerRoadBlocking (grounded player within lane look-ahead stops cars - "You Blocked
  Traffic!"). Modernized = the stoplight system (ApplyTrafficLights: per-approach queue slots,
  TrafficLightPhaseSeconds per axis, staggered by intersection tile parity) plus blockage
  recovery and straight-preference turns. **The mode IS the stoplight switch** — ApplyTrafficLights
  is only reached on the Modernized branch of ApplyTrafficRules and nothing logs that it was
  skipped, so "the lights stopped working" always means the default moved. Turned on
  2026-08-04 at 5 s per phase and turned straight back off the same day: the project wants the
  decoded no-lights traffic. Note the two rules are exclusive: ApplyPlayerRoadBlocking does not
  run in Modernized, so a landed helicopter does not stop traffic there. Road family + no-bWater
  fix applied to node building (cars now cross bridges; Z comes from agent ground snap hitting
  the bridge deck mesh). Whole-map far cars share the same graph/turn rules.
- Not yet ported: intersection-graph Dijkstra for emergency vehicles, station->road spawn
  records, per-segment speed byte, train system (FUN_004b5290).

**Update 2026-07-02 (rooftop pedestrians: move-step Z rules decoded, FUN_004c9470):**
- FUN_004c9470 (the per-step check called by move core FUN_004c9300) decoded to
  Docs/scratchpad/ghidra/out_movecheck.txt. Sequence per step:
  1. New Z := FUN_004c82c0(newX, oldZ, newY) + 0x30000 - i.e. the walker Z IS warped to the
     walked surface each step ("warp on top of whatever you walk on").
  2. **MAX-CLIMB GATE**: if (newZ - oldZ) > (person+0x144 << 16) the move is REJECTED with
     result 1 (binds FaCl recoil clip; move core turns to next facing). Drop below
     -(0x8000 + max) -> result 2 (Whoa). This is why originals never stand on buildings:
     the surface warp exists but big climbs bounce off. Player (+0x12e == 32000) gets a
     boosted allowance via FUN_0042de60(1) (+0x140000).
  3. Object hit via FUN_004c9000 -> result 4 (object) or 5 (bumped person: broadcasts
     reaction 0xd via FUN_004c1050 - street-chat trigger), 6 (attached-object collision),
     10 (hit own bound object).
  4. On tile change: class := FUN_004c9220; FUN_004c9cc0/FUN_004c9dc0 gates (fail -> 0xb);
     ambient (+0x168 != 0) requires DAT_0058ec00[row=person+0x146] contains new class ->
     else result 3; ALSO a PER-TILE OCCUPANCY CAP: count figures with flag bit3 on target
     tile list (DAT_005d9200[x*0x100+y]+0x10) must be < DAT_0058d6d4[class*3].
- **SCALE (critical)**: original positions are 16.16 fixed, tile = 64 units (coords = pos>>22);
  max climb person+0x144 = 5 units (FUN_004c7d10) = 5/64 of a tile ~= 31cm at the remake's
  400cm tile. One-story roofs at the remake scale (BuildingHeightScale 150) sit ~150cm up, so
  any probe margin near 200cm STARTS ABOVE SMALL ROOFS and snaps peds onto them (first-fix bug).
- Remake fix (final): pedestrian ground probe anchors at the tile's terrain Z (traffic actor
  TryGetTerrainWorldZAtWorldLocation, new) + 40cm on building tile classes 10-13 (flat by
  construction; margin only needs to clear curb/road detail) and +40+110cm slope headroom on
  non-building classes (sloped parks/trees/roads rise up to half a terrain step above the
  tile-center Z, and have no roofs). Falls back to terrain Z when the probe misses inside
  building geometry; self-heals agents already on roofs. Vehicles keep the tall probe (bridge
  decks). Far whole-map ped records already used TileCenterWorldZ and were never affected.
- Still open from this decode: per-tile occupancy cap DAT_0058d6d4, bump result 5 chats.

**Update 2026-07-02 (movement/animation cadence + shipped ambient BHAV fully decoded; jerk /
walk-in-place / building walk-through fixed):**
- **Move speed is person+0x164 (attr 18), NOT op23's +0x150.** The op4 timed-move handler
  (FUN_004ca7d0) passes +0x164 into the move core, and the shipped programs assign it directly
  with expressions: 'Wander out of the road' 6, 'random motion' 8/12/16 (rand(4): 0 = HipH dance,
  else n*4+4), 'Run-10' 25, flee 18, rescue/transport approach 16, 'Run laterally' 3. +0x150 is
  the "logic" speed compared/incremented by riot programs (op23) and read for the player by op22.
- **Per-tick displacement = octantDir * moveSpeed / 12 original units.** FUN_004c3010 builds
  DAT_0058da98 as 16.16 unit octant vectors (compass (0,-1) start, heading = (facing+2)&7)
  fixed-DIVIDED by 0xc0000 (12.0) via FUN_0046c4bf; FUN_004c9300 multiplies by speed<<16. No
  target seeking, no deceleration - constant velocity per tick, instant 45-degree facing snaps.
- **The post-move selector (FUN_004c6970) runs EVERY move tick** and rebinds the clip from
  result+speed: 0/8 with speed 0 -> NoMo, 1-6 -> 1Wal, 7+ -> 1Run, 1 -> FaCl, 2/6/4 -> Whoa,
  5 -> face bumped person + 2Gab/HipH. **On counter exhaustion op4 still calls it with result 8 /
  speed 0 -> binds NoMo** - this ends walk anims when a burst stops (the remake's walking-in-place
  bug came from missing this).
- **Figure frames advance +1 per behavior tick** in the driver FUN_004c6450 (wrap at ARPP rows),
  so clip playback rate == tick rate, not wall-clock.
- **FUN_004c82c0 (walked surface) = max(object tops, terrain) at the point** (scene-cell object
  list DAT_005d9200, else FUN_004ae7a0 terrain). With the climb gate (5 units up, 5.5 down) this
  is what stops people at building WALLS: the surface inside a footprint is the roof. Tile-class
  checks alone allow building tiles - the remake walked people through building interiors until
  this gate was ported (top-down trace at the step target).
- **Programs toggle attr 21 (+0x16a) auto-turn and attr 40 (+0x190) "move through walls"**
  (BHAV 308 sets it after >4 move fails; fail counter attr 41). Ambient class-row gating
  (DAT_0058ec00) applies only while attr 20 (+0x168 ambient) is set; 'Rxn: Run away' clears
  ambient while fleeing.
- **Shipped ambient graph (people.df BHAV 600 + subprograms, dumped via scratchpad dump_bhav.py
  reusing Tools/privanim_extract.py DougFile):** 600 sets ambient:=1 then loops { 'Wander out of
  the road' (op34 speed 6, scatter retries) -> 'random motion' -> riot check -> gawk hooks ->
  1-in-12 'look for spotlight' (op22 same tile as player -> face player, bind 'Wave', wait 15,
  restore facing) }. 'random motion' = rand(140)-tick straight walk at 8/12/16 with 1-in-4 HipH
  dance instead; blocked -> idle rand(20)+1 + Idle-20, then facing := rand(7). 'Scatter
  direction' = facing += rand(3)-1.
- Remake port (this pass): MoveStep reads MoveSpeed attr, commands a per-tick constant velocity
  (renewed each VM tick, 1.25-tick TTL; no SetMoveTarget, no arrival stop - fixes the pulse);
  climb/drop gate via top-down ECC_Camera trace at the step target; op4 exhaustion binds NoMo;
  frames advance per VM tick; yaw snaps from the facing attribute (op29 turns show while
  standing); BehaviorTickRate default 15. New attrs in EBhavAttr: MoveSpeed 18, AmbientFlag 20,
  AutoTurn 21, MoveThroughWalls 40, MoveFailCounter 41.

**Update 2026-07-29 (the opcode table is finished: every shipped record site is ported).**
Full decode + port notes: `Docs/scratchpad/ghidra/people_vm_opcode_table_20260728.md` (second-pass
section). 73 of the 81 table entries are implemented; `opcode_map.py`'s "used by shipped programs but
NOT ported" list is finally empty, and the eight left over (42/43/45/49/52/64/65/81) appear in no
`people.df` record.
- **Op 50 = free seats.** `FUN_0048c1e0` on the seat manifest at `DAT_005040d0+0x1d4` is
  `capacity(+4) - occupied(+8)`; an emergency vehicle (obj+0xc flag 0x10, the field `FUN_004c4e10`
  copies into person+0x170) answers the flat constant 0x1721. All three sites follow it with
  `local > 0`, so it is the gate an officer boards through - a full cabin makes them wait.
- **Op 78 + state 16 = the UFO abduction.** BHAV 666 'Porkchop' is state 16; `FUN_004c0f40` puts a
  person into it with the saucer in person+0x1a8, and the driver is `FUN_004c0d10`, called from
  `FUN_004b2630`'s non-PLANE1 arm. **`DAT_0058dc3a` is 65000 at runtime, not figure.twk's 4** -
  `FUN_004c3010` stores it after `FUN_004c8120` binds the tweak - so the roll is 1-in-16250 per UFO
  tick, then a coin flip per person slot. Op 78 itself teleports movespeed WHOLE units per tick
  (no `/12`, no move check, no climb gate) and is done on arrival or past 21 tiles from the camera.
- **Op 80 = the street conversation**, closing the "bump result 5 chats" gap. `FUN_004c9470` calls
  `FUN_004c1050(0xd, me, them, -1, person+0x180)`; mode 13 with a non-zero param_5 uses **that value
  as the program id**, and attribute 32 (+0x180) is 916 for cops/paramedics, 0 (-> table entry 914)
  for everyone else. Move result 5 blocks the step and turns the walker one octant on.
- **The ambient stubs behind BHAV 600's hooks were the expensive ones.** 600 calls 270 (riot),
  274 (fire) and 273 (corpse), so ops 24/27/28/31/36 were dead for every person in the city.
  Op 24 measures crowd count + mean agitation + bearing (agitation is +0x150, NOT a walk speed);
  op 28 converts the person into a state-3 rioter on the live 0x1000 record; op 27 halves their body
  radius (+0x1c4, which is also the bump radius) when agitated; op 31 faces AWAY from the selection;
  op 36 finds the nearest cell with flag 0x20 (fire) and BHAV 274 runs toward it from 6+ tiles,
  gawks at 4-5, flees under 4.
- **Op 35 is not a despawn.** `FUN_004c9b50` posts EVT_PersonDied on the old record, creates a fresh
  MedEvac record and makes the person its state-6 victim - a swoon becomes a real casualty, capped at
  difficulty + 2 live medevacs. Ops 28 and 35 must YIELD in the remake, not Stop: Stop is wired to
  the despawn path and would delete what they just created.
- **Op 79 is a stopwatch.** `DAT_00506448` is the people tick counter (`FUN_004c5fb0`), and BHAV 444
  subtracts two samples to time the tuba player's notes. That function also pins the original
  behaviour tick rate: `DAT_00506450 = 0x147a` = 0.08 s = **12.5 Hz** (the remake runs 15).
- Also decoded here: person+0x15e = "written off" (`FUN_004c0ba0` sets it on everyone aboard when the
  helicopter is destroyed), attribute 33 (+0x182) = "recently spooked" (BHAV 600 rec[14] holds off
  the gawk hooks until a 1-in-6 roll clears it), and `FUN_004c1050`'s exact acceptance tests -
  including **state 6 never reacts to anything**.
- Still open: op 15 **class 15** (the corpse BHAV 273 gawks at) needs a capstone pass over the
  jump-table body at `0x004caee5`, and cell flag 0x20's writer is not in the Ghidra exports, so
  "0x20 = alight" rests on the program name plus the spawn gate.
- **Opcode 24 (the riot crowd measure) cannot fail on a calm crowd** (2026-08-01). `FUN_004c9f10`
  is a **`void`** function: with nobody in range, or a total agitation of zero, it writes
  `bearing = 0xffff` and `mean = 0` but still reports the head count, and `FUN_004cb480` returns 1
  regardless. Only "no live 0x1000 record" fails. Porting the calm case as a failure deadlocked
  every riot - BHAV 852 returned before its `speed += 1`, so agitation never left 0, and BHAV 311
  `Rioter maybe leave riot` (which retires anyone under 3) dispersed the whole crowd on its first
  tick, completing the mission the instant it was created. The seed is now pinned too:
  **`FUN_004c4190`'s spawn-mode-3 arm writes literal 7 to person+0x150** before placement. The
  remake uses that exact value (`SimCopterMissions::RioterSpawnAgitation`), not the former inferred
  threshold value 3. A riot requests 9..30 people depending on difficulty and creation succeeds
  only when at least 11 actual pedestrian actors spawn; those actors remain until megaphone/tear
  gas, calming, arrest, or casualty outcomes satisfy the record.
