# SimCopter Low Power Graphics — one checkbox, three layers, and what each one is worth

*"The expensive part of this project is not the content. It is Unreal 5.8's defaults."*

*Recorded 2026-08-05.*

The Settings screen's Graphics page ([[simcopter-settings-menu]]) grew a **Low Power Graphics**
checkbox. `USimCopterSettings::bLowPowerMode` stores it; `SimCopterLowPowerMode.h/.cpp`
(`Public/Game`, `Private/Game`) is the mode itself.

The city is a few hundred thousand triangles of flat-shaded 8-bit palette art and it was being drawn
through Lumen, virtual shadow maps, MegaLights, a volumetric cloud layer and volumetric fog. None of
that renders anything a SimCopter player would miss, and all of it is priced for a discrete GPU.

*(TSR was on that list too, until 2026-08-06 — see "Anti-aliasing stayed" below.)*

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

## Anti-aliasing stayed: the mode no longer forces FXAA (2026-08-06)

The table used to carry `r.AntiAliasingMethod 1`, and `ApplyGraphics`' low power early-out sat
*above* the line that writes the player's chosen method — so the Anti-Aliasing row still moved, still
read back TSR, and did nothing. **Both halves are fixed**: the switch is gone from the table, and the
`if (!bDlssEnabled) SetRenderCVar(TEXT("r.AntiAliasingMethod"), ...)` write moved above the
`if (bLowPowerMode) { ... return; }` block, so the row is honest in both modes.

Why TSR is the *right* thing to keep in a mode built for weak GPUs, which is the part that reads
backwards: **the mode renders at 75%, and TSR is the only method in the dropdown that upscales.**
FXAA at 75% is a spatial stretch of a blurry image — the mode's worst-looking property, paid for
with a cheap AA pass. And TSR is not at full price here either: the mode's scalability half applies
`AntiAliasingQuality@0`, which (checked in `C:\GameDev\UE_5.8\Engine\Config\BaseScalability.ini`)
sets `r.TSR.History.ScreenPercentage=100` rather than 200, `History.R11G11B10=1`,
`History.UpdateQuality=0`, and turns off `ShadingRejection.Flickering`, `ReprojectionField`,
`RejectionAntiAliasingQuality` and `Resurrection`. That section notably does **not** touch
`r.PostProcessAAQuality`, so Low never disabled AA by itself — the forced FXAA was the whole story.

A player who wants the milliseconds still has **None** in the dropdown. That is the honest control,
rather than the mode deciding for them.

Two things went with it, because the AA switch was their only user:
- `FRenderSwitch::bSkipWhileUpscaling` and the `bExternalUpscalerActive` parameter on
  `SimCopterLowPower::Apply`. The DLSS stand-down is not lost — it lives in `ApplyGraphics`'
  `if (!bDlssEnabled)` guard, which was always there and is now the only copy.
- The stand-down half of `SimCopter.Settings.LowPowerMode`. In its place the test asserts
  `r.AntiAliasingMethod` is **not** in the table, because putting it back would pin it at
  `ECVF_SetByGameOverride` and silently kill the dropdown all over again — the failure mode where
  the row still moves and still reads back the stored value.

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

## The forward renderer: TRIED, BUILT, AND REVERTED (2026-08-05). Do not re-attempt blind.

Forward shading looks like the obvious next lever for a low-end machine. It was implemented in full
— a `SimCopterRemakeBoot` module at `PostConfigInit`, the preference persisted, a restart notice on
the page — and then **reverted, because the game crashes with it on**. Everything below is what that
cost to find out, so the next attempt starts from here instead of from scratch.

**It is not a runtime setting.** `r.ForwardShading` is `ConfigRestartRequired` and is consumed when
the shader platform is built, long before any `UGameInstanceSubsystem` exists. Applying it needs code
at `ELoadingPhase::PostConfigInit` — after config, before the RHI.

**Read-only is not the obstacle it looks like.** `FConsoleVariableBase::CanChange` tests **priority
only**; `ECVF_ReadOnly` is enforced solely on the console-command path. A plain `Set` at
`ECVF_SetByGameOverride` from a PostConfigInit module works and outranks DefaultEngine.ini's
`ECVF_SetByProjectSetting`, which the engine then logs as correctly ignored.

**The user's `Engine.ini` is NOT viable storage for any restart-required setting** — this one
generalises well beyond forward shading. A `[/Script/Engine.RendererSettings]` *or* `[SystemSettings]`
block in `Saved/Config/<Platform>/Engine.ini` is read early enough and *does* apply, but the editor
rewrites that file from its in-memory hierarchy on shutdown and **deletes it**. Measured twice, with
both section names. The failure is vicious: the setting works for exactly one session, and the run
after it silently reverts while looking like it worked — which is how a "not deterministic" reading of
the crash below got recorded, wrongly, from a run that was no longer forward at all. Use
`GGameUserSettingsIni` (nothing prunes it; the flight model's Easy Mode flag lives there).

**The editor asserts under forward, every time, with no project content loaded.** A bare
start-and-quit is enough:

```
Shader attempted to bind uniform buffer 'FOpaqueBasePassUniformParameters' at slot
[Name: SceneTextures, Slot: 13] ... but the shader expected 'TranslucentBasePass'
Breadcrumbs: ParallelDraw -> BasePass -> Scene
```

That is upstream — all six project materials compile clean under forward. A `-game` session reached
the main menu and rendered for minutes with zero asserts, which is what made it look shippable. **It
is not: the game crashes once a city is actually loaded.** Whatever the real incompatibility is, it
lives past the main menu and was never isolated.

**And the payoff was doubtful anyway.** Forward roughly TRIPLES every material, because lighting
moves into the base pass:

| material | deferred | forward |
| --- | --- | --- |
| city atlas | 382 | 1087 |
| terrain | 452 | 1157 |
| water | 369 | 1073 |
| lit vertex colour | 340 | 1045 |
| particle FX | 99 | 315 |

Forward wins where deferred's overhead hurts: many lights per pixel, a fat G-buffer, MSAA wanted.
Low Power has already removed all of that — no Lumen, no MegaLights, no shadows, and the beacons and
headlights switched off — so its deferred lighting pass is one directional light and a skylight over
an already-slim (Substrate-free) G-buffer. Forward would trade that near-free pass for a 3x base pass
over a city with real overdraw. **The frame was never measured**, on either renderer, so if this is
picked up again the first step is a profile on the target hardware, not more plumbing.

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

Re-run after `r.Substrate=False`: every shader recompiled, all six materials still compile, 173
tests still pass.

Re-run after the anti-aliasing change above (2026-08-06): built clean, **185 tests pass, 0 fail**
(`Docs/scratchpad/tests-aa-lowpower.txt`). Still not seen on screen — the "does TSR at 75% actually
look better than FXAA at 75%" question is argued from what the two passes do, not measured.

The forward-shading experiment above was reverted in full; the tree carries none of it. What it did
prove is that the *deferred* configuration described in this note builds, tests and runs — that is
the state to compare any future renderer experiment against.

**Not verified on screen.** Nobody has yet run a city with the box ticked, so the frame-rate gain is
argued from what each switch does rather than measured, and the "does it still read as a city with
no shadows and no GI" question is open. Substrate coming off is the other thing to look at, because
it changes **normal** mode too — it should be invisible on materials this simple, but it is a
shading-path swap and it has not been seen on screen either.
