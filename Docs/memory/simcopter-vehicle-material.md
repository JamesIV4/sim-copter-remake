# SimCopter shared vehicle material

*"One material asset serves the vehicles AND the city — only the vehicles go through the instance."*

*Recorded 2026-07-30.*

`/Game/Materials/M_SimCopterLitVertexColor` is loaded by **everything** that draws palette-coloured
original GEO geometry: the helicopter pawn, the ground agents' cars, the ambient
planes/trains/boats, the on-foot pawn, the dispatch marker, the hangar shell **and the city's
buildings and terrain**. Every one of them used to assign the base asset directly, so there was
nothing per-object to tune at runtime.

`USimCopterVehicleMaterialSubsystem` (a `UWorldSubsystem`) now owns a single
`UMaterialInstanceDynamic` of it. The three **vehicle** classes swap their material pointer for
that instance in `BeginPlay`, so one scalar moves the whole fleet and every existing
`SetMaterial` call site is untouched.

**The city deliberately stays on the base asset.** That is the trap: because the material is
shared, routing `ASimCity2000CityActor` through the same instance would make the helicopter's
metallic slider turn the entire skyline to chrome. If you ever want city-wide material tuning,
give the city its *own* instance rather than reusing this one.

## Scalar parameters

`Roughness`, `SelfIllum`, `Specular` were authored with the material. **`Metallic` was added
2026-07-30** (a ScalarParameter, default 0, wired to the Metallic input) specifically so the
helicopter debug panel could drive it — the C++ was written first and did nothing until the
parameter existed, because `SetScalarParameterValue` on a name a material does not have is
**silently ignored, not an error**. If a slider in the debug panel appears dead, check the
material for the parameter name before debugging the C++.

**`EmissiveNits` was added 2026-08-12, DEFAULT 0**, for the dispatch pylons — the AICON / PICON /
FICON beacons a responding unit hangs over its destination, which now carry a low emissive so they
stay legible in shadow and after dark. Two things make it safe on a material this widely shared:

- **The default is zero**, unlike `M_SimCopterSpriteTexture`'s sunlit one. Anything that does not
  write the parameter — the whole untextured city, every vehicle, the hangar shell — renders exactly
  as before.
- **It is ADDED to the existing emissive, not connected over it.** That pin already carries
  `BaseColor * SelfIllum`, the city's readability floor for shadowed faces, and the first attempt at
  this re-pointed the pin at a parameter defaulting to 0 — which silently takes that floor away from
  every untextured building and every car at once. The graph is
  `SelfIllum * BaseColor + VertexColor * EmissiveNits` now. When adding to an emissive pin in this
  project, read what is on it first.

`USimCopterDispatchMarkerComponent` writes it through **its own** MID (not the subsystem's fleet
instance, which belongs to the vehicles), at `SimCopter.Dispatch.MarkerEmissive` — default 0.12 —
times the current effect-card emissive, so it tracks the sun like everything else rather than being
a constant that is invisible by day or a lamp by night.

The upgrade lives in `upgrade_lit_vertex_color_emissive()` in `CreateSimCopterMaterials.py` and is
an in-place edit, not a delete-and-recreate: half the project holds this asset as a parent.

**It did not light the pylons on the first attempt, and there are only two ways that can happen**, so
the component now tells you which: `ApplyMarkerEmissive` reads the parameter straight back with
`GetScalarParameterValue` and warns ONCE, loudly, when the parent does not carry it — the silent-drop
trap above, found by the code instead of by eye. `SimCopter.Dispatch.MarkerLog 1` then prints the
written nits, the effect brightness behind them and the pylon's visibility every frame, which
separates "the write is going nowhere" from "0.4 nits at noon is invisible". The fraction also moved
from 0.12 to **0.35**: a beacon has to be comparable to the ground around it, and under a 120,000-lux
sun anything much below a quarter of white-ground luminance cannot read as lit by construction.

## Debug panel knobs added at the same time

* **METALLIC** (0..1) — the subsystem's scalar. 0 is the faithful look; the original had no PBR.
* **BLINK LIGHT x** (0..5) — a *multiplier* on `PointLightIntensity` for the face-type-25 blink
  markers, driving the helicopter's component and the city's together. It is a multiplier and not
  an absolute value because the two are tuned to different bases (6 vs 20), and one number would
  flatten that. See [[simcopter-flashing-lights]].
