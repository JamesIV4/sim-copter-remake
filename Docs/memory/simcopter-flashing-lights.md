# SimCopter flashing lights

*"Face type 25 is a blink marker, and the whole city strobes off one global counter."*

*Recorded 2026-07-30.*

Ported as `FSimCopterFlashingLightSchedule` + `USimCopterFlashingLightsComponent`
(`Public/Ground/SimCopterFlashingLights.h`), wired into the helicopter pawn and the city actor.

## The data

A blinking light is a **Maxis face type 25 (0x19)** record: always **exactly one vertex**, and its
**material byte is a VGA palette index**, not a material. Across all three GEO packs there are 347
of them and the palette bytes are only ever these five:

| index | colour | used for |
| --- | --- | --- |
| 246 `0xf6` | `#FFFAF0` white | landing/strobe |
| 249 `0xf9` | `#FF0000` red | nav + beacons |
| 250 `0xfa` | `#00FF00` green | starboard nav |
| 251 `0xfb` | `#FFFF00` yellow | hazard |
| 252 `0xfc` | `#0000FF` blue | police |

Every flyable airframe (`JETRANG`, `HUGH500`, `APACHE`, `BELL212`, `SCWZR300`, `AGUSTA`,
`DAUPHIN`, `MD520`, `MDEXPLRR`) carries the same four: 1 white, 2 red, 1 green. Buildings carry
them too (`CO183`, `HO209`, `AR254/255`, `IN160..165`, `PP202/203`, `FS211`, airport `AP221..226`),
`CARPOLIC` has 4 red + 4 blue, `SIGNAL1` has red/yellow/green, and `UFO` has **138**.

**Do not confuse face type 25 with face type 26 (0x1a).** Type 26 is the neighbouring
*effect*-marker type — `FIREPTS`, `SMOKE`, `LP213` lamp posts — whose material byte is an effect
class (1/2/3), not a palette index. See [[simcopter-fire-water-fx]].

## The blink rule — `FUN_00496c00`

The entire body of the rasteriser is:

```c
phase = DAT_005039c8 & 7;
switch (light->colour) {
  case 0xf6: if (phase != 0) return; break;
  case 0xf9: if (phase != 1) return; break;
  case 0xfa: if (phase != 2) return; break;
  case 0xfb: if (phase != 3) return; break;
  case 0xfc: if (phase != 4) return; break;
  default:   if (phase != 5) return; break;
}
// plot a 4x4 block of that palette byte at (x >> 12, y >> 12)
```

So the lights **do not blink independently** — the whole world round-robins **by colour** through
an 8-step cycle, one colour lit per step, and **phases 6 and 7 light nothing at all** (the dark gap
between cycles). That the switch names exactly the five palette bytes the shipped type-25 faces
use, and no others, is what pins this function to face type 25: Ghidra reports it as having **zero
callers**, because the only call site (`0x004eac87`, inside `FUN_004eab6f`) sits in a region its
function boundaries get wrong. Brute-force scanning `.text` for `E8` rel32 targets found it —
`Docs/scratchpad/find_callers.py`.

`DAT_005039c8` is incremented **once per rendered frame** by `FUN_0047a760` (the main tick, from
`FUN_00449850`), so the original's blink rate rode the frame rate outright. The port pins it to
`PhaseSeconds = 0.05` — the same nominal tick the rest of the delta-time conversion uses; see
[[simcopter-heli-flight-model]]. At a modern frame rate the literal per-frame counter would strobe
about six times too fast.

The 4x4 block is the shipped `DAT_004f9750 == 0x10` branch (`FUN_00479bb0` stores 0x10); the
alternate mode writes 2x2.

## Two deliberate divergences

Both are visual-quality calls, not porting slips. Written down so nobody "fixes" them back.

**1. The card has a fixed WORLD size; the original's was a fixed SCREEN size.** `FUN_00496c00`
stamps four pixels at the projected point regardless of depth, which in world terms grows without
bound — invisible on a 560x400 software renderer, ugly here, where a beacon across the city ends
up metres across and blooms over the building it is bolted to. The port sizes the card at what the
4-pixel block covered at `LightSizeReferenceDepthCm` (600 cm) through the original's own
projection, and lets perspective shrink it from there.

There is a **hard floor of one physical output pixel** on the screen footprint, calculated from
the live viewport width and camera horizontal FOV, so a marker never shrinks out of sight however
far away it is. It is deliberately not one original 560x400 pixel, which can cover several modern
output pixels. Do not add a distance cull to the cards.

**2. Each lit marker also casts a real `UPointLightComponent` of its own palette colour.** The
original had no dynamic lighting whatsoever.

**Every lit marker gets one — there is no cap and no distance cull.** `MaxPointLights` defaults to
**0, meaning uncapped**, because *this project renders with MegaLights*, which solves local lights
by stochastic sampling at a roughly fixed cost. The usual "a renderer cannot take hundreds of
dynamic lights" instinct does not apply here, so do not re-introduce a budget: an earlier pass
capped the pool at 4/32 and culled past 120 m, and that was wrong. A positive `MaxPointLights`
still works (nearest markers win) but exists only for a configuration without MegaLights.

`bAllowMegaLights` is set explicitly on every created light rather than left to the engine default,
since the no-cap policy rests on it. Shadows are off by default (`bPointLightsCastShadows`) —
they buy little on a pinprick marker, but MegaLights makes turning them on affordable if a beacon
is seen leaking through the building it sits on.

## Ports

`ExtractLightPoints` pulls the markers out of any `FMaxisMeshObject` using the caller's own
units/scale, with a `bApplyCityMeshOrientation` flag because the city builder folds in the global
180-degree yaw and the helicopter/vehicle mesh path does not ([[simcopter-mesh-orientation-rules]]).

Not yet wired up, though the data is there: `CARPOLIC`, the `UFO`, `PLANE1`, `TRAIN2`, and
`SIGNAL1` (which the road decoration pass would place — see [[simcopter-road-tile-variants]]).

Separately, face types 15 and 19 carry `lightType == 4` on 99 faces (`BR86`, `TRAIN2`, `RD75`,
`RD76`). Those are big polygons, not point markers, and are unexamined.
