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

XBLD `0x43/0x44` are RD67/RD68, the power-line-over-road pieces. Their mesh now imports its own
face-type-20 centre line correctly, so `GetOriginalRoadMarkingOpeningMask` must return zero for
both. The former procedural centre-line workaround creates a second raised strip/block across the
road and is not part of the original piece.

Cars also must not discover their driving Z by tracing the highest arbitrary mesh. Their
nonswept movement intentionally phases through scenery and `TryGetVehicleRoadSurfaceZ` interpolates
the road graph. The decoded dedicated surface ramps (`0x1f..0x22`), raised-span ramps
(`0x3f..0x42`), and highway/on-ramp range (`0x5d..0x6b`) can refine that result from their road
mesh so a vehicle uses the exact visible slope for both Z and pitch. The raised caps and road
bridge pieces (`0x3f..0x42`, `0x49..0x59`) put their driving plane one ALTM step above the scene-cell
terrain origin (confirmed by their GEO vertices and `FUN_004c82c0`), so the road graph carries that
explicit offset. Bridge pieces must otherwise remain graph-driven: each bridge is a composite mesh
containing supports/towers as well as its straight deck, and a highest-hit trace can teleport cars
onto those non-road surfaces. Roofs and the power crossing likewise remain non-solid to traffic.

`TL63..TL66` have no face-type-20 centre line, so their procedural marking uses that same raised
object-top plane instead of the conditioned terrain under the cap. `RD67`/`RD68` and bridge pieces
`RD73`/`RD74` already own face-type-20 line geometry; their old procedural duplicates are disabled.

Trains have a separate exact rule in `FUN_004b7020`: rail bridge ids `0x5a/0x5b` and the
`0x805a/0x805b` XZON-bit-1 variants add `0x1f` original units to the target up-coordinate. Since a
tile is `0x40` units, the remake raises the RL90/RL90F train plane by `31/64` of a tile and keeps
that height at both shared edges of the bridge. Using plain tile-centre ALTM was why trains drove
through the water below those meshes.

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
