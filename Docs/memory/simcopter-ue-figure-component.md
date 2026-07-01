---
name: simcopter-ue-figure-component
description: "Phase 3 done: UE5 renders original privanim figures for pedestrians (reader + builder + agent integration); Z byte is screen-space y-down (negate for UE); unity builds disabled"
metadata: 
  node_type: memory
  type: project
  originSessionId: ac7bbbd4-fa8e-4787-8737-b11dd7d4b850
---

Phase 3 (original pedestrian look) shipped 2026-07-01:
- `Source/.../Formats/SimCopterPrivAnimReader.{h,cpp}` - pure C++ privanim.df parser (mirrors
  Tools/privanim_extract.py). Automation test `SimCopter.Formats.PrivAnim.Reference` PASSES
  (21 figures, pilot 75 parts/18 clips, exact segment bytes, 2woman NoMo=412! 3x51).
- `Source/.../Ground/SimCopterPopulationFigure.{h,cpp}` - mesh builder: per-frame section pairs
  (body strokes vertex-colored + head card textured from SIM3D.BMP), shared cache
  `FSimCopterPopulationFigure::GetShared(root)` (model + GEO CMAP palette + head images).
- `ASimCopterGroundAgent::BuildPedestrianFigure()` replaces box people (box = fallback);
  clip switch 1Wal/NoMo by speed, frame stepping via section visibility, `FigureFrameRate=8`.
  Pool: fatman 2blonde Child 5.5man SUIT 5man SHADES Blonde 2woman Woman; per-agent stable
  clothes offset (mod 14) + head image (11-entry SIM3D.BMP table).

**Gotchas:**
- **ARPP Z byte is screen-space y-DOWN**: negate for UE +Z-up and set feet offset from MaxZ
  (figures spawned upside down before the fix; user confirmed look otherwise great).
- **Unity builds are DISABLED** in SimCopterRemake.Build.cs (`bUseUnity=false`): several format
  readers had same-named anonymous-namespace helpers that collided whenever a new .cpp shifted
  unity chunks. Keep it off (or rename helpers) when adding files.
- Engine at `C:\GameDev\UE_5.8` (registry HKLM\SOFTWARE\EpicGames\Unreal Engine\5.8), NOT
  Program Files. Build: Engine\Build\BatchFiles\Build.bat SimCopterRemakeEditor Win64 Development.
- Headless test run: UnrealEditor-Cmd.exe <uproject> -ExecCmds="Automation RunTests
  SimCopter.Formats.PrivAnim; Quit" -unattended -nullrhi -stdout.
- On-foot player pawn still uses the box body (follow-up: same figure path).
- The old wrong glTF/display rules are gone; Tools/privanim_to_gltf.py is rewritten too.

See [[simcopter-privanim-decoded]] for the format, [[simcopter-people-logic-next]] for Phase 4.
