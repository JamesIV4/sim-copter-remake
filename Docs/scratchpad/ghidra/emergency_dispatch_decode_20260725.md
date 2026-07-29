# Emergency Vehicle Dispatch - Decoded Notes

Date: 2026-07-25

Scope: the original's F2-F5 emergency dispatch - the key handlers, the service
station registries, the vehicle pools, how a dispatched vehicle is chosen and
routed, what it does when it arrives, and how it goes home.

Status keys follow `Docs/DecompilationWorkflow.md`: `Confirmed`, `Hypothesis`,
`Follow-up`.

Raw decompiles this note is derived from:

- `out_dispatch_core_20260725.txt` - manager construction (`FUN_004bcc80`),
  candidate heap (`FUN_004bc250`), heap pop (`FUN_004bc530`), station slot
  release (`FUN_004bc660`), adjacent-road search (`FUN_004bc110`), spiral walker
  (`FUN_004beda0`/`FUN_004bedd0`), road-graph search (`FUN_004bef30`), tile
  predicates (`FUN_004bb900`/`FUN_004bb970`), re-dispatch (`FUN_004be0c0`),
  release (`FUN_004bdc70`).
- `out_dispatch_vehicles_20260725.txt` - the three vehicle constructors
  (`FUN_004b8e10`/`FUN_004b9350`/`FUN_004b9ce0`), nearest-vehicle query
  (`FUN_0049b060`), clear-dispatch tail (`FUN_0049b5c0`), road-position probe
  (`FUN_00492240`), node index lookup (`FUN_00492bc0`), A* variant
  (`FUN_004bf2c0`), on-scene helpers (`FUN_004bd770`/`FUN_004bd980`/
  `FUN_004bdb50`), re-route (`FUN_004be5e0`), tile link/unlink
  (`FUN_004be820`/`FUN_004be890`), body placement (`FUN_004be750`).
- `out_dispatch_ai_20260725.txt` - police per-frame state machine
  (`FUN_004b9e40`), criminal-car arrest sequence (`FUN_004b8b60`/`FUN_004b8c90`),
  fire-truck target acquisition (`FUN_004b9790`/`FUN_004b9890`/`FUN_004b99c0`/
  `FUN_004b9b10`), save/load hooks.
- `out_dispatch_objectives_20260725.txt`,
  `out_dispatch_objectives2_20260725.txt` - crew deployment (`FUN_0049bd00`,
  `FUN_004c3eb0`), police target filter (`FUN_0049dab0`), stop-here test
  (`FUN_0049df60`), fire suppression (`FUN_004a5ca0`/`FUN_004a5dd0`), pylon
  create/remove (`FUN_004a42f0`/`FUN_004a4340`), despawn (`FUN_004bd5f0`,
  `FUN_0049d5a0`).
- `out_dispatch_alloc_asm_20260725.txt`,
  `out_dispatch_wrapper_asm_20260725.txt` - raw disassembly of `FUN_004bc680`
  and `FUN_004b9480`. **Required**: Ghidra mis-typed `FUN_004bc680`'s calling
  convention and produced a scrambled argument list; every claim about its
  arguments below comes from the assembly, not the C output.

---

## 0. Function hashes

SHA-256 (first 16 hex) of each `.ghidra-exports/<addr>.json` at the time of the
pass.

| Address | Hash |
| --- | --- |
| `0x0044ac80` | `0405CF3483C99A42` |
| `0x0048a580` | `74E5B5BEFECD0B9A` |
| `0x00492240` | `94D5BA2DB61CC55C` |
| `0x00492bc0` | `6637462E3F84E1C2` |
| `0x0049b3f0` | `CF5CDC55ACBA8092` |
| `0x0049b5c0` | `A2B87AC2D886E7CD` |
| `0x0049bd00` | `CE6F76DFFC52125B` |
| `0x0049dab0` | `1E14A2FAD80757E7` |
| `0x0049df60` | `10974CEABF2EEE62` |
| `0x004a42f0` | `488E0A069E2410FF` |
| `0x004a4340` | `54C5B06A4563E5C2` |
| `0x004a5ca0` | `CCFF40F7DA211046` |
| `0x004a5dd0` | `902E08A2DA015FDF` |
| `0x004b8e10` | `53B3A1BAF8457F02` |
| `0x004b8f30` | `8EE0AFC3F17DB5D3` |
| `0x004b9350` | `82E66A916E2D0E9A` |
| `0x004b9480` | `7C25079A2E279C90` |
| `0x004b9890` | `C069E350B4BF0745` |
| `0x004b9ce0` | `4B5BBACD544A469D` |
| `0x004b9e10` | `C3D84F48FD6B0597` |
| `0x004b9e40` | `844CE4AFB289E2BD` |
| `0x004bb900` | `046B3026F5E78BDF` |
| `0x004bb970` | `02A877EEC940FB5D` |
| `0x004bc110` | `926B0BA3AF95377E` |
| `0x004bc250` | `AAD9CC84458A8409` |
| `0x004bc530` | `17F3FAEFB9D119DA` |
| `0x004bc660` | `407A81EE55F7E5CE` |
| `0x004bc680` | `75FA2EABABF52BEB` |
| `0x004bcc80` | `B69F0124969A866F` |
| `0x004bdc70` | `64D270C65094E938` |
| `0x004be0c0` | `3BE249F953C644F9` |
| `0x004be910` | `3F5117819F3E00F6` |
| `0x004beda0` | `2EFA03154BDDE9CE` |
| `0x004bedd0` | `00E03316AFE4886A` |
| `0x004bef30` | `FF6DABB85C623A91` |

Refresh with `ghidra-bridge export decompiled` before trusting them again.

---

## 1. The four dispatch keys - Confirmed

`FUN_0044ac80` (global command dispatcher) routes command ids `0x16`..`0x1a` to
`FUN_0048a580` when the session is in flight mode (`session state == 6`,
`param_1[0x2e] != 0`, `param_1[0x2f] == 0`) and the view is **not** the cockpit
(`DAT_00503aa0 != 3`); in the cockpit only `0x1a` survives, going to
`FUN_004c0d80`.

`FUN_0048a580(cmd)` reads the **spotlight** ground node (`DAT_005040d0 + 0xc0`)
for the target tile - not the helicopter position:

```
tileX = (lightNode[0x18] + 0x20000000) >> 0x16
tileY = (0x20000000 - lightNode[0x20]) >> 0x16
```

This is the same tile formula the spotlight publishes in `DAT_005d70f0/f4`
(`heli_tools_models_decode_20260724.md` section 4 step 9).

Each key first tests `FUN_0042de60(1)`, which returns non-zero when
`DAT_0051a078 != 0`. `FUN_0042de60` is a modifier-key test (`&1` ->
`DAT_0051a078`, `&2` -> `DAT_0051a079`, `&4` -> `DAT_0051a07a`), and the help
(`09tut.htm`) documents `Shift F2/F3/F4/F5` as "clear the dispatch", so
`DAT_0051a078` is **Shift**.

| Cmd | Key (help `09tut.htm`) | Shift held | Otherwise |
| ---: | --- | --- | --- |
| `0x16` | F2 - Fire truck | clear msg `0x11c` | `FUN_004be910(1, 4, tile)` |
| `0x17` | F3 - Ambulance | clear msg `0x11f` | `FUN_004be910(2, 4, tile)` |
| `0x18` | F4 - Police | clear msg `0x11d` | `FUN_004be910(4, 4, tile)` |
| `0x19` | F5 - Chase (police follow the spotlight) | clear msg `0x11d` | `FUN_004be910(3, 3, tile)` |

`FUN_004be910(serviceType, initialState, tileX, tileY, &outObjectId)` rejects
tiles outside `0..127` and then routes:

| Service type | Wrapper | Manager (`this`) | Vehicle pool | Message id |
| ---: | --- | --- | --- | ---: |
| 1 | `FUN_004b9480` | `DAT_0051af00` FIRE STATIONS | `DAT_00582b38` | `0x11c` |
| 2 | `FUN_004b8f30` | `DAT_0051ac7c` HOSPITALS | `DAT_00582b20` | `0x11f` |
| 3 | `FUN_004b9e10` | `DAT_0051ac50` POLICE STATIONS | `DAT_00582b50` | `0x11d` |
| 4 | `FUN_004b9e10` | `DAT_0051ac50` POLICE STATIONS | `DAT_00582b50` | `0x11d` |

The manager globals are named by `FUN_00495700`, the road-graph debug dump,
which prints `HOSPITALS:` from `DAT_0051ac7c`, `POLICE STATIONS:` from
`DAT_0051ac50` and `FIRE STATIONS:` from `DAT_0051af00`.

Service types 3 and 4 share the police manager and pool; they differ only in the
**initial vehicle state** (3 vs 4), which is what makes F5 the chase dispatch.
See section 6.

Each wrapper is a fixed 7-argument `__thiscall` bridge, e.g. `FUN_004b9480`:

```
FUN_004bc680(this = DAT_0051af00, tileX, tileY, serviceType,
             initialState, &DAT_00582b38, poolSize = 5, &outObjectId)
```

(verified in `out_dispatch_wrapper_asm_20260725.txt`; `RET 0x1c` at the end of
`FUN_004bc680` confirms 7 stack arguments plus `ECX`.)

**Five vehicles per service.** `poolSize` is 5 for all three pools, and
`FUN_004bcc80` writes `manager[0x2c] = 5` for every service.

---

## 2. Service stations - Confirmed

`FUN_004bcc80(this, xbldId)` builds one manager. It is called three times from
`FUN_004bbdb0`:

| Call | XBLD id | Manager |
| --- | ---: | --- |
| `FUN_004bcc80(0xd3)` | `0xd3` | `DAT_0051af00` fire stations |
| `FUN_004bcc80(0xd1)` | `0xd1` | `DAT_0051ac7c` hospitals |
| `FUN_004bcc80(0xd2)` | `0xd2` | `DAT_0051ac50` police stations |

### Scan - Confirmed

1. The XBLD grid `DAT_005910b0` (column-pointer array, `[x][y]`) is copied
   **transposed** into the scratch buffer `DAT_0051af08`, so
   `scratch[y * 0x80 + x] == XBLD[x][y]`.
2. Two identical passes walk the scratch. Pass one counts, pass two fills.
3. On a tile whose id equals `xbldId`, the code zeroes the scratch cells at
   `+1, +2, +0x80, +0x81, +0x82, +0x100, +0x101, +0x102` - i.e. the rest of a
   **3x3 footprint** - so one building yields exactly one station record.
4. The station's tile is recorded as `(x + 1, y + 1)`: the **centre** of that
   3x3 block.
5. `FUN_004bc110` is asked for a road tile next to the centre. When it returns
   `0xff` (none) the building is **skipped entirely** - a station with no road
   access never appears in the registry.

### Station record (0x10 bytes) - Confirmed

| Offset | Field |
| ---: | --- |
| `+0x00` | approach direction 0..3 (N, E, S, W - the "NESW" string in `FUN_00495700`) |
| `+0x04` | station centre tile X |
| `+0x05` | station centre tile Y |
| `+0x06` | road access tile X |
| `+0x07` | road access tile Y |
| `+0x08` | outstanding dispatches from this station (saved/loaded, see below) |

`+0x08` is persisted in the save file as chunk `NTSF` (fire), `NTSP` (police),
`NTSH` (hospital) - four bytes per station (`FUN_004bc030`, `FUN_004bbed0`).

### Manager layout - Confirmed

| Offset | Contents |
| ---: | --- |
| `+0x04` | voice clip: **no unit available** |
| `+0x08` | voice clip: **dispatched** |
| `+0x0c` | voice clip: **cannot reach that location** |
| `+0x10` | voice clip: **dispatch cleared** |
| `+0x14` | station array |
| `+0x18` | station count |
| `+0x1c` | 8-bytes-per-station scratch array |
| `+0x2c` | vehicle pool size (5) |
| `+0x30` | index of a free (unspawned) vehicle slot, `32000` = none |
| `+0x34` | candidate min-heap (12 bytes per entry) |
| `+0x38` | candidate heap size |

Voice clips (`FUN_004bcc80`):

| Service | no unit | dispatched | no route | cleared |
| --- | --- | --- | --- | --- |
| Fire `0xd3` | `dis095.wav` | `dis101.wav` | `dis104.wav` | `dis098.wav` |
| Police `0xd2` | `dis100.wav` | `dis094.wav` | `dis103.wav` | `dis097.wav` |
| Hospital `0xd1` | `dis096.wav` | `dis102.wav` | `dis105.wav` | `dis099.wav` |

The two police names are built inline from `DAT_00506410` / `DAT_00506404`; the
string table shows their tails `"0.wav"` at `0x00506409` and `"4.wav"` at
`0x00506415`, i.e. `dis100.wav` and `dis094.wav`. `Hypothesis` on the leading
digits only - the tails and the owning function are `Confirmed`.

### Adjacent-road search `FUN_004bc110` - Confirmed

```
dist = 2
dir  = (tileX + tileY) & 3          // deterministic per station, not random
while dist <= 4:
    for i in 0..3:
        candidate = step(tile, dir, dist)      // 0:-Y 1:+X 2:+Y 3:-X
        write candidate back into the caller's tile bytes
        if IsRoadTile(candidate) and FUN_00492240(candidate, candidate, ...) != 0:
            return dir
        dir = (dir + 1) & 3
    dist++
return 0xff
```

The caller keeps the mutated bytes as the station's road tile, so on success the
tile buffer holds the road tile and the return value is the approach direction.
`dist` starts at **2** because the centre of a 3x3 building is one tile from its
own edge.

### Road tile predicate - Confirmed

Used identically in `FUN_004bc110`, `FUN_004bc680`, `FUN_004bb970` and the
police retarget in `FUN_004b9e40`. XBLD id ranges:

```
0x1d..0x2b   or   0x3f..0x46   or   0x51..0x59
```

`FUN_004bb900` additionally classifies **intersections**: id `0x27..0x2b` is an
intersection node, and id `0x69` explicitly is not.

---

## 3. Choosing which unit responds - Confirmed

`FUN_004bc250(manager, packedTargetTile, vehicleArray, poolSize)` builds a
min-heap of candidates and returns whether any exist.

Distance metric (used everywhere in this subsystem, including `FUN_004bf2c0`'s
A* heuristic and `FUN_0049b060`'s nearest-vehicle query):

```
dx = |ax - bx|; dy = |ay - by|
cost = (dx < dy) ? dy + (dx >> 1) : dx + (dy >> 1)     // octile-ish
```

Candidates:

| Kind | Source | Gate | Heap entry |
| ---: | --- | --- | --- |
| 2 | station `i` | `station[i].Outstanding <= 0` | `{cost(station.tile, target), 2, i}` |
| 1 | vehicle `j` | vehicle is spawned (`veh[4] & 2`) **and** `veh.State == 2` (idle) | `{cost(veh.tile, target), 1, j}` |

A vehicle that is **not** spawned (`!(veh[4] & 2)`) is not a candidate; instead
its index is remembered in `manager[0x30]` as the free slot a station spawn will
use. `manager[0x30]` starts at `32000` ("none").

`FUN_004bc530` is the standard binary-heap pop; entries come out cheapest first.
`FUN_004bc660(manager, stationIndex)` decrements `station.Outstanding`, floored
at 0.

### `FUN_004bc680` - the dispatch transaction - Confirmed (from assembly)

```
FUN_004bc680(manager, tileX, tileY, serviceType, initialState,
             vehicleArray, poolSize, int* outObjectId)
```

1. `FUN_004bc250(...)`. Empty heap ->
   play `manager[0x04]`, `*out = 32000`, **return 0**.
2. Snap the target to a road tile: a spiral of radius 4 (`FUN_004beda0(4)`)
   walked by `FUN_004bedd0`, starting on the requested tile, taking the first
   tile that satisfies the road predicate. No road tile in range ->
   play `manager[0x0c]`, `*out = 32000`, **return 2**.
3. Pop candidates cheapest-first and test reachability:
   - kind 1, vehicle already driving (`veh[4] & 2`): build the road position
     from its current graph branch (`veh[0x7c] + veh[0x8a] * 0xb + 5`).
   - kind 1, vehicle idle in the pool: `FUN_00492240(roadGraph, veh.tile,
     target, &RouteA, &RouteB)`.
   - kind 2: `FUN_00492240(roadGraph, station.roadTile, target, &RouteA,
     &RouteB)`.
   A probe result `> 2` accepts immediately. Otherwise, when the start tile
   equals the target tile, or when the probe returned `> 0`, the code runs
   `FUN_004bef30(searchCtx = DAT_0051ac48, fromNode, toNode)` - the road-graph
   search of section 4 - and accepts on success.
4. No candidate reachable -> play `manager[0x0c]`, `*out = 32000`, **return 2**.
5. Success:
   - kind 1: `FUN_004bdd40(veh, RouteA, RouteB, targetTile, routeResult,
     serviceType, initialState, 0)` - retarget a vehicle that is already out.
   - kind 2: `station.Outstanding++`, then `FUN_004be0c0(veh, stationIndex,
     RouteA, RouteB, station.roadTile, RouteA', RouteB', targetTile,
     station.dir, routeA, routeB, serviceType, initialState)`.
   - `*out = veh.ObjectId` (`short` at `veh + 0x26`), play `manager[0x08]`,
     **return 4**.

Return codes therefore mean: `0` = no unit available, `2` = cannot reach,
`4` = dispatched.

### `FUN_004be0c0` - launching from a station - Confirmed

Beyond the route bookkeeping it writes the fields the rest of the system reads:

| Field | Value |
| --- | --- |
| `veh + 0x12b` | station road tile (the vehicle's home) |
| `veh + 0x12d` | destination tile |
| `veh + 0x12f` | service type (when not `-1`) |
| `veh + 0x299` | **state** = the `initialState` argument |
| `veh + 0x29d` | station index (so the slot can be released later) |
| `veh + 0xf3` | station approach direction |
| `veh[1] \|= 2` | mark spawned/in service |

It then links the vehicle into the destination tile's object list, and calls
`FUN_004a42f0(objectId, kind, -1, &veh[0x22], &veh[0x12d])` where kind is
`0` for message `0x11c` (fire), `1` for `0x11d` (police), `2` for `0x11f`
(hospital).

`FUN_004a42f0` is the **dispatch pylon**: a 20-slot table at `DAT_005d3eb0`
(`0x28` bytes per slot) holding `{active, objectId, kind, -1, vehicleTilePtr,
destTilePtr}`. `FUN_004a4340(objectId)` removes it. The help calls these the
"Police / Fire Truck / Ambulance Dispatch Pylon" markers.

---

## 4. Navigation - Confirmed

The road network has two representations:

- **Tile grid**: XBLD ids, tested by the road predicate above.
- **Intersection graph**: `DAT_0051ac80[x]` is an array of `0x38`-byte
  intersection records for map column `x`, `DAT_0051ae80[x]` is the count.
  `FUN_00492bc0(graph, x, y)` maps a tile Y to the record index in column `x`,
  returning `0xff` when the tile is not an intersection.

Intersection record (cross-checked against the `FUN_00495700` dump format
string `"X=%3d Y=%3d turnFlags=%4s deadEndF..."`):

| Offset | Field |
| ---: | --- |
| `+0x00` | tile X |
| `+0x01` | tile Y |
| `+0x02` | low nibble = turn/connection flags (bit per NESW), high nibble = dead-end flags |
| `+0x03` | extra flags |
| `+0x05 + n * 0x0b` | branch record `n` for direction `n` (0..3) |
| branch `+0x00/+0x01` | neighbour node (row index, column) |
| branch `+0x02` | arrival direction at the neighbour |
| branch `+0x03` (u16) | segment length (the edge weight) |
| branch `+0x05` (u16) | road tile count along the segment |
| branch `+0x07` | pointer to the 3-bytes-per-tile road path |
| `+0x31/+0x32/+0x33` | search back-links (prev Y, prev X, entry direction) |
| `+0x34` | search visited flag |

`FUN_004bef30(ctx, fromNode, toNode)` is **Dijkstra** over that graph: it clears
every node's visited flag and back-links, pushes the start with cost `0xff`,
and expands the cheapest node, accumulating `branch.length + parentCost`. It
returns 1 when `toNode` is reached, leaving the back-links in place so the
caller can walk the route.

`FUN_004bf2c0` is the same walk with the octile heuristic instead of the
accumulated cost (greedy best-first); it is used by the police chase retarget
(`FUN_004ba930`).

`FUN_00492240(graph, tile, destTile, &outA, &outB)` is the **road position
probe** for a tile that is not necessarily an intersection:

- If the tile *is* an intersection (`FUN_004bb900` == 1), `outA` is filled from
  the intersection record and the result is `1`.
- Otherwise the four neighbours are tested with `FUN_004bb970` (the
  direction/one-way compatibility table for road, highway and bridge ids), and
  `FUN_00492d80` fills `outA` (and `outB` for a second usable direction). The
  return code is `0` = no usable direction, `1` = one, `2`/`3` = two, with the
  pair possibly swapped so `outA` is the one that heads toward `destTile`.

`FUN_004bc680` treats `> 2` as "definitely routable"; anything else falls
through to the Dijkstra check.

Per-frame movement is the ordinary traffic mover `FUN_0049be50` (shared with
ambient cars); the emergency classes only add their state machines on top.
`FUN_004be5e0` re-runs the probe plus `FUN_004bef30` and calls `FUN_004bdd40`
to re-issue the route (used when the destination moves, e.g. the chase mode).

---

## 5. Vehicle pools - Confirmed

| Service | Constructor | Array | Count global | Struct size | vtable | Pylon icon |
| --- | --- | --- | --- | ---: | --- | ---: |
| Ambulance | `FUN_004b8e10` | `DAT_00582b20` | `DAT_00506380` | `0x2b2` | `PTR_FUN_004f4d20` | `0x121` |
| Fire truck | `FUN_004b9350` | `DAT_00582b38` | `DAT_00506384` | `0x2cc` | `PTR_FUN_004f4d48` | `0x123` |
| Police car | `FUN_004b9ce0` | `DAT_00582b50` | `DAT_00506388` | `0x31c` | `PTR_FUN_004f4db0` | `0x122` |

Each constructor writes `veh[5] = messageId` (`0x11f`/`0x11c`/`0x11d`), which is
the identity every later lookup switches on.

### The message id is also the body's GEO object id - Confirmed

`FUN_0049dbb0`, the base vehicle constructor every service calls, builds the body
render node from `FUN_00470571(veh[0x14])` - i.e. straight from the message id.
Resolved against the shipped `GEO/SIM3D2.MAX` object table (id at object header
`+0x78`):

| Id | Table name | Object name | Meaning |
| ---: | --- | --- | --- |
| `0x11c` | `CARFIRET` | `firetruk` | fire truck body |
| `0x11d` | `CARPOLIC` | `popo` | police car body |
| `0x11e` | `CARROBBR` | `badguy` | **criminal car** - the id `FUN_0049dab0` tests for |
| `0x11f` | `CARAMBUL` | `amblance` | ambulance body |
| `0x121` | `AICON` | `MedicPoint` | ambulance dispatch pylon icon |
| `0x122` | `PICON` | `CopPointer` | police dispatch pylon icon |
| `0x123` | `FICON` | `FirePointe` | fire dispatch pylon icon |

The `0x121`/`0x122`/`0x123` objects each service constructor loads are therefore
**not** the vehicle bodies: they go to a second render node at `veh + 0x13b`
(sort class `0x21`, hidden at build with `FUN_0046f610(node, 0)`) and are the
pylon markers the help calls the "Police / Fire Truck / Ambulance Dispatch Pylon".
`FUN_0049dbb0` also gives an emergency vehicle a different cruise speed than an
ambient car: `0x1e0000` at `+0xcb`/`+0xcf` for ids `0x11c..0x11f`, versus
`0x50000` for everything else.

Vehicle fields used by the dispatch layer:

| Field | Meaning |
| --- | --- |
| `+0x04` | object flags; bit 1 (`0x2`) = spawned/in service |
| `+0x14` | message id = service identity |
| `+0x18` | world position (also the sound emitter position) |
| `+0x26` (s16) | object id |
| `+0x88` (u16) | current tile (x, y) |
| `+0x8a` | current graph branch index |
| `+0xaf` | stop timer |
| `+0x12b` (u16) | home (station road) tile |
| `+0x12d` (u16) | destination tile |
| `+0x12f` | service type |
| `+0x299` | state (see section 6) |
| `+0x29d` | station index |
| `+0x2a5` / `+0x2a9` / `+0x2ad` | on-scene / retarget / give-up timers |
| `+0x2b1` | status flags: `0x01`/`0x02` siren-sound stages, `0x04` on scene, `0x08` arrived once, `0x10` recalled, `0x20` linked into a tile list, `0x40` at destination, `0x80` retarget cooldown |

---

## 6. States and the per-frame machines - Confirmed

`veh + 0x299`, dispatched as `switch(state - 1)` in `FUN_004b9e40`:

| State | Meaning |
| ---: | --- |
| 1 | on scene / working (dispatch complete, doing the job) |
| 2 | idle - parked at the station, and the only state that makes an already-spawned vehicle a redispatch candidate |
| 3 | **chase**: destination re-read from the helicopter spotlight every frame |
| 4 | responding: drive to the fixed destination tile |
| 5 | wrap-up, falls back to returning |
| 6 | (reachable through `FUN_004bdc70`'s recall set; treated as returning) |

F2/F3/F4 pass `initialState = 4`; F5 passes `3`.

### Chase (state 3, F5) - Confirmed

`FUN_004b9e40` case 2 recomputes the destination from
`DAT_005040d0 + 0xc0` (the spotlight node) **every frame**, exactly as
`FUN_0048a580` does for the initial dispatch, then:

- scans a 3-ring spiral around the spotlight tile for an object with tile-object
  flag `0x10` (vehicle) that passes `FUN_0049dab0`;
- if none, spirals radius 4 outward from the spotlight tile for the nearest road
  tile and drives there;
- if the target is within `0x600000` of the police car and `FUN_0049df60`
  allows stopping, invokes the target's `vtable[1]` (the "pull over" call).

This is the help's "F5 issues a special dispatch that allows police to follow
your spotlight... to help catch a criminal you are chasing who won't stop".

### Responding (state 4) - Confirmed

Drive to `veh[0x12d]`. On arrival (`veh[0x12d] == veh.tile`) the on-scene flag
`0x04` is set, the give-up timer `+0x2ad` starts, and the service action runs.
`FUN_004bd980` plays sound `0x6f` then `0x70` (arrive / doors), performs the
service action once between them, then arms `+0x2ad = 0xb40000` (180.0 s) as the
stay timer. If the vehicle never manages to act, `FUN_004bdb50` sends it home.

### Returning (state 1 via recall, state 2 target) - Confirmed

`FUN_004bdc70(veh)` is the **recall**: it unlinks the vehicle from its tile list
and, for states 1/3/4/5/6, sets flag `0x10` and state 1. State 2 (already idle)
returns 0 and is left alone.

`FUN_004b9e40` case 1 (state 2 target) checks arrival at the home tile
`veh[0x12b]` and then:

```
FUN_004bc660(stationIndex)   // release the station slot
FUN_0049d5a0()               // unlink from the world
FUN_004a4340(objectId)       // remove the dispatch pylon
```

`FUN_004bd5f0` (vehicle teardown) does the same pylon removal plus
`FUN_004bc660` for all three message ids.

---

## 7. What an arrived vehicle actually does - Confirmed

### Crew deployment `FUN_0049bd00(veh, spawnMode, personState)`

Sweeps 8..0x20 units out in 0x24 angular steps around the vehicle, looking for a
spot whose terrain height is within +/-5.0 of the vehicle's, and spawns a person
there via `FUN_004c3eb0(spawnMode, personState, tileX, tileY, eventId, vehPos,
&spot)` - the same people spawner the mission system uses. Returns 1 on the
first success, 0 when nothing fits.

| Service | Call | Meaning |
| --- | --- | --- |
| Police | `FUN_0049bd00(0xe, 8)` (or `(0xe, 0xe)` when the target object carries flag `0x8`) | deploy an officer |
| Ambulance | `FUN_004b8f60` -> `FUN_004bd980(0x0c, 5, ...)` -> `FUN_0049bd00` | deploy a behavior-class-12, state-5 `Medik` |

Fire trucks do not deploy crew; they spray (below).

### Ambulance paramedic interaction - Confirmed 2026-07-29

The ambulance vtable at `0x004f4d20` points its on-scene update slot at
`FUN_004b8f60`. Both of that function's deployment arms call
`FUN_004bd980(0x0c, 5, ...)`. The similar `(0x0f, 0x0d)` call is in the
criminal-car function `FUN_004b8b60`; it is not an ambulance call.

The shipped `people.df` supplies the complete interaction:

1. person state 5 starts BHAV 801, `Medevac paramedic new initbhav`;
2. away from XBLD `0xd1`, BHAV 801 calls BHAV 262, which searches eight tiles
   for object class 5 / person state 6 and uses opcode 44 to tote that victim;
3. BHAV 272 selects object class 10 and BHAV 275 walks to it, uses opcode 51
   to set down the same patient, then pushes BHAV 285 onto the patient;
4. BHAV 285 posts outcome 0 (picked up), outcome 1 (medevac delivered), then
   disappears;
5. BHAV 269 selects `person+0x170`, the vehicle that deployed the medic,
   boards it, and messages it through opcode 61 so it returns.

`FUN_004cac70`'s object-class jump table proves classes 10..12 call
`FUN_0049b060` with kinds 0..2. The actual pools are:

| behavior object class | `FUN_0049b060` kind | pool |
| ---: | ---: | --- |
| 10 | 0 | ambulance (`DAT_00582b20`) |
| 11 | 1 | police (`DAT_00582b50`) |
| 12 | 2 | fire truck (`DAT_00582b38`) |

The old port had classes 10 and 12 reversed, so BHAV 272 sent the medic toward
a fire truck. It also deployed `(0x0f, 0x0d)` through a helper whose arguments
are person state first and behavior class second, producing neither the
state-5 paramedic nor BHAV 801.

### Police target filter - Confirmed

`FUN_0049dab0(obj)` accepts an object only when
`obj[0x14] == 0x11e` (the criminal-car message id) **or** `obj[5] & 8` (the
speeding/fleeing flag). So a dispatched police car looks specifically for
criminals and speeders, in a 3-ring spiral around its own tile.

`FUN_0049df60(veh)` is the "may I stop here" gate: not on an intersection tile
(`0x27..0x2b`), not flagged `0x80`, stop timer `<= 0`, the next tile ahead is
not an intersection either, and no other vehicle-class object with flags `0xf0`
occupies the tile.

### Fire truck target acquisition and suppression - Confirmed

`FUN_004b9890(veh, tile)` walks a **5-ring** spiral and takes the first of:

- a burning **building**: the scene cell flagged `0x20`, choosing among its
  burning display-list entries (flag `1`) with a reservoir-style random pick;
- a burning **object**: a tile object with flags `0x1000`.

If neither is found, `FUN_004b99c0` falls back to the current mission/fire
target (via `FUN_004c9df0`) when it is within `0x1400000` (320.0 units).

`FUN_004b9790` then, once per frame, calls `FUN_004a5ca0` (building) or
`FUN_004a5dd0` (object), which **spray water**:

```
FUN_0048e0b0(6, &tile, &nozzlePos, &dir, 1, veh,
             (rand() % 100) * 0x10000 + distance / 2, -1)
```

Emitter type 6 is the fire-truck water jet. The elevation term
`DAT_00505f84` sweeps up and down by `0x1999` per shot between 0 and `0x40000`
(building) / `0x30000` (object), which is what makes the jet arc. Each call
returns "done" with probability 1/8. The water particles douse the fire through
the ordinary water-impact path (`FUN_004a50c0`), the same one the helicopter
bucket uses - the fire truck has no special extinguish call.

### Clearing a dispatch - Confirmed

`FUN_0049b3f0(-0x600, 1, &messageId)` (the Shift+F<n> path):

1. Spiral radius **2** around the spotlight tile.
2. For each tile-object with flag `0x10` (vehicle):
   - if its vehicle record's message id != the requested one, **abort and return
     0** (a different service parked there blocks the release);
   - otherwise play that service manager's `+0x10` clip and call
     `FUN_004bdc70` to recall it.
3. Nothing found in range -> return 0.

---

## 8. Nearest-unit query `FUN_0049b060` - Confirmed

`FUN_0049b060(kind, tileX, tileY)` returns the world position (`veh + 0x18`) of
the nearest vehicle of a kind that is both spawned (`flags & 2`) and linked into
a tile (`flags & 0x20`), using the same octile metric, or 0 when there is none.

| kind | Pool |
| ---: | --- |
| 0 | `DAT_00582b20` ambulances |
| 1 | `DAT_00582b50` police |
| 2 | `DAT_00582b38` fire trucks |
| 3, 4 | `DAT_00582b08` (speeder/criminal-car pool) |

The indirect caller is people-object selection `FUN_004cac70`: behavior object
classes 10..13 reach `FUN_0049b060` kinds 0..3 through its jump table. BHAV 272
is the ambulance-side class-10 call site.

---

## 9. Port status (2026-07-25)

Ported as `Source/SimCopterRemake/{Public,Private}/Ground/SimCopterDispatch.*` (the pure
selection core, unit-tested as `SimCopter.Dispatch.*`) plus the runtime state machines in
`SimCopterTrafficSystemActor.cpp`, the F2-F5 keys on `ASimCopterHelicopterPawn`, and a
DISPATCH section in the helicopter debug panel.

Verified in a `-game` session on `ArcoCity.sc2` (1 station per service):

| Step | Observed |
| --- | --- |
| Station scan | fire `(15,53)` road `(15,51)`, police `(48,66)` road `(50,66)`, hospital `(42,45)` road `(40,45)` - every road access at ring 2, as `FUN_004bc110` starts |
| Dispatch | all three returned result 4 and incremented their station's outstanding counter |
| Navigation | all three drove cross-city routes to the requested road tile and reached `on scene` |
| Return | each recalled itself after the stay timer, drove home, and released the station slot back to 0 |

`Demo.sc2` scans 6 fire, 6 police and 5 hospital stations.

Divergences from the original, all deliberate:

- Routing uses the remake's per-tile road graph (BFS) rather than the original's
  `0x38`-byte intersection graph and `FUN_004bef30` Dijkstra. Same shortest path over
  unit edges; the intersection records were not reproduced.
- The clear-dispatch scan only aborts on another **dispatched** unit. The original also
  aborted on an ambient car, since every car carries the same tile-object flag.
- Fire suppression is split: the **visible** jet is emitter type 6 with the decoded nozzle
  offset (`+0x1e0000`), elevation sweep (`+/-0x1999` between 0 and `0x40000`) and speed roll
  (`rand() % 100 + distance / 2`), while the **effect** is applied directly rather than by
  waiting for a droplet to land. The jet is throttled to ~15 droplets a second: the
  original's one-per-frame at its own pacing, and slow enough that the truck does not
  exhaust the 70-slot trajectory pool the player's bucket and cannon share.
- The douse is applied **at the targeted flame's own local offset**, not at its anchor
  cell's origin: `IgniteBuilding` places a multi-tile building's flames up to `+/-0x700000`
  from that origin while Fire Radius is only `~0x2beb99`, so a cell-origin douse reaches
  nothing on anything larger than 1x1. Regression test:
  `SimCopter.Missions.ServiceFireSuppressionUsesFlameOffset`.
- A parked truck re-scans on the short (0.5 s) cadence whether or not it found something,
  matching `FUN_004b9790`'s per-frame `FUN_004b9890` search - only the give-up timer is
  long. That is what lets one truck work through several fires inside its five-ring reach,
  one after another. Fires outside that reach need their own dispatch, as in the original.
- Dispatched units are not members of the ambient traffic pool, so the ambient
  following/traffic-light passes do not see them.

## 10. Remaining unresolved items

- `FUN_004bdd40` (retarget an already-driving vehicle) failed to decompile;
  its argument list was recovered from the `FUN_004bc680` call site but its body
  was not read. The remake re-plans the route from scratch instead.
- `FUN_00492d80`, the per-direction road-position filler behind
  `FUN_00492240`, was not decoded; the remake uses its own road-tile graph.
- The precise leading digits of the two police voice clips (section 2).
