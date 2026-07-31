# The front end — main menu and career/city select — decoded

Ground truth for the original's two front-end screens. Everything below is read off the
decompile, the assembly or the shipped resources; nothing is inferred from the artwork except
where it says so. Addresses are `SimCopter.exe` VAs as Ghidra names them.

Overlays that prove the geometry: `Docs/scratchpad/overlay_mainmenu_rects.py` and
`overlay_career_rects.py` draw every rectangle below back over the page art, where they land on
the printed furniture.

## The screen state machine — `FUN_00449cb0` (app vtable +0x74)

`EnterState(n)`; `FUN_0044a640` (+0x78) is the matching LeaveState.

| n | screen |
| --- | --- |
| 1 | shutdown |
| 4 | **main menu** — `FUN_00411900` builds it, `FUN_004118c0` binds it, `FUN_0044d070` starts `menusky.smk` |
| 5 | **career / city select** — `FUN_00411ca0` |
| 6 | in the city |
| 7 | hangar |

## Main menu — page id `0x7d2`

### The page descriptor — `FUN_00411900`

Ghidra reuses stack slots here, so the values come from `dump-asm`
(`Docs/scratchpad/asm-00411900.txt`). A 0x54-byte descriptor is built at `ESP+0x10` and handed to
`FUN_0045f290` -> `FUN_0045e5f0`, which copies it into the page object at bytes 0x7c..0xc4:

| descriptor | page field | value | meaning |
| --- | --- | --- | --- |
| +0x00 | — | `main1.bmp` | page background |
| +0x0c..+0x18 | 0x80..0x8c | 2, 29, 426, 416 | page rect; the control sizes itself from the bitmap (425x414), so only (2,29) carries information |
| +0x1c | 0x90 | **0x37 = 55** | string id of the first item |
| +0x20 | 0x94 | **5** | item count |
| +0x24 | 0x98 | 0 | command-id base |
| +0x28 | 0x9c | bytes 80 85 4a | normal text colour |
| +0x2c | 0xa0 | bytes ea ef 9a | highlighted text colour |
| +0x30 | 0xa4 | **0x74 = 116** | item x |
| +0x34 | 0xa8 | **0x2a = 42** | first item y (0x2d = 45 for languages other than 1/2) |
| +0x38 | 0xac | **0x1a = 26** | item font height (0x14 = 20 otherwise) |
| +0x3c | 0xb0 | **0x40 = 64** | item y stride |
| +0x40 | 0xb4 | -1 | title string id — the main menu has no title |
| +0x50 | 0xc4 | 0x24 = 36 | title font height (0x1a = 26 otherwise) |

`DAT_004f86d4` is the **language** index (`FUN_00423270` turns it into the per-language string
block offset), and English is 1, so the first column applies.

Colours are written a byte at a time — `80 85 4a` and `ea ef 9a` — i.e. Win32 `COLORREF`
0x00BBGGRR: normal **RGB(128,133,74)** olive, highlighted **RGB(234,239,154)** pale yellow-green.

### The item strings

Win32 `RT_STRING`, English = 1033, ids 55..59, exactly the five the shipped help documents
(`help/English/37ref.htm`):

| id | item |
| --- | --- |
| 55 | New Career Game |
| 56 | Open Career Game |
| 57 | New User Game |
| 58 | Open User Game |
| 59 | Quit |

`FUN_0045e920` creates one text control per item at `(116, 42 + 64*i)`, clipped to the rest of the
page, with `[vt+0xe0](26,0,0)` for the font and `[vt+0xe8]` for the normal or highlighted colour.

### The two sprite strips — `FUN_0045f670`, `FUN_0045fcf0`

`FUN_0045f670` loads **main5.bmp into +0x214** and **main4.bmp into +0x218** (the pointers are
`PTR_DAT_004f9be8` and `PTR_DAT_004f9be4`; the decompile's field order is easy to read backwards).

`FUN_0045fe10` (main4.bmp, the round LEDs) and `FUN_0045fed0` (main5.bmp, the arrow keys) each
build a five-entry blit table. Destinations are in page space; sources are `(l,t,r,b)`:

| row | LED dest | LED source | arrow dest | arrow source |
| --- | --- | --- | --- | --- |
| 0 | 334, 31 | 0,0,60,56 | 33, 35 | 0,0,39,65 |
| 1 | 334, 87 | 0,56,60,120 | 33, 100 | 0,65,39,129 |
| 2 | 334, 151 | 0,120,60,184 | 33, 164 | 0,129,39,192 |
| 3 | 334, 215 | 0,184,60,248 | 33, 227 | 0,192,39,254 |
| 4 | 334, 279 | 0,248,60,312 | 33, 289 | 0,254,39,297 |

Both bitmaps are two columns. The **selected** row has 0x3c (LED) / 0x27 (arrow) added to its
source left and right, which moves it to column 1 — the lit LED and the pressed key.

`main2.bmp` and `main3.bmp` are the two hose decorations, built by `FUN_0045f3d0` with degenerate
1x1 rects at **screen** (289, 0) and (427, 315); at their own sizes (81x29 and 213x165) they meet
the page's top and right edges on a 640x480 screen.

### Input — `FUN_0045f040` (keys), `FUN_0045f1a0` / `FUN_0045f210` (mouse), `FUN_0045eed0` (mnemonics)

| key | action |
| --- | --- |
| Down / Page Down | next item, wrapping to the first past the end |
| Up | previous item, wrapping to the last at item 0 |
| Page Up | previous item, but only while the selection is > 0 (no wrap) |
| Home | select the first item |
| End | select the last item |
| Enter | activate the selection — `[vt+0xec]` posts message **0x3e9** with the index |
| Esc | posts message **0x3ea** with the index |
| a letter | `FUN_0045eed0` selects the first item whose text starts with it |

Mouse: `FUN_0045f210` (+0xa4, move) hit-tests with `FUN_0045fc60` and selects what is under the
cursor, so hovering highlights. `FUN_0045f1a0` (+0x9c, button) selects and then activates.
`FUN_0045fc60`'s hit test is `29 < x < 394` and the item's own top/bottom.

`FUN_0045ed60` is the selection setter: it recolours the old and new items, stores the index at
+0xd4 and plays the page's own sound object once — **menu.wav**.

### Sound

The front-end screens do not use the 130-slot table; each builds a standalone sound object.
`FUN_0045f3d0` loads **menuback.wav** and plays it with `Play(1,1)` — looping — and `[vt+0xfc]`
(`FUN_0045f630`) stops it when the menu goes away. `menusky.smk` is the animated backdrop behind
the panel; it is a zero-byte stub in the reference install (a CD file), as are all the `_b.smk`
fly-throughs.

### What each item does — `FUN_0044c710` (app vtable +0x8c)

Reached from `FUN_0044bf70` when the message's control id is 0x7d2. `arg4` points at the item
index.

```
if (msg == 0x3ea) { msg = 0x3e9; index = 4; }   // Esc on the main menu IS Quit
if (msg != 0x3e9) goto idle;

index 0  New Career Game   app[0xb0] = 1; LeaveState(4); EnterState(5)
index 1  Open Career Game  file dialog, title 44 / filter 45 (*.scc)  -> load -> EnterState(6)
index 2  New User Game     file dialog, title 40 / filter 41 (*.sc2)  -> load -> EnterState(6)
index 3  Open User Game    file dialog, title 42 / filter 43 (*.scu)  -> load -> EnterState(6)
index 4  Quit              LeaveState(4); EnterState(1)               // no confirmation

idle:
if (msg == 0x989681) { FUN_00438690(app); }     // the 300 s idle timeout starts the attract demo
```

There is **no quit confirmation from the main menu**; string 3 ("Are you sure you want to quit?")
belongs to the in-game Settings screen (control 0x7dd/0x7de).

## Career / city select — page id `0x7d7`

`FUN_00411ca0` builds it over **career.bmp** at screen (0,0) (614x435, again a degenerate rect).
`FUN_00457c90` is the constructor.

**Which cities are offered.** `FUN_00411ca0` passes the successor trio
`careerRecord[DAT_00518d64] + 0x28` when `app[0xb0] == 0` (advancing through a career), and NULL
when it is 1 (a brand new career) — and `FUN_00457c90`'s null branch is literally `{0, 1, 2}`.
Slots whose id is -1 are dropped, leaving 1..3 panels.

### Layout

| what | rect | source |
| --- | --- | --- |
| panel 0 | (77,71)-(277,179) | `FUN_00457c90` screen[0x1e..0x21] |
| panel 1 | (339,71)-(539,179) | screen[0x22..0x25] |
| panel 2 | (77,249)-(277,357) | screen[0x26..0x29] |
| city name | (334,236)-(534,262) | `FUN_004580b0`, font 18, centred, RGB(181,240,0) |
| level name | (334,271)-(534,297) | same |
| OK / Cancel | (327,338) / (431,338) | 1x1 rects; sized from `button.bmp`'s 100x28 frames |
| OK alone | (380,338) | when advancing rather than starting |

Cancel exists **only for a new career** (`screen[0x2e] != 0`); a career advancement gets one
centred OK and its Esc key does nothing.

The two readouts are string **240 + cityIndex** (the city's name) and **290 + record level**
("Level 1".."Final Level"), refreshed by `FUN_00458d90` every time the selection moves.

### The selection highlight — `FUN_004589f0` / `FUN_00458e70` / `FUN_004590b0`

**carsel.bmp** (557x743) is a two-copy sheet in the page's own coordinate space: the glowing
frames at y+0 and the plain ones at **y+0x168 (360)**. Each panel is redrawn as four border
strips (left, top, right, bottom) blitted at their own coordinates:

| panel | left | top | right | bottom |
| --- | --- | --- | --- | --- |
| 0 | (54,51)-(77,216) | (77,51)-(276,69) | (276,51)-(305,216) | (77,180)-(276,216) |
| 1 | (312,51)-(339,216) | (339,51)-(537,69) | (537,51)-(556,216) | (339,180)-(556,216) |
| 2 | (54,217)-(77,382) | (77,217)-(276,248) | (276,217)-(305,382) | (77,359)-(276,382) |

The middle is left alone because that is where the city's preview movie plays:
`FUN_00407c50(2, mapName, out)` appends **`_s.smk`**, so panel *i* runs `city<N>_s.smk` on loop.
Those files are present in the reference install (246 KB each), but decoding Smacker is not
something the remake does.

### Input — `FUN_00458a90`

Esc posts 0x3ea (only when Cancel exists), Enter posts 0x3e9, and the four arrow keys move the
selection and play **carsel.wav**. The wheel is hand-written rather than modular; with three
panels it is:

| from | Left | Right | Up | Down |
| --- | --- | --- | --- | --- |
| 0 | 2 | 1 | 2 | 2 |
| 1 | 0 | 2 | 0 | 2 |
| 2 | 1 | 0 | 0 | 0 |

With two panels every key toggles; with one, nothing moves. **career.wav** plays once when the
screen opens.

### What OK does — `FUN_0044bf70`, control 0x7d7

```
city = screen[0x2a + screen[0x1d]];         // the selected slot's city id
if (msg == 0x3ea) { LeaveState(5); EnterState(4); return; }   // Cancel -> main menu
if (app[0xb0] == 0) FUN_00408210(city);     // advancing: adopt the record, zero the score
else                FUN_00407f30(city);     // new career: mode 2, $1000, 0 points
FUN_0044ce50(city);                         // the cityride intro + city<N>_b.smk fly-through
LeaveState(5); EnterState(6);
```

## The career progression table — `FUN_00408370`

30 records of 0x50 bytes at **0x518dc8** (four bytes below the base the older session note uses,
because that note started at the difficulty field):

| off | field |
| --- | --- |
| +0x00 | **career level 0..11** -> STRINGTABLE 290..301 |
| +0x04..+0x24 | difficulty, the seven weights, day/night (career.twk Ctrl0..Ctrl8) |
| +0x28/+0x2c/+0x30 | successor trio (-1 = fewer) |
| +0x34/+0x38/+0x3c | a second trio, all -1 in the shipped table |
| +0x40 | the city's own index |
| +0x44 | map base name, `"city0".."city29"` |
| +0x48/+0x4c | points needed, $ earned (career.twk Ctrl9/Ctrl10) |

Rebuilt by `Docs/scratchpad/parse_career_table.py`:

```
city level successors      city level successors      city level successors
 0    0    3,4,1           10    3    12,13,14        20    6    19,22
 1    0    3,4,5           11    3    10,13,14        21    7    23,24
 2    0    1,4,5           12    4    13,15,16        22    7    23,24
 3    1    4,6,7           13    4    15,16,17        23    8    25,26
 4    1    6,7,8           14    4    13,16,17        24    8    25,26
 5    1    4,7,8           15    5    16,18,19        25    9    27,28
 6    2    7,9,10          16    5    18,19,20        26    9    27,28
 7    2    9,10,11         17    5    16,19,20        27   10    29
 8    2    7,10,11         18    6    19,21           28   10    29
 9    3    10,12,13        19    6    21,22           29   11    (none)
```

City names are STRINGTABLE **240 + index**: Sea Cliff, Islandtown, Diabloville, CatNip Cove,
Cypress, Berkeley, Treeton, Keithly, Circlopolis, River Rail, Cumberland, Scotville, Kentown,
Tigger Town, Terraton, Happyland, Roseland, Waterton, Myrtle Dam, River Valley, Canyon, Hidden
Valley, Cancer, Calebopolis, Valley, Whattheheck, Four Cities, Toronto, Conville, Metropolis.

## Sliders

There are none anywhere in the front end. The original's slider dialogs — City Settings
(`cityset.bmp`, `slidcity.bmp`, strings 330..340), Graphics (`render.bmp`) and Sound
(`sound.bmp`, strings 130..142) — hang off the **in-game** Settings screen (`playmenu.bmp`,
control 0x7d3, strings 60..67), which `FUN_0044c9e0` routes and which is not reachable from the
main menu.
