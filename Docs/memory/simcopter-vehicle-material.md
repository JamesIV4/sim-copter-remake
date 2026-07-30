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

## Debug panel knobs added at the same time

* **METALLIC** (0..1) — the subsystem's scalar. 0 is the faithful look; the original had no PBR.
* **BLINK LIGHT x** (0..5) — a *multiplier* on `PointLightIntensity` for the face-type-25 blink
  markers, driving the helicopter's component and the city's together. It is a multiplier and not
  an absolute value because the two are tuned to different bases (6 vs 20), and one number would
  flatten that. See [[simcopter-flashing-lights]].
