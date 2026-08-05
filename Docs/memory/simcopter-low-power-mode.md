# SimCopter Low Power Graphics — one checkbox, three layers, and what each one is worth

*"The expensive part of this project is not the content. It is Unreal 5.8's defaults."*

*Recorded 2026-08-05.*

The Settings screen's Graphics page ([[simcopter-settings-menu]]) grew a **Low Power Graphics**
checkbox. `USimCopterSettings::bLowPowerMode` stores it; `SimCopterLowPowerMode.h/.cpp`
(`Public/Game`, `Private/Game`) is the mode itself.

The city is a few hundred thousand triangles of flat-shaded 8-bit palette art and it was being drawn
through Lumen, virtual shadow maps, MegaLights, a volumetric cloud layer, volumetric fog and TSR.
None of that renders anything a SimCopter player would miss, and all of it is priced for a discrete
GPU.

## The three layers, and why they are kept apart

1. **Overall scalability drops to Low** (`SetOverallScalabilityLevel(0)`), which covers the long
   tail: shadow resolutions, post-process quality, texture streaming, translucency, effects. Going
   through the engine's own group is what keeps the page's Quality rows reading back the truth.

   It costs **almost nothing visually here**, and that is not luck — it is worth knowing why, because
   the same reasoning is what makes Low safe to keep: the atlas pages are 256x256 with **no mips**
   (`BakeCityAtlas.py`), so `r.Streaming.MipBias=16` has nothing to bias and `r.MaxAnisotropy=1`
   nothing to filter; and nothing in the project uses Cascade, Niagara, hair or subsurface, which is
   most of the rest of the Low profile. `r.EyeAdaptationQuality` stays at **2** at every level, which
   matters more than anything else on this list — the whole emissive scheme is metered against auto
   exposure ([[simcopter-exposure-scale]]) and freezing it would black out or blow out the city.

2. **The CVar table in `SimCopterLowPowerMode.cpp`**, which is deliberately **only what scalability
   does not do**. Duplicating a value there would pin it at `ECVF_SetByGameOverride` and quietly kill
   its own dropdown. What is left:

   | Switch | Why scalability does not cover it |
   | --- | --- |
   | `r.AntiAliasingMethod 1` | Not a scalability group at all. Low only tunes TSR's history; TSR itself is one of the largest items in an iGPU frame. **Stood down while DLSS super resolution is on** — DLSS owns the AA method and forcing FXAA stops it engaging. |
   | `r.DynamicGlobalIlluminationMethod 0`, `r.ReflectionMethod 0`, `r.Lumen.HardwareRayTracing 0` | `GlobalIlluminationQuality@0` sets `r.Lumen.DiffuseIndirect.Allow 0`, which stops Lumen *lighting* but leaves the method as Lumen — the scene still maintains a Lumen scene and surface cache. Dropping the method is what stops that work. |
   | `r.RayTracing.ForceAllRayTracingEffects 0` | The project ships `r.RayTracing=True`. |
   | `r.MegaLights.Allowed 0` | **The trap.** MegaLights is switched on by the level's *global post process volume* (`bOverride_bMegaLights`), so `r.MegaLights.EnableForProject` cannot turn it off — `FSceneView::StartFinalPostprocessSettings` reads that CVar and the volume then blends over it. `r.MegaLights.Allowed` is the scalability gate above both. |
   | `r.Shadow.Virtual.Enable 0` | Low only shrinks the physical page pool to 512. |
   | `r.VolumetricCloud 0` | The `CelestialVaultDaySequenceActor` carries a cloud layer and scalability has no opinion about it. Ray-marched per pixel. |
   | `r.VolumetricFog 0` | Also in `ShadowQuality@0`, but listed so the page's own Volumetric Fog row can be greyed out honestly. |
   | `r.BloomQuality 1` | `PostProcessQuality@0` leaves bloom at **4**, the most expensive thing left in post. Not 0: the night windows, the fire and the effect cards are emissive and read as flat squares with no bloom at all. |

   Every switch captures its previous value on the way in and is restored on the way out with
   `IConsoleVariable::Unset(ECVF_SetByGameOverride)`, falling back to an explicit write when the
   engine build has no CVar history. `SimCopter.Settings.LowPowerMode` checks every name still
   resolves — a renamed CVar is a **silently dead optimization**, not a crash.

3. **Runtime responders**, reading `SimCopterLowPower::IsEnabled()` (a free-standing global, because
   the callers are per-spawn and per-frame paths with no world context handy):
   - `USimCopterFlashingLightsComponent` stops spawning point lights. A city puts hundreds on screen
     and the mode has just taken MegaLights away ([[simcopter-flashing-lights]]). No subscription
     needed — the cards rebuild on every phase step and the pool is released with them. The **cards
     stay**: they are the gameplay-visible part and they are free.
   - `ASimCopterGroundAgent` hides its two headlight spotlights. Only cars subscribe to
     `SimCopterLowPower::OnChanged()`, and only once `ConfigureVehicleHeadlights` has run, so the
     delegate list stays short in a city that is mostly pedestrians.

   **Nothing touches per-component cast-shadow flags, on purpose.** `r.ShadowQuality 0` from the Low
   profile clears the `DynamicShadows` show flag, so the city has no dynamic shadows in this mode at
   all — plumbing the flags would be dead code.

## Screen percentage goes through the user setting, not `r.ScreenPercentage`

75% (56% of the pixels) is the single largest saving available, and it is applied with
`SetResolutionScaleValueEx` so the Resolution Scale row still reads it back and the player can move
it. Order matters: `SetOverallScalabilityLevel` **also** writes `ResolutionQuality` (level 0 is 50),
so the explicit scale has to come second.

`LowPowerRestoreScalabilityLevel` / `LowPowerRestoreResolutionScale` are both the values to go back
to *and* the applied/not-applied latch — `INDEX_NONE` means "not applied", which is what stops a
second `ApplyGraphics` capturing Low as the level to restore. They are persisted because the mode
is: a session that starts with it already on still has to know what to go back to.

## Materials

A `LowPower` scalar joined `MPC_SimCopterDayNight` (published by
`USimCopterDayNightSubsystem::PublishLowPower`, from `Tick` rather than `Refresh` so it still
reaches a level with no day sequence). Authored in `Tools/Unreal/CreateSimCopterMaterials.py`:

- **`M_SimCopterTerrain`** skips its four octaves of value-noise normals — four hashed 4-corner
  lookups per pixel across the whole ground plane, the most expensive material in the city by area.
- **`M_SimCopterWater`** stops displacing and shades flat. The original's sea was a five-frame
  texture cycle on a flat plane, which is exactly what is left.

Three early-outs are **free in both modes**, and are exact rather than approximations:

- terrain noise and the water wave both exit at `Weight <= 0` (shoreline, building and road pads),
  where the result already collapses to `normalize(BaseNormal)`. The water normal's exit is gated on
  `BaseNormal.z > 0` because the `max(BaseNormal.z, 1e-3)` clamp is the only thing that would make
  the two disagree.
- the night window mask exits at `Blend <= 0`, i.e. **all day, on every wall in the city** — the last
  line multiplies everything by `saturate(Blend)` anyway.

**The window mask has no LowPower branch, deliberately.** Skipping the two per-pixel hashes means
lighting *every* window, and a uniformly lit skyline is the exact failure
[[simcopter-night-lighting]] records reverting once already. Two `sin`s are not worth it.

## Substrate is off project-wide, for BOTH modes

`r.Substrate=False` in `DefaultEngine.ini` (was True). Not part of the mode — it is read-only and
needs a full shader recompile, so a checkbox could never drive it — but it is the same argument:
Substrate's multi-closure G-buffer and tile classification buy layered and coated surfaces this
project has none of. Every material here is base colour, roughness, specular, emissive and a normal,
and nothing in `CreateSimCopterMaterials.py` uses a Substrate-only node.

**Measured, so nobody re-measures the wrong thing:** the materials' own instruction counts are
**identical** with it on and off (atlas 382/172, terrain 452/158, water 369/187 pixel/vertex). The
graph translates the same. The saving is in the G-buffer layout and the deferred shading passes
around it, which `MaterialEditingLibrary.get_statistics` does not count — so that API cannot
confirm this change, and a flat instruction count is not evidence it did nothing. Confirm it in the
log instead: `LogConfig: Set CVar [[r.Substrate:0]]`.

## Regenerating

`CreateSimCopterMaterials.py` **deletes and recreates** the atlas material, which nulls every
`MI_CityPage_*` parent, so the bake has to follow it:

```
Tools/Unreal/CreateSimCopterMaterials.py
Tools/Unreal/BakeCityAtlas.py
Docs/scratchpad/verify_low_power_materials.py   # collection scalar, node wiring, orphaned instances
```

## Verified

Built clean; 171 automation tests pass, including a new `SimCopter.Settings.LowPowerMode` covering
the table's uniqueness, every name resolving in this engine build, the value round trip, and the
DLSS stand-down. The materials were rebuilt, re-baked and checked in the editor (all six compile;
terrain 452 / atlas 382 / water 369 pixel instructions with the branches in).

Re-run after `r.Substrate=False`: every shader recompiled, all six materials still compile, 171
tests still pass.

**Not verified on screen.** Nobody has yet run a city with the box ticked, so the frame-rate gain is
argued from what each switch does rather than measured, and the "does it still read as a city with
no shadows and no GI" question is open. Substrate coming off is the other thing to look at, because
it changes **normal** mode too — it should be invisible on materials this simple, but it is a
shading-path swap and it has not been seen on screen either.
