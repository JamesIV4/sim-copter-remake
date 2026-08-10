# SimCopter terrain flattening

*"Original tmap conditioning (FUN_004abce0) decoded + ported: auto-flatten under buildings/flat roads, +0x20 ramps under raised spans 0x3f-0x42, water corner dip"*

*Ported into the repo 2026-07-29.*

SimCopter conditions the tmap corner height grid inside FUN_004abce0 (full decompile:
repo `Docs/scratchpad/ghidra/out_tmap_build.txt`; canonical write-up in repo
`Docs/ReverseEngineering.md` terrain section; port = `BuildConditionedTerrainCornerSamples`
in `SimCity2000CityActor.cpp`, threaded through type grid / terrain quads / bridge dispatch,
built once per RebuildCity):

- **Flatten**: XBLD >= 0x70 (buildings) and flat network {0x1d,0x1e,0x23-0x2d,0x32-0x3a}
  force all 4 tile corners to the tile's own ALTM sample ((step+1)*0x20).
- **Ramps**: raised spans 0x3f-0x42 pull one corner pair +0x20 above the opposite edge
  (0x3f low-X pair from corner(x+1,y); 0x41 mirror; 0x40 low-Y pair from corner(x+1,y+1);
  0x42 mirror). ORDER MATTERS: single raster sweep (Y outer, X inner), ramp reads see
  earlier writes - do not convert to per-corner evaluation.
- Water dip: plain water tiles at even/even coords lower origin corner by 8.
- NOT conditioned: bridges 0x49-0x59, highways 0x5d-0x6b, tunnels (decks float by design).

**Two deliberate divergences added 2026-08-04**, both because the remake flattens more aggressively
around these tiles than the original did and the original's compensating geometry is gone:

- **Power crossings 0x43/0x44 are flattened** like any other flat street. The original left them
  alone because the crossing object carried its own slab and had a sloped RD67H/RD68H variant; the
  remake gives the tile the ordinary straight-road piece instead, so unflattened it stands up out of
  the road as a raised block. See [[simcopter-road-tile-variants]].
- **Surface road ramps 0x1f-0x22 wedge +0x20** onto the tile's own ALTM sample (the low pair gets
  the sample, the high pair sample + 0x20) - anchored to the tile rather than to a neighbouring
  corner, because that sample is exactly where `GetAverageTerrainSurfaceZ` places the ramp mesh, so
  the ground meets the deck at both edges whatever order the sweep visits the neighbours in. Their
  high edges run **north, west, south, east** for 0x1f, 0x20, 0x21, 0x22 - the same order as rail
  slopes 0x2e-0x31, and measured off the meshes rather than guessed
  (`Docs/scratchpad/dump_ramp_direction.py`). The step is exactly right: RD31..RD34's asphalt climbs
  32 mesh units, and 32 units x 25 cm x the 400/1600 mesh scale = 200 cm = one `TerrainHeightScale`.
  Without it the neighbouring flat roads flatten the shared corners to their own levels and the ramp
  deck hangs in the air.

  **Two kinds of ramp opt out** (`IsRampTerrainClampSuppressed`) and keep the decoded untouched
  tmap: a ramp with a **one-step raised road deck** in its neighbourhood (0x3f-0x42 or 0x49-0x59 -
  a bridge approach), whose grade is already resolved by the raised-span rule and the bridge
  object's own one-step top, so wedging the ground too drives it into the deck; and a ramp with
  **water** in its neighbourhood, where raising a corner pushes land through the water surface. The
  test is the full eight-cell neighbourhood, not the four edge-sharing ones, because this pass
  writes CORNER samples and a diagonal neighbour shares one.
- FUN_004ae530 = slope limiter for the generated OUTSIDE-map terrain only (runs before the
  flatten pass; irrelevant to the city area). FUN_004ad7c0 = fractal fill for outside area.

Related: [[simcopter-mesh-orientation-rules]] (tile->world axis mapping used by the sweep).
