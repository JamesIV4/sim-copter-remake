# Traffic / Road System Notes (handoff 2026-06-24)

Working notes for `ASimCopterTrafficSystemActor` + `ASimCopterGroundAgent`. Read alongside
`Docs/ReverseEngineering.md` and the `simcopter-population-rendering` auto-memory.

## What is already done (compiles + validated)

- **Road routing overhaul → clean graph walk.** Removed the decompiled `FUN_004b5290` step table.
  Every road tile is a graph node linked to its 4-neighbour road tiles (`RebuildSpawnData`).
  `ChooseNextRouteNode` picks a connected neighbour each hop: excludes the node it came from (no
  U-turns) unless dead-end, prefers continuing straight (~70%) / turns at junctions (~30%). Agents
  carry `RouteTargetNodeIndex`/`RoutePrevNodeIndex`; `AssignNextTarget` advances them. Because it
  only ever targets an *adjacent* graph node, agents cannot leave the network. The old code had a
  16% "drive to a random distant node" branch that beelined cars over buildings - gone.
- **Kinematic movement.** `UpdateMovement` now moves with `MoveComponent(..., bSweep=false)` so cars
  never jam/stall on each other or building corners; `UpdateGroundSnap` (ECC_Camera trace) fixes Z.
- **City actor source of truth.** Traffic now discovers/uses the active `ASimCity2000CityActor`
  (or an explicit `SourceCityActor`) for resolved city path, original game root, tile size,
  effective terrain height scale, and actor transform. This fixes the class of bugs where traffic
  was generated for one city file/origin while a different city actor was rendered.
- **Road crossings and angled pieces.** `0x43`/`0x44` are treated as road-continuity crossing
  pieces. Curve/diagonal road tiles `0x23..0x26` now bias vehicle and pedestrian route points toward
  the tile corner shared by their two openings, so angled-road chains no longer make agents zig-zag
  back and forth across the road.
- **Validated** by replaying the graph on the real `cape wells` network: 1405 nodes, 200 cars ×
  300 hops, 0 stuck, 100% on road tiles, 73% straight.

## Verified data facts

- XBLD building-id ranges (from the mesh-library `KnownXbldMappings` + GEO names + parsing the
  `.sc2` XBLD layer): **power lines `0x0E..0x1C`** (`WR14..WR28`), **surface roads `0x1D..0x2B`**
  (`RD29..RD43`), **rails `0x2C..0x3A`** (`RL44..RL58`). `RD29` = id 29 = `0x1D`.
- `cape wells.sc2` road histogram: `0x1D`(589 N/S straight), `0x1E`(448 E/W straight),
  `0x1F..0x22` slopes (few), `0x23..0x26` curves (~50 each), `0x27..0x2A` T-junctions, `0x2B`(11
  four-way). 1394 road tiles, 99.4% in one connected component.
- City and traffic world mapping are **textually identical**:
  `WorldX = (FileX+0.5)*TileSize - HalfMapSize`, `WorldY = -((FileY+0.5)*TileSize - HalfMapSize)`,
  `HalfMapSize = 128*TileSize*0.5 = 25600`, `TileSize = 400` (default on both). Road mesh is placed
  at the tile centre for a 1x1 footprint (`SimCity2000CityActor.cpp` ~line 2268). FileY = XBLD row,
  FileX = XBLD col; tile index = `FileY*128 + FileX`.
- Population world scale is **0.25** (city renders original meshes at 0.25x of real cm). Pedestrian/
  player bodies + capsules + on-foot camera are multiplied by `PopulationWorldScale`.

## Handoff Issues (current status)

The offset/city-mismatch, `0x43`/`0x44` crossing, first diagonal-road zig-zag, and right-lane
offset items below have first-pass fixes in code. Remaining work is fidelity polish: true directed
two-lane graphs, richer pedestrian sidewalk waypoints, and multi-point arcs through curve meshes.

1. **2-tile perpendicular offset (cars + peds).** In one axis (N/S *or* E/W) agents are on the road;
   in the perpendicular axis they run ~2 tiles off the actual road meshes, over buildings. Because
   the city/traffic formulas are identical, this is almost certainly a **runtime mismatch**, not a
   formula bug. Leading hypotheses, in order:
   - **Different city files.** `ASimCopterTrafficSystemActor::CityFile` defaults to
     `../Reference/SimCopterOriginalGame/cities/cape wells.sc2`, but `ASimCity2000CityActor::CityFile`
     has **no default** - it is set per-instance in `Content/CityRender.umap`. If the umap's city
     actor points at a *different* `.sc2`, the two road layouts differ and cars sit on cape-wells
     roads that are buildings in the rendered city. **VERIFY**: read the `CityFile` on the city actor
     in `CityRender.umap` (was about to grep the umap for `.sc2` strings when the session ended).
   - **City actor transform.** If the umap's city actor is moved/rotated/scaled (a 90° yaw would give
     exactly the "one axis right, perpendicular wrong" symptom), the traffic nodes (computed at
     origin) won't follow. Check the city actor's transform in the umap.
   - **ROBUST FIX (do this regardless):** the traffic actor should *align to the city actor at
     runtime* instead of assuming defaults: `UGameplayStatics::GetActorOfClass(ASimCity2000CityActor)`,
     then use its resolved city path, its `TileSize`/terrain params, and transform every node
     position by the city actor's `GetActorTransform()`. Needs small public getters on the city actor
     (resolved city path, TileSize, bUseOriginalTerrainHeightScale, TerrainHeightScale). This makes
     the two systems share one source of truth and kills the whole class of offset bugs.

2. **Power-line / rail crossings break the road graph.** A tile where a power line (or rail) crosses
   a road is NOT in `0x1D..0x2B`, so `IsSurfaceRoadTile` rejects it: the graph splits there and the
   city renders no sidewalk on it. Need to treat road+overlay crossing tiles as road-connective.
   **TODO:** identify the crossing ids by parsing - look for tiles whose N/S and E/W neighbours are
   roads but whose own id is not a road id. Candidates seen frequently in `cape wells`: `0x43`(74),
   `0x44`(64) (note `GetOriginalBridgeDispatch` maps `0x43/0x44` to flat mesh `0x128/0x129`). Confirm
   whether these are road+power / road+rail crossings, add them to road detection, and make the city
   render the road/sidewalk on them.

3. **Diagonal / curve roads handled as square turns.** SC2000 roads include curve (`0x23..0x26`) and
   diagonal pieces; the current graph uses tile-*centre* nodes with orthogonal links, so a car turns
   90° at the centre instead of arcing (curve) or cutting corner-to-corner at 45° (diagonal). Needs
   shape-aware waypoints (below).

4. **Lane offset / drive-on-the-right (requested).** Cars should follow waypoints offset to the
   right-hand side of the road, not the centreline.

## Recommended next architecture (the user's "waypoints on the meshes" idea)

Build a **shape-aware waypoint network**, ideally owned/exposed by the city actor so it shares the
exact mesh placement + transform (kills issue 1 by construction):

- For each road tile, derive its **openings** (which of N/E/S/W - and for diagonals, which corners -
  the road connects to). Easiest data-driven version: a tile connects in direction D if the neighbour
  in D is also a road/crossing tile. Map the XBLD id → shape only where the data is ambiguous
  (curves vs straights vs diagonals). The road mesh names (`RD29..RD43`) encode the shapes; decode
  one mesh per id if a lookup is wanted.
- Place **2 directional lanes** per opening pair, offset to the right of travel (lane offset ≈
  `0.18..0.25 * TileSize`). Connect a lane's entry waypoint (mid of the entry edge, right side) to its
  exit waypoint following the road path: straight across for straights, a short **arc** (2-3 interior
  waypoints) for curves, a **diagonal** corner-to-corner segment for diagonal tiles.
- Pedestrian sidewalk graph is the same idea at a larger outward offset (already partially done via
  `GetRoadSidewalkWorldOffset`, single side; upgrade to per-opening edge waypoints).
- Keep the current `ChooseNextRouteNode` graph-walk for choosing the next opening; it already gives
  good flow once the node positions are correct.

## Where things live

- `Private/Ground/SimCopterTrafficSystemActor.cpp`: node build (`RebuildSpawnData`), graph walk
  (`ChooseNextRouteNode`, `AssignNextTarget`), spawn (`TrySpawnAgent`), `IsSurfaceRoadTile`,
  `IsOriginalTrafficRoadTile`, `GetRoadSidewalkLocalOffset`, `GetRoadCenterlineLocalOffset`.
- `Private/Ground/SimCopterGroundAgent.cpp`: movement (kinematic), ground snap (ECC_Camera high
  trace), pedestrian 3D body, vehicle headlight spotlights, `PopulationWorldScale`.
- `Private/City/SimCity2000CityActor.cpp`: world mapping (`GetWorldTileCenterCoordinate` ~line 367,
  tile loop ~line 2197, mesh placement ~line 2268), `GetOriginalBridgeDispatch` (~line 518),
  `IsRoadLikeTile` (0x0E..0x6F).
- Parsing helpers used this session (scratchpad, not committed): RLE-decode XBLD per
  `FSimCity2000Reader::DecodeRleChunk`; tile index `row*128+col`; road test `0x1D..0x2B`.
