# The in-game Settings screen and its sub-dialogs — decoded

Ground truth for the original's options menu, read off the decompile, the assembly and the
shipped resources. Addresses are `SimCopter.exe` VAs as Ghidra names them.

Overlays that prove the geometry: `Docs/scratchpad/overlay_settings_rects.py` draws every
rectangle below back over the page art, where they land on the printed furniture.

The rect tables all come from **`dump-asm` + `parse_dialog_rects.py`**, never from the decompile —
Ghidra aliases the reused stack slots, exactly as it does for the Check-up dialog
(`Docs/memory/simcopter-checkup-menu.md`). Reminder of the trap: a `PUSH` between the four stores
shifts every later displacement by four, so the slots pair up by **address order**.

## How it opens — `FUN_00437d10`, page id `0x7d3`

`FUN_0044ac80` (app vtable `+0xac`, the command handler) answers command **0x3f** with

```c
(**(code **)(*param_1 + 0x40))();   // FUN_004346c0 - pause
FUN_00437d10();                     // build playmenu.bmp
```

`FUN_004346c0` is a **reference-counted** pause: it bumps `app+0xbc` and only calls
`FUN_00424ab0` on the 0->1 edge. `[vt+0x44]` is the matching resume. Every sub-dialog opener
pauses again on the way in and its handler resumes on the way out, so the counter stays balanced.

Command 0x3f is **not in `input.cfg`** — the file binds 78 commands and 0x3f is not among them, so
the key that raises it is wired in the window procedure, not rebindable.

## The Settings page descriptor — `FUN_00437d10`

Same 0x54-byte stack descriptor the main menu uses (`FUN_0045e5f0` copies it into the page). Read
from `Docs/scratchpad/asm-00437d10.txt`; descriptor base is `ESP+0x24` after the string ctor.

| descriptor | value | meaning |
| --- | --- | --- |
| +0x00 | `playmenu.bmp` | page background (343x433) |
| +0x0c..+0x18 | 50, 10, 51, 11 | page rect - degenerate 1x1, so only **(50,10)** carries information |
| +0x1c | **60** or **61** | string id of the first item |
| +0x20 | **8** or **7** | item count |
| +0x24 | **0** or **1** | command-id base |
| +0x28 | bytes 80 85 4a | normal text colour, RGB(128,133,74) |
| +0x2c | bytes ea ef 9a | highlighted text colour, RGB(234,239,154) |
| +0x30 | **102** | item x |
| +0x34 | **64** or **104** | first item y (+3 for languages other than 1/2) |
| +0x38 | **26** | item font height (20 otherwise) |
| +0x3c | **40** | item y stride |
| +0x40 | -1 | title string id - none |
| +0x50 | 36 | title font height (26 otherwise) |

Same two colours as the main menu. The eight printed plates on `playmenu.bmp` are fixed; the
seven-item variant simply starts on the second one, which is why the first y moves from 64 to 104
and the command base from 0 to 1 — **the item's command id is stable whether or not the first row
is shown**.

### What decides it — `DAT_00518d50`

```c
undefined4 * FUN_00407bb0(void)                 // "the editable city settings"
{
  if (DAT_00518d50 == 1) return &DAT_00518cd0;              // user game: a global block
  return &DAT_00518dcc + DAT_00518d64 * 0x14;               // career: the city's own record
}
```

`DAT_00518d50 == 1` is a **user game**; anything else is a career. `0x518dcc` is
`career base 0x518dc8 + 4` and the stride is `0x14 * 4 = 0x50` bytes, i.e. the same 30-record,
80-byte career table `Docs/memory/simcopter-front-end.md` describes. So a career city's rates are
the fixed ones from the table and cannot be edited — which is exactly why **"City Settings" is
only on the menu in a user game**.

### The items — STRINGTABLE 60..67

| cmd | id | item |
| --- | --- | --- |
| 0 | 60 | City Settings *(user game only)* |
| 1 | 61 | Graphics |
| 2 | 62 | Sound |
| 3 | 63 | Controls |
| 4 | 64 | Save Game |
| 5 | 65 | Save Game As |
| 6 | 66 | Leave City |
| 7 | 67 | Continue |

## Routing — `FUN_0044c9e0` (app vtable **+0x88**)

Ghidra never made a function here, so this is `Docs/scratchpad/asm-0044c9e0.txt` (capstone).
`FUN_0044bf70` dispatches control id 0x7d3 to it.

| message | action |
| --- | --- |
| 0x3ea (Esc) | `[vt+0x44]` resume, then close the page — **identical to Continue** |
| 0x3e9 item 0 | close page, `FUN_004383c0` -> City Settings, control 0x7d8 |
| 0x3e9 item 1 | close page, `FUN_004380a0` -> Graphics, control 0x7d5 |
| 0x3e9 item 2 | close page, `FUN_00438200` -> Sound, control 0x7d6 |
| 0x3e9 item 3 | close page, `FUN_00437f30` -> Controls, control 0x7d4 |
| 0x3e9 item 4 | `FUN_00407c30()` picks the path: a named save goes to `FUN_004200e0`, otherwise `FUN_00420670(0)` asks for a name. 0 = ok -> message box 0x7da with string **48** "Game saved!"; 9 = cancelled; anything else -> `FUN_00421060(err)` |
| 0x3e9 item 5 | `FUN_00420670(0)` (always Save As), same result handling |
| 0x3e9 item 6 | `FUN_004352f0(0, 11, 0x20002)` - modal Yes/No on string **11** "Are you sure you want to leave this city?". On 2 (Yes): close the page, set `app+0x30 = 1`, then `FUN_00435140(0x7dd, 49, 0x20002)` - Yes/No on string **49** "Do you want to save the game?" |
| 0x3e9 item 7 | close the page, `[vt+0x44]` resume |

`FUN_00435140(controlId, stringId, flags)` and `FUN_004352f0(...)` both `LoadStringA` and hand off
to `FUN_00434d50` (the MBox page). Flags **0x20002** is the two-button Yes/No form, **1** the
one-button OK form; `FUN_004352f0` is the modal one that returns the answer.

## City Settings — `cityset.bmp`, control `0x7d8`

Opened by `FUN_004383c0` (which pauses unconditionally), built by `FUN_00440370`
(`asm-00440370-cityset.txt`), answered by `FUN_00438440`. The page is 594x435 at (0,0).

Eight vertical sliders, evenly spaced, all 26x202 at y 96..298 — exactly the size of
**`slidcity.bmp` (26x202)**, which is the track art. Ranges come from
`FUN_0040bb20(min)` / `FUN_0040bb50(max)` right after each construction:

| # | cmd | x | range | label (string) | city dword |
| --- | --- | --- | --- | --- | --- |
| 0 | 3 | 42 | **0..3** | 333 Difficulty | +0x00 |
| 1 | 4 | 111 | 0..100 | 334 Fire | +0x04 |
| 2 | 5 | 179 | 0..100 | 335 Crime | +0x08 |
| 3 | 6 | 248 | 0..100 | 336 Rescue | +0x0c |
| 4 | 7 | 316 | 0..100 | 337 Riot | +0x10 |
| 5 | 8 | 385 | 0..100 | 338 Traffic | +0x14 |
| 6 | 9 | 454 | 0..100 | 339 Medical | +0x18 |
| 7 | 10 | 522 | 0..100 | 340 Transport | +0x1c |

The slider objects live at `dialog+0x74 .. +0x90` and `FUN_00440e40` / `FUN_00440ec0` copy them
to and from the eight consecutive dwords `FUN_00407bb0()` returns. That is the remake's
`FSimCopterCareerCity` — `Difficulty` then `Weights[7]` = Fire, Crime, Rescue, Riot, Traffic,
MedEvac, Transport — **in the same order**, which is what pins the labels to the sliders: the
label-to-slider pairing is construction order, and the alignment confirms it (labels for the even
sliders share their slider's LEFT edge, labels for the odd ones share its RIGHT edge).

Labels are font height 18, justify 1 (centre), and alternate above (y 47) and below (y 327) the
troughs. Buttons: OK cmd 1 string 331 at (130,376), Cancel cmd 2 string 332 at (364,376), font 16.

Cancel discards; only `param_3 != 0` (OK) runs `FUN_00440ec0` to write the values back.

## Sound — `sound.bmp`, control `0x7d6`

Opened by `FUN_00438200`, built by `FUN_0043f7c0`, answered by `FUN_00438320`. Page 550x434 at
(0,0). This dialog is the **radio head unit**, not a table of mixer channels — strings 131..136
(Dispatch, Sound Effects, Vehicle Sounds, Classical, Rock, Techno) are *not* used by it.

| control | cmd | rect | range |
| --- | --- | --- | --- |
| Game Volume slider (horizontal) | 6 | (120,334)-(312,366) | **320..10000** |
| Radio volume slider (vertical) | 11 | (350,78)-(382,270) | 320..10000 |
| Radio tuner slider | 10 | (393,91)-(439,279) | **0..2** |
| toggle, above label 138 | 4 | (108,253) | - |
| toggle, above label 137 | 3 | (196,253) | - |
| toggle, above label 139 | 5 | (286,253) | - |
| OK / Cancel | 1 / 2 | (334,331) / (334,359) | strings 141 / 142 |

Labels, font height 14: 130 Game Volume (150,368) centred; 138 Commercials (110,287); 137 DJ
(192,287); 139 Auto-Quiet (238,287) right-justified; 140 "Vol." (348,287).

The volume sliders are **logarithmic**. `FUN_00440020` sets a slider from a stored volume as
`A ** ((B - v) * C) * B` and `FUN_00440130` inverts it with `ln(B/value) * D * E + B`, where the
five constants are doubles at 0x4f2180, 0x4f2188, 0x4f2190, 0x4f2198, 0x4f21a0. The tuner's 0..2
is three stations.

## Graphics — `render.bmp`, control `0x7d5`

Opened by `FUN_004380a0`, built by `FUN_0043df80`, answered by `FUN_00438150`. Page 594x435 at
(0,0). Decoded for completeness; **the remake replaces the contents of this page** (see the
memory note), so only the furniture is reused.

| control | cmd | rect | string |
| --- | --- | --- | --- |
| checkbox | 3 | (78,64) | 71 Building textures, label (138,70)-(276,90) font 18 |
| checkbox | 4 | (78,104) | 72 Ground textures, label (138,112)-(276,132) |
| checkbox | 5 | (78,146) | 73 Sky and clouds, label (138,156)-(276,176) |
| slider | 10 | (72,213) | fog, with 78 Near (76,245), 80 Fog Closeness (142,245) centred, 79 Far (222,245) right |
| radio labels | - | (74,293)/(74,322)/(74,352), font 16, right-justified | 75/76/77 the three Display Resolution options |
| OK / Cancel | 1 / 2 | (328,318) / (432,318) | 81 / 82 |

The three checkbox and OK/Cancel rects are 3x3 rather than 1x1 — still degenerate, still "size
yourself from the bitmap". The big panel on the right (x ~338..560, y ~64..285) is the preview,
which ships printed with a "Paul woz here" easter egg.

## Controls — `input.bmp`, control `0x7d4`

Opened by `FUN_00437f30`, constructed by `FUN_00417790` (a different, larger class — 0xb0 bytes,
vtable `PTR_FUN_004f10c0`), built by `FUN_00417cd0`, answered by `FUN_00437fd0`.

| control | cmd | rect | string |
| --- | --- | --- | --- |
| panel (`0x0043b470`) | 7 | (32,240)-(294,334) | instructions, font 16 |
| panel (`0x0043b470`) | 8 | (302,240)-(560,334) | instructions, font 16 |
| button | 9 | (326,346) | 8 "List All" |
| button | 3 | (444,346) | 5 "Defaults" |
| button | 1 | (?,380) | 20 "OK" |
| button | 2 | (444,380) | 21 "Cancel" |

Art: `input.bmp` 594x435 (a mostly blank metal page with one big instruction well),
`keyboard.bmp` 506x188, `joystick.bmp` 173x213, `keylight.bmp` 10x8 (the per-key tint), and
`input_b.bmp` 506x217. Strings 6 and 7 are the two instruction blocks ("Green keys are assigned to
the current command. Red keys are other commands. Dark gray keys are reserved." and the joystick
wording), 9 "Keyboard" and 10 "Joystick" the two modes.

**The per-key hit-rect table is not in the Ghidra exports.** `keyboard.bmp` is a picture of a
keyboard that the dialog hit-tests key by key and tints with `keylight.bmp`; the table sits in an
unanalysed gap the same way the cockpit flap click-boxes do
(`Docs/memory/simcopter-cockpit-flaps.md`). Recovering it needs a byte scan, not a decompile.
