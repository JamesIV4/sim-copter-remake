# SimCopter road tile variants (the curb bug)

*"Every road tile has two meshes, and the heuristic XBLD table always picks the wrong one."*

*Recorded 2026-07-30.*

## The rule

`FUN_0047c0c0`'s XBLD **0x1d..0x2b** cases each run the same four-corner tmap flatness test the
bridges use, and dispatch to **two different meshes**:

| tile is | mesh | what it looks like |
| --- | --- | --- |
| flat | object 0x3b, 0x3c, 0x3d..0x45 = `RD29L`, `RD30L`, `RD35L`..`RD43L` | plain slab, **no curb** |
| sloped | object = the XBLD id itself = `RD29`, `RD30`, `RD35`..`RD43` | raised edges, **has a curb** |

The curb exists because a sloped road piece has to meet terrain across a grade change. **The
original therefore only shows road curbs on sloped roads.**

XBLD **0x1f..0x22** (`RD31`..`RD34`) are the four dedicated slope pieces. They have no `L`
counterpart in the pack at all, and the original dispatches them unconditionally — that absence is
the cross-check that the `L` suffix means "level".

## The trap

Roads were being resolved through `FMaxisMeshLibrary::FindObjectByTileId`, the heuristic
XBLD→mesh table. That table scores by object *name*, and `MappingVariantPenalty` deliberately
demotes the `F`/`H`/`L` suffixes — so a tile-id lookup for 29 always lands on `RD29`, never
`RD29L`. Result: a curb down both sides of every flat street in the city.

Fixed 2026-07-30 with `GetOriginalRoadTileObjectId` in `SimCity2000CityActor.cpp`, which feeds the
same `IsOriginalTerrainTileFlat` result the bridge dispatch already computed. Guarded by
`SimCopter.Formats.MaxisMesh.FlatRoadVariants`, which asserts both halves of all eleven pairs
resolve *and* that the tile-id lookup still prefers the curbed piece (i.e. that the regression is
still possible if someone reverts the dispatch).

`GetOriginalBridgeDispatch` already had a corner of this: its cases 0x45/0x46 (rail crossing under
a road) return `bTileIsFlat ? 0x3b : 0x1d` and `bTileIsFlat ? 0x3c : 0x1e` — the same pair, which
is what confirms the mapping independently.

## Power crossing and vehicle surface follow-up (2026-08-01)

XBLD `0x43/0x44` are RD67/RD68, the power-line-over-road pieces. **Superseded 2026-08-04 — see
"Road lines are procedural" below; those tiles now take an ordinary road primary and the procedural
dashes, and the crossing object contributes only its pylon and wires.**

Cars also must not discover their driving Z by tracing the highest arbitrary mesh. Their
nonswept movement intentionally phases through scenery, and every placed drivable road cell now
caches the plane of the exact authored asphalt face selected by `FUN_0047c0c0`. The selector uses
face type 15/material 48 at the authored road-line height, rather than the highest mesh face, so a
composite bridge contributes its deck instead of its towers, cables, or supports. Route nodes
sample that plane at the tile centre; `TryGetVehicleRoadSurfaceZ` samples it again at the vehicle's
current XY. This makes ordinary surface ramps, raised-span ramps, highway/on-ramp pieces, and bridge
approaches use the visible slope continuously for both Z and pitch. Collision traces remain only a
fallback when no authored road plane was extracted. `FUN_004c82c0` independently confirms that the
original queries the placed scene-object surface rather than terrain alone.

## Road lines are procedural, and that is the settled look (2026-08-04)

Surface-road `RD*` meshes do own face-type-20/material-112 centre-line endpoints, and for three days
the remake rendered *those* as ribbons instead of drawing its own. **It was reverted.** Whatever the
MAX exporter authored per piece is not the cadence the project settled on, so switching to it wiped
out a long-approved dash frequency and spacing, and it put a line through every intersection.

The road-line system is `GetOriginalRoadMarkingOpeningMask` -> `AppendRoadMarkingsForTile` ->
`AppendTiledDashedRoadMarkingSegment`, and the three things to know about it:

* **Frequency** is `SegmentCount`: two dashes when the tile's two openings oppose, one across a
  corner. **Spacing** is `DashFillRatio` (0.52 of each period painted, the gap split at both ends).
* **Z comes from the placed road object's asphalt plane, not the terrain grid.** `AppendRoadMarkings-
  ForTile` runs *after* the mesh block in the tile loop precisely so `RoadSurfaceProfiles[TileIndex]`
  is already filled, and each dash vertex evaluates that plane. Drawing on the conditioned terrain
  instead is what left ramp dashes hanging in the air below their deck. The ribbon also takes its
  normal from the emitted quad, so a dash on a ramp is not lit as if it were flat.
* **Intersections are blocked by the opening count, not by a list.** `0x27..0x2b` declare three or
  four openings and `AppendRoadMarkingsForTile` returns early on anything but exactly two. Making
  those ids return fewer openings, or relaxing the early return, puts yellow through every junction.
* **Only one of the two systems may be on for a tile.** `bBuildVectorLines` is false for
  `0x1d..0x2b`, `0x3f..0x42`, `0x0e..0x1c`, and `FPlacedObjectRoadFaceFilter` suppresses the
  face-20/material-112 pair per placed object where the range test cannot (the power crossings).

`TL63..TL66` have no authored centre line either way; their dashes use the cap's object-top plane.

**Power crossings (`0x43`/`0x44`) are a deliberate divergence.** The original packs road and pylon
into one object, and that packed road half is a different slab from the street either side — it
reads as a raised block across the crossing and carries its own centre line at its own cadence.
`GetOriginalBridgeDispatch` now splits it the way the original already splits the rail crossing at
`0x45/0x46`: primary = the ordinary straight road (`0x3b`/`0x3c` flat, `0x1d`/`0x1e` sloped),
secondary = `RD67/RD68` (`RD67H/RD68H` sloped) with its road half filtered out, and the tile is
added to the terrain flatten set so it sits level with the street ([[simcopter-terrain-flattening]]).

**Measure the materials before filtering** (`Docs/scratchpad/dump_maxis_object.py` dumps any object's
faces straight out of the `.MAX` container, no editor needed). RD67 is 35 verts / 20 faces:

| faces | type/material | what it is |
| --- | --- | --- |
| 1 | 15 / 48 | flat asphalt slab |
| 12 | 15 / 208 | the poles - the only geometry reaching full height |
| 4 | 20 / 50 | the wires, strung at pole top |
| 3 | 20 / 112 | yellow centre line, at the same height as RD29L's |

RD67H adds **10 faces of 15/128**, the sloped body the H and ramp pieces build on top of the 15/48
plane. Filtering only material 48 leaves those ten standing as a raised block — that is the exact
bug, and it is why `FPlacedObjectRoadFaceFilter` drops 48 **and** 128.

**Do not drop the secondary entirely to "remove the road": the wires on that tile come from it.**
The dynamic span builder at the bottom of `RenderCity` only gathers `0x0e..0x1c`, so with the
crossing object gone the power line breaks at every road.

Trains have a separate exact rule in `FUN_004b7020`: rail bridge ids `0x5a/0x5b` and the
`0x805a/0x805b` XZON-bit-1 variants add `0x1f` original units to the target up-coordinate. Since a
tile is `0x40` units, the remake raises the RL90/RL90F train plane by `31/64` of a tile and keeps
that height at both shared edges of the bridge. Rail slopes `0x2e..0x31` use `15.5/64` at their
centres and `31/64` at their direction-specific high edge (north, west, south, east respectively),
with zero lift at the low edge. Shared-edge waypoints therefore form a continuous low-edge to
half-height to high-edge grade instead of dipping through terrain or water at tile boundaries.

## Still unported

The same switch cases place **decoration objects** on flat road tiles, which the remake does not:

* straight roads (0x1d/0x1e) at odd/odd tile coordinates pick one of four objects at random
  (`rand() & 0xf` → 0x186/0x188/0x18a/0x18c for 0x1d, 0x187/0x189/0x18b/0x18d for 0x1e);
* intersections 0x23..0x26 at `(x&3)==3 && (y&3)==3` place 0x181..0x184; 0x27..0x2a place 0x185 at
  odd/odd; 0x2b places 0x185 unconditionally.

Object 0x185 is `SIGNAL1`, a traffic signal — and it carries face-type-25 blink markers in red,
yellow and green, so porting these would light the intersections. See
[[simcopter-flashing-lights]].

Related: [[simcopter-instanced-buildings]], [[simcopter-terrain-flattening]],
[[simcopter-mesh-orientation-rules]].
