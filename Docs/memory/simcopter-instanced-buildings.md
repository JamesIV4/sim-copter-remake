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
