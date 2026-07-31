# SimCopter mission system

*"Milestone 5 mission/career layer: scheduler + fire + lifecycle located in exe (2026-07-02); plan at Docs/Milestone5SimulationPlan.md"*

*Recorded 2026-07-28; ported into the repo 2026-07-29.*

**Milestone 5 (missions/simulation) recon done 2026-07-02; full plan: repo
`Docs/Milestone5SimulationPlan.md`; dumps `Docs/scratchpad/ghidra/out_m5_*.txt`.** Key anchors:

- **Master sim tick `FUN_0047a760`**: fps EMA, then scheduler `FUN_004a6e60` → fire `FUN_004a4ac0`
  → ~12 more subsystems in fixed order (list in plan doc). Tick order matters, port it as-is.
- **Scheduler `FUN_004a6e60`**: cap = [General Miss] Max Easy `DAT_00505fa8` + difficulty
  `DAT_004f9740` vs active `DAT_0057f9c8`; countdown `DAT_00505fb4`; `rand()%100` vs CUMULATIVE
  weight table `DAT_0058173c..54` built by `FUN_004a6d20` from the current career city record
  (`FUN_00407bb0`: `DAT_00518dcc + city*0x50`, user game `DAT_00518cd0`). Bucket order = career.twk
  weight order Fire/Crime/Rescue/Riot/Traffic/MedEvac/Transport; difficulty sub-rolls pick masks.
- **Bucket→mask table** (Fire: 1,4,0x100,0x408; Crime: 0x200,0x2000,0x20000,0x4000; Rescue:
  0x80010,0x90,0x110; Riot 0x1000; Traffic 0x800; MedEvac 0x20; Transport 0x40). **TRAP: old
  people-notes guess "crime 0x20 / building-crowd 0x40" is WRONG per this order — re-derive from
  `FUN_004a7a10` (dump saved, unmapped).**
- Placer `FUN_004a92f0` (per-mask tile checks; ruins 0xd1-0xd3 excluded; flammable = bit 0x4 from
  `FUN_0049a4d0`); lifecycle walker `FUN_004a73e0` (30 recs × 0xd4 at `DAT_0057f9dc`); scoring
  binder `FUN_004ab170` (all [.* Miss] globals 0x505fa4..0x50604c); fire array `DAT_005ce0a0`
  0x8c×0xa0; fire twk binder `FUN_004a64d0`; car crash → MedEvac chance `FUN_0049fd00`.
- This layer uses MSVC `_rand()` (LCG), NOT the people LFSR [[simcopter-people-logic-next]].
- **Incremental text ids are not zero-based from the event number.** `FUN_004aa150`
  maps rescue/transport/medevac/pickup to `0x3a7/0x3a8/0x3a9/0x3aa`, whose retail
  strings are `Sim Rescued!`, `Sim Transported!`, `Sim MedEvaced!`, and
  `Sim Picked Up!`. Traffic creation `FUN_0049fca0 -> FUN_0049fe30` posts one
  `EVT_JamCarAdded` for the initial car; megaphone message 0 posts one
  `EVT_CarCleared` per affected jammed car, and the lifecycle completes when the
  cleared/burned count reaches the added-car count.

**Fire simulation decoded + ported 2026-07-24** — full notes at repo
`Docs/scratchpad/ghidra/fire_simulation_decode_20260724.md`; **outstanding gaps
catalogued at repo `Docs/FireAndDemolitionGaps.md`** (check there before assuming
something fire-related is unported or broken). Traps worth keeping here:
- Flame `+0x0c` is the **growth step (one storey)**, NOT a render size — the remake had been
  feeding it into the visual scale. FUN_004a47c0 gives every flame the same 0x100000 scale at +0x34.
- A flame does not expire at the end of its TimeToLive: it **climbs one storey and re-arms the
  full burn**, `footprint - 1` times. That is why fires last minutes, not seconds.
- The spread clock `DAT_00505f80` is a **single global accumulator advanced once per active flame
  per frame**, so bigger fires spread sooner. Neighbour table at `0x00505f60` is 4-way, not 8-way.
- Burn-out and douse are opposite endings: expiry posts EVT_CellBurnedOut(4) + FUN_004a5fd0
  demolition; water posts EVT_ObjectCaughtFire(6), the "Bldg Saved" award.
- `FUN_004a5340`/`FUN_004a48e0`'s last arg is the event **silent** byte: the mission's own ignition
  is free, spread flames each dock the "Flame($)" penalty.

**All world hooks are now implemented (2026-07-27).** The last four - plane crash 0x4, train crash
0x100, boat rescue 0x90, train rescue 0x110 - hang off the ported ambient vehicle pools; see
[[simcopter-ambient-vehicles]] for the traps (the second "plane" is the UFO, the crash tuning
controls are bound but dead, category 4 means "retire silently"). Rescue pickup/delivery
(`EVT_RescueDelivered`, spawn modes 1/2/0x13 per `FUN_004ccf50`) was also unimplemented until then,
so fire rescues could never complete either.
