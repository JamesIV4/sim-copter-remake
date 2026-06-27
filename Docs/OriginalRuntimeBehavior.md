# Original Runtime Behavior: Traffic, Terrain Slope, and Helicopter Flight

This document covers decompiled **runtime logic** from `SimCopter.exe` (as opposed to file formats, which are in `Docs/OriginalGameFileFormats.md` and `Docs/OriginalGameFileCodeWalkthrough.md`). It closes three of the reverse-engineering gaps listed in `Docs/DocumentationCoverage.md`: the original `TRAN` traffic system, the remaining `ALTM` slope bits, and the helicopter physics integrator.

Status legend (see `Docs/DecompilationWorkflow.md`): `Confirmed` = validated against decompiled code; `Hypothesis` = plausible but not fully validated; `Follow-up` = known missing work.

Evidence: Ghidra decompiles in `Docs/scratchpad/ghidra/` - `out_traffic_terrain.txt` (`FUN_004b5290`, `FUN_00495700`, `FUN_004abc20`), `out_heli_physics.txt` (`FUN_00484d20`, `FUN_00486a30`), `out_heli_tuning.txt` (`FUN_00489e20`), and the `out_heli_*.txt` xref/caller files.

Shared fact (`Confirmed`): the SC2 tile layers are stored in memory as **arrays of 128 row pointers**, not flat buffers. `DAT_005910b0` = XBLD rows, `DAT_00590800` = XBIT rows, `DAT_00590d70` = ALTM rows, `DAT_00591a80` = XTER rows. Indexing is `*(rowPtrArray[x]) [y]` with `x,y` in `0..0x7f`. The scene-graph render grid is `DAT_005d9200[x*0x100 + y]` (stride `0x100`), matching memory `simcopter-mesh-orientation-rules`.

## Traffic: the Original `TRAN` System

The remake replaced the original per-tile car steering with a clean road-graph walk (memory `simcopter-population-rendering`, `Docs/GameplayCodeWalkthrough.md`). For fidelity and for completeness, here is what the original actually did.

### Per-tile car steering: `FUN_004b5290`

`Confirmed.` This is the original "route-step" function (the `FUN_004b5290` in the address ledger). It advances one vehicle by one road tile. `param_1` is the current road tile id (an `XBLD` value; the `0x8000` bit flags special/bridge variants `0x805a`, `0x805b`). `in_ECX` is the vehicle/`TRAN` object:

| Vehicle offset | Meaning |
| --- | --- |
| `+0x11` | direction code: `1`/`2`/`4`/`8` = the four cardinal directions; `3`,`6`,`9`,`0xc`,`0x11`,`0x12`,`0x14`,`0x18` = diagonal/curve/turn codes |
| `+0x35` | committed tile X |
| `+0x39` | committed tile Y |
| `+0x55` | working tile X (`= +0x35` at entry) |
| `+0x59` | working tile Y (`= +0x39` at entry) |

Algorithm (`Confirmed`):

1. Copy committed position into the working position.
2. `switch (tileId)`: for each road shape, pick the next direction and step the working X or Y by +/-1.
   - Straight roads (e.g. `0x2c`) flip a coin (`_rand() & 1`) to choose which way along the road to go.
   - T-junctions and crossings (`0x2d`, `0x2e`, ...) coin-flip between two exits.
   - 3-way and 4-way intersections (`0x36`..`0x3a`) roll `_rand() % 3` or `_rand() & 3` to pick among 3 or 4 exits.
   - Bridge/elevated road ids (`0x45`..`0x5b`, `0x805a`/`0x805b`) use the tile's own parity bit (`XBLD & 1`) to choose direction deterministically, so bridges keep traffic flowing one consistent way.
3. Validate the candidate tile: read `XBLD` and the `XBIT & 2` flag for the next `(x,y)`; if the result is **not** a valid road continuation (the allowed ranges are road `0x2c`..`0x3e`, bridge `0x45`..`0x48`/`0x4c`..`0x4e`/`0x59`, and `0x805a`..`0x805b`), revert to the committed tile and take a **reverse step** (the second large switch), i.e. turn around at a dead-end rather than drive off the road.

`Confirmed` consequences for fidelity:

- Original traffic is **memoryless and coin-flip driven**, not pathfinding. Cars wander; they do not route to a destination. The remake's graph walk (prefer-straight 70%, no U-turn unless dead-end) is a faithful *feel* but not a byte-identical reproduction.
- Turning uses MSVCRT `_rand()` (the `_holdrand` family, see `Docs/ReverseEngineering.md`), **not** the people LFSR from `Docs/OriginalGameFileFormats.md`.
- The direction codes (`1`/`2`/`4`/`8` cardinals + turn codes) double as the animation/orientation selector, so a car's facing is implied by its step, with no separate rotation field - consistent with the no-per-tile-rotation rule for static meshes.

### The road graph: `FUN_00495700`

`Confirmed.` `FUN_00495700` is a debug dump (it writes `dump_bm.txt`), but it reveals the original road-network data structures the game builds from the `XBLD` layer:

- **Intersection node = `0x38` (56) bytes** (`DAT_0051ac80` array, "RoadGraph Struct Size = 0x38"):
  - `+0x00` byte: location X
  - `+0x01` byte: location Y
  - `+0x02` byte: turn flags. Low nibble = "turn allowed" mask N/E/S/W (`1`/`2`/`4`/`8`); high nibble = a second per-direction mask (likely the dead-end/one-way mask). The dump prints these as the string `"NESW"` with `_` for cleared bits.
  - `+0x03` byte: dead-end flag.
  - `+0x05` onward: **four connection records of `0xb` (11) bytes**, one per N/E/S/W direction. Each connection: `byte targetIndex`, `byte targetRow`, `ushort T` (turn/type), `ushort tileCount`, `ptr -> road-tile list`.
- **Road tile = 3 bytes** ("Road Struct Size = 3"). A connection's tile list is `tileCount` of these between two intersections.
- The dump prints totals: `Intersections`, `RoadTiles`, and a combined memory estimate.

`Confirmed.` The same function dumps three **service-building registries**, which are the mission target sources:

- `HOSPITALS` (`DAT_0051ac7c`) - medevac destinations.
- `POLICE STATIONS` (`DAT_0051ac50`) - crime/criminal mission origins.
- `FIRE STATIONS` (`DAT_0051af00`) - fire mission origins.

Each registry is a list (count at `+0x18`, array pointer at `+0x14`) of `0x10`-byte entries: `{ int type, ... byte locX@+4, byte locY@+5, byte nearestRoadX@+6, byte nearestRoadY@+7 }`. Each building caches its nearest road tile so vehicles/missions can route to it. This is the data a faithful mission system (`Docs/MissionsAndTweakSystem.md`) needs to place medevac/crime/fire jobs.

`Follow-up.` The graph *builder* (the function that constructs `DAT_0051ac80` from the `XBLD` grid) is referenced by this dumper but not yet decompiled. The structures above are enough to mirror it.

## `ALTM` Slope Bits (10..14)

`Confirmed.` The base altitude decode is `FUN_004abc20(row, col)`:

```c
uint FUN_004abc20(row, col):
  if (row<0 || col<0 || row>0x7f || col>0x7f) return <carry/high word>;
  altm = *(ushort*)(ALTM_rows[row] + col*2);    // DAT_00590d70
  base      =  altm        & 0x1f;               // bits 0..4   -> ground height
  secondary = (altm & 0x3e0) >> 5;               // bits 5..9   -> water/raised height
  if (base < secondary && XTER[row][col] > 0x0f) // DAT_00591a80, water/shore
      return secondary;
  return base;
```

This confirms the altitude model already implemented by the remake's `GetOriginalTerrainHeightStep` (`Docs/CityRenderingCodeWalkthrough.md`): base = bits 0-4, secondary = bits 5-9, secondary wins only when higher and the tile is water-like (`XTER > 0x0f`).

`Confirmed (new).` The remaining bits **10..14** (mask `0x7c00`, i.e. the high byte `& 0x7c`) are a **slope code**, and the test the engine uses is "is this tile flat?". In `FUN_00495700` the road classifier reads the ALTM high byte and branches on:

```c
(*(byte*)(ALTM_rows[row] + 1 + col*2) & 0x7c) == 0   // slope bits all zero == flat tile
```

So:

- `ALTM bits 10..14 == 0` -> the tile is **flat**.
- `ALTM bits 10..14 != 0` -> the tile is **sloped**, and the value encodes which corners are raised (the slope shape). This is the per-tile slope that the SC2 terrain editor produces.

`Hypothesis.` This slope code is the authoritative source for the flat-vs-sloped road/bridge **mesh variant** choice. The remake currently derives "flat?" indirectly by comparing the four `tmap` grid-corner heights (`IsOriginalTerrainTileFlat`, see memory `simcopter-mesh-orientation-rules`). Reading `ALTM` bits 10-14 directly would be a more faithful and cheaper flat test. `Follow-up`: decode the exact 5-bit slope-shape enumeration (which bit = which raised corner/edge) and switch the remake's flat test to use it.

## Helicopter Flight Model

`Confirmed.` The flight model is data-driven from `heli.twk` (`Docs/MissionsAndTweakSystem.md`) and runs every frame. Three layers:

### 1. Tuning binding: `FUN_00489e20`

Called once from the asset loader `FUN_00479bb0`. It registers each of the nine helicopter types' 14 `heli.twk` controls into a global tuning array and binds the section name (`FUN_00463520(&block, 0xe, "Jet Ranger")`, where `0xe = 14` controls). Each type occupies a **`0x5c` (92) byte tuning block**; physics code addresses a field as `&DAT_<fieldBase> + heliType * 0x5c`.

Confirmed field addresses (block 0 = Jet Ranger; add `type*0x5c` for others), cross-checked against the physics reads below:

| heli.twk control | global (block 0) | used for |
| --- | --- | --- |
| MaxBank | `0x005040ec` | clamp roll target |
| MaxSlide | `0x005040f0` | clamp slide target |
| MaxPitch | `0x005040f4` | clamp pitch target |
| PitchRate | `0x005040fc` | response-lag divisor |
| ClimbRate | `0x0050410c` | collective/climb |
| Max YawRate | `0x005040f8` | clamp yaw-rate target |
| Fuel | `0x00504120` | fuel/damage logic |

(The other controls - SlideRate/RollRate/YawRate/MaxLoad/FuelRate/NewCost/MaxDamage - occupy the rest of the 92-byte block; `MaxLoad` at `0x005040e8`, used in the ground-height calc in `FUN_00484d20`.)

### 2. Per-frame master tick: `FUN_00484d20`

`Confirmed.` This is the helicopter update, called once per frame with the aircraft struct (`param_1`). Notable struct fields:

| Aircraft offset | Meaning |
| --- | --- |
| `[0]` | helicopter type index (x `0x5c` into tuning) |
| `[1]` | state: `0` = off, `5`/`6` = crashing/crashed |
| `[2]` | flag bits: bit 0 = engine on / controls active |
| `[4]`,`[5]` | current scene-grid tile X,Y |
| `[6]`,`[7]` | new tile X,Y, computed as `(pos + 0x20000000) >> 0x16` |
| `[0x29]` | scene-graph node: position `+0x18/+0x1c/+0x20` (`+0x1c` = up/altitude), orientation matrix `+0x24` (16 dwords) |
| `[0x28]`,`[0x31]` | rotor / shadow sub-nodes (transform mirrored from `[0x29]`) |
| `[0x43]` | yaw heading (integrated) |
| `[0x45]`/`[0x46]`/`[0x47]`/`[0x4c]` | raw target roll/slide/pitch/yaw-rate inputs |
| `[0x48]`/`[0x49]`/`[0x4b]`/`[0x4a]` | smoothed (filtered) pitch/slide/roll/yaw-rate |

Per frame it: runs the engine/crash state machine; computes the current tile from the fixed-point position and **relinks the aircraft into the scene-graph cell** `DAT_005d9200[tileX*0x100 + tileY]` (linked list at cell `+0x10`) when it crosses a tile boundary; calls `FUN_00486a30` (attitude, below) and the other sub-steps (`FUN_00486e90`, `FUN_00487160`, `FUN_00487740`, `FUN_00487bb0`, `FUN_00488060`, ...); and mirrors the body transform onto the rotor and shadow sub-nodes. Crash states (`5`/`6`) drive a scripted tumble.

### 3. Attitude integrator: `FUN_00486a30`

`Confirmed.` The core flight feel. For the four control axes (pitch, roll, slide, yaw-rate) it runs the same pipeline:

1. **Accumulate** the raw input target while the control is held (`+= DAT_0057f2e0` for pitch, `DAT_0057f2b8` slide, `DAT_0057f240` yaw - these are the per-axis keyboard ramp rates).
2. **Clamp** the target to `+/- tuning_max` for that axis (`MaxPitch` `0x5040f4`, `MaxBank` `0x5040ec`, `MaxSlide` `0x5040f0`, `MaxYawRate` `0x5040f8`).
3. **First-order lag filter** toward the target: `smoothed = (smoothed*(n-1) + target) / n`, where the divisor `n` is derived from `PitchRate` and the frame time: `n = ((0x3e80000 - PitchRate) / 500) * (frameScale) >> 16`. `0x3e80000` = `1000.0` in 16.16, so a higher `PitchRate` shrinks `n` and makes the helicopter snappier. This is why `heli.twk` rates feel like responsiveness, not literal angular velocity.
4. **Integrate heading**: `yaw[0x43] += smoothedYawRate * dt`, wrapped to a full circle at `+/- 0xe100000` (the engine's full-turn constant).

Frame timing: `DAT_005039a0` is the frame delta, clamped to a minimum of `0xccc` (~0.05, i.e. a 20 fps floor) so the lag filter and integration stay stable on slow frames. Idle/auto and crash states inject small `_rand()`-based perturbations (random pitch/bank wobble of `MaxPitch>>2`, and crash-spin angles of `rand()%400+100` / `rand()%900+900`).

`Confirmed` fidelity note: the remake's `SimulateFlightStep` (`Docs/GameplayCodeWalkthrough.md`) interpolates pitch/roll/yaw toward input targets - structurally the same **input -> clamp -> lag -> integrate** model, so the remake is faithful in shape. The exact 16.16 constants (`0x3e80000`, `0xe100000`, the `/500` factor, the ramp rates `DAT_0057f2e0/2b8/240`) are now recovered and could be ported for exact tuning. `Follow-up`: the horizontal-velocity-from-attitude and position-integration sub-steps (`FUN_00488060`, `FUN_00487bb0`, etc., past the part decompiled here) are not yet fully documented; they convert the smoothed pitch/roll into world translation and apply collision/ground contact.

## Summary vs. the Old Gap List

Now documented (previously open in `Docs/DocumentationCoverage.md`):

- Original `TRAN` traffic = coin-flip per-tile steering (`FUN_004b5290`) over a road graph of `0x38`-byte intersections + 3-byte road tiles (`FUN_00495700`), with hospital/police/fire service registries for missions.
- `ALTM` slope bits 10..14 (mask `0x7c00`) = per-tile slope code; `== 0` means flat (used for road/terrain treatment).
- Helicopter flight = `heli.twk`-tuned input -> clamp -> first-order lag -> integrate, in `FUN_00486a30`, driven by the master tick `FUN_00484d20`; tuning bound by `FUN_00489e20` in `0x5c`-byte per-type blocks.

Still open (`Follow-up`): the road-graph *builder*, the exact slope-shape bit enumeration, and the helicopter velocity/position integration sub-steps.
