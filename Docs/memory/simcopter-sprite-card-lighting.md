# SimCopter sprite-card lighting (trees and signs)

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
