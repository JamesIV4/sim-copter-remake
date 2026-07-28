# Planes, Trains and Boats — decode notes (2026-07-27)

Decoded from `SimCopter.exe` (PE32, base 0x400000) with the `.ghidra-exports` bridge, plus raw
`.rdata` vtable reads and capstone disassembly for the functions Ghidra left in unanalyzed gaps.

This closes the last four stub world hooks in `FSimCopterMissionSystem` — plane crash (`0x4`),
train crash (`0x100`), boat rescue (`0x90`) and train rescue (`0x110`) — and documents the three
ambient vehicle subsystems they hang off.

## 1. The three object pools

| Pool | Global | Slots | Save chunk | Record size | Tick (from `FUN_0047a760`) |
| ---- | ------ | ----- | ---------- | ----------- | -------------------------- |
| Planes | `DAT_00582910` | 2 | `'PLAN'` | `0xbc` | `FUN_004b3b80` → `FUN_004b2330` |
| Boats  | `DAT_00582840` | 3 | `'BOAT'` | `0xe3` | `FUN_004b1800` |
| Train  | `DAT_00582afc` | 1 | `'TRAN'` | `0x1d9` | `FUN_004b7f40` → `FUN_004b4440` |

Boat and train records are byte-packed (`#pragma pack(1)`), hence the unaligned offsets.

### GEO model ids (confirmed against `GEO/SIM3D2.MAX` object ids — `FMaxisMeshLibrary::FindObjectByObjectId`)

| Id | Table name | Used by |
| -- | ---------- | ------- |
| `0x12d` (301) | `TRAIN1` | train locomotive |
| `0x12e` (302) | `PLANE1` | **plane slot 0** — the only crashable plane |
| `0x12f` (303) | `BOAT1`  | **boat slots 1 and 2** — the ambient boats |
| `0x14c` (332) | `TRAIN2` | train car 1 |
| `0x14d` (333) | `TRAIN3` | train car 2 |
| `0x163` (355) | `CAPBOAT1` | **boat slot 0** — the capsized boat used by the rescue mission |
| `0x17c` (380) | `UFO`    | **plane slot 1** — the UFO, not a second aircraft |

`FUN_004b3a10` (plane factory) gives the first plane `0x12e` and every later one `0x17c`
(`DAT_00506258` latches the first). `FUN_004af6f0` (boat factory) gives slot 0 `0x163` and slots
1..2 `0x12f`. `FUN_004b4250` (train factory) always sets `0x12d/0x14c/0x14d`.

**The second "plane" is the UFO.** `FUN_004b2910` posts mission event `0x27`
(`EVT_UfoResolved`, which pays `[General Miss] UFO Money/Points`) when a non-`0x12e` plane starts
its crash, and `FUN_004b3ba0` counts ten hits on it before it goes down. That resolves the
"where does the UFO come from" gap in `Docs/Milestone5SimulationPlan.md` — it is a plane-pool
object, not a mission the placer can create (`FUN_004a92f0` has no `0x100000` case).

## 2. Planes (`FUN_004b2330` and friends)

### Record layout (`0xbc`, 4-byte aligned)

| Offset | Meaning |
| ------ | ------- |
| `+0x00` | vtable (`PTR_FUN_004f4ca0`) |
| `+0x04` | byte: allocated |
| `+0x05` | byte: linked into the scene cell (visible) |
| `+0x06` | byte: **crash requested** (set by `FUN_004b3aa0`) |
| `+0x07` | byte: **crashing** (falling) |
| `+0x08/+0x0c/+0x10` | direction vector (16.16) |
| `+0x14` | remaining distance in the current segment |
| `+0x18` | current speed; `+0x1c` cruise speed (`0x780000` = 120.0/s) |
| `+0x20/+0x24` | tile x/y |
| `+0x30` | respawn delay (`0x3c0000` = 60 s); `+0x34` respawn accumulator |
| `+0x38` | per-frame vertical step |
| `+0x3c` | owning mission event id |
| `+0x48` | effect timer (`0xb40000` = 180 s); `+0x4c/+0x50` UFO hit counters |
| `+0x54` | GEO model id |
| `+0x58` | scene-cell display-list node |
| `+0x60` | mesh instance; `+0x66` slot index |
| `+0x70/+0x74/+0x78` | world position (16.16) |
| `+0x7c` | 4x4 orientation matrix (16 dwords) |

### Behaviour

- **`FUN_004b2330`** (per plane, per frame): when not visible, accumulate `+0x34` and call
  vtable `+0x10` (`FUN_004b3530`, respawn) once it passes `+0x30`. The UFO additionally requires
  `DAT_00504084 != 0` and `+0x50 <= 9`. Then vtable `+0x04` (`FUN_004b2630`, move) and push the
  transform to the renderer. `FUN_004b23e0` drives the engine loops (sounds `0x1b`/`0x1c` for the
  airliner, `0x23` for the UFO) with distance attenuation out to `0x7800000`.
- **`FUN_004b3530`** (respawn): picks one of four quadrants off the camera forward vector
  (`DAT_0061a664/8/c`), places the plane `viewRange/2` tiles away, sets altitude to the cell
  height plus `0x17c0000` (380.0) — or `buildingTop + 0x1e0000` where that is higher — points it
  at the camera tile with a random ±30° yaw offset, then `FUN_004b3420`.
- **`FUN_004b3420`** (new segment): 1-in-8 chance to turn by `(800 - rand%1600)` tenth-degrees
  (±80°); sets segment length `+0x14 = 0x200000` (32.0) and the vertical step `+0x38` to
  `((cellY + 30) - y + max(buildingTop, 0x15e0000)) >> 4`.
- **`FUN_004b2630`** (move): despawns (unlinks) past `(viewRange >> 1) + 4` tiles from the camera.
  Otherwise the UFO ticks its 180-s effect spawn (`FUN_0048e0b0` type `0xb`), then `+0x06` starts
  the crash, `+0x07` runs it, and otherwise `FUN_004b2ab0` advances the position; when the segment
  is used up the plane re-links to its new cell and calls `FUN_004b3420`.
- Vtable `+0x08` = `FUN_004b28e0`: `speed = cruise`, forced to 1 when `DAT_0050625c == 0`.
  Vtable `+0x0c` = `FUN_004b2900`: `return 0` (always).

### Plane crash

`FUN_004b3aa0(eventId)` — **the `0x4` hook**, called from `FUN_004a7a10`. Walks the two slots for
one whose `+0x06` and `+0x07` are clear **and whose model is `0x12e`**, sets `+0x06 = 1` and
`+0x3c = eventId`, returns 1. Returning 0 fails the mission creation. On success the record's
category becomes `2` (background), so a plane crash is not counted against `Max Easy` and does not
become a real mission until it hits something.

`FUN_004b2910` (crash start, `+0x06`): for the UFO, post `EVT_UfoResolved`. Requires
`FUN_0049ad30` (the plane must be over a valid cell). Aims the airliner at the cell centre below
it; the UFO gets a random `(1 - rand%3, -1, 1 - rand%3)` dive. Sets `+0x07 = 1`, `+0x06 = 0`,
`+0x48 = 0` and plays sound `0x1d` (airliner) or `0x2d` (UFO).

`FUN_004b2ab0` (crash update, `+0x07`): moves along the dive vector, sprays damage into the cell
every `0x3333` ticks, and calls `FUN_004b2cd0` for the impact test.

`FUN_004b2cd0` (impact) is where the mission actually appears:

1. Ray-test the motion against every object in the cell (`FUN_00491370` / `FUN_0046efe0`), else
   against the terrain (`FUN_004912b0`).
2. On a hit: `FUN_004af100(cell, dx, dy, dz, 4, eventId)` damages the cell contents, sound `0x1a`,
   unlink the plane, clear `+0x05`/`+0x07`.
3. **Terrain hit on water** (`DAT_005bde80[tile] < 10`): `FUN_004a7a10(tile, 0x90)` — the ditched
   airliner becomes a **boat rescue** — and the plane's own record is retired with
   `EVT_SetCategory` value `4` (expire silently).
4. **Otherwise**: if the tile is fire-suitable (`FUN_004a5f60`), not already flagged `0x20`, and
   has no fire within the `FUN_004a6860` spiral, create a **building fire** (`FUN_004a7a10(tile, 1)`)
   and again retire the plane record with `EVT_SetCategory` 4, re-pointing `+0x3c` at the new fire.
5. If no fire could be created, spawn `3 + rand % cellRadius` debris effects (`FUN_0048e0b0` type 4,
   random yaw over 3600 tenth-degrees, pitch `750 + rand%120`, life `25 + rand%30`), then
   `EVT_SetCategory` **0** — which promotes the background plane-crash record to a live mission —
   and `EVT_SetPrimaryCoords` at the impact tile.

`FUN_004b3ba0` is the plane's message handler: cases 3 and 7 (collision) kill the airliner and post
`EVT_CrashPenaltyC` (`0x30`, -100 pts / -200 cash); on the UFO they show its hit-flash parts
(face type `0xb`) for three frames and retire it after ten hits.

## 3. Boats (`FUN_004af770`, vtable `PTR_LAB_004f4c80`)

### Record layout (`0xe3`, packed)

| Offset | Meaning |
| ------ | ------- |
| `+0x04` byte | allocated; `+0x05` linked/visible |
| `+0x0b` | wake/effect countdown (reset to `0xe666` ≈ 0.9 s) |
| `+0x13/+0x17/+0x1b` | direction vector; `+0x1f` distance left to the target tile |
| `+0x2b` | current speed; `+0x2f` base speed |
| `+0x33/+0x37` | tile x/y; `+0x3b/+0x3f` next tile; `+0x43/+0x47` target tile |
| `+0x4b` | respawn delay (`0xa0000` = 10 s, ambient only); `+0x4f` accumulator |
| `+0x53` | mission event id; `+0x57` **mission timer** |
| `+0x5b..+0x77` | the four neighbour tiles used for route choice |
| `+0x7b` | GEO model id; `+0x7f` cell display-list node |
| `+0x97/+0x9b/+0x9f` | world position; `+0xa3` 4x4 matrix |

Base speed: `((rand & 7) + 10) << 16` = 10..17 units/s. The capsized boat divides that by
`5 - difficultyTier`. Ambient boats get the 10-second respawn; `0x163` never respawns on its own.

`FUN_004afb60` (vtable `+0x08`, speed): if the player's helicopter is on the boat's tile and less
than `0x460000` (70.0) above the ground, the boat accelerates by up to `+30.0` units/s scaled by
how low the helicopter is — the "buzz the boat" reaction.

`FUN_004af770` (vtable `+0x04`, move):
- `FUN_004afdf0` reports "too far from the camera"; ambient boats then unlink. The `0x163` boat
  never despawns that way; instead it floats (`+0x9b = FUN_004ae7a0(x, z, 0)`, the water surface)
  and burns down its mission timer `+0x57`. When that hits zero it calls `FUN_004b2150`.
- Normal step: vtable `+0x0c` (`FUN_004afbd0`) is the blocked test; vtable `+0x10`
  (`FUN_004b0150`) returns the legal exits from the current water tile and `FUN_004b06c0` picks
  one. `FUN_004affe0` advances the position; every `0.9 s` an ambient boat drops a wake
  (`FUN_0048e0b0` type 7, behind it, size `0x140000`) and the capsized boat spawns effect `0`.
  `FUN_004b00a0` promotes it into the next tile once the world position crosses (terrain class
  must be 5..9 = water). Reaching the target tile picks a new one and re-orients.

`FUN_004b10a0` (vtable `+0x18`, place at tile): outward spiral up to 30 rings from the requested
tile for a water tile (class 5..9) whose scene cell is empty. **For the `0x163` boat the whole 3x3
neighbourhood must be water**, so the capsized boat always lands in open water, never against the
shore. World position is `(tileX * 0x400000 - 0x1fe00000, (waterHeight + 1) * 0x200000,
tileY * -0x400000 + 0x1fe00000)`.

`FUN_004b2150` / `FUN_004b1d20` (sink/destroy): effect `9` then `0` at the boat, sound `0xf`, and
then the split that matters — **an ambient `0x12f` boat that is destroyed creates a boat rescue**
(`FUN_004a7a10(tile, 0x90)`), while the `0x163` boat calls `FUN_004c3f00(eventId)`, removing the
people it was carrying.

### Boat rescue — the `0x90` hook

`FUN_004b1aa0(tileX, tileY, eventId, timer)` → `FUN_004b1950` (argument order confirmed from the
assembly at `0x4b1950`; the Ghidra decompile drops two of them):

1. Fail if boat slot 0 is already in the world (`+0x05 == 1`) — one boat rescue at a time.
2. `vtable+0x18(tileX, tileY)`; failing that (no open water in 30 rings) fails the mission.
3. `+0x53 = eventId`, `+0x57 = timer` (the scheduler's difficulty-scaled mission timer,
   `DAT_00505fc8`).
4. Spawn `3 + rand % 3` (3..5) people with `FUN_004c3eb0(-1, mode 1, boatTileX, boatTileY,
   eventId, &boat+0x7f, 0)`.
5. If none spawned, unlink and fail. Otherwise post `EVT_SetPrimaryCoords` with the **boat's**
   tile (so the mission marker moves from the placer's random tile to the water) and
   `EVT_RescueVictimAdded` with the spawned count, silent.

`FUN_004a7a10` leaves `SecondaryX/TertiaryX` at `-1`: a boat rescue has **no delivery tile**; the
rescue is complete once the victims are aboard/delivered by the winch.

## 4. Trains (`FUN_004b4440`, vtable `PTR_LAB_004f4cb8`)

### Record layout (`0x1d9`, packed)

| Offset | Meaning |
| ------ | ------- |
| `+0x04` allocated, `+0x05` linked, `+0x08` turning flag |
| `+0x0a` byte | **crash requested** (`FUN_004b7f60`) |
| `+0x0b` byte | **derailing** |
| `+0x0c` byte | **rescue active** |
| `+0x11` | current direction bits (1 = -Y, 2 = +X, 4 = +Y, 8 = -X; `0x10` marks a diagonal leg) |
| `+0x15/+0x19/+0x1d` | direction vector; `+0x21` distance left |
| `+0x2d` | current speed; `+0x31` base speed |
| `+0x35/+0x39` | loco tile; `+0x3d/+0x41` car 1; `+0x45/+0x49` car 2; `+0x4d..+0x59` trailing history and the next tile |
| `+0x5d` | respawn delay (`0x1e0000` = 30 s); `+0x61` accumulator |
| `+0x65` | derail spin counter; `+0x69` mission event id; `+0x6d` derail timer; `+0x71` mission timer |
| `+0x95/+0x99/+0x9d` | the three GEO model ids |
| `+0xa1/+0x105/+0x169` | the three cell display-list nodes (pointers cached at `+0x1cd/+0x1d1/+0x1d5`) |
| `+0xa9/+0x10d/+0x171` | mesh instances; `+0xb9../+0x11d../+0x181..` world positions; `+0xc5/+0x129/+0x18d` matrices |

Base speed: `(((rand & 7) + 0x56) << 16) * 4 / 5` ≈ 68.8..74.4 units/s. Sound `0x19` plays while
`+0x2d != 0`, attenuated out to `0x7800000` (`FUN_004b4570`).

**Rail tiles.** `FUN_004b7890` (vtable `+0x18`, place at tile) spiral-searches up to 20 rings for a
tile whose `xbldId | ((xzon & 2) << 14)` is one of

```
0x2c 0x2d                      (plain rail)
0x32..0x3a                     (rail shapes / crossings)
0x45..0x48   0x4d 0x4e         (rail bridges / tunnels)
0x5a 0x5b    0x805a 0x805b     (the XZON-bit-1 raised variants)
```

then requires the track to continue for six steps and the cell to be empty.
`FUN_004b5290(tileType)` chooses a starting leg on the tile (a `rand`-selected branch per tile
shape, falling back to the opposite leg when the chosen next tile is not rail), and `FUN_004b6030`
(vtable `+0x10`) is the per-tile "given my current direction and the four neighbours' tile types,
which leg do I leave by" table. Between them they encode the SC2 rail-shape topology.

*Port note:* the remake follows a **rail graph built from the id set above** (never reversing,
preferring the straight continuation) rather than transcribing the two shape tables, exactly as
`ASimCopterTrafficSystemActor` already does for roads instead of the equivalent road tables. The
tile-id set, spawn spiral, speeds, respawn delay, car spacing and every mission-facing behaviour
below are transcribed.

`FUN_004b4660` (vtable `+0x04`, move): despawns past `(viewRange >> 1) + 4` tiles unless derailing
or on a rescue; runs the derail branch when `+0x0b`; converts a pending crash (`+0x0a`) into a
derail once the middle car is over a valid cell; advances the three cars, re-linking cells and
posting `EVT_SetPrimaryCoords` every tile while a rescue is running so the marker tracks the train.

### Train crash — the `0x100` hook

`FUN_004b7f60(eventId)`: fails if `+0x0a` or `+0x0b` is already set; otherwise `+0x0a = 1`,
`+0x69 = eventId`, return 1. Record category becomes `2` (background), like the plane.

`FUN_004b49b0` (derail): the three cars keep moving at **half speed** while spinning
(10.0°, ~x, 5.0° per frame for loco/car1/car2), each spraying cell damage on its own frame slot,
for `+0x6d = 0x20000` (2.0 s). Then each car does `FUN_004af100(..., 4, eventId)`, sound `0x1a`,
three debris effects (`FUN_0048e0b0` type 4, same random yaw/pitch/life as the plane's), clears
`+0x0b`/`+0x0c`, calls `FUN_004c3f00(eventId)` to remove the mission's people, unlinks all three
cars, and posts `EVT_SetCategory` **0** — promoting the background record to a live mission.

### Train rescue — the `0x110` hook

`FUN_004b7fb0(eventId, timer)` → `FUN_004b7fd0`:

1. Fail if `+0x0a`, `+0x0b` or `+0x0c` is set.
2. If the train is not in the world, place it with `vtable+0x18(rand & 0x7f, rand & 0x7f)` — a
   **random map tile**, not the placer's tile; the spiral then finds the nearest rail. Failure
   fails the mission.
3. `+0x69 = eventId`, `+0x71 = timer`.
4. Spawn `1 + rand % 3` (1..3) people with `FUN_004c3eb0(-1, mode 0x13, trainTileX, trainTileY,
   eventId, &train+0xa1, 0)`.
5. None spawned → clear `+0x0c` and fail. Otherwise post `EVT_SetPrimaryCoords` at the train tile
   and `EVT_RescueVictimAdded` (count, silent), set `+0x0c = 1`, return 1.

Like the boat rescue, `Secondary`/`Tertiary` stay `-1`: no delivery tile.

`FUN_004b4660` decrements `+0x71` while `+0x0c` is set and, at zero, forces the derail
(`+0x0b = 1`, `+0x6d = 0x20000`) — the rescue that is not finished in time ends with the train
going off the rails and `FUN_004c3f00` removing the passengers.

## 5. Scoring

`FUN_004aabf0` has **no branch for bit `0x4` or bit `0x100`**. `Plane Crash($)/(pts)` and
`Train Crash($)/(pts)` (`0x506008`..`0x506014`) are bound by `FUN_004ab170` and then never read by
anything: the only xref to those four globals in the whole executable is the binder. A crash
mission therefore pays nothing of its own — its value is whatever it starts (the building fire, the
boat rescue, or the doubled rescue award below). The remake matches this; the +0/+0 completion of a
bare train crash is correct, not a missing branch.

What `FUN_004aabf0` does pay for these types is the rescue bit: when `0x10` is set **and** `0x100`
is also set, the per-person award doubles (`Mult = 2`) and the pickup term is the full
`VictimsPickedUp` instead of `>> 2` — already transcribed in
`FSimCopterMissionSystem::CompleteMission`. `GetTypeTextId` maps `0x90` → `0x23d` and
`0x110`/`0x100` → `0x23e`, and the completion voice for a water rescue is `0x67` vs `0x68`.

`FUN_004a73e0`'s `category == 4` arm deactivates the record on the spot with no scoring and no
completion message — the path `FUN_004b2cd0` uses to retire a plane whose fire became its own
mission. The remake's `UpdateLifecycle` had been treating category 4 like category 2 (leave it
alone forever); that is fixed as part of this port.

## 6. Not ported

- **Audio.** The sound table (`FUN_00424b70`) assigns ids in registration order, and the ids the
  vehicle code uses resolve to `TRAIN1.WAV` (0x19), `CRSH2.WAV` (0x1a), `DIVE1.WAV` (0x1b) and
  `CESSLP1.WAV` (0x1c) — cross-checked against `FUN_004b4570`'s train loop (0x19) and the mission
  layer's cash-register 0x1e (`CA_CHING.WAV`). The remake has no loader for these positional
  clips yet, so planes, boats and the train are silent.
- **The rail-shape tables** `FUN_004b5290` / `FUN_004b6030` (see the port note in section 4).
- **`FUN_004b2cd0`'s per-object ray test.** The remake resolves a diving plane against the surface
  under it rather than against each object in the cell; the three outcomes that follow are the
  original's.
- **The UFO's ten-hit damage model** (`FUN_004b3ba0`) — nothing in the remake shoots at it yet.
