# SimCopter exposure scale

## InverseExposureBlend is invisible to Lumen (2026-08-07)

Every gameplay light in the project is physically tiny and made to *look* right by
`InverseExposureBlend` — the searchlight's 650,000 unitless is ~1,000 candelas, the headlights'
9,000 is ~14. **That compensation happens on the renderer's deferred-lighting side, and Lumen never
sees it**, so what Lumen was handed to bounce really was a hand torch. That is the whole reason the
searchlight and the headlights lit a patch of ground and contributed nothing around it.

The fix is `ULightComponent::IndirectLightingIntensity`, and the engine source is unambiguous about
it being the right knob — `LumenSceneDirectLighting.cpp` gathers a light into the Lumen scene only
when

```cpp
LightSceneInfo->ShouldRenderLightViewIndependent()
    && LightSceneInfo->Proxy->GetIndirectLightingScale() > 0.0f
```

and then does `DeferredLightUniforms.LightParameters.Color *= GetIndirectLightingScale()`. So it is
**both the gate and the scale**, and raising it is free: it multiplies lighting Lumen already
computes for that light. `SearchLightIndirectLightingIntensity` (24) and
`HeadlightIndirectLightingIntensity` (8) are the tunables.

**There is no per-light Lumen distance cap to set.** The budget that actually exists is
`r.LumenScene.DirectLighting.MaxLightsPerTile` — default **8**, valid 4/8/16/32, "pick per tile
based on their intensity and attenuation". In a city carrying hundreds of beacon point lights that
budget is the thing most likely to drop the searchlight, and raising it is the lever if the beam's
bounce comes and goes as you fly. Costs memory and surface-cache time, so move it a notch at a time.
 (why the effects and lights went black)

*"The sun went up by 30,000x. Nothing else did."*

*Recorded 2026-08-04, after the level moved to a `CelestialVaultDaySequenceActor`.*

## The symptom

All at once, and only after the celestial vault went in:

* the helicopter searchlight threw no light,
* the blinking marker cards rendered as **black quads**,
* their point lights had no visible effect at all,
* fire, water spray, dust and tear gas rendered **black**.

## Why

`ACelestialVaultDaySequenceActor::SunLightIntensity` is **120,000 lux** - the real number for
midday sun. The `SimCopterDayNightCycleActor` it replaced (added and deleted the same week, see
`git show 37fdd0e`) ran `SunIntensityDay` at **4.0 lux** with a hand-set exposure of 1.15.

Auto exposure is on for the project (`r.DefaultFeature.AutoExposure=True` plus
`ExtendDefaultLuminanceRange=True`), so it followed the sun up. Everything the remake authored
against the old scale stayed where it was, roughly 30,000x under the new exposure:

| Thing | Authored | In physical terms | Against a 120,000 lux sun |
| --- | --- | --- | --- |
| Effect card emissive | `VertexColor * 1.4` | ~1.4 nits | sunlit road is ~23,000 nits -> **black** |
| Marker point light | 12 x 0.02 unitless | ~0.0004 cd | nothing |
| Helicopter searchlight | 650,000 unitless | ~1,040 cd (a hand torch) | invisible |
| Car headlight | 9,000 unitless | ~14 cd | invisible |

`ULocalLightComponent::GetUnitsConversionFactor` is where the unitless->candela number comes from:
unitless x 16 / 100 / 100. A "650,000" light is not a big light.

**This was never a bug in the lights or the effects.** They are correct for the scene they were
tuned in. Nothing about them changed; the scale under them did.

## The fix

Two halves, because the lights and the materials need different mechanisms.

* **Lights:** `ULocalLightComponent::InverseExposureBlend` = 1 divides the exposure back out, so a
  gameplay light keeps constant presence on screen whatever the sun is doing. Set on the marker
  point lights, the searchlight (`SearchLightExposureCompensation`, an exposed property) and the
  vehicle headlights. The renderer applies this on the CPU side from
  `View.GetLastEyeAdaptationExposure()`, and it works.

* **Materials:** an `EmissiveNits` scalar on each unlit effect material, written every frame by
  `USimCopterEffectExposureSubsystem` from the level's actual key light: an effect card is drawn as
  bright as white ground under the same sun (`E / PI`), floored so it never vanishes at night. The
  drawing components (`USimCopterParticleFXComponent`, `USimCopterFlashingLightsComponent`) push it
  into a MID when they rebuild, so there is no new asset and nothing to wire up in the level. Live
  knob: `SimCopter.Effects.Brightness`. Covers `M_SimCopterParticleFX`, `M_SimCopterParticleFXSoft`
  and `M_SimCopterSpriteTexture` - every card, the FIREPTS kernels, and the flashing markers.

## THE OBVIOUS FIX DOES NOT WORK - do not "simplify" back to it

A `MaterialExpressionEyeAdaptationInverse` in the material does the same division in the shader with
no C++ at all, and it is what this was written with first. **It had no visible effect in game.** The
rebuilt material was live (checked the log timestamps against the session), it compiled clean, its
pins were connected and asserted, and the cards stayed exactly as black. The node is documented as
*experimental* access to the eye adaptation RT for post process materials, and this project renders
with Substrate + Nanite + Lumen. Whatever `EyeAdaptationLookup()` returns in this base pass, it is
not the exposure. Test on screen before believing it.

## Traps for next time

* **`DefaultEffectBrightness` is 1.0, not 1.4.** The effect materials multiply their palette colour
  by a hard-coded 1.4 of their own - that is the authored pop - and this supplies the white-ground
  luminance it applies to. Putting 1.4 in both places doubles every effect.
* **`Intensity` on a light component is not illuminance.** A directional light's forward axis points
  the way the light TRAVELS, so the cosine against flat ground is `-Direction.Z`. Getting it
  backwards is silent: the effects simply come out brightest at midnight.
* **Find the sun by COMPONENT, not by actor class.** The celestial vault's sun is a
  `UDirectionalLightComponent` on the day sequence actor, so `TActorIterator<ADirectionalLight>`
  finds nothing at all in this level.
* **The material helper checks its own pin connections.** `connect_material_expressions` returns
  False rather than raising when a pin name is wrong, and a dropped emissive connection looks
  exactly like this bug; `add_scene_scaled_emissive` logs `unreal.log_error` instead.
* **The material change only lands when the script is re-run** - both FX materials are in the
  delete-and-recreate list at the bottom, so nothing else is needed, but nothing happens without it.
  With the editor CLOSED (§6): `UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<file>`.
  It exits 1 on a clean run: `create_or_load_material` probes with `load_asset` after the delete and
  the failed probe is counted in the error summary. Check the log, not the exit code.
* **That run also rewrites `M_SimCopterCityAtlas`, `Terrain` and `Water`** with new `StateId`s even
  though nothing about them changed - they are in the same list. Revert those three unless they were
  the point, or everyone eats a shader recompile for nothing.
* **The rotor disc is fine and needs nothing**: `RotorDiscColor` is authored black
  (`SimCopterHelicopterPawn.h`), and black is black at any exposure.
* **`M_SimCopterSpriteTexture` is upgraded IN PLACE, not deleted and recreated**, because a material
  instance may hold it as a parent and deleting the asset nulls that out. `upgrade_sprite_texture_emissive`
  adds the nodes to the existing graph and skips a material that already has the parameter.
  That material also carries the pedestrian sprites and the privanim figure *heads*, so they stop
  being black too - though the better fix for those is probably to make them *lit*, the way
  [[simcopter-sprite-card-lighting]] fixed the trees.
* If the sky is ever retuned again, this is the first place to look before "fixing" an effect or a
  light: check the sun's lux against what the thing was authored for. See also
  [[simcopter-fire-water-fx]] and [[simcopter-flashing-lights]].
