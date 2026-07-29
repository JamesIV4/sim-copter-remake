# SimCopter fire water fx

*"How SimCopter renders fire, water dump, and rotor-wash effects; how the remake reproduces them."*

*Ported into the repo 2026-07-29.*

Decoded 2026-07-05 (evidence: `Docs/scratchpad/ghidra/out_fire_water_effects.txt`,
`out_effect_pool_init.txt`, `out_effect_sprite_creator.txt`, `out_m5_firecreate.txt`,
`out_heli_landing.txt`). The mission/fire **simulation** was already ported
(`FSimCopterMissionSystem`); this work added the **rendering + heli wiring** (fire had no visuals,
`DouseAt` was declared-but-unimplemented, bucket dump wasn't wired). See [[simcopter-mission-system]]
and [[simcopter-vertex-animation-wpo]].

## Original rendering facts (do not re-derive)
- **Fire = GEO meshes.** Flame = **FIREPTS** (`Object.Header.Id 0x120`); car fire = **CARFIRET**
  (0x11c); smoke/debris = **DEBRIS1/2/3** (0x149/14a/14b, cycled by table `DAT_00504560`, count 3)
  + **SMOKE** billboard (0x148); burnt building tiles → **BURNTREE** (0x14e) / **GRUBBLE1..4**
  (0x14f..0x152) by footprint size. IDs resolve via `FMaxisMeshLibrary::FindObjectByObjectId`.
- **CRITICAL: FIREPTS is NOT a solid mesh.** Its 22 "faces" are **1-vertex point sprites** ("fire
  points") — a cloud of palette-coloured dots. `BuildPaletteColoredSection` triangulates ≥3-vertex
  faces, so it builds **zero triangles** for FIREPTS (the bug that first shipped). Render each point
  as a camera-facing card coloured by `ColorMap[face.MaterialIndex]`. SMOKE = 1 point; BURNTREE = 1
  two-vertex (line) face.
- **Effect particles (spray/dust/splash/drips) are flat palette-coloured, camera-facing cards**
  (Maxis face type `0x17`) built procedurally by `FUN_0046edb0(shape,0x17)` (shape 1=rotor-wash
  "trajectory", 3=splash "bullet", 4=fire), coloured by a **palette index** (small consts 0/3/5),
  moved by velocity + rise rate over a short life. **No sprite atlas / texture** — the frame tables
  (`DAT_00581c40`, `DAT_005d91e0`) are runtime-filled, static image all-zero. Creator
  `FUN_0048e0b0` (type 6=bucket drip, 4/10=smoke/debris, 9=splash sub-particles); tile-splat
  `FUN_004af220` (class 8 = wash, rise 0xf0000, life 0x20000); big splash column `FUN_004af100` →
  updater `FUN_004af3b0` bursts it into a ring of type-9 sub-particles ("smaller versions").
  Master-tick effect updater = `FUN_0048ed00`.
- **Wind kickback over water = rotor wash `FUN_004881b0`**: heli within `0x140000` (~20 units) of
  the surface top AND altitude field `heli+0x158 > 0x1180000`, scatter class-8 wash cards with a
  **random-yaw** matrix + `×0x20` offset; spray over water, dust over land. Count scales with
  proximity. Bucket drip = `FUN_00488060` → `FUN_0048e0b0(6, size 0xf0000)`.

## Remake implementation
- `USimCopterFireRenderComponent` (on `ASimCopterMissionSystemActor`): loads FIREPTS once, extracts
  the 22 points+colours, rebuilds ONE procedural mesh of billboarded point-sprites every tick for
  all flames (building + burning cars), with per-point flicker. Driven by **polling**
  `MissionSystem.GetFlames()` — the deterministic sim core is untouched.
- `USimCopterParticleFXComponent` (on the heli): CPU-billboarded cards for bucket drips, douse
  steam, and the rotor-wash. `UpdateRotorWash` ports `FUN_004881b0` (surface trace + water flag +
  random-yaw scatter). Bucket dump wiring is in `UpdateRopeAndBucket`.
- **`DouseAt`/`DouseAtTile`** (were unimplemented): flames are tile-keyed (no world pos in the
  simplified sim), so douse by Chebyshev-≤1 tile, subtracting a frame-EMA-scaled chunk from
  `BurnCountdown`, removing with `EVT_FlameDoused`. Car fires: `bMissionOnFire` on the vehicle
  traffic state; `Traffic::GetBurningVehicles` / `DouseBurningVehiclesNear`; mission actor posts
  `EVT_CarDoused`/`EVT_CarCleared`.
## FULL DECODE 2026-07-05 (supersedes the guessed look below)
Complete decompile in `Docs/scratchpad/ghidra/out_effects_DECODED.md` (+ raw `out_fx_*.txt`,
`out_effect_pool_init.txt` = master updater `FUN_0048ed00`). The user rejected our dithered-quad
version as inauthentic and asked for a guess-free decompile of the real spawn/render funcs.
Authoritative facts:
- Every effect particle is EITHER a small **GEO billboard** (SMOKE `0x148`/`0xae`, DEBRIS
  `0x149-14b`) OR a short **1/3/4-point "trajectory" card** (`FUN_0046edb0(N,0x17)`), colored by a
  **SIM3D palette INDEX** and moved with gravity(−0x280000/frame·dt)+velocity over a short life.
  NO texture atlas, NO alpha — transparency was the SW rasterizer's job; the effect code only sets
  a palette index + a 16.16 world size. "Many small particles" = many small sprites accumulating.
- Colors are `GetSharedColorMap()` (SIM3D.BMP palette, == each .MAX CMAP). Exact RGB extracted:
  FIRE ramp = indices **0x10..0x1F** (`#8F1005` dark-red → `#E59A0A` amber) + tips 0x73`#FFF01F`/
  0x7B`#FFDA6F`; smoke/glow = 0x08`#C0DFC0`/0x09`#A5CAF0`. (Face-type-`0x1a` drip/sub-spray use
  small indices 0/3/5 through a rasterizer sub-ramp — only color path not pinned to RGB.)
- ROTOR WASH (`FUN_004881b0`, 1×/frame from `FUN_00484d20`): only when `<0x140000`(~20u) over
  ground AND altitude `heli+0x158>0x1180000`; spawns ONE class-8 SMOKE puff (`FUN_004af220`,
  life 0xF0000) at a random-yaw ×0x20 offset. Downwash dust disc = `FUN_00489250` (frame table
  `DAT_005d91e0`=pal 8,9,A,B). Splash column `FUN_004af100`→`FUN_004af3b0` bursts a type-9
  sub-particle ring ("smaller versions of the water effect"); frame table `DAT_00581c40` = pal
  {0xF,0x10,0x11,0x12,0x19,0x1A,0x1B,0x1C,0x1D}. Bucket drip `FUN_00488060`→`FUN_0048e0b0(6)`.
- REBUILT to the decode (2026-07-05): new `Public/Ground/SimCopterEffectFX.h` = authentic
  constants + EXACT palette colors (FireRamp 0x10-0x1F, tips 0x73/0x7B, spray 0x08/0x09,
  OriginalUnitToCm=6.25, GravityCmPerSec2). `USimCopterParticleFXComponent` now integrates gravity
  (`SpawnParticle(pos,vel,size,color,life,gravity)`; `Rise` removed; `SpawnCard`→`SpawnParticle`).
  Fire component colors FIREPTS points via `FireRamp(height)`+yellow tips (dropped invented
  red/orange/yellow+grey). Heli wash = pale SprayWhite/SprayBlue puffs w/ gravity (RotorWashCardsPerSec
  110); bucket drips = spray palette. Mission actor gained `FireSmokeComponent` + `SpawnFirePlume()`
  = rising dark-grey smoke + fire embers above each flame. Pool/param/lifetime tables in DECODED.md.
- DITHERING REVERTED (2026-07-05, user: "look terrible, don't match original; use the smoothed
  effect with no dithering"): ALL effect components now default to the SMOOTH
  `M_SimCopterParticleFXSoft` (BLEND_TRANSLUCENT, unlit, soft radial alpha, emissive=vtxColor*1.4,
  opacity=radialMask*vtxColor.A) — the Bayer-dither M_SimCopterParticleFX is no longer referenced
  in code (asset still exists). Coloration per user: rotor wash LAND = tan/dark brown dust
  (`DustBrown`/`DustDarkBrown`), WATER = blue+white (`SprayBlue`/`SprayWhite`); bucket drips =
  `WaterBlue`; fire = FireRamp+tips; smoke = dark sooty brown-grey. Color helpers in
  SimCopterEffectFX.h. Build clean.
- MATERIAL BUG FOUND+FIXED (2026-07-05): particles rendered "transparent black"/colorless because
  `CreateSimCopterMaterials.py` connected the VertexColor node's **"RGB"** output to the emissive
  multiply — "RGB" is NOT a valid pin name on MaterialExpressionVertexColor, so it silently no-oped
  (emissive=0×1.4=black) while the valid "A" pin still drove opacity (=> transparent black). Fix:
  use the node's DEFAULT unnamed output `connect_material_expressions(vertex_color, "", boost, "A")`
  (same as the working M_SimCopterLitVertexColor). Affected BOTH M_SimCopterParticleFX and ...Soft.
  GOTCHA: `connect_material_expressions` fails silently on a bad output-pin name — VertexColor
  outputs are "" (RGBA), "R","G","B","A" (no "RGB"). Regenerate materials after editing the .py.

## (SUPERSEDED guess) Material `M_SimCopterParticleFX` (in `Tools/Unreal/CreateSimCopterMaterials.py`): unlit +
  **MASKED** + two-sided, emissive = vertexColor.RGB×1.4, opacity mask = 4x4 Bayer dither. Was our
  best guess before the full decode; the real effects use small palette-indexed GEO/point sprites,
  not dithered quads. `M_SimCopterParticleFXSoft` = translucent radial-soft variant.
- (SUPERSEDED) Particle style guess: dozens of 8-20cm specks/frame, height-ramped fire colors.
- Debug: `SimForceFire` / `SimForceCarFire` console commands on the heli pawn.
- Tests: `SimCopter.Missions.FireDouse` (ignite → douse → all flames out, credited doused).
