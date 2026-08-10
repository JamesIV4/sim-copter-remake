# SimCopter night lighting DECODED

How the original made the city look like night, and how the remake ports it. Decoded and ported
2026-08-04.

## `DAT_004f9720 == 1` is NIGHT

This closes the "which value is night" follow-up that sat open in
[MissionsAndTweakSystem.md](../MissionsAndTweakSystem.md). The flag is the `career.twk` Day/Night
column, copied into `DAT_004f9720` when a city is entered, and toggled by the **0x37 debug key** in
`FUN_004796c0`. Three independent proofs that 1 means night:

1. `FUN_0049a8b0` hands the renderer a **dimmer** ambient/diffuse for it -
   `FUN_00470c40(0x1999, 0x3333, 0x1999, 0x4ccc)` against the day's
   `(0x1999, 0x6666, 0x4ccc, 0xcccc)`, 16.16 - and then re-lights every loaded object.
2. `FUN_0047a240` calls `FUN_004a03a0(1)` + `FUN_004834f0` for it, which **show** the face-type-11
   light cards. See below.
3. `FUN_004606d0` loads `skydark.bmp` instead of `sky.bmp`.

## Face type 11 is the light cards, and it is NOT the windows

`FUN_004a03a0(bIsNight)` walks the model table `DAT_0057f768 .. DAT_0057f880`, and `FUN_004834f0` /
`FUN_00483700` walk GEO objects `0x181..0x184`. Both clear (night) or set (day) bit `0x80000000` -
the hide flag - on every face whose type is `0xb`.

Scanning the shipped GEO (`Docs/scratchpad/scan_face_type_11.py`) shows exactly what those faces
are: **550 of them across the three `SIM3D*.MAX` files, and not one is on a building.** They are

- `LAMP35`..`LAMP38` (ids 0x181-0x184) - the street lamp glows, which is why those four objects get
  their own hard-coded pass;
- the `AUTO*` / `CARPOLIC` / `CARAMBUL` / `CARFIRET` "Headlights" quads;
- rotor discs, helicopter drop shadows, the UFO, `LASER`, `SPOTLITE`, `ARCO`.

So type 11 is "translucent card", and at night the original simply stops hiding the light ones. The
remake already renders headlights as real spotlights
([population rendering](simcopter-population-rendering.md)) and the rotor disc through
`M_SimCopterRotorDisc`, so nothing here needed a new mechanism.

**The lit windows are somewhere else entirely.** Going looking for them in the geometry is the wrong
tree; they are texture.

## The windows are a five-page atlas swap out of `SKYDARK.BMP`

`FUN_004606d0` (reached from `FUN_00460610`, and again from `FUN_00460690` whenever the flag flips)
loads `sky.bmp` or `skydark.bmp` - chosen off renderer `+0x4f`, which `FUN_00460690` sets from
`DAT_004f9720` - and then:

| composite image | size | destination |
| --- | --- | --- |
| 0 | 640x200 | blitted straight into the frame buffer as the sky backdrop |
| 1 | 256x256 | **copied over live atlas page 2** |
| 2 | 256x256 | copied over live atlas page 39 (0x27) |
| 3 | 256x256 | copied over live atlas page 40 (0x28) |
| 4 | 256x256 | copied over live atlas page 20 (0x14) |
| 5 | 256x256 | copied over live atlas page 13 (0x0d) |

Both files are ordinary Maxis composite bitmaps, readable by `FMaxisTextureReader` /
`decode_composite`, despite the `.BMP` extension.

Two consequences worth holding on to:

- **Those five pages are sky.bmp's, not SIM3D's, in *both* lighting states.** The copy is
  unconditional; only the source file changes. The remake used to take four of them from SIM3D and
  only page 20 from SKY (page 20 had to be an exception anyway - SIM3D image 20 is a degenerate
  1x256 strip that was never the page the renderer used). The SIM3D and SKY day pages turn out to
  resolve to the same picture through the palette, so this was invisible; it is now sourced
  correctly regardless.

  **These five are the *only* atlas pages there are.** `SIM3D.BMP` holds 68 images and exactly four
  of them are 256x256 - 2, 13, 39 and 40, precisely the four sky.bmp overwrites - with everything
  else a smaller direct image (face types 2 and 13). So the bake produces five `MI_CityPage_*`, not
  dozens, and every textured building surface in the city is covered by the night swap.
- **`SKYDARK`'s pages 1, 2 and 3 - atlas pages 2, 39 and 40 - are where the lit windows are
  painted.** Pages 20 and 13 are terrain/water and are only darkened: measured across the whole
  page, they contain *no* texel above 0.45 luminance, whereas the three wall pages put 7-16% of
  their area above 0.55. That measurement is what sets the remake's glow threshold, and it is
  reproducible with `Docs/scratchpad/analyse_night_windows.py`.

## The port

Nothing about the original's approach survives verbatim, because the remake's sun moves
continuously where the original had a boolean fixed for the whole city.

- `USimCopterDayNightSubsystem` (`City/SimCopterDayNight.h`) is the single answer to "is it night".
  It resolves the level's `ADaySequenceActor`, computes a night alpha with
  `SimCopterDayNightFog::ComputeNightAlpha` (so the fog, the pacing and the windows share one curve
  and one pair of sunrise/sunset anchors), and publishes it as `NightBlend` on the material
  parameter collection **`/Game/Materials/MPC_SimCopterDayNight`**. A collection, not per-instance
  parameters: `MI_CityPage_*` are `MaterialInstanceConstant`s and cannot be animated at runtime at
  all.
- `BakeCityAtlas.py` bakes `T_CityNightPage_<id>` alongside each page and writes a `NightTexture` on
  every `MI_CityPage_*`. The code still falls back to "night texture = day texture" for a page with
  no night variant, so adding an atlas page later cannot leave an unset texture parameter reading
  the parent's grey checker after dark. It also imports the painted window masks and writes
  `WindowTexture` + `HasWindowMask` - see below.
- `M_SimCopterCityAtlas` samples both pages through the same cell UV, blends Base Color, and adds an
  emissive term for the windows.

### Base Color only leans *part* of the way onto the night art

`NightAlbedoStrength`, default **0.25**, scales `NightBlend` for the Base Color lerp only. The
original had no lighting model, so skydark's pages *are* the finished night image - walls already
darkened. Here they are albedo under a physically scaled sun that has already set, so lerping all
the way onto them darkens the city twice and the buildings go to mud. Albedo is a property of the
paint, not of the hour; what should change after dark is the light, and the light already does. A
quarter is enough to pick up skydark's cooler, greyer tint, which reads as night. Setting it to 1.0
reproduces the original's exact night pixels at that cost.

The emissive window term is **not** scaled by it - the windows come on fully.

### Not every window is lit

A skyline with every window on reads as a render. The material rolls each window instead:
`WindowLitFraction` (0.30) per window, plus `WindowRowLitFraction` (0.05) that lights a whole floor
at once, both hashed off a `WindowSeed` the subsystem **re-rolls at every sunset** - and after any
seek, so switching to Static midnight from the Settings screen draws a fresh set rather than the one
from the last time it was night.

The identity being rolled is `floor(InCellUV * WindowRandomGrid)` plus the building's own
`ObjectPositionWS` — on the painted path `WindowRandomGrid` is not used at all and the unit is one
whole tile, `floor(InCellUV)`; the rest of this section is the derived fallback's version of it. `InCellUV`'s **integer** part is already "which repeat of the atlas cell am I on
across this wall", so it is a stable per-bay index for free; the object position keeps two identical
towers from lighting identically (the city places each model as its own runtime static mesh
instance, so this really is per-building). The grid does not have to line up exactly with the art -
the glow mask has already restricted the effect to window texels, so a bucket spanning two windows
just lights both, which reads as one flat. **Too fine is the failure that looks wrong**: buckets
smaller than a window leave a single pane half-lit.

### Distant windows flickered: it was Lumen, and the fix is `#ifdef LUMEN_CARD_CAPTURE`

**Cause (confirmed on screen 2026-08-05):** Lumen. Its surface cache captures Emissive *through this
same material*, and the window glow is the worst thing to hand it - a handful of very bright texels
per building, re-captured at whatever card resolution the distance happens to allow. As cards change
resolution or get evicted, the indirect contribution pulses, and that is the flicker.

**The fix is one preprocessor guard at the top of the window mask:**

```hlsl
#ifdef LUMEN_CARD_CAPTURE
	return 0.0;
#endif
```

`LumenCardPixelShader.usf` does `#define LUMEN_CARD_CAPTURE 1` on line 3, *before* it includes
`/Engine/Generated/Material.ush` on line 18 - so the define reaches a Custom node's body. The base
pass has it undefined and draws the windows exactly as before. The windows look identical and simply
stop lighting anything indirectly.

**Things that are NOT the lever, checked so nobody re-checks them:**

- `bEmissiveLightSource` on the primitive already defaults to **false**, so "turning it off" is a
  no-op. Turning it *on* makes this worse: it multiplies Lumen's minimum card surface area by 0.2
  (`LumenMeshCards::GetCardMinSurfaceArea`), so more small emissive detail gets cards.
- There is **no per-material switch** to exclude emissive from Lumen. The only per-primitive one is
  `bAffectDynamicIndirectLighting = false`, which drops the primitive out of the Lumen scene
  entirely - no cards at all, so rays pass through the buildings and light leaks. Far too blunt.

**A distance fade on the mask is also NOT the answer. Tried and reverted the same day.** It measured
the texel-to-pixel ratio with `fwidth(InCellUV) * 32` and blended the mask toward its statistical
average past 1 texel per pixel. It did stop the flicker, and it looked far worse: the average
applies to *every texel of the building*, walls included, so a distant tower emitted uniformly
instead of showing windows and the whole skyline came up bright. The minification story it rested on
was wrong anyway - it predicts flicker only once windows go sub-pixel, and the flicker was visible
while they were still comfortably readable.

If something like this ever needs diagnosing again, `SimCopter.NightWindows.LitFraction 1` with
`.RowLitFraction 0` makes `lit` a constant and takes the per-pixel hash out of the shader, which
separates "the roll is unstable" from "something downstream is".

#### Re-checked 2026-08-06: the guard is intact, and it was never the whole story

Asked again because Lumen looked like it was back on the windows. The guard is **not** the
regression - it is present verbatim in the committed `M_SimCopterCityAtlas.uasset` (grep the asset,
the HLSL is stored as plain text) and nothing has touched it since. Two things worth recording so
this is not re-derived a third time:

- **UE 5.8 has TWO card-capture entry points**, and the guard reaches both. `LumenCardPixelShader.usf`
  is the raster path; `LumenCardComputeShader.usf` is the **Nanite** path, and the city is Nanite
  (`used_with_nanite`), so that is the one that actually matters here. Both `#define
  LUMEN_CARD_CAPTURE 1` on line 3, before `#include "/Engine/Generated/Material.ush"` on line 18.
- **The guard only covers the surface cache, and Lumen gathers light two ways.** The other is
  **screen traces**, which sample the frame that was just drawn - so they see the windows exactly as
  the base pass drew them, and no material-side guard can reach that path at all. That is the half
  that was never closed.

The one knob screen tracing has is `r.Lumen.ScreenTracingSource`, and the engine's own text for it
names this exact failure: 0 is "Scene Color (**noise from small emissive elements**)", 1 is
"Anti-aliased Scene Color ... **less noise from small emissive elements**". A skyline of 25-nit
windows over ground at a fraction of a nit is precisely "small emissive elements". The project was on
0 (the engine default); **it is 1 now**, in `DefaultEngine.ini`.

Read the limit honestly: `GLumenScreenTracingSource` is read in `LumenReflectionTracing.cpp` and
nowhere else, so this is **reflections only** - the diffuse screen probe gather still traces the
lit frame. If the windows still push light around after this, the remaining lever is
`r.Lumen.ScreenProbeGather.ScreenTraces 0`, and it is a much blunter instrument: it takes out every
screen-space contact bounce in the city, not just the windows.

Things checked and ruled out, so nobody re-checks them: hardware-RT hit lighting (the project pins
`r.Lumen.HardwareRayTracing.LightingMode=0`, surface cache, so materials are never evaluated in a
hit shader); the `SelfIllum` floor (0.08 of base colour is ~0.06 nits at a window against the glow's
25, and it is there by day too); and `bEmissiveLightSource`, which is still false and still a no-op.

### Brightness

`WindowGlowNits` started at 2500 and that was four orders of magnitude over the moonlit ground - the
skyline bloomed into one continuous halo. It is **25** now, and it lives in the collection rather
than the material so it can move at runtime: `SimCopter.NightWindows.Nits`, alongside
`.LitFraction` and `.RowLitFraction`.

### The window mask is PAINTED now; deriving it is the fallback

Which texels are windows is not recorded anywhere in the data, so there are two ways to know it and
the material carries both. `HasWindowMask` (0 on the parent, 1 on an instance the bake found a file
for) picks between them.

**Painted, and this is what ships.** `SimCopterRemake/Content/NightWindows/windows_page_<page>.png`,
drawn in `Tools/WindowLayoutEditor.html`, imported by `BakeCityAtlas.py` as `T_CityWindowPage_<page>`
and bound to the `WindowTexture` parameter. All three wall pages (2, 39, 40) are done; 13 and 20 are
terrain and have no windows to paint. Channels:

| channel | meaning |
| --- | --- |
| R | 255 where the texel is a lit window. Binary, so `step(0.5, r)` is the mask. |
| G | a per-window byte, from connected-component labelling of the painted blobs |
| B | a per-row byte, shared by every window on the same row **of the same 32x32 cell** |

So a "window" is whatever shape was drawn and a "row" is one floor of one cell - no grid, no spacing
assumption, and edges that are right rather than nearly right.

Four things about it that are easy to get wrong:

- **The file name is the ATLAS page, not the composite index.** They overlap enough to be genuinely
  ambiguous - SKY composite images 1, 2, 3 are atlas pages 2, 39, 40, so `windows_page_1.png` and
  `windows_page_2.png` could each mean either. The first painted file was named for the composite
  index; it took correlating the mask against all three night pages to find out which page it was
  (mean night luminance under the mask 0.76 on page 2 against 0.29 and 0.24 on the other two - the
  answer is never close, so `Docs/scratchpad/agent-sessions/2026-08-06-night-windows/`
  `verify_window_masks.py` settles it in one run). The painter now offers a fixed *list* of atlas
  pages instead of a number box, so this cannot recur.
- **Imported with `srgb=False`.** G and B are IDs, not colour. A transfer curve on the way in stops
  `Authored.gb * 255.0` recovering the bytes the painter wrote.
- **The sampler parameter stays on the DEFAULT (Color) type anyway**, which looks contradictory and
  is not. `ProcessMaterialColorTextureLookup` and `ProcessMaterialLinearColorTextureLookup` both
  `return TextureValue` - the type changes no shader code at all, only which default texture the
  *parent* is validated against, and the parent's default is the engine's sRGB grey checker.
  Declaring Linear Color there breaks the whole material for no gain.
- **The unit of the lit/unlit roll is one TILE, not one painted blob.** `windowCell =
  floor(InCellUV)` - which repeat of the atlas cell we are on across this wall - so every window a
  tile paints comes on together. The mask's G and B are deliberately *not* in the hash key. Rolling
  each blob separately was tried first and reads as speckle at city distance, where a whole tile
  reads as a room with the light on. The bytes stay in the file, so a finer unit is a change to that
  one line and nothing else.

Cost, measured: **389 pixel instructions against 382 before**, vertex unchanged at 172, samplers
5 -> 6. The extra sample is paid on the terrain pages too, which have no mask; that is the price of
one material for all five pages.

**The derived mask below is not dead code** - it is what an unpainted page falls back to, and it is
what made the feature possible before there was a painter. The night art darkens the whole page
*except* the windows:

```hlsl
float gotBrighter = saturate((nightLum - dayLum) * Contrast);
float isBright    = saturate((nightLum - Threshold) * Contrast);
return min(gotBrighter, isBright) * saturate(Blend);
```

**Both tests are needed.** "Brighter at night" alone catches nothing on the two pages that were only
darkened - correct - but would also catch any texel nudged up a palette step. "Bright in absolute
terms" alone lights pale daylit stone. The pair isolates the windows on the wall pages and leaves
the terrain pages completely dark, which is what the measurements above predict.

`WindowGlowNits` is **absolute (2500), not scaled to the sun** the way the unlit effect cards are
([exposure scale](simcopter-exposure-scale.md)). A window has a bulb behind it; its brightness does
not follow the sky. At the day sequence's 120,000 lux noon white ground is ~38,000 nits, so 2500 sits
far below daylight and only reads once the sun is down - which is also what `NightBlend` is doing, so
the two agree without being tied together.

## The trap: one shared unlit material, six consumers, one of them setting the parameter

`M_SimCopterSpriteTexture` is UNLIT - colour goes straight to Emissive - and it bakes a daylight
`EmissiveNits` default of **26000** so a card is never black with nothing driving it
([exposure scale](simcopter-exposure-scale.md)). That default is a trap, because a consumer that
creates a `UMaterialInstanceDynamic` and forgets the parameter renders at 26000 nits **forever, at
every hour**. Six sites create MIDs from it and originally only one wrote the parameter:

| consumer | what it draws | wrote EmissiveNits? |
| --- | --- | --- |
| `USimCopterParticleFXComponent` (kernel + card) | smoke, embers, spray | yes |
| `USimCopterFireRenderComponent` | **the flames themselves** | no |
| `ASimCopterGroundAgent::SpriteMaterialInstance` | pedestrian sprites | no |
| `ASimCopterGroundAgent::FigureHeadMaterialInstance` | privanim heads | no |
| `ASimCopterOnFootPawn::FigureHeadMaterialInstance` | the player's head | no |
| `USimCopterFlashingLightsComponent` | blink markers | yes |

### The heads left this material entirely (2026-08-06)

Reported as "at night people's heads are totally black - and we should not need to set a brightness
for heads, that's silly", which is the right conclusion. A surface with a zero floor is only ever a
*computed* imitation of shading, and the imitation fails at both ends: too bright before the floor
existed, black once it did.

Both `FigureHeadMaterialInstance` rows above are now on **`M_SimCopterLitSpriteTexture`**, the same
masked Default Lit card material [[simcopter-sprite-card-lighting]] gave the trees, with
`CardNormalUpBias` set to **0** — that material's default of 1.0 flattens the normal to world up for
crossed vertical quads, and `AppendBall` gives a head real normals. It shares `SelfIllum` /
`Roughness` / `Specular` with `M_SimCopterLitVertexColor`, which is the *body*'s material, so the
head and the body under it now shade identically at every hour and nothing writes a brightness at
all. `RefreshSpriteExposure` is left with only the legacy PEOPLE1 billboard.

Checked with `Docs/scratchpad/verify_figure_head_material.py` (Default Lit, Masked, and both
parameters present — a `SetScalarParameterValue` for a parameter the material lacks is silently
ignored, so this is worth one headless run).

So the fire burned at a fixed 26000 while the smoke and embers *rising out of the same fire* tracked
the sun, and every person in the city glowed all night. All of it is one bug with one shape.

`USimCopterEffectExposureSubsystem::ApplyEmissiveNits(Mid, World, bIsLightSource)` is now the single
way to write it, and the flag matters:

- **Light source** (fire, kernels, markers): floored at `DefaultMinimumEmissiveNits` (1.5) so a flame
  is still the brightest thing in frame at midnight.
- **Surface** (people, heads): floor of **zero**. They are ordinary surfaces that happen to be drawn
  as unlit cards, and the floor is exactly what would make them glow after dark.

Anything new that draws on this material has to call it. The default is deliberately not lowered:
a forgotten consumer being too bright is at least visible, where a dark one looks like missing art.

## One knob over all of it

`USimCopterSettings::EmissiveBrightness` (Settings > Graphics > Emissive Brightness, 5%..300%)
multiplies **everything** derived above - effect cards, people, window lights - because none of them
carry an authored brightness, so scaling them together preserves their relationship.
`SimCopter.Effects.Brightness` still multiplies on top for live tuning.

## Hangar shell

`FUN_0043b6e0` writes `*(uint *)(ui + 0x112) = (DAT_004f9720 == 0)`, so **that field is "is it
DAY"**, the inverse of the global - easy to get backwards. `FUN_0043c540` picks `dhangar.bmp` or
`nhangar.bmp` from it. The remake now does the same with `NHANGER-upscaled.png` /
`DHANGAR-upscaled.png` (and `NHANGAR.BMP` / `DHANGAR.BMP` as the fallbacks), read **once when the
shell opens** rather than tracked - the original's flag was a per-city constant and could not change
while the player was inside.

## Settings

The Graphics page (`SSimCopterGraphicsSettings`) carries the player-facing half:

- **Time of Day**: Dynamic or Static, plus a Static Time slider. Static is
  `SetRunDayCycle(false)` -> `SetTimeOfDay(h)` -> `Pause()`, **in that order**: `SetTimeOfDay` scrubs
  with `EUpdatePositionMethod::Play` and therefore *resumes* the sequence, so pausing first and
  seeking second leaves the clock running. Dynamic is `SetRunDayCycle(true)` -> `Play()`, also in
  that order, because `Play()` refuses outright while `bRunDayCycle` is false.
- **Daytime / Nighttime Length** in real minutes, which are the player-facing face of
  `USimCopterDayNightLengthComponent`'s `DayRealMinutes` / `NightRealMinutes`. The subsystem pushes
  them at the level's component; the component keeps the ramp maths.
- **Lumen** (Hardware / Software / Off), **Volumetric Fog**, and **NVIDIA Reflex**. Lumen's first two
  are the same GI method (`r.DynamicGlobalIlluminationMethod 1`) differing only in
  `r.Lumen.HardwareRayTracing`; Off also drops reflections to screen-space rather than to nothing.

`USimCopterSettings::SeedResolutionFromDisplay` seeds the very first run's resolution from the native
size of the monitor the game opened on, because `UGameUserSettings` otherwise defaults a fresh ini to
1280x720. Native resolution, **not** the display rect - a DPI-scaled 4K panel reports a 2560x1440
rect and seeding from that opens the game at the scaled size.

## The painted masks are geometry, so they also make glass (2026-08-07)

The masks were painted to decide which texels LIGHT UP at night — but which texels are windows is a
fact about the building, not about the hour, so the same data now drives **reflection**, day and
night. Windows take `CityWindowRoughness` / `CityWindowSpecular` (0.10 / 0.85) while the masonry
around them stays on the matte City pair. Glass was the one surface in the city with no way to tell
itself apart from the wall it is set into.

**It needed a SECOND mask node, not a reuse of the glow one.** `OriginalNightWindowMask` answers
"is this window lit right now": it early-outs to zero when `NightBlend <= 0` and multiplies by the
per-window occupancy roll, because only ~30% of windows have anyone home. Reflection wants neither
gate — every pane is glass whether or not there is a light behind it — so `OriginalWindowGlassMask`
is the same painted/derived pair with the time gate and the roll removed. Reusing the glow mask
would have given reflective windows only at night, and only the lit ones.

Only `M_SimCopterCityAtlas` carries the mask, so only it takes the glass pair; the other two City
materials stay matte. Painted pages are 2, 39 and 40 — the three wall pages, which is what
`BakeCityAtlas.py` reports as `paintedWindowPages` — so the derived fallback is effectively dead in
retail.

## A lit window is tungsten, not daylight (2026-08-06)

The glow is `nightTexel * mask * WindowGlowNits` — literally the night art's own pixel, which is why
a window keeps the colour it was painted. The catch nobody had looked at: **several of `skydark`'s
lit texels are painted at or near white.** A 256-entry VGA palette shared with the entire city has no
headroom to spend on warm bulbs, so a good part of the skyline came out reading as fluorescent
panels rather than lamps — and it was not blown out, it was just white at source.

`WindowGlowTint`, a vector parameter on `M_SimCopterCityAtlas`, now multiplies that product.
Default `(1.25, 0.97, 0.56)`, which is the chroma `(1.0, 0.78, 0.45)` divided by its own Rec.709
luminance of 0.803 — so it is **normalised to luminance 1 and changes hue, not exposure**. A warm
texel gets warmer, a white one stops being white, and the per-window variation in the art survives
because the tint is a multiply rather than a replace. Re-normalise if you re-tune it, or
`WindowGlowNits` stops meaning what its own comment says.

Verify with `Docs/scratchpad/verify_window_glow_tint.py` — it prints the parameter and its
luminance, and the luminance is the half that is easy to get wrong.

## Regenerating

Editor Python, in this order (the atlas material is deleted and recreated each run, which nulls the
`MI_CityPage_*` parents until the bake re-sets them):

```
Tools/Unreal/CreateSimCopterMaterials.py    # MPC_SimCopterDayNight, then M_SimCopterCityAtlas
Tools/Unreal/BakeCityAtlas.py               # day + night + painted window pages, params on every instance
Tools/Unreal/ImportSlateArt.py              # the committed Content/Slate PNGs -> .uasset
```

Headless is the fast way to check one of these landed, and it is how the painted masks were verified:

```powershell
& "C:\GameDev\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" <uproject> `
    -unattended -nop4 -nosplash -stdout -FullStdOutLogOutput `
    -ExecutePythonScript="<repo>\Tools\Unreal\BakeCityAtlas.py"
```

**Leave `-NullRHI` off when the point is to know a material compiled.** With it the run succeeds and
`MaterialEditingLibrary.get_statistics` returns 0/0/0, which is indistinguishable from a broken
graph. Without it the same call returned 393/172/6 and that is the proof.
`Docs/scratchpad/agent-sessions/2026-08-06-night-windows/verify_window_wiring.py` is the check.
