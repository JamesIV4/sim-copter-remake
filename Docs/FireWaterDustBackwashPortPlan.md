# Fire, Water, and Helicopter Dust-Backwash Port Plan

Status: renderer decompilation captured; semantic reconstruction and authentic Unreal port pending  
Scope: original SimCopter effect rendering in `SimCopterRemake/`  
Evidence: `Docs/scratchpad/ghidra/effect_renderer_gap_20260724.md`,
`Docs/scratchpad/ghidra/out_effects_DECODED.md`, and related Ghidra outputs

## Objective

Replace the current generic particle approximations with the original effect-pool behavior needed for:

- building and vehicle fire;
- bucket water drips and splash effects;
- rotor wash over water and land;
- downwash dust/disc effects;
- hard-landing splash, dust, debris, and fire follow-up effects.

The mission and flight state machines should remain intact. This work should replace their visual emission and update paths, not rewrite mission or helicopter gameplay semantics.

## Current port boundary

The current implementation has:

- `USimCopterParticleFXComponent` with a generic `FCard` array, camera-facing translucent quads, and simplified gravity/lifetimes;
- `ASimCopterHelicopterPawn::UpdateRopeAndBucket()` with manually generated drips and steam;
- `ASimCopterHelicopterPawn::UpdateRotorWash()` with an approximate land/water particle ring;
- `USimCopterFireRenderComponent` with FIREPTS extraction but invented height-based colors, jitter, and soft translucent quads;
- `ASimCopterMissionSystemActor::SpawnFirePlume()` with manually generated smoke and embers.

The typed pool lifecycle is now substantially decoded and represented in the Unreal code. The
blocking missing piece is the original software-renderer consumer. The earlier pass stopped at
face creation/submission and inferred Unreal colors, alpha, and shapes without decoding the
face-type handlers. Those inferred visuals are not an authentic port.

Do not tune the current soft radial materials as a substitute for this work. The renderer
decompilation pass is captured in
`Docs/scratchpad/ghidra/effect_renderer_decompile_run_20260724.md`; reconstruct its pixel
coverage, palette-remap behavior, depth scaling, and authored point placement before replacing
the approximations.

## Decompiled function coverage

| Priority | Original functions | Port purpose |
|---|---|---|
| 1 | `FUN_0046f7e0`, `LAB_004e4fcd`, `FUN_004e4fdd`, `FUN_004e54d0`, `FUN_004e5602` | Recover the renderer entry point, distance selection, and face-type dispatch |
| 2 | `FUN_00491520`, `LAB_004eac78` | Exact face type `0x17` projected point coverage |
| 3 | `FUN_00496da0`, `LAB_004eac90`, `PTR_LAB_0049a41c`, entry points `0x00496e55`, `0x00496e6e`, `0x00496e87`, `0x00496ea3`, `0x00496ebe`, `0x00496ed5` | Exact face type `0x1a` depth-scaled effect kernels |
| 4 | `FUN_004e5780`, `FUN_004e5d04`, `FUN_004e62ef`, `FUN_004ecae0`, `FUN_004ee290`, `FUN_004eeaf0` | Projection, clipping, sorting, face descriptor construction, and final dispatch |
| 5 | Tables `0x00514930`, `0x00514a10`, `0x00514af0`, `0x00514bd0`, `0x0049a41c`, `0x0049a44c`; globals/tables `DAT_005d41d0..DAT_005d41e0`, `DAT_0059ea30`, `0x00504828..0x00504840` | Distance-mode dispatch, kernel selection, palette/shade composition, and framebuffer read/modify/write behavior |
| 6 | `FUN_004a47c0`, `FUN_004a48e0`, `FUN_004a5340`, `FUN_004a4ac0`, `FUN_004a64d0`, `FUN_0046e600` | FIREPTS cloning, original flame placement, per-frame point movement, and restoration |
| Already decoded; retain as inputs | `FUN_0046edb0`, `FUN_004aeba0`, `FUN_0048da50`, `FUN_0048db20`, `FUN_0048e0b0`, `FUN_004af220`, `FUN_004af100`, `FUN_004af3b0`, `FUN_00490690`, `FUN_0048ed00`, `FUN_00488060`, `FUN_004881b0`, `FUN_00489250`, `FUN_0048a8b0` | Pool, spawner, updater, rotor-wash, downwash, and landing inputs to the renderer |
| Context only | `FUN_00449850`, `FUN_004814c0`, `FUN_004817c0`, `FUN_004e6910`, `FUN_00484d20`, `FUN_0047a760` | Confirm frame/caller integration without porting the whole game loop |

`FUN_00483c20` is not fire setup: it initializes helicopter bodies, rotors, and shadows.
`FUN_0046e2c0` and `FUN_0046e430` are geometry bounds/height queries, not flame transforms.
Neither should be used as evidence for effect appearance.

## Architecture

### 1. Typed effect pools

Extend or replace the generic `FCard` model with typed pools. Each particle should retain:

- original effect type and flags;
- position and velocity;
- age, life, and spawn sub-timer;
- original tile cell;
- size exponent and frame cursor;
- render primitive type;
- state needed by fire, smoke, splash, and wash updates.

The known pools and visuals are:

| Pool | Capacity | Visual |
|---|---:|---|
| `DAT_005d4900` | 10 | SMOKE `0xae` |
| `DAT_005d4bd0` | 10 | GEO `0x147` |
| `DAT_005d4ea0` | 2 | GEO `0x7c` |
| `DAT_005d6880` | 30 | DEBRIS `0x149`/`0x14a`/`0x14b` |
| `DAT_005d62e0` | 20 | 1-point trajectory card, rotor wash |
| `DAT_005d4f30` | 70 | 3-point trajectory card, splash/embers |
| `DAT_005d41f0` | 25 | SMOKE slot plus 4-point fire cards |
| `DAT_00581788` | 20 | SMOKE `0x148` splash columns |
| `DAT_00581c68` | 100 | SMOKE `0x148` tile puffs/splats |

### 2. Rendering primitives

Reproduce two authentic primitive families:

1. GEO billboards from the Maxis mesh library for SMOKE, DEBRIS, and related objects.
2. Small 1-, 3-, and 4-point trajectories whose single-vertex faces are consumed by the
   original `0x17` or `0x1a` rasterizer.

Face type `0x17` is now confirmed to write an exact `2x2` palette-indexed screen block in the
normal renderer and one pixel in the alternate mode. Face type `0x1a` uses one of 12
depth-scaled, randomized framebuffer-composition kernels. It is not a conventional translucent
billboard.

Do not continue using large soft radial quads. Unreal implementation technology may differ from
the original software renderer, but projected coverage, palette/remap selection, depth scaling,
random cadence, and clipping must match the decoded handlers.

### 3. Effect-world adapter

Add a small adapter around the renderer for the original concepts that Unreal does not expose directly:

- original fixed-point units and conversion to Unreal centimeters;
- world-to-effect-cell lookup;
- surface/terrain lookup;
- shared SIM3D palette lookup;
- GEO object lookup and render batching;
- active-pool and tile-list bookkeeping.

Keep this adapter independent from mission state so it can be driven by both the mission actor and helicopter pawn.

## Decompiler/re-agent workflow

The repository utility is `Tools/re-agent/.venv/Scripts/re-agent.exe`, backed by the exported Ghidra data in `.ghidra-exports/`. Use one function at a time.

### Preflight each target

```powershell
$gb = "Tools/re-agent/.venv/Scripts/ghidra-bridge.exe"
$ra = "Tools/re-agent/.venv/Scripts/re-agent.exe"
$address = "0x0046edb0"

& $gb decompile $address
& $gb xrefs-to $address
& $gb xrefs-from $address
& $ra reverse --address $address --dry-run
```

Save or refresh focused evidence files under `Docs/scratchpad/ghidra/` when the existing outputs do not answer a specific question.

### Run order

Run the utility in these bounded groups, reviewing each candidate before moving on:

1. Renderer entry and dispatch: `0046f7e0`, `004e4fcd`, `004e4fdd`, `004e54d0`,
   `004e5602`.
2. Type `0x17`: `004eac78`, `00491520`.
3. Type `0x1a`: `004eac90`, `00496da0`, all `0049a41c` kernel entry points, and all
   secondary jump-table targets.
4. Palette composition: writers for `005d41d0..005d41e0`, `0059ea30`, and
   `00504828..00504840`.
5. Authored fire placement: `004a47c0`, `004a48e0`, `004a5340`, `004a4ac0`,
   `004a64d0`, `0046e600`.

The `0x1a` entry points are jump targets inside a large optimized function. Use raw
`ReverseExplore.java disasm` and targeted function recovery when a normal decompile hides those
blocks. A small wrapper decompile is not sufficient evidence.

Example paid run after the dry-run has been reviewed:

```powershell
& $ra reverse --address 0x0046edb0 --max-rounds 3
& $ra status
```

Generated candidates belong in `Docs/scratchpad/re-agent/code/`. Treat them as review material. Before accepting a candidate, compare it against:

- the decompile;
- callers and callees;
- the relevant `out_*.txt` evidence;
- the current Unreal subsystem;
- observed original behavior.

The live pass completed on 2026-07-24. Do not resume Unreal visual changes until the semantic
renderer completion gate in `effect_renderer_gap_20260724.md` is met.

## Implementation sequence

### Phase 0: Original rasterizer recovery

Decode the face dispatch and rasterizer targets listed above. Record:

- exact screen-space coverage for face types `0x17` and `0x1a`;
- the 12 `0x1a` class kernels;
- palette/remap table construction and selection;
- depth, clipping, framebuffer-read, high-bit, and random behavior;
- class `1` versus class `2` FIREPTS behavior.

Add a reference implementation that produces deterministic indexed-color buffers for fixed
inputs. This becomes the oracle for Unreal automation and materials.

### Phase 1: Geometry, palette, and pools

Port the primitive and initialization semantics first. Establish exact palette indices, point counts, sizes, pool capacities, and lifetimes.

Expected outputs:

- typed pool definitions;
- point-card construction for 1/3/4-point effects;
- shared palette-index representation;
- GEO object lookup for SMOKE and DEBRIS;
- deterministic pool initialization tests.

### Phase 2: Spawners and surface puffs

Port `FUN_0048e0b0`, `FUN_004af220`, `FUN_004af100`, and `FUN_004af3b0`.

Preserve:

- type-specific pool selection;
- exact capacities and lifetimes;
- `FUN_004af220`'s fixed `0x20000` tile-puff countdown separately from its
  class-dependent vertical rise velocity (`0xF0000` for class 8);
- exact point-card sizes;
- tile splats emitted by the appropriate moving-particle types;
- splash-column frame progression;
- type-9 sub-particle ring emission;
- class-8 SMOKE puffs for rotor wash.

Important correction: `FUN_004881b0` does not select a separate water palette versus dust palette. It emits class 8; the SMOKE visual reads as spray or dust based on the surface beneath it.

### Phase 3: Master updater

Port `FUN_00490690` and then `FUN_0048ed00` into the typed Unreal pools.

Preserve:

- fixed-point integration;
- gravity `0x280000 * frameTime`;
- tile-cell list movement;
- periodic table-driven splat emission;
- smoke turbulence;
- fire spread/death hooks where they are visual effects rather than mission state;
- 24 type-`0xD` fire particles emitted when a building-fire column expires;
- render updates for GEO and trajectory-card primitives.

Use Unreal arrays/maps and explicit active flags instead of reproducing the original raw pointer chains.

### Phase 4: Helicopter effects

Replace the manual paths in `UpdateRopeAndBucket()` and `UpdateRotorWash()`.

#### Bucket water

`FUN_00488060` should produce type-6, 3-point drip cards from the bucket position and downward velocity. The current random generic drips should become a caller into the typed effect subsystem.

#### Rotor wash

`FUN_004881b0` should:

- require the original low-surface and minimum-altitude gates;
- derive the ground cell under the helicopter;
- create the random-yaw offset from the helicopter orientation;
- emit the class-8 SMOKE puff at the decoded offset;
- let the surface determine whether it reads as dust or spray.

#### Downwash and landing effects

Port the distance and density behavior from `FUN_00489250` for rotor wash/downwash particles.
Explicitly omit the black-square primitive/frame beneath the helicopter at the user's direction;
do not translate it into opacity and do not remove the surrounding downwash effect. Port
`FUN_0048a8b0` for hard-landing state transitions, debris, splash columns, and follow-up puffs.

### Phase 5: Fire rendering

Keep the existing mission fire state and active-flame list, but replace manual plume emission in `SpawnFirePlume()` with typed SMOKE and type-`0xD` fire effects.

Correct `USimCopterFireRenderComponent` so it uses:

- FIREPTS face type `0x1a`/light type `1` entries as runtime fire/smoke effect markers, not as
  literal palette colors;
- the decoded `FUN_00496da0` class `1` and `2` kernels and palette/remap paths;
- original projected point sizes and depth scaling;
- a 280x200 virtual low-resolution effect layer, scaled uniformly to the actual
  viewport so the full card, individual dither dots, and transparent gaps retain
  the same screen ratio at modern resolutions and remain constant on screen
  rather than constant in world centimeters;
- one rendered point per source point;
- authored positions and transformations from the flame update path;
- the same Maxis axis conversion and global city yaw as the building meshes, for
  both the FIREPTS marker template and `FUN_004a5340`'s runtime X/Y-up/Z offsets;
- the literal sparse 0x10-mode framebuffer write masks and `DAT_00505c48`
  selector reuse for radii 0..9, including the executable's radius-8 fall-through;
- exact kernel coverage rather than an opaque or soft-alpha backing quad.

`CARFIRET` is the authored fire-truck vehicle model (`ObjectName = firetruk`), not a car-fire
effect. Burning vehicles use the same runtime fire-point treatment at a smaller scale.

## Verification gates

### Automated tests

Add focused `SimCopter.Effects.*` tests for:

- pool capacities and primitive shapes;
- palette indices, sizes, and lifetimes;
- face `0x17` framebuffer coverage in both renderer modes;
- all 12 face `0x1a` kernels at fixed depths and random seeds;
- FIREPTS class `1` versus class `2` remap behavior;
- rotor-wash altitude rejection and eligible emission;
- splash-column type-9 ring emission;
- fire-column death and ember burst;
- particle movement between effect cells;
- bucket drip type and downward motion.

### Build and static checks

```powershell
git diff --check
RebuildUnrealCpp.bat
```

Run the affected Unreal automation groups separately if combined `-ExecCmds` parsing is unreliable.

### Visual scenarios

Create a repeatable debug pass covering:

- building fire;
- car fire;
- bucket dumping onto water;
- bucket dumping onto fire;
- low hover over land;
- low hover over water;
- hard water landing;
- hard land landing.

Log active pool counts, emitted effect types, palette indices, cell coordinates, and rejection reasons during these scenarios.

## Acceptance criteria

The work is complete when:

1. Fire, water, and wash effects use the typed pools and decoded primitive families.
2. Particle sizes, lifetimes, palette/remap behavior, projected coverage, cadence, gravity, and threshold gates come from decoded behavior rather than visual heuristics.
3. Rotor wash uses the same class-8 effect over land and water.
4. Fire rendering uses authored point data, original transforms, and the decoded type-`0x1a`
   kernels, with correct smoke/ember follow-up behavior.
5. Bucket drips, splash columns, sub-particle rings, landing debris, and downwash particles are visibly present; the black-square primitive is absent.
6. Focused automation passes, the Unreal build passes, and the visual scenarios show no regressions in mission or flight behavior.

## Stop criteria and risks

- Do not accept a `re-agent` candidate solely because its objective verifier passes; review pointer-heavy logic against xrefs and the existing port.
- Do not port the entire helicopter or mission function when only its effect call site is needed.
- Do not preserve the current doubled gravity or invented water/dust color branching once the native updater is in place.
- Do not accept a visually plausible radial texture, alpha value, color ramp, or particle scale
  unless it is derived from the original rasterizer.
- If a renderer helper remains ambiguous, keep the authenticity gate open and add it to the
  decompilation queue. Do not hide renderer uncertainty behind a production approximation.
