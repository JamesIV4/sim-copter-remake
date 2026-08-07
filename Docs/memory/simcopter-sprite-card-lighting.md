# SimCopter sprite-card lighting

## A masked card is SOLID in every representation but the raster (2026-08-07)

The trees cast a full-rectangle shadow under the helicopter's searchlight, and the qualifier is the
diagnosis: **not under the sun.** The leaves are cut out of a quad by the opacity mask, and that mask
only exists where a pixel shader runs. A virtual shadow map is rastered, so the sun masks correctly.
The distance fields and the ray tracing scene run no pixel shader at all, so both carry the card as
the solid quad it geometrically is — and any local light shadowing against those casts the whole
rectangle.

The tree instances are now `SetAffectDistanceFieldLighting(false)` +
`SetVisibleInRayTracing(false)` (`bNaturalObjectsAffectDistanceFieldLighting` /
`bNaturalObjectsVisibleInRayTracing`). A rectangular shadow is strictly worse than none, and thin
cards buy little occlusion either way in a city whose GI is dominated by ground and buildings.

Ruled out on the way: these meshes are **not Nanite** (no `NaniteSettings` anywhere in the city
actor — the material's `used_with_nanite` flag is defensive), so the Nanite programmable-raster
explanation does not apply here.

## Per-family shading ceilings (2026-08-07)

Making the cards Default Lit fixed the inversion but created a new one at the other end: **in bright
sun the trees blew out to white.** Two causes stack, and both are in this file's own design.

1. The palette texel can be near 1.0 albedo. Under a physically scaled 120,000-lux sun that
   tonemaps to white with no help from anything else. Real foliage is 0.15-0.25.
2. **`CardNormalUpBias` is 1.0**, which is the thing that makes trees shade sanely — and it also
   means that at noon *every card in the city faces the sun at once*, so the shared 0.3 Specular put
   one broad highlight across the entire canopy. A leaf is not wet plastic.

Trees, terrain and water now take **roughness, specular and an albedo ceiling** from
`MPC_SimCopterDayNight` instead of `add_shading_nodes`' shared 0.65/0.3, published live by
`USimCopterDayNightSubsystem::PublishSurfaceShading` off nine `SimCopter.Shading.*` console
variables. Defaults in `SimCopterDayNight.h`; the Python mirror is `SURFACE_SHADING_DEFAULTS`.

| family | max brightness | roughness | specular | materials |
| --- | --- | --- | --- | --- |
| Tree | 0.42 | 0.92 | 0.02 | `M_SimCopterLitSpriteTexture` |
| Terrain | 0.55 | 0.88 | 0.04 | `M_SimCopterTerrain` |
| City | 0.55 | 0.88 | 0.04 | `M_SimCopterCityAtlas`, `M_SimCopterLitTexture`, `M_SimCopterLitVertexColor` |
| Water | 0.75 | 0.12 | 0.90 | `M_SimCopterWater` |

**City was added after the ground was tuned**, because buildings and roads left on the shared
0.65/0.3 stood out badly beside a terrain that already looked right — so it starts on the terrain's
exact numbers and exists to be pulled away from them later, not to differ now. It needs all three
materials: the atlas pages are most of the city, but the direct-image faces and every flat
palette-coloured face are the rest, and doing only the atlas leaves buildings standing out on fewer
faces. **`M_SimCopterLitVertexColor` is also the VEHICLES' material**
([[simcopter-vehicle-material]]) so cars ride the City ceiling too; roughness and specular were
already shared with the city, and their MID still overrides Metallic on top.

**The ceiling is on ALBEDO, and it has to be** — a material cannot see its own lit result, so there
is nothing to clamp after the lighting. Bounding albedo is what bounds diffuse brightness. It is
also **hue preserving** (`ALBEDO_CEILING_CODE` scales by the brightest channel) rather than a
per-channel `min`, which would walk a vivid green tree towards the ceiling GREY instead of just
making it darker.

Water was the same change in the opposite direction: it had been sharing the dirt's matte numbers,
which is why it had no sun glint. Making it glossy then exposed how it ENDS, which is its own note
below.

## The water's shoreline: softness was never the problem, SHAPE was

Two separate fixes, and only the second one is interesting.

**Softness** is `WaterShoreRoughness` / `WaterShoreSpecular` easing in over `WaterShoreFadeWidth`
(0.15) of the wave-weight ramp, so the mirror ends as wet sand instead of on a hard line. Vertex
colour R is already that mask, so no new vertex data and no re-bake.

**Shape** is the one that reads as a bug. The weight is interpolated **bilinearly across 400 cm
quads**, so any contour thresholded out of it *traces the grid*: straight runs along tile edges,
45-degree cuts across quad diagonals, a kink wherever two quads meet. **Softening the ramp cannot
fix this — a soft grid is still a grid**, and that is the trap: the obvious knob looks like it
should help and does not.

What fixes it is warping the weight in WORLD SPACE before the ramp
(`WaterShoreEdgeNoiseStrength` / `...Scale`). Displacing the value displaces the contour laterally,
by roughly `strength / |grad(weight)|`, so the stair-step becomes a line that wanders across tile
boundaries instead of along them. **The wavelength has to be several tiles**: at tile scale it just
adds fizz to the same stair-step, and the wander has to be bigger than the 400 cm step it is hiding.
Strength is in weight units, so how far it actually moves depends on how fast the weight ramps -
a shallow coast wanders more than a steep one.

There are **two independent layers** (`...Strength2` / `...Scale2`), each with its own amplitude and
its own wavelength rather than the second being an octave chained off the first. They sample at
different world offsets so they cannot line up and reinforce into one wave.

**The tile-scale reasoning above was tested on screen and LOST.** The argument says the wavelength
must exceed the 400 cm tile so the contour wanders further than the stair-step it hides, and the
first defaults (900 / 300 cm, strengths 0.18 / 0.06) were picked that way. What actually looks right
is **25 cm and 4 cm at strengths 0.25 / 0.2** — an order of magnitude finer, which *dissolves* the
boundary into a stochastic band instead of moving it. Both hide the grid; the fine one hides it
better, because a wandering line is still a LINE and the eye finds it. The automation test that
asserted the coarse-than-a-tile floor has been deleted rather than loosened: it was encoding a
hypothesis, not a fact. Shipping values are `FadeWidth 0.4` with those four.

The ramp is smootherstep rather than smoothstep for the same family of reason: one more order of
continuity removes the faint crease where the fade starts and stops.

**Both layers animate, and the animation is a THIRD AXIS ON THE HASH, not a scroll**
(`WaterShoreEdgeNoiseSpeed` / `...Speed2`, in noise FRAMES PER SECOND). The field changes while
staying put. Sliding the sample position instead - the obvious way to animate noise, and the first
attempt - reads as a conveyor belt at these wavelengths: the whole shoreline visibly travelling one
way.

**The frame interpolation is a CATMULL-ROM THROUGH FOUR FRAMES, and the two-frame smoothstep it
replaced is the interesting failure.** Smoothstep between adjacent frames is C1 and looks wrong
anyway: ease-in-ease-out means the field reaches every keyframe with *zero velocity*, so it arrives,
holds, and rushes to the next - still, fast, still, fast, once per frame. On screen that is exactly
"framey". **The obvious next move makes it worse**: smootherstep flattens the ends further and
lengthens the hold.

What it wants is a curve that passes *through* each frame without stopping on it - the ordinary
keyframe-interpolation problem, with the ordinary answer. A cubic through four control points takes
its slope at each frame from that frame's neighbours, so the field evolves at a near-constant rate
and there is no beat to see. Costs four spatial taps per layer instead of two. The slight [0,1]
overshoot between frames is left unclamped on purpose: clamping would reintroduce a flat spot of
exactly the kind being removed, and the warp is saturated at the end regardless.

The frame index is wrapped modulo 256 like the xy indices already were. Time climbs without bound
and `sin()` of a large argument loses the precision the hash depends on, so without the wrap the
water would quietly stop changing after a while.

Layer 2 is the fine one (4 cm), so **its rate is where shimmer comes from**: detail that small is
near sub-pixel from altitude and animating sub-pixel detail sparkles. If the water fizzes from high
up, that is the number to bring down, not the strength.

**Three traps when re-running the generator:**

* **`create_day_night_parameter_collection()` must run before ANY material that reads a collection
  scalar**, and it used to sit two-thirds of the way down the script. A CollectionParameter node
  resolves its ParameterId from the name *through* the collection, so a material built before the
  collection has the new scalar keeps a null name and compiles to a **constant 0** — a zero albedo
  ceiling is a black building. It is not enough for the collection asset to merely exist; it has to
  already contain the parameter. This is precisely how the City family shipped broken on its first
  run, caught only by the verifier below.

* `M_SimCopterLitSpriteTexture`, `M_SimCopterLitTexture` and `M_SimCopterLitVertexColor` are all
  `create_if_missing` rather than in the delete-and-recreate list, so **editing their generator does
  nothing until the asset is gone**. `Docs/scratchpad/delete_lit_sprite_material.py` deletes all
  three; then run the create script, then `BakeCityAtlas.py`.
* **A `create_if_missing` asset can be carrying flags its generator never sets**, and deleting it
  loses them. `M_SimCopterLitVertexColor` had `used_with_instanced_static_meshes` on disk - the
  engine patches that flag on at runtime and somebody saved it years ago - while the generator only
  ever set `used_with_nanite`. The first regeneration dropped it, and the only symptom was a map
  check warning (it would have rendered wrong in a cooked build). The generator sets it now. Read
  the map check output after any regenerate, not just the Python log.
* After that delete, the create script's re-parent pass **cannot rescue those instances**: it only
  moves instances still on the OLD unlit parent, and a delete leaves them on *null*. In practice the
  recreated asset keeps the same object path so the references resolved (verified: 68/68 still
  lit) — but check with `Docs/scratchpad/repair_city_image_parents.py`, which repairs them if not.

Verify the whole thing landed with `Docs/scratchpad/verify_surface_shading.py`. It checks both
halves, because **a CollectionParameter whose name does not resolve compiles to a constant zero** —
a zero roughness is a mirror and a zero albedo ceiling is a black surface, and neither says why.
 (trees and signs)

*"An unlit material does not look constant under a day/night cycle — it looks INVERTED."*

*Recorded 2026-08-04, after the level moved to a `CelestialVaultDaySequenceActor`.*

## The symptom

Trees rendered **dark at noon and bright at midnight**. Nothing was inverted anywhere in the code:
the trees were simply the only city geometry that was not lit.

## Why

Maxis face type 2 is a textured **sprite card** — trees and signs — and face type 13 is the rare
direct-image polygon. `SimCity2000CityActor` puts both into baked direct-image sections
(`MakeBakedDirectImageSectionKey`), which resolve to `MI_CityImage_<n>` from
`Content/Generated/CityAtlas`. `BakeCityAtlas.py` parented those instances to
**`M_SimCopterSpriteTexture`, which is `MSM_Unlit`**: it writes the decoded palette colour straight
into Emissive.

An unlit surface holds one fixed brightness while everything around it tracks the sun. Under a
static sky nobody notices. Under a day sequence the *relative* brightness flips: at noon the lit
ground and buildings out-render the fixed card, so the tree reads dark; at midnight everything else
falls away and the card is the brightest thing on screen.

Every other city surface was already Default Lit — atlas pages (`M_SimCopterCityAtlas`), terrain
and water, and the untextured vertex-colour geometry — which is why only the trees misbehaved.

## The fix

`M_SimCopterLitSpriteTexture` (authored in `Tools/Unreal/CreateSimCopterMaterials.py`): the same
masked chroma-key sampling, Default Lit, sharing the city's `SelfIllum` / `Roughness` / `Specular`
parameters. `BakeCityAtlas.py`'s `SPRITE_MATERIAL` now points at it.

**The shading normal is the part that is not obvious.** `AppendMaxisSpriteCard` builds a card as a
crossed pair of *vertical* quads, each emitted with both windings, so its geometric normals are
horizontal and point opposite ways across the crossing. Lit off those, a tree goes black under a
high sun (`N·L → 0`) and shows a seam where the quads meet. So the material blends the normal
towards world up with a `CardNormalUpBias` parameter, **default 1.0**: the card then shades exactly
like the flat ground it stands on and tracks the sun in step with its own tile. Lower the bias to
let the geometric normal back in — only the type-13 polygons have meaningful normals here.

The blended normal is world space, so `tangent_space_normal` must be **off**. The material also
needs `used_with_nanite` and `used_with_instanced_static_meshes`: trees ride both the merged city
mesh and the per-model instanced building meshes (see [[simcopter-instanced-buildings]]).

## Traps for next time

* **`MI_CityImage_*` is gitignored** (decoded original art). Re-parenting them is a pass at the end
  of `CreateSimCopterMaterials.py` rather than a re-bake, because `BakeCityAtlas.py` re-decodes all
  of SIM3D.BMP in pure Python to produce identical textures. Run the materials script and the
  instances re-attach; it is idempotent.
* **`CreateSimCopterMaterials.py` deletes and recreates the "tuned" materials on every run**
  (`M_SimCopterCityAtlas`, `Water`, `Terrain`, `ParticleFX`, `ParticleFXSoft`). Their `/Game` paths
  are stable so serialized references still resolve, but their `StateId`s change, so the next
  editor start recompiles those shaders. `M_SimCopterLitSpriteTexture` is deliberately **not** in
  that list — `MI_CityImage_*` holds a hard reference to it as a parent.
* **Still unlit, on purpose:** fire (`FIREPTS`), particle-FX kernels and the rotor disc are their
  own light source. Do not "fix" those. See [[simcopter-fire-water-fx]].
* **The figure heads are on this material now too (2026-08-06).** They were the "still unlit, and
  probably wrong" case this note used to leave open; the interim fix computed their brightness from
  the key light every frame as a zero-floored surface, which turned "glowing all night" into
  "totally black all night" and is silly either way — a head is painted geometry, not a card that
  emits. Both `FigureHeadMaterialInstance` sites (`SimCopterGroundAgent.cpp`,
  `SimCopterOnFootPawn.cpp`) create their MIDs from `M_SimCopterLitSpriteTexture` and set
  **`CardNormalUpBias` to 0**: the default 1.0 is for the crossed vertical quads a tree is made of,
  and `AppendBall` gives a head real normals to shade off. Full note in
  [[simcopter-night-lighting]]. The legacy PEOPLE1 billboard path
  (`LoadOriginalPedestrianSpriteFromOriginalGameRoot`, reached only for a pedestrian whose mesh name
  is a PEOPLE1 one) is the last unlit person surface and keeps its computed exposure.
  See [[simcopter-ue-figure-component]].
