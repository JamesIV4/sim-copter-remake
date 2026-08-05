# SimCopter instanced buildings

*City buildings are per-model runtime UStaticMesh instances (not baked into the merged city mesh) so one can be removed when it burns down*

*Recorded 2026-07-24; ported into the repo 2026-07-29.*

**Since 2026-07-24 the city's buildings are instanced, not baked.** Roads, bridges,
power lines and terrain still go into the merged `OriginalMeshComponent`; only
buildings changed, because only buildings can be destroyed.

Each distinct GEO building model is built once at load as a runtime `UStaticMesh`
(`SimCopterRuntimeStaticMesh::Build` -> `UStaticMesh::BuildFromMeshDescriptions`
with `bFastBuild`), collision-cooked once as complex-as-simple, and every
placement is an instance of it in a per-model `UInstancedStaticMeshComponent`.
Demo city: 107 models (103 buildings + 4 rubble), 2624 placements, triangle
totals unchanged. Rubble models are 0x14f..0x152 by footprint size 1..4.

**Why:** the merged mesh gave a building no identity to remove - dropping its
triangles meant re-uploading a whole texture section and re-cooking all 509k
triangles of the component's collision. Instancing makes demolition
(`ASimCity2000CityActor::DemolishBuildingAtTile`, driven by
[[simcopter-mission-system]]'s `OnBuildingBurnedDown`) a `RemoveInstance`: no
cook, no buffer rebuild, and the collision leaves with the instance.

**How to apply:**
- Model geometry is built at the ORIGIN. The global 180-degree city yaw is
  already folded into the per-vertex work, so a placement transform is a pure
  translation to the tile origin - no rotation.
- Identity is the BUILDING ID (index into the city actor's `Buildings` array), not
  an instance index. Every tile of the footprint maps to it; instance indices live
  only inside the `FSimCopterCityBuilding` record.
- `UInstancedStaticMeshComponent::RemoveInstance` SHIFTS later instances down one
  by default. The components call `SetRemoveSwap()` so exactly ONE instance moves
  (last into the freed slot) and the displaced record is re-pointed in O(1).
- **Runtime static meshes do not survive world duplication.** RenderData is a bare
  TUniquePtr, not a UPROPERTY, and there is no committed source description - so
  PIE gets a non-null mesh that cannot draw or collide. That made buildings
  editor-only. `AreBuildingInstancesIntact()` checks `HasValidRenderData()` (a
  null check misses it) and BeginPlay rebuilds. Any future runtime-built asset
  here needs the same treatment.
- `bAllowCPUAccess` must be set BEFORE `BuildFromMeshDescriptions` or the runtime
  tri-mesh cook silently yields no collision.
- Two tiles share a model iff (primaryObjectId, secondaryObjectId, meshTileId)
  match - that triple fully determines the geometry; per-tile zone/bitflags only
  feed into resolving those ids.
- `bInstanceBuildingMeshes` on the city actor falls back to the old merged path
  (and disables demolition with it).

## Trees and the park are instanced too (2026-08-05), for a different reason

XBLD 0x06..0x0C are TREE6..TREE12 and 0x0D is LP13, "small park" - 8 models, **3250 placements** in
the demo city. Baked, every one of them was part of a single *movable* `UProceduralMeshComponent`
spanning the whole map, and that is what the virtual shadow map objects to: a movable primitive that
size cannot have its pages cached, so anything touching it invalidates shadow pages across the
entire city. Pulling them out dropped the merged mesh from ~509k triangles to **434,366**.

They take a **separate** path from buildings (`NaturalObjectInstanceComponents`,
`bInstanceNaturalObjectMeshes`), not a shared one, because the building path keeps
`ComponentInstanceBuildings` in lockstep with every instance so a demolition can repair the single
index a swap-remove displaces. A tree has no building record to be in lockstep with, and mixing them
would leave that invariant silently half-true.

Their components are **Static**, unlike the buildings' Movable - nothing adds or removes a tree
after the build, and static is precisely what lets the VSM cache the pages. Buildings have to stay
Movable because demolition mutates them.

**The park is flattened like a building, and that is a deliberate divergence.** The original treats
the whole 0x06..0x0D band as natural cover and leaves it on the ALTM grade. But LP13 is not cover -
it is a flat authored slab with paths printed on it, the same kind of pad a building sits on, and on
a slope its corners cut into the hill on one side and hang in the air on the other. So 0x0D joins
the flatten sweep and drops out of `bGroundHuggingObjectTile` (it now takes the pad sample like a
building instead of the bilinear surface sample). The trees either side of it in the band genuinely
are foliage and still follow the slope - which is why this is the one id, not the range.
