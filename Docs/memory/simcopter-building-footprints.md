# SimCopter building footprints

*`FUN_0047c0c0` claims square XBLD blocks in row-major scene-cell order; XZON does not own mesh placement*

*Decoded and ported 2026-07-31.*

## Original rule

The city builder reads the footprint size from `FUN_004e4f80`: buildings `>= 0x70` use the
signed-short table at `DAT_004fad30`, whose values are square sizes 1-4. For sizes 2-4,
`FUN_0047c0c0` verifies that the full square beginning at the current raster cell contains the same
XBLD byte. On failure it clears that origin in its working XBLD grid. On success it writes one
scene-cell pointer into every covered `DAT_005d9200` slot, so later cells in the row-major sweep are
suppressed.

The placement position is the square center: size 2 adds `0x20`, size 3 adds `0x40`, and size 4 adds
`0x60` to the first tile-center coordinate on each horizontal axis. No XZON byte participates in
this path.

## Islandtown regression

The remake previously treated `XZON & 0x80` as a top-left mesh owner, then scanned right/down for
other XZON corner bits. In Islandtown (`cities/career/city1.sc2`) those 0x80 cells are at the far
corner of many multi-tile XBLD squares. The scans therefore fell back to 1x1, placing a full 2x2,
3x3, or 4x4 GEO model half to one-and-a-half tiles off its actual center along each axis and across
neighboring roads.

There are 104 wrong owners/sizes in Islandtown under the old rule. A representative XBLD `0xd6`
building occupies `(79,52)..(81,54)`: the original claims `(79,52)` as a 3x3 placement, while the
old remake selected the `XZON 0x80` cell at `(81,54)` as a 1x1 placement.

## Port

`FSimCopterCityGeometryRules::ClaimOriginalBuildingFootprint` owns the pure claim/validation rule.
`ASimCity2000CityActor::RebuildCity` retains the scene-cell state across its row-major pass and uses
the returned square for mesh centering, terrain-height sampling, instanced-building ownership, and
demolition coverage. `XZON` remains available for zoning/base dispatch but no longer decides where a
building mesh is placed.

Automation coverage:

- `SimCopter.City.BuildingFootprintClaim` uses a synthetic 3x3 building whose XZON 0x80 is at the
  far corner and checks that only the XBLD origin claims it; it also rejects an incomplete square.
- `SimCopter.City.IslandBuildingFootprints` checks the reference Islandtown file has 274 original
  claims, including the `(79,52)` 3x3 placement and suppression of `(81,54)`.
