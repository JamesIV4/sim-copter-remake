# Standalone figure editor

Double-click `OpenFigureEditor.bat` at the repo root (Python 3.10+). Unreal does not
need to run. The launcher creates `Tools/.figure-editor-venv` and installs the pinned
PyVista, PyVistaQt and PySide6 requirements on first use (internet required for setup).
Alternatively run `Tools/.figure-editor-venv/Scripts/python.exe Tools/FigureEditor.py`.
Original data is read from
`Reference/SimCopterOriginalGame`; use `--original <directory>` to choose another copy.

- Choose any of the 21 figures, including `pilot`, and an animation. Play/pause or
  scrub frames to check joints through a whole movement.
- LOD 1 is the full-detail model. LOD 2/4 expose the original distant-view parts;
  overrides use the same part names across all detail levels.
- Click a visible part, or select its numbered original node in the parts list.
  The selected part gets a gold outline. Isolate helps reach overlapping pieces.
- Adjust **Position offset**, **Scale**, or **Size** independently on X/Y/Z.
  Press Enter or leave the field to apply typed values; spin arrows apply immediately.
  Scale/size use the piece's X depth, Y width, Z length axes, following the piece
  through each animation pose. Position offsets use person X forward, Y width, Z up.
- Drag to orbit in any direction, scroll to zoom, middle-drag or Shift-drag to pan. Front, Side,
  Back, Top and Fit whole figure are shortcuts. Projection is orthographic.
- PyVista's native SSAO is enabled by default, with a toggle and AO radius slider.
  `QtInteractor.enable_ssao` uses a 64-sample kernel and blur. VTK renders the actual
  meshes with depth buffering, native picking and camera interaction; there is no
  custom AO bake or Tk painter renderer. AO updates with every rendered frame. These
  preview controls do not change saved geometry or game lighting.
- **In-game head textures** displays the original `SIM3D.BMP` head panoramas with
  the game's UVs, palette transparency and nearest-neighbour sampling. Each figure
  starts with its usual head; the selector previews all 11 variants, including the
  injured head. Texture selection is preview-only. Body pieces use their in-game
  palette colours. Missing texture data is reported in the controls with a solid
  head fallback, so geometry editing remains available.
- Undo/redo works across figures. Reset selected part restores its original geometry.
- Uncheck **Render selected part** to hide a piece in every pose, in the preview and
  game. Hidden pieces stay in the parts list marked `[hidden]`; select one there
  and check the box to restore it. Visibility supports undo/redo and Save. Reset
  selected part also restores visibility. Older files default to visible, and
  hiding a piece does not hide its children.
- Save (Ctrl+S) writes `SimCopterRemake/Config/FigureAdjustments.json`. Closing warns
  about unsaved changes; external file edits are detected before overwriting.

The file contains only manual overrides, keyed by exact figure and part names
(including trailing spaces). Original game files are never rewritten. An empty
`figures` object preserves the existing game appearance. Position is in original
model units with the vertical sign already flipped to Z-up, so adjustments scale
with population height. Scale is applied about each pose's bounds in the piece's
own basis, preserving its edge directions even when rotated. The basis follows
the generated stroke cross-section and segment; round parts retain their authored
axes. Size edits calculate that scale from the current pose's piece-local bounds;
other poses retain the scale rather than a fixed bounding-box size. Existing saved
scale numbers now use these piece axes; the file and numeric values are preserved. Edits
affect only the selected primitive, not descendants or animation endpoints.

The preview mirrors `SimCopterPopulationFigure.cpp`'s nearest-LOD blocks, tapers,
balls, painter bias, and default thickness/depth. Head texture coordinates stay
attached during piece scaling and position edits. Lighting uses PyVista/VTK.

Restart the game/Unreal after saving: overrides load once per process and apply
to all newly built pilot/pedestrian animation meshes. No C++ rebuild is needed for
subsequent art edits. The JSON is staged as loose NonUFS data in packaged builds;
repackage or copy the edited file to the packaged project's `Config` directory.
These are intentional remake art corrections to the inferred 3D geometry, not
changes to the original behavior or animation data.

Verification: `Tools/.figure-editor-venv/Scripts/python.exe -m unittest discover -s Tools -p test_figure_editor.py`
checks all authored geometry, native viewport picking, edits, undo/redo, save round
trips and rendered SSAO on/off differences. The UI test briefly displays a window
and uses a scratch file. Unreal automation: `SimCopter.Figures.Adjustments` and
`SimCopter.Formats.PrivAnim`.
