# Effect renderer resolution trace — 2026-07-24

Fresh bytes were read from the imported `SimCopter.exe` image after the
face-`0x17` and face-`0x1a` renderer pass exposed the resolution globals.

Evidence: `out_effect_resolution_globals_20260724.txt`.

| Global | Value | Meaning |
| --- | ---: | --- |
| `DAT_004f9750` | `0x10` | active high-resolution renderer mode |
| `DAT_004f9780` | 560 | high-resolution gameplay viewport width |
| `DAT_004f9784` | 400 | high-resolution gameplay viewport height |
| `DAT_004f978c` | 640 | high-resolution framebuffer stride |
| `DAT_004f9790` | 480 | high-resolution framebuffer height |
| `DAT_004f9760` | 280 | alternate gameplay viewport width |
| `DAT_004f9764` | 200 | alternate gameplay viewport height |
| `DAT_004f976c` | 320 | alternate framebuffer stride |
| `DAT_004f9770` | 240 | alternate framebuffer height |

The active renderer facts above remain the decompilation reference. For modern
presentation, the remake intentionally treats face-`0x1a` fire/dust/water
stencils as a 280x200 virtual low-resolution effect layer and scales that whole
layer to the actual viewport. This keeps the card size, dot size, and gaps in
the same ratio instead of packing the decoded writes more tightly at 1440p.
Face `0x17` retains the active 560x400 projection.

Building fire placement has no additional object rotation in
`FUN_004a47c0`, `FUN_004a48e0`, or `FUN_004a5340`: FIREPTS is cloned and
translated. The remake must therefore apply its established city transform to
the template and to the stored `(X, Y-up, Z)` flame offsets. The decoded
screen-pixel kernels remain camera-aligned, matching their framebuffer-space
nature; no wall-plane reconstruction is applied.
