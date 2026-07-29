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
- FUN_004ae530 = slope limiter for the generated OUTSIDE-map terrain only (runs before the
  flatten pass; irrelevant to the city area). FUN_004ad7c0 = fractal fill for outside area.

Related: [[simcopter-mesh-orientation-rules]] (tile->world axis mapping used by the sweep).
