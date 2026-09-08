# Tree grounding during city load

The remake grounds each original face-type-2 tree once in `RebuildCity`.
`TREE6` has one tree; `TREE7..12` contain 2, 3, 4, 6, 9 and 12 trees.
Each source face becomes a crossed pair in `AppendMaxisSpriteCard`. Both cards
share a single vertical placement correction; neighbouring trees in a dense
tile get their own correction.

The ground contacts are the opaque texels in the lowest occupied image row,
scanning from V=0 (the bottom in the existing card renderer). Fully transparent
columns and margins contribute nothing. Branch undersides in higher rows are
excluded: the earlier per-column outline could bury the trunk to ground a branch.
Samples lie at column centres on the bottom edges of the base texels. Both card
orientations contribute to the solve.

For each sample, compute `terrainZ - pixelZ` and take the minimum. Adding this
offset closes the largest gap exactly, leaving no sampled bottom edge above
the terrain. It also lifts trees that were buried too deeply. Ground height now
comes from Unreal's `LineTraceComponent` against the actual terrain component,
after all its sections and synchronous collision cooks finish. This includes
neighbouring terrain and ignores other tree cards and buildings. If any base
sample has no terrain hit, the original placement is preserved.

Image outlines and split model meshes are cached per source model during the
load. Each tree's solved position is stored in its static instance transform
(or baked vertices with instancing disabled). There is no tick-time solve,
terrain trace or alpha readback. A full city rebuild regenerates these placements.
This is a requested remake presentation change, not original placement parity.
