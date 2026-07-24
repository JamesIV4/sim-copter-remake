# Original effect renderer gap audit (2026-07-24)

This audit answers whether the original fire, water, dust, and rotor-wash rendering path is
currently decompiled deeply enough to port faithfully.

## Result

The missing renderer decompilation pass was run on 2026-07-24. It produced live C decompiles for
the normal function targets, a complete raw instruction capture for the optimized type-`0x1a`
rasterizer, exact dispatch/jump-table dumps, palette-producer decompiles, palette selector bytes,
and data xrefs. See `effect_renderer_decompile_run_20260724.md`.

The earlier effect-layer pass did not contain this evidence, so its Unreal soft radial particles
and inferred colors were approximations. The follow-up port now uses the decoded sparse masks,
selector colors, random ordering, and active renderer viewport. This document remains the audit
trail for why that replacement was required.

## Confirmed frame-to-rasterizer chain

Fresh `ghidra-bridge decompile`, xref, raw-table, and disassembly checks establish this path:

1. `FUN_00449850` starts the frame, calls `FUN_004814c0`, and brackets it with
   `FUN_00461120`/`FUN_00461350`.
2. `FUN_004814c0` gathers visible scene entries through `FUN_004817c0`, prepares the camera
   matrix through `FUN_004e6910`, and calls `FUN_0046f79c`.
3. `FUN_0046f79c` calls `FUN_0046f7e0`.
4. `FUN_0046f7e0` jumps through `PTR_LAB_004fc178`. `FUN_004e4fc0` installs
   `LAB_004e4fcd` as that entry point; execution continues into `FUN_004e4fdd`.
5. `FUN_004e4fdd`, `FUN_004e54d0`, `FUN_004e5602`, `FUN_004ecae0`, and `FUN_004ee290`
   transform, clip, sort, and dispatch faces.
6. The four distance tables at `0x00514930`, `0x00514a10`, `0x00514af0`, and
   `0x00514bd0` consistently route:
   - face type `0x17` to `LAB_004eac78`, which calls `FUN_00491520`;
   - face type `0x1a` to `LAB_004eac90`, which calls `FUN_00496da0`.

The table entries remain the same with the alternate `+0x1c` renderer-mode offset.

## What the two effect face handlers actually do

### Face type `0x17`

`FUN_00491520` is fully decompiled. It converts the projected point from 20.12 fixed point to
integer screen coordinates and writes the face's byte at `faceDescriptor+0x0c` directly to the
8-bit framebuffer:

- normal `DAT_004f9750 == 0x10` renderer: a `2x2` block in the 560x400 gameplay viewport
  (640x480 framebuffer), clipped at the right and bottom edges;
- alternate renderer mode: a single pixel.

This is not a camera-facing translucent quad and has no radial alpha texture.

### Face type `0x1a`

`FUN_00496da0` has a partial C decompile, but the exact control flow remains an optimized
assembly rasterizer with nested jump tables.

Confirmed behavior:

- `faceDescriptor+0x0c & 0x7fffffff` selects one of 12 kernel classes through
  `PTR_LAB_0049a41c`;
- the table targets are `0x00496e55`, `0x00496e6e`, `0x00496e87`, `0x00496ea3`,
  `0x00496ebe`, and `0x00496ed5`;
- classes `1` and `2`, used by FIREPTS, share the size branch at `0x00496e6e` but select
  different palette/remap sources later in the function;
- the projected depth controls kernel dimensions;
- the high bit changes the drawing branch;
- `_rand` selects offsets and a palette-remap row;
- the function modifies existing framebuffer pixels through lookup tables rather than drawing
  an RGBA sprite with conventional alpha.

The palette/remap sources referenced by the function include `DAT_005d41d0`,
`DAT_005d41d8`, `DAT_005d41dc`, `DAT_005d41e0`, `DAT_0059ea30`, and the tables at
`0x00504828`, `0x00504830`, `0x00504838`, and `0x00504840`. Their producers and exact table
layouts are not yet decoded.

## Corrections to earlier notes

- `FUN_0046e2c0` and `FUN_0046e430` are face-bounds/height query functions, not flame-shaping
  transforms.
- `FUN_0046e600` translates all geometry vertices and its stored origin.
- `FUN_00483c20` initializes helicopter body, rotor, and shadow objects. It is not per-fire-type
  material setup.
- `CARFIRET` (`0x11c`, object name `firetruk`) is a fire-truck vehicle model.
- FIREPTS material values `1` and `2` are inputs to the type-`0x1a` kernel/palette paths. They
  are not literal Unreal colors and must not be replaced with inferred smoke/fire ramps.

## Completed renderer decompilation coverage

### A. Renderer entry and face dispatch

1. `FUN_0046f7e0`, `LAB_004e4fcd`, and `FUN_004e4fdd`
2. `FUN_004e5780`, `FUN_004e5d04`, `FUN_004e62ef`
3. `FUN_004ecae0`, `FUN_004ee290`, `FUN_004eeaf0`
4. `FUN_004e54d0`, `FUN_004e5602`
5. Tables `0x00514930`, `0x00514a10`, `0x00514af0`, and `0x00514bd0`
6. Jump-in wrappers `LAB_004eac78` and `LAB_004eac90`

### B. Exact effect rasterizers

1. `FUN_00491520` (type `0x17`; lifted, needs parity tests)
2. `FUN_00496da0` in full, including raw assembly through its final return
3. `PTR_LAB_0049a41c` and entry points `0x00496e55`, `0x00496e6e`, `0x00496e87`,
   `0x00496ea3`, `0x00496ebe`, `0x00496ed5`
4. All secondary jump tables inside `FUN_00496da0`, beginning with `0x0049a44c`
5. `FUN_0046c4bf`, whose return controls projected kernel size

### C. Palette and framebuffer composition

1. Writers/xrefs for `DAT_005d41d0`, `DAT_005d41d8`, `DAT_005d41dc`, `DAT_005d41e0`
2. Writers/xrefs for `DAT_0059ea30`
3. Table layouts and builders for `0x00504828`, `0x00504830`, `0x00504838`,
   `0x00504840`
4. The SIM3D palette load and shade/remap construction path used by these tables
5. The high-bit branch in `FUN_00496da0` and its framebuffer read/modify/write semantics

### D. Authored fire placement and transformation

1. `FUN_004a47c0` (FIREPTS clone/pool initialization)
2. `FUN_004a48e0` and `FUN_004a5340` (flame creation and placement)
3. `FUN_004a4ac0` (per-frame flame update)
4. `FUN_004a64d0` (restore/reposition path)
5. `FUN_0046e600` (geometry translation)

All targets in sections A-D were included in the live pass. Jump-in labels and the optimized
type-`0x1a` body were captured as exact raw disassembly because Ghidra cannot express their
control flow completely as ordinary C.

## Completion gate before Unreal replacement

The renderer phase is decoded only when tests can reproduce, from original inputs:

- the exact `0x17` framebuffer footprint in both renderer modes;
- all 12 type-`0x1a` projected kernel footprints;
- class `1` versus class `2` FIREPTS color/remap behavior;
- depth scaling, high-bit behavior, clipping, random selection, and palette lookup;
- the original FIREPTS point positions after the flame update path.

Only then should Unreal replace the current soft material. The port may use Unreal render
targets, Niagara, generated masked textures, or custom material logic, but its observable
coverage, color sequence, scale, and cadence must be derived from these decoded functions.

The user-requested exception remains explicit: retain authentic rotor wash/downwash particles,
but omit the black-square primitive beneath the helicopter rather than emulating it.
