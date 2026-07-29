# Memory Index

This directory is the canonical memory for the project. Read this index at the start of a
session. Notes go here, scratch files go in `Docs/scratchpad/` — see the workspace conventions.

Working instructions (build, tests, porting rules, style) are in `AGENTS.md` at the repo root.

## Working practice

- [Agent workspace conventions](agent-workspace-conventions.md) — memories and scratch files live IN THE REPO (`Docs/memory/`, `Docs/scratchpad/`), never in an agent's machine-local dirs.
- [Building and running](build-and-run.md) — always compile with `RebuildUnrealCpp.bat` at the repo root (it pins `-NoLiveCoding`); feed it empty stdin or its trailing `pause` hangs.
- [SimCopter Ghidra workflow](simcopter-ghidra-workflow.md) — MAIN PATH: ghidra-bridge instant queries over `.ghidra-exports`; re-agent for parity/LLM loops; analyzeHeadless only for scan/bytes/decompileforce.
- [SimCopter live memory rip](simcopter-live-memory-rip.md) — read the live process with ReadProcessMemory for `.data` ONLY (SimCopterX relocates `.text` in memory; read code from Ghidra); per-region `.data` calibration.
- [SimCopter in-game verification](simcopter-ingame-verification.md) — launch `-game -windowed` and drive/screenshot the Slate UI from PowerShell; poll the log for readiness; centred panels shift and stale click coords silently no-op.

## File formats and city data

- [SimCopter mesh orientation rules](simcopter-mesh-orientation-rules.md) — the original uses no per-tile mesh rotation; the col axis is negated in world placement.
- [SimCopter privanim DECODED](simcopter-privanim-decoded.md) — exact DF container spec; 21 named figures incl. Elvis/Nessie; ARPP = per-frame line segments; the OLD display rules were wrong; `people.df` = same container.
- [SimCopter terrain flattening](simcopter-terrain-flattening.md) — FUN_004abce0 tmap conditioning decoded+ported: flatten under buildings/flat roads, +0x20 ramps under raised spans 0x3f-0x42, water dip; single raster sweep, order matters.
- [SimCopter instanced buildings](simcopter-instanced-buildings.md) — buildings are per-model runtime UStaticMesh instances (103 models / 2624 placements), not baked into the merged city mesh, so one can be removed when it burns down.
- [SimCopter airport spawn](simcopter-airport-spawn.md) — FUN_004829f0 STAMPS the airport over the SC2 city (XBLD 0xf6 + 0xde) and levels the 5x5 corner patch; finding the block is only half the port, and there is no separate "first helicopter" placer.

## Flight and the helicopter

- [SimCopter heli flight model DECODED](simcopter-heli-flight-model.md) — physics/controls/rotor decoded+ported as `FSimCopterFlightModel` (16.16); traps: tenth-deg units, speed = smoothed pitch, SlideRate (not PitchRate) ramps pitch keys, rotor gate 300, fps-capped EMA.
- [SimCopter heli tools/models](simcopter-heli-tools-models.md) — tools+registry decoded; traps: tear gas is interaction mode 5 not 7, stowed flags are 1=raised and the rope node counts DOWN, FUN_00489250 is the spotlight not a downwash disc, runtime type order != twk/shop order.
- [SimCopter water gameplay](simcopter-water-gameplay.md) — the bucket never douses directly; particle impact does, at strength = remaining life; a water cannon exists and is unported.
- [SimCopter cockpit flaps](simcopter-cockpit-flaps.md) — tool flaps decoded; the click-box table sits in an UNANALYZED Ghidra gap (scan `.text` for pointer refs); flapbtn frames are unequal widths.

## Population, traffic and agents

- [SimCopter people logic](simcopter-people-logic-next.md) — the big one; traps: dispatch table 0x58ef78 is SPARSE (opcode numbering!), the PRNG is a left-shift xor, figures bind by behavior class at spawn (dog=10/cow=17/Elvis=20), walk anims come from the post-move selector.
- [SimCopter population rendering](simcopter-population-rendering.md) — car headlights are face-type-11 beam cards; people aren't in GEO (use a procedural box body); ground agents must Camera-trace from high up or they hover.
- [SimCopter UE figure component](simcopter-ue-figure-component.md) — privanim figures render in UE; ARPP Z is y-down (negate!); unity builds disabled in `Build.cs`; engine at `C:\GameDev\UE_5.8`.
- [SimCopter ambient vehicles](simcopter-ambient-vehicles.md) — planes/trains/boats + the four crash/rescue missions ported; plane slot 1 IS the UFO, boat slot 0 is CAPBOAT1, Plane/Train Crash tuning is bound-but-dead, a plane ditching on water makes a boat rescue, and only cities with rail (e.g. 12) can show a train.

## Missions, dispatch and effects

- [SimCopter mission system (M5)](simcopter-mission-system.md) — scheduler/fire/lifecycle; plan at `Docs/Milestone5SimulationPlan.md`; the old 0x20/0x40 event-mask guesses were wrong; the mission layer uses MSVC rand, not the people LFSR. Fire sim decoded+ported (flames CLIMB and re-arm; +0x0c is a growth step not a size).
- [SimCopter emergency dispatch](simcopter-emergency-dispatch.md) — F2-F5 decoded+ported; traps: Ghidra mis-types FUN_004bc680 (read the asm), the "message id" IS the body's GEO object id, FUN_0042de60(1) is the Shift key, svc 3 vs 4 is the initial STATE not a count.
- [SimCopter speeder pursuit](simcopter-speeder-pursuit.md) — the searchlight IS the mechanic (obj[0x11b] is an illumination counter); police cannot stop an unlit speeder; FUN_004b89a0 isn't in the Ghidra exports — read `.rdata`.
- [SimCopter fire/water FX](simcopter-fire-water-fx.md) — FIREPTS(0x120) = 22 POINT sprites not a mesh; effect cards are flat palette-coloured (no texture) with a radial soft-alpha material; rotor-wash = FUN_004881b0; fire/car-fire/bucket-douse/wind-kickback rendering added.
- [SimCopter vertex animation via WPO](simcopter-vertex-animation-wpo.md) — animate/displace city mesh verts with a material World Position Offset (`M_SimCopterWater`), NOT CPU `UpdateMeshSection`; GPU-only, works in editor+game; bake control data into vertex colors.

## UI

- [SimCopter hangar shell](simcopter-hangar-shell.md) — catalog/log/inventory ported; the shell's TEXT is in the exe's Win32 STRINGTABLE (NOT the Ghidra `.rdata` export); the hangar belongs on the airport's TERMINAL plot (demolish 0x096+0x165 first or the base slab z-fights); its skin is SIM3D page 40 cells 20-23/61; all nine `CAT_*T` tab strips share one hit-rect table; overlap events miss a teleported pawn.
