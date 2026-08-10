# SimCopter roadside decorations, street lights and smoking chimneys

*"The builder hangs a SECOND object off nine of its road cases, and the props carry their own
in-tile offset."*

*Recorded 2026-08-06.* Closes the "Still unported" section of
[[simcopter-road-tile-variants]] and the "not yet wired up: SIGNAL1" note in
[[simcopter-flashing-lights]].

Ported as `SimCopterRoadDecorations` (`Public/City/SimCopterRoadDecorations.h`),
`USimCopterStreetLightsComponent`, `USimCopterSmokeStacksComponent`. Tests:
`SimCopter.City.RoadDecorationDispatch`, `SimCopter.City.RoadDecorationMeshes`,
`SimCopter.City.SmokeStackMarkers`.

## What the objects actually are

`Docs/scratchpad/list_maxis_objects.py <lo> <hi>` lists any id range out of the three `.MAX` packs.
The block the builder's decoration arm draws from:

| id | name | what |
| --- | --- | --- |
| 0x181..0x184 | `LAMP35`..`LAMP38` | **street lights**, one per T-junction orientation |
| 0x185 | `SIGNAL1` | traffic signal, red/yellow/green x2 |
| 0x186/0x187 | `TRASH29`/`TRASH30` | litter bin |
| 0x188/0x189 | `PHONE29`/`PHONE30` | phone box |
| 0x18a/0x18b | `FIREH29`/`FIREH30` | fire hydrant |
| 0x18c/0x18d | `MAIL29`/`MAIL30` | post box |

The `*29` set belongs to XBLD `0x1d` and the `*30` set to `0x1e` — **the same four props authored
against the two road orientations**, not eight different props.

## The placement rule — `FUN_0047c0c0`, its `local_2c == 2` arm

The builder hangs the decoration off the scene cell as a **second object behind the road slab**.
Every case sits *inside* the four-corner flatness test, so a sloped road carries nothing.

```
0x1d  straight NS, flat, (x&1)&&(y&1)   -> rand() & 0xf : 0-2 FIREH29 / 3-5 PHONE29
                                                          6-8 MAIL29 / else TRASH29
0x1e  straight EW, same gate            -> the *30 variants
0x23..0x26  T junction, flat, (x&3)==3 && (y&3)==3  -> LAMP35..LAMP38, one per id
0x27..0x2a  crossroads, flat, (x&1)&&(y&1)         -> SIGNAL1
0x2b        crossroads, flat, UNCONDITIONAL        -> SIGNAL1
```

**A litter bin is 7 rolls in 16** — it is the `default:` arm, the other three get 3 each. Ghidra
renders the roll as `(((ushort)r ^ s) - s & 0xf ^ s) - s` with `s = (short)r >> 15`; MSVC's `rand()`
tops out at `0x7fff` so the sign word is always zero and it collapses to a plain `rand() & 0xf`.

**THE THING THAT SAVES YOU A DAY: the props carry their own in-tile offset.** A road tile spans
-32..+32 original units and `TRASH29`'s vertices sit at X 27.9..31.2 / Z 21.4..24.3 — already parked
against the far curb. So a decoration is placed by appending it *at the tile origin* and nothing
else. It rides the city actor's existing `SecondaryObjectId` path unchanged, which also gets its
blink markers extracted for free. `Docs/scratchpad/dump_maxis_bounds.py` is what shows this.

The `local_4` word stored beside the decoration (4 for street furniture, `0x204` for lamps and
signals — the same slot the primary's `0x20`/`0x40`/`0x280` goes in) is read by the render walk, not
the placer. **Unexamined**, and the port does not need it.

**DIVERGENCE:** the original rolls the global `rand()` while building, so a reload shuffles the
street furniture. `MakeStreetFurnitureRoll` hashes the tile and the city name instead, so a city
looks like itself every time you fly into it.

## Two traps the first pass fell into (fixed 2026-08-06, same day)

**1. Road tiles have `bBuildVectorLines = false`, and the decoration inherits it.** That flag exists
so the road SLAB's authored centre line does not fight the procedural marking system - but a lamp's
three face-type-20 lines at the head and a signal's one at the top of its mast are structure, not
road paint, and they went with it. The visible symptom is a signal head floating with no arm
joining it to the pole. The secondary object now gets its own flag
(`bSecondaryVectorLines = bBuildVectorLines || isDecoration`), and `Append3DVectorLine` renders them
as solid tubes in the face's own palette colour, which is what the rest of the city does with a
two-point face.

**2. The lamp's painted cone is what was hiding the spot light.** `LAMP35..38`'s eighteen
face-type-11 quads and its 14-vertex ground pool render as an opaque grey cone hanging off the head
and a grey egg on the road - and the cone **encloses the spot light's apex**, so the light was there
and invisible. `FPlacedObjectRoadFaceFilter::bSkipLightConeFaces` drops face type 11 for street-light
secondaries only; every other user of that face type (car headlight beams, rotor discs) is wanted
and untouched.

## Street lights: the cone is geometry, and it is the measurement

`LAMP35`..`38` draw their light as **eighteen face-type-11 quads in three stacked bands under the
head, plus a 14-vertex pool on the pavement** (the post tops out at 55.7 units; the cards run
39.8..52.6). Face type 11 is the light-CARD type the night pass already knows — car headlights,
rotor discs, these posts. Not type 25 (blink) and not type 26 (effect).

That painted cone is a complete description of the light the remake wants, so
`TryGetStreetLightEmitter` reads apex, throw and spread straight off those vertices: apex = the
top band's XY centroid at its Z (**the arm hangs out over the road, so it is not the pole**),
length = top to pool, half angle = `atan(poolRadius / length)`. Only the intensity and colour are
the remake's, and they follow the car headlights' conventions — unitless 9000,
`SetInverseExposureBlend(1)`, no shadows, off in Low Power, faded in on the day/night `NightAlpha`.

## Traffic signals get cards but NOT point lights

Deliberate, and do not "fix" it. `FUN_00496c00` round-robins the **whole world by colour** on one
8-step counter at 20 Hz, so every signal in view changes together several times a second. As drawn
cards that is fine. As hundreds of coloured point lights strobing the streets in unison it is
genuinely unpleasant and a seizure risk. `FSimCopterFlashingLightPoint::bCastPointLight` is the
opt-out and the city clears it for `SIGNAL1`'s six markers only.

## Street lights are the one local light Low Power keeps

`ASimCopterGroundAgent`'s headlights and `USimCopterFlashingLightsComponent`'s beacons both go dark
under Low Power Graphics ([[simcopter-low-power-mode]]) because of VOLUME - dozens of moving cars
with two spotlights each, hundreds of blink markers, all landing on the standard deferred light loop
once MegaLights is off. A city's street lights are a static handful by comparison, and they are the
only thing lighting the roads at night, so `bDisableInLowPower` defaults **false**.

Worth knowing when a report says "no car headlights at all": check `bLowPowerMode` in
`Saved/Config/WindowsEditor/GameUserSettings.ini` before hunting for a regression. That is working
as designed and it takes the beacons with it.

## Smoking chimneys are face type 26, and they are not particles

Eight shipped models carry face-type-26 markers, all **effect class 1**, all running in a vertical
trail from ~3/4 of the building's height to its very top — **they ARE the plume, authored as static
positions**:

`IN160` x6, `IN162` x8, `IN163` x6, `IN164` x4, `IN165` x6, `IN192` x8, `PP202` x4, `PP207` x6.
(`Docs/scratchpad/scan_face_type_26.py` regenerates this list.)

The original draws each with `FUN_00496da0` — the same stochastic screen-pixel kernel that draws
`FIREPTS`'s 22 fire points — and class 1 resolves through the selector table at `0x00504830` to the
**"light smoke" greys** `#959595 #A5A5A5 #B5B5B5 #C0C0C0`. So the port is
`FSimCopterEffectRasterizer` again, pointed at a different marker set:
`USimCopterSmokeStacksComponent` is `USimCopterFireRenderComponent`'s draw loop against the city's
static chimneys instead of a cloned `FIREPTS` template.

They were missing because **`AppendMaxisMeshObject` drops them** — a single vertex is neither a
polygon nor one of its two-point lines, exactly the reason type 25 was dropped.

The point light is one per chimney (the topmost marker is flagged `bPointLightAnchor`), coloured by
**averaging the effect class's own eight selector entries** rather than by an invented colour, and
faded on `NightAlpha`. The cards go through `ApplyEmissiveNits(..., bIsLightSource=false)` —
smoke is a **surface** and must go dark with the sun, or `M_SimCopterSpriteTexture`'s baked
26000-nit default has every chimney glowing at midnight ([[simcopter-night-lighting]]).

## Still not placed

`TLNS`/`TLEW` (0x18e/0x18f) are hanging traffic-light heads with face-type-11 cards, and `LP213` /
`LP213L` (0x144/0x146, effect class 3) are a second lamp-post model. **No case in `FUN_0047c0c0`
places any of them** — they are reachable only through the heuristic XBLD table, if at all.

Related: [[simcopter-road-tile-variants]], [[simcopter-flashing-lights]], [[simcopter-fire-water-fx]],
[[simcopter-night-lighting]], [[simcopter-exposure-scale]], [[simcopter-instanced-buildings]].
