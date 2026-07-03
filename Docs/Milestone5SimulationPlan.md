# Milestone 5: Simulation And Missions — Decode-First Plan

## Ground Rule

Same standard as the flight model (`FSimCopterFlightModel`, commit `a83c59c`) and the people/traffic
work: **only original decoded behavior is accepted**. Every mechanic is decompiled from
`SimCopter.exe` with the headless Ghidra workflow (`Docs/DecompilationWorkflow.md`,
`Tools/Ghidra/ReverseExplore.java`), the decompiles are saved under `Docs/scratchpad/ghidra/`,
and the port reproduces the original math (16.16 fixed point, original PRNG streams, original
tick order). Anything not yet confirmed against the exe is listed below as a decode task, not
implemented from guesses. Status labels follow `Docs/MissionsAndTweakSystem.md`:
`Confirmed` / `Hypothesis` / `Decode task`.

## What Milestone 5 Already Has (done in earlier passes)

- Behavior bytecode VM, people spawn rules, figures/animations, movement, ambient driver:
  fully decoded and ported (`Docs/memory/simcopter-people-logic-next.md`).
- Original traffic AI, road graph, bridges; emergency Dijkstra router `FUN_004bef30` and the
  station→road registries are decoded but **not yet ported**.
- All mission/career tuning data transcribed from `tweak/` (`Docs/MissionsAndTweakSystem.md`):
  career cities, mission scoring tables, fire model values, `[General Miss]` scheduler values.
- Service-building registries (hospitals `DAT_0051ac7c`, police `DAT_0051ac50`, fire stations
  `DAT_0051af00`) decoded (`Docs/OriginalRuntimeBehavior.md`).
- Event/mission creator `FUN_004a7a10` (30 slots, per-type spawn behavior) and reaction
  broadcaster `FUN_004c1050` (900-series interrupt BHAVs) first-pass decoded.

What is missing is the layer that *drives* all of this: the simulation tick, the mission
scheduler, mission lifecycle/scoring, the fire simulation, and career progression.

## New Recon Anchors (2026-07-02 pass, saved as `out_m5_*.txt`)

`Confirmed` (decompiles on disk):

| Function | Role | Evidence |
| --- | --- | --- |
| `FUN_0047a760` | **Master simulation tick.** Updates the fps EMA `DAT_005039a8` (same `*7+x>>3` filter the flight model uses), then calls in fixed order: `FUN_004c0710`, mission scheduler `FUN_004a6e60`, fire update `FUN_004a4ac0`, `FUN_004a1d30`, `FUN_0049dd30`, `FUN_004a1d50`, train `FUN_004b7f40`, `FUN_004b3b80`, `FUN_004b1800`, `FUN_00488980`, `FUN_00488f90`, `FUN_0048ed00`, people `thunk_FUN_004c5fb0`, `FUN_004af3b0`, `FUN_0047fef0`, `FUN_00448040`. | `out_m5_weighttable.txt` |
| `FUN_004a6e60` | **Mission scheduler.** Concurrency cap = `[General Miss]` Max Easy (`DAT_00505fa8`) + difficulty (`DAT_004f9740`) vs active count `DAT_0057f9c8`; countdown `DAT_00505fb4` decremented by the frame EMA and re-inflated by Interval Adj (`DAT_00505fb0`) per free slot; when it goes negative and no modal UI (`DAT_005812b4`), rolls `rand()%100` (`DAT_0057f9d4`) against a **cumulative weight table** `DAT_0058173c..DAT_00581754` (7 buckets = the 7 career weights), then a difficulty-indexed sub-roll picks the concrete event mask and calls the placer. | `out_m5_scheduler_core.txt` |
| `FUN_004a6d20` | **Career weight → cumulative table.** Reads the current city record: `[0]` difficulty (also sets `DAT_004f9740 = difficulty+1`), `[1..7]` the seven weights, normalizes to percentages, accumulates into `DAT_0058173c..54`. All-zero weights ⇒ no missions. | `out_m5_weighttable.txt` |
| `FUN_00407bb0` | **Current career city record**: user game ⇒ `DAT_00518cd0`, career ⇒ `DAT_00518dcc + cityIndex(DAT_00518d64) * 0x50`. This is where `career.twk` City0..29 lands in memory. | `out_m5_weighttable.txt` |
| `FUN_004a92f0` | **Event placer.** Per event-mask tile selection: random tile near camera via `FUN_004abb30` with per-mask validity (buildings `0x70..0xdb` excluding burned ruins `0xd1..0xd3` for `0x40`/`0x20`-family; flammability flag bit `0x4` from `FUN_0049a4d0` for `0x80010`; masks `4`/`0x100` pass `(-1,-1)` so the creator picks its own site), then `FUN_004a7a10(x, y, mask)`. | `out_m5_scheduler.txt` |
| `FUN_004a73e0` | **Mission lifecycle walker.** 30 records × `0xd4` bytes at `DAT_0057f9dc..DAT_005812b4`; per-flag completion checks (delivered/rescued/doused counters vs totals), periodic reminder events `0x28..0x2d` through `FUN_004a89c0`, timer expiry (`0x800` jam ⇒ `FUN_004a01a0`), completion via `FUN_004aabf0`. | `out_m5_weighttable.txt` |
| `FUN_004a4ac0` | **Fire simulation.** Array `DAT_005ce0a0`, `0x8c` slots × `0xa0` bytes: per-flame burn countdown scaled by difficulty, matured flames on flammable tiles create the `0x80010` fire event, extinguish/cleanup path (unlinks from scene cell `DAT_005d9200`, event msgs 3/4 via `FUN_004a89c0`), growth (`FUN_0046e2c0/FUN_0046e430` axis stretch), spread roll `rand() % (difficulty*10 + spreadBase)` ⇒ `FUN_004a4fb0`, nearest-flame tracking for an audio emitter (`FUN_0042a1f0/2f0/310` channel `0xd`). | `out_m5_scheduler.txt` |
| `FUN_004ab170` | **Mission-scoring tweak binder**: `[General Miss]`→`DAT_00505fa4/fa8/fb0/506048/50604c`, `[Riot Miss]`→`DAT_00505fcc..fdc`, `[Rescue Miss]`, `[Transport Miss]`, `[Medevac Miss]`, `[Fire Miss]`(20 controls `DAT_00505ff8..506044`), etc. Gives every scoring global a name. | `out_trafficmiss.txt` |
| `FUN_004a64d0` / `FUN_004a6770` | Fire tweak binder + fire save-chunk IO (`'FIRE'`/`'CFID'` tags + "Fire Parms"). | `out_m5_parms_strings.txt` |
| `FUN_0049fd00` | Car-crash handler: marks the car wrecked, raises event `0x18`, and with chance `1/(0x40 >> difficulty)` spawns event `0x20` at the crash tile (supports `0x20` = MedEvac, see below). | `out_m5_scheduler.txt` |

`Confirmed` scheduler bucket → event-mask variants (career weight order is Fire, Crime, Rescue,
Riot, Traffic, MedEvac, Transport; sub-rolls depend on difficulty tier 1..4):

| Bucket | Career weight | Event masks rolled |
| --- | --- | --- |
| 1 | Fire | `1`, `4`, `0x100`, `0x408` |
| 2 | Crime | `0x200`, `0x2000`, `0x20000`, `0x4000` |
| 3 | Rescue | `0x80010`, `0x90`, `0x110` |
| 4 | Riot | `0x1000` |
| 5 | Traffic | `0x800` |
| 6 | MedEvac | `0x20` |
| 7 | Transport | `0x40` |

`Decode task`: the human-readable meaning of each mask (`4` and `0x100` are placed with no tile —
plane/train crash? `0x408` UFO? `0x90` vs `0x110` water vs roof rescue?) must come from
`FUN_004a7a10`'s per-mask config (`out_m5_eventcreator.txt`, saved but not yet mapped) and the
name/message tables, **not** from intuition. Note: the earlier people-pass note that guessed
"crime 0x20, building-crowd 0x40" is contradicted by the bucket order above — re-derive both.

## Phased Plan

### Phase A — Mission core (scheduler, records, lifecycle, scoring)

1. **Semantic map of `FUN_004a7a10`** (dump already saved): the `0xd4` mission-record layout,
   per-mask spawn config (people modes/counts/states, vehicles, objects), the event name table,
   and where the scheduler countdown `DAT_00505fb4` is re-armed. Cross-check each mask against
   the `[.* Miss]` scoring globals bound in `FUN_004ab170` to pin the mask→mission-type names.
2. **Decode completion/scoring**: `FUN_004aabf0` (award math vs `[.* Miss]` money/points
   globals, `(pp)` and `(*size)` scaling), `FUN_004a89c0` (event/message pipe — this is also the
   WAV/radio trigger point) and `FUN_004a8890` (record lookup). Decode failure paths
   (timer expiry penalties, `End Points Penalty`).
3. **Port** as `FSimCopterMissionSystem` (plain C++ core + UE actor bridge, same pattern as
   `FSimCopterFlightModel`): 30-slot record table, exact scheduler cadence (fps-EMA countdown,
   Max Easy + difficulty cap, cumulative weight roll, difficulty sub-rolls), exact placer tile
   checks. PRNG parity matters: this layer uses MSVC `_rand()`, not the people LFSR — port the
   LCG and its seeding.
4. **Tests** (automation, like `SimCopterFlightModelTests`): cumulative table from each of the
   30 `career.twk` cities matches `FUN_004a6d20` math; seeded scheduler produces the exact
   original mask sequence; placer accepts/rejects the exact tile sets on a reference city.

### Phase B — Fire simulation (the biggest mission type)

5. **Decode the flame record** (`0xa0` bytes: flags axis bits 2/4/8/0x10, position `+0x10..`,
   burn timer `+4`, growth size, scene-cell link `+0x20`, owning event `+0x9c`) and
   `FUN_004a4fb0` (spread target selection), ignition callers (who creates flames for event
   masks `1`/`0x408`/car fires), and the douse path — find the bucket water-drop → douse-points
   consumer (`fire.twk` `Douse Points`/`Douse Mult`/`Fire Radius` globals from `FUN_004a64d0`).
6. **Decode building destruction**: burned-ruin tile ids `0xd1..0xd3` (the placer excludes
   them; `FUN_004a92f0` treats them specially), tile swap + `Bldg Dest(neg pts)` /
   `Bldg Saved($)` accounting.
7. **Port** as a fire subsystem on the city actor (flame instances + smoke/debris rendering can
   reuse the existing ISM/population patterns), driven from the mission system in original tick
   order (scheduler → fire update, per `FUN_0047a760`). Tests: TTL/spread/douse math parity
   with `fire.twk` values; difficulty scaling.

### Phase C — Mission types on top of the people/traffic base

8. **Per-type integration** in decoded order of dependency:
   - **Traffic jam `0x800`**: attach `[Traffic Miss]` scoring + `Jam Timer` to the already-ported
     jam behavior; decode `FUN_004a01a0` (jam expiry).
   - **MedEvac `0x20`** and the car-crash chain `FUN_0049fd00` (crash → victim people, hospital
     delivery). Needs the station registries + emergency Dijkstra port (`FUN_004bef30`) for
     ambulances.
   - **Rescue `0x90`/`0x110`/`0x80010`**: victim spawn states, winch pickup (person carrier
     `+0x1a0` follow already decoded), delivery targets.
   - **Crime `0x200`/`0x2000`/`0x20000`/`0x4000`**: criminal/cop behavior classes (15 Badguy,
     14 Kopp) + the shipped `crim -`/`cop -` BHAV programs already in `people.df`; police-car
     dispatch; speeder (`[Speeder Miss]`) identification.
   - **Riot `0x1000`**: 16+ people mode 3 (first-pass decoded), 900-series reaction BHAVs via
     `FUN_004c1050`/`DAT_0058d728`, `Riot Timer`, tear-gas/quell interaction with the heli.
   - **Transport `0x40`**: building crowd spawn, passenger pickup/dropoff counting
     (`Inc Pickup`/`Inc Trans`).
   - **Fire missions `1`/`4`/`0x100`/`0x408`**: wire Phase B to scoring; decode the no-tile
     masks `4`/`0x100` (self-placing crash events) and the UFO event (`UFO Money/Points`
     globals `DAT_00506048/4c`).
9. **Remaining tick subsystems**: classify every callee of `FUN_0047a760` not yet decoded
   (`FUN_004c0710`, `FUN_004a1d30/50`, `FUN_0049dd30`, `FUN_004b3b80`, `FUN_004b1800`,
   `FUN_00488980/f90`, `FUN_0048ed00` — it also creates events, `FUN_004af3b0`, `FUN_0047fef0`,
   `FUN_00448040`) plus the train system `FUN_004b7f40`/`FUN_004b5290`. Port what belongs to
   Milestone 5; document and defer the rest explicitly.

### Phase D — Career progression and economy

10. **Decode career state**: writers of the city index `DAT_00518d64`, the city-complete check
    (`Points Needed` vs score global), award flow ("Career Advancement, Award: %ld Bucks" —
    string is reference-free, so locate its consumer via the resource/format-table `scan`
    command), money/score globals, helicopter purchase (`New Cost ($)`), and the
    city→`.sc2` binding (`cities\career\` string at `0x4f8fcc`; refs are in un-analyzed blocks —
    use `decompileforce`).
11. **Port** the career state machine + save-game-relevant state; upgrade `FSimCopterTweakReader`
    to the structured `Ctrl<i>_*`/`fxpt` model and the `sim3d.twk` tree (`Redirect`/`Prefix`),
    which Phase A..C loaders will already be consuming.
12. Radio/WAV/SMK hooks: `FUN_004a89c0` event codes are the message/audio trigger surface;
    map event id → WAV/SMK asset names as a documented table (playback itself can land with the
    media milestone).

## Working Conventions (unchanged from previous passes)

- Every decode lands as `Docs/scratchpad/ghidra/out_m5_*.txt` before any C++ is written;
  findings appended to `Docs/memory/` notes with exact addresses.
- Ports are deterministic plain-C++ cores with UE bridges, covered by automation tests that
  assert numeric parity with the decompiled math (fixed-point, PRNG streams, tick order).
- Remake-side deviations (frame rate, input, rendering) are allowed only where the original is
  fps-dependent, and are documented the way `FSimCopterFlightModel` documents its EMA cap.

## Suggested Commit Slices

1. Mission core decode + `FSimCopterMissionSystem` scheduler/records/lifecycle + tests.
2. Fire simulation decode + port + douse/scoring integration.
3. Per-mission-type integrations (one commit per type or small groups).
4. Career progression + economy + tweak-tree loader upgrade.
