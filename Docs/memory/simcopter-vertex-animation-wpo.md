# SimCopter vertex animation wpo

*"Animate/displace city mesh vertices via material World Position Offset, not CPU mesh updates."*

*Ported into the repo 2026-07-29.*

For vertex animation/displacement on the city meshes, use a **material World Position Offset (WPO)** shader, not per-frame CPU `UProceduralMeshComponent::UpdateMeshSection` rebuilds.

**Why:** The sea undulation was first done on the CPU (recompute ~200k verts + full vertex-buffer re-upload every frame, throttled to 30Hz). It worked but was expensive and — because it relied on actor Tick — animated in the editor viewport but not reliably in gameplay. Reimplemented as WPO in `M_SimCopterWater`: GPU-only, zero per-frame CPU, and it animates identically in the editor and in game. User reaction: "extremely good, very smooth, much better than before."

**How to apply:**
- Author the material in `Tools/Unreal/CreateSimCopterMaterials.py` (see `create_water_material` + the `add_custom_node` helper). Custom HLSL nodes do the displacement (`WorldPositionOffset`) and analytic normals (`Normal`, world-space so set `tangent_space_normal=False`). Run it with `UnrealEditor-Cmd <uproject> -ExecutePythonScript="<abs path>" -unattended -nopause`.
- Drive params from a `UMaterialInstanceDynamic` at build time (texture + scalar params from the actor's UPROPERTYs).
- Bake per-vertex control data (e.g. the shoreline pin weight, 0=pinned/1=free) into **vertex-color channels** so the shader reads it for free; a value that's a pure function of world XY moves shared verts identically → no cracks.
- Reuse the exact same texture the neighboring static surface samples (read it off the baked MI via `GetTextureParameterValue`) so the animated section looks identical at rest.

Related: [[simcopter-population-rendering]], [[simcopter-terrain-flattening]] (the tmap conditioning that flattens terrain under buildings/flat roads — relevant to the follow-up request to smooth natural terrain while keeping man-made edges flat).
