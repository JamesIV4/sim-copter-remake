# SimCopter cockpit flaps

*"Cockpit tool flaps decoded 2026-07-26; the click-box table lives in an UNANALYZED Ghidra gap, so find it by scanning .text for pointer refs."*

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

Still unported: the ten canisters printed on flap3 are the tear-gas round counter and are not
wired to the ammo state. Related: [[simcopter-heli-tools-models]].
