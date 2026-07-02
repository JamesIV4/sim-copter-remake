# Add Flashing Lights From Original Mesh Data

## Background

The original SimCopter `.MAX` mesh files encode light positions directly in the face data via two fields: `LightType` and `FaceType`. A probe of all 400 mesh objects across the three GEO packs reveals the following combinations that indicate lights:

| LightType | FaceType | Count | Objects | Meaning |
|-----------|----------|-------|---------|---------|
| 1 | 26 | 77 faces | 12 objects | **Point light markers** - single-vertex faces at lamp/beacon positions |
| 1 | 25 | 347 faces | 37 objects | **Flashing colored faces** - solid poly faces that blink on/off |
| 4 | 15 | 77 faces | 5 objects | Bridge/rail structural faces (not flashing - always visible) |
| 4 | 19 | 22 faces | 2 objects | Bridge structural faces (not flashing - always visible) |

### FaceType 26, LightType 1 — Point Light Beacons

These are **single-vertex faces** (1 vertex each) used as positional markers for lights on:
- Industrial buildings: `IN160`, `IN162`, `IN163`, `IN164`, `IN165`, `IN192`
- Power plants: `PP202`, `PP207`  
- Fire station points: `FIREPTS` (22 point lights)
- Lamp posts: `LP213`, `LP213L` (3 lights each, at different heights)
- Smoke effect: `SMOKE` (1 marker at origin)

These faces are currently **silently dropped** because `AppendMaxisMeshObject` skips faces with `< 3` vertices (line 2377 of `SimCity2000CityActor.cpp`).

### FaceType 25, LightType 1 — Flashing Solid Faces

These are **polygon faces** (3+ vertices) colored via the palette that flash on/off. Found on:
- **Helicopters**: `JETRANG`, `HUGH500`, `BELL212`, `SCWZR300`, `APACHE`, `AGUSTA`, `DAUPHIN`, `MDEXPLRR`, `MD520` — navigation/anti-collision lights (red/green/white, typically 4-5 per heli)
- **Buildings**: `PO210` (police station, 36 lights), `FS211` (fire station, 12), `HO209` (hospital, 11), `RE174`/`RE175`/`RE177` (residential), `CO154`/`CO155`/`CO157`/`CO183`/`CO184` (commercial), `PP203` (power plant)
- **Airport/Arenas**: `AP221`/`AP221F`/`AP222`/`AP225`/`AP226`, `AR252`/`AR253`/`AR254`/`AR255`
- **Traffic signals**: `SIGNAL1` (6 faces — red/yellow/green for 2 directions)
- **Special**: `UFO` (138 flashing faces), `PLANE1` (3 nav lights), `CARFIRET`/`CARPOLIC` (emergency vehicle roof lights), `TRAIN2`
- **Bridges**: `BR83`/`BR83F`, `BR86`/`BR86F`

## Open Questions

> [!IMPORTANT]
> **Material index colors for FaceType 25**: The palette indices used for flashing faces are:
> - `249` = red (nav light)
> - `250` = green (nav light)  
> - `251` = white/yellow (beacon)
> - `246` = white (tail light)
> - `252` = blue (police/UFO)
>
> Should these be rendered as **emissive point lights** at the face center (with the face geometry hidden), or as **emissive faces that toggle visibility**? The original game simply toggled face rendering on a timer. I'll go with toggling emissive face visibility to match the original, plus an optional point light for FaceType 26 markers.

> [!NOTE]
> **LightType 4 faces** (bridges/rails with FaceType 15/19) appear to be accent/structural faces rendered with a specific shading mode, not flashing lights. The probe shows them on `BR86`, `BR86F`, `RD75`, `RD76`, `TRAIN2` - these are geometry faces, not light markers. I'll leave them as normal rendered faces.

## Proposed Changes

### Formats — Light Data Extraction

#### [MODIFY] [MaxisMeshReader.h](file:///S:/Repos/sim-copter-remake/SimCopterRemake/Source/SimCopterRemake/Public/Formats/MaxisMeshReader.h)

Add a helper struct `FMaxisMeshLightPoint` to hold extracted light position + color info from FaceType 26 faces. Add a helper to check if a face is a light marker (`IsLightMarkerFace`).

#### [MODIFY] [MaxisProceduralMeshBuilder.h](file:///S:/Repos/sim-copter-remake/SimCopterRemake/Source/SimCopterRemake/Public/Formats/MaxisProceduralMeshBuilder.h)

- Add `static bool IsFlashingFace(const FMaxisMeshFace&)` — returns true for LightType==1 faces
- Add `static bool IsLightPointFace(const FMaxisMeshFace&)` — returns true for FaceType==26, LightType==1

#### [MODIFY] [MaxisProceduralMeshBuilder.cpp](file:///S:/Repos/sim-copter-remake/SimCopterRemake/Source/SimCopterRemake/Private/Formats/MaxisProceduralMeshBuilder.cpp)

Implement the two new classifiers. In `BuildPaletteColoredSections`, route flashing faces (FaceType 25, LightType 1) to a **new third output section** (`OutFlashingSection`) so helicopter/vehicle code can toggle them separately.

---

### City Actor — Building Flashing Lights

#### [MODIFY] [SimCity2000CityActor.h](file:///S:/Repos/sim-copter-remake/SimCopterRemake/Source/SimCopterRemake/Public/City/SimCity2000CityActor.h)

- Add `UPROPERTY` toggle: `bEnableBuildingFlashingLights` (default true)
- Add `UPROPERTY` for flash rate: `BuildingFlashIntervalSeconds` (default 1.0)
- Add array of spawned light components or a child actor for managing them
- Enable tick for flashing

#### [MODIFY] [SimCity2000CityActor.cpp](file:///S:/Repos/sim-copter-remake/SimCopterRemake/Source/SimCopterRemake/Private/City/SimCity2000CityActor.cpp)

1. In `AppendMaxisMeshObject`: **Extract FaceType 26 single-vertex light positions** instead of silently dropping them. Collect the vertex world position + palette color into a returned light data array.
2. In `AppendMaxisMeshObject`: **Route FaceType 25 + LightType 1 faces** into a separate flashing mesh section (key = special sentinel value). These are rendered normally but toggled on/off.
3. After building all city mesh sections in `RebuildCity`: spawn `UPointLightComponent`s at the extracted FaceType 26 positions (for buildings that have them). Use the palette color from `MaterialIndex`.
4. Build a separate `UStaticMeshComponent` (or a section in the existing mesh) for FaceType 25 flashing faces, toggled by a tick timer.
5. Add a `Tick` function that alternates the flashing section visibility at `BuildingFlashIntervalSeconds`.

---

### Helicopter Pawn — Navigation Lights

#### [MODIFY] [SimCopterHelicopterPawn.h](file:///S:/Repos/sim-copter-remake/SimCopterRemake/Source/SimCopterRemake/Public/Flight/SimCopterHelicopterPawn.h)

- Add `UPROPERTY` toggle: `bEnableNavigationLights` (default true)
- Add flash timer state for helicopter nav lights

#### [MODIFY] [SimCopterHelicopterPawn.cpp](file:///S:/Repos/sim-copter-remake/SimCopterRemake/Source/SimCopterRemake/Private/Flight/SimCopterHelicopterPawn.cpp)

When building helicopter meshes via `BuildPaletteColoredSections`:
1. Use the new three-section variant to extract flashing faces separately
2. Create a second `UProceduralMeshComponent` for flashing nav light geometry
3. Toggle its visibility in `Tick` at approximately 1Hz (matching original game feel)
4. Optionally spawn small point lights at the flashing face centers for modern visual effect

---

### Ground Agents — Emergency Vehicle Lights  

#### [MODIFY] [SimCopterGroundAgent.h](file:///S:/Repos/sim-copter-remake/SimCopterRemake/Source/SimCopterRemake/Public/Ground/SimCopterGroundAgent.h)

- Add optional `UProceduralMeshComponent` for flashing faces (CARFIRET, CARPOLIC roof lights)

#### [MODIFY] [SimCopterGroundAgent.cpp](file:///S:/Repos/sim-copter-remake/SimCopterRemake/Source/SimCopterRemake/Private/Ground/SimCopterGroundAgent.cpp)

When building vehicle meshes, extract FaceType 25 + LightType 1 faces into a separate flashing mesh component. Toggle in the existing tick.

---

## Verification Plan

### Automated Tests
- Run existing `MaxisMeshReaderTests` and `MaxisProceduralMeshBuilderTests` to ensure no regressions
- `cd SimCopterRemake && UnrealEditor-Cmd.exe SimCopterRemake.uproject -ExecCmds="Automation RunAll SimCopter" -NullRHI`

### Manual Verification
- Load a city in PIE and visually confirm:
  - Point lights appear at industrial building / power plant / fire station locations
  - Flashing faces toggle on buildings (police station PO210 should have the most obvious lights)
  - Helicopter nav lights flash red/green/white when flying
  - Emergency vehicle roof lights flash
