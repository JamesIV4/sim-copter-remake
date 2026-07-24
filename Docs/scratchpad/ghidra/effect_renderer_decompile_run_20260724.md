# Effect renderer decompilation run (2026-07-24)

Decompiler: Ghidra 12.1.2 PUBLIC, run live through `analyzeHeadless` with
`ReverseExplore.java` against the `SimCopter.exe` program in the repository Ghidra project.

Project mode: `-readOnly -process SimCopter.exe -noanalysis`

This is a new live decompilation pass, not a summary copied from the exported bridge cache.

## Coverage

### Normal function bodies

`out_effect_renderer_decompile_20260724.txt` contains 19 decompiler results:

| Address | Function |
|---|---|
| `0x0046f7e0` | `FUN_0046f7e0` |
| `0x004e4fc0` | `FUN_004e4fc0` |
| `0x004e4fdd` | `FUN_004e4fdd` |
| `0x004e54d0` | `FUN_004e54d0` |
| `0x004e5602` | `FUN_004e5602` |
| `0x00491520` | `FUN_00491520` |
| `0x00496da0` | `FUN_00496da0` |
| `0x004e5780` | `FUN_004e5780` |
| `0x004e5d04` | `FUN_004e5d04` |
| `0x004e62ef` | `FUN_004e62ef` |
| `0x004ecae0` | `FUN_004ecae0` |
| `0x004ee290` | `FUN_004ee290` |
| `0x004eeaf0` | `FUN_004eeaf0` |
| `0x004a47c0` | `FUN_004a47c0` |
| `0x004a48e0` | `FUN_004a48e0` |
| `0x004a5340` | `FUN_004a5340` |
| `0x004a4ac0` | `FUN_004a4ac0` |
| `0x004a64d0` | `FUN_004a64d0` |
| `0x0046e600` | `FUN_0046e600` |

No target reported `Decompile failed` or `No function`.

### Palette and projection producers

`out_effect_palette_producers_20260724.txt` contains seven additional live decompiles:

- `FUN_00496180`: binds the effect palette-ramp bases from SIM3D palette entries `0x33`,
  `0x34`, `0x32`, `1`, and `0x29`;
- `FUN_0047bf90`: selects the active background/shade ramp and projection thresholds;
- `FUN_0046c4bf`: signed 16.16 division used for projected kernel size;
- `FUN_0046cd20`: palette-entry lookup;
- `FUN_0046f8e0` and `FUN_0046f4cb`: renderer setup;
- `FUN_00479bb0`: original global asset/palette load path.

No producer reported a decompile failure.

### Jump-in and optimized assembly

Normal Ghidra C output is not sufficient for the optimized effect handlers because their
dispatch targets enter the middle of larger functions. The pass therefore also captured:

- `LAB_004e4fcd..0x004e4fdc`: renderer entry thunk;
- `LAB_004eac60..0x004eac9b`: face-dispatch wrappers, including:
  - `LAB_004eac78` -> `FUN_00491520` for face type `0x17`;
  - `LAB_004eac90` -> `FUN_00496da0` for face type `0x1a`;
- every instruction from `0x00496da0` through the final epilogue at `0x0049a411`
  (4,261 instruction lines);
- all four `FUN_00496da0` jump tables from `0x0049a41c` through `0x0049a4c3`;
- all four distance/mode face dispatch tables from `0x00514930` through `0x00514caf`;
- the selector bytes at `0x00504828..0x00504847`;
- xrefs for all effect palette bases, selector arrays, and framebuffer-composition state.

The raw `FUN_00496da0` instruction range is authoritative. Its Ghidra C result still collapses
the indirect branches and does not represent the full function.

## Output verification

| Output | Bytes | Lines | SHA256-16 |
|---|---:|---:|---|
| `out_effect_renderer_decompile_20260724.txt` | 82,556 | 2,270 | `3699CC86BEF23E6E` |
| `out_effect_palette_producers_20260724.txt` | 13,979 | 534 | `FDA200E0904B52DF` |
| `out_effect_face_1a_rasterizer_asm_20260724.txt` | 143,766 | 4,261 | `839539002BC96BF2` |
| `out_effect_face_1a_jump_tables_20260724.txt` | 1,971 | 54 | `9238029DBD44FAD7` |
| `out_effect_face_dispatch_wrappers_asm_20260724.txt` | 685 | 25 | `217E277C255FA1F3` |
| `out_effect_renderer_entry_asm_20260724.txt` | 135 | 4 | `A08A2833FEE2F7A0` |
| `out_effect_renderer_dispatch_tables_20260724.txt` | 10,249 | 281 | `3A5D20A7698A166E` |
| `out_effect_palette_tables_20260724.txt` | 777 | 21 | `FAE8B777AA2A5262` |
| `out_effect_palette_xrefs_20260724.txt` | 25,930 | 1,188 | `01D314C4087E305E` |
| `out_effect_renderer_core_asm_20260724.txt` | 9,118 | 291 | `2144E6666F0DB04E` |

Validation counted all expected function headers, checked the first and final `0x1a` addresses,
confirmed all six kernel-size entry points, and found no `Decompile failed`, `No function`, or
`could not disassemble` markers.

## Remaining work

The decompilation pass is complete. The next stage is semantic reconstruction and a reference
indexed-framebuffer implementation for `FUN_00496da0`; that is required before replacing the
current Unreal visual approximations.
