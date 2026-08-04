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
  the parent's grey checker after dark.
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

### Deriving the window mask instead of authoring it

Which texels are windows is not recorded anywhere in the data. It is derivable, because the night
art darkens the whole page *except* the windows:

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

## Regenerating

Editor Python, in this order (the atlas material is deleted and recreated each run, which nulls the
`MI_CityPage_*` parents until the bake re-sets them):

```
Tools/Unreal/CreateSimCopterMaterials.py    # MPC_SimCopterDayNight, then M_SimCopterCityAtlas
Tools/Unreal/BakeCityAtlas.py               # day + night pages, NightTexture on every instance
Tools/Unreal/ImportSlateArt.py              # the committed Content/Slate PNGs -> .uasset
```
