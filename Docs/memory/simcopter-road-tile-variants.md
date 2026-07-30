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
