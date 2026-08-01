# SimCopter cockpit flaps

*"Cockpit tool flaps decoded 2026-07-26; the click-box table AND the flap tick that drives the tear-gas/water readouts live in an UNANALYZED Ghidra gap - read them out of the bytes."*

*Recorded 2026-07-26; ported into the repo 2026-07-29.*

The cockpit's per-tool control flaps (`flap0..3.bmp`, 138x58) are built by `FUN_004127d0`, and the
per-flap click boxes are written by the flap constructor `FUN_00454ee0` at `this+0x1d`.

Traps that cost time:

- **The flap class's methods are not in `.ghidra-exports`.** Ghidra ends `FUN_00454ee0` at
  `0x455093`, but the vtable at `0x4f3150` points at `0x455140 / 0x455170 / 0x455280 / 0x4553a0`,
  which were never turned into functions. `ghidra-bridge decompile` and `containing` both fail on
  them. The `flapbtn*.bmp` name pointers are likewise unreferenced in the export - find them by
  scanning `.text` for the DWORD of the pointer's own address (`0x4f994c/50/54`). See
  [[simcopter-ghidra-workflow]].
- **PowerShell PE offset maths**: `VA = imageBase + sectionRVA + (fileOffset - rawPtr)`. Adding the
  already-absolute section VA double-counts `imageBase` and puts every hit 0x400000 too high.
- **Equipment bits** (career + 0x48): `0x01` bucket, `0x02` megaphone, `0x04` harness, `0x08` tear
  gas, `0x10` water cannon. flap0 is gated on `0x11` - bucket and cannon share one flap.
- **`flapbtn*.bmp` frames are unequal widths**, so an equal-split strip reader is wrong:
  `flapbtn0` is rocker 17x29 x2 then water-drop 20x29 x2; `flapbtn1` is octagon 17x24 x2 plus a
  4x24 sliver; `flapbtn2` is the rocker again, byte for byte identical to flapbtn0's first two.
- **Click boxes are not the sprite rects.** They carry a pixel or two of slop and the padding is
  inconsistent: the bucket rocker's two boxes total exactly the sprite's 29 rows, the harness
  rocker's total 31. Get sprite origins by template-matching the frame against the page (all four
  matched 97-100%), not by assuming `Hit.Min`.
- **The help GIFs are not in this reference copy** (`help/English/gifs/` is absent), so the
  annotated panel diagrams 19/85/100.GIF cannot be used to confirm button semantics. The prose in
  `31ref`-`35ref.htm` is what is available.

**Every bar readout in the cockpit is a three-cell strip blitted N times** — flap0's water gauge,
and dash6's points bar (`managge.bmp`, 15x13, fifteen 5x13 cells from page (20,37), repaint at
`0x004534f2`). Same loop shape in both: `level` full cells, one leading edge, the rest empty. The
shared rule is `UI/SimCopterSegmentedBar.h`; the geometry stays with each bar. **Never draw one
of these bitmaps as a single image** — it is three states side by side, not one block, which is
how the points bar shipped wrong until 2026-08-01.

**The flaps have a per-frame tick, and it owns the two readouts** (decoded and ported
2026-07-31). Vtable `0x4f3150` slot `+0xd0` is `0x00455300`, which runs every fourth call:

- flap **3**: reads `career + 0x54` and, on a change, calls `0x00455790` - the ten canister
  lamps. Ten 4x4 dots at `x = 18 + 12 * (i % 5)`, `y = 12 + 13 * (i / 5)`, blitted from
  **`flapbtn1.bmp`'s right-hand 4-pixel sliver** (that is what the sliver is for): rows 0-3 are
  the pale "loaded" dot, rows 4-7 the dark "spent" one. The first `10 - rounds` lamps get the
  dark sprite, so the row empties from the **left**. `flap3.bmp` already prints all ten lamps
  pale - byte for byte the same sprite - which is why the original only ever paints the dark
  ones, and why the remake does the same instead of drawing both states.
- flap **0**: `heli[0x74] * 11 / maxLoad[type]` into an eleven-step water bar drawn by
  `0x00455700` from **`watergge.bmp`** (a fifth bitmap, loaded only by flap0 - it goes on
  `this+0xbc`, the button sheet on `this+0xc0`). Eleven 5x10 cells from page **(16, 43)**;
  the bitmap is exactly 15x10, three cells: full water | meniscus | empty. The row is
  `level` full cells, then **one** meniscus (skipped when the tank is full), then empties.
  **Unlike the canister lamps, all three states must be drawn** - `flap0.bmp` ships the
  *empty* gauge with the meniscus already printed at x 16, so a partly full tank has to
  paint over it. Divide by the heli.twk max load, not the static table's.

**The Apache has no flap and never will.** `FUN_004127d0` builds flaps from the five *equipment*
bits at career + 0x48; the Apache's missile and machine gun are **model capabilities**, so the
original ships no artwork and no layout for them. The remake gives them an invented strip built
from the same donor frame as the (also invented) dispatch strip: a missile button and a held gun
button, each firing directly, and nothing else - the ammunition is unlimited and the only limits
(the shared 1 s cooldown, the pool sizes) are things the player feels rather than reads. What is
actually in the air is on the debug panel instead. The strip collapses on the eight civilian
airframes.

Related: [[simcopter-heli-tools-models]], [[simcopter-water-gameplay]].
