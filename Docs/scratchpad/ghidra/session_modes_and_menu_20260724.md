# Session modes, career table, and the shell menus — decode 2026-07-24

Why: the remake needed a debug main menu that can start free roam or load a
specific mission. Before building one it was worth knowing what the original's
own shell actually offered, what "starting a game" writes, and whether the
original had any mode in which missions do *not* arrive.

Sources (all via `ghidra-bridge decompile`): `FUN_00407a50`..`FUN_00408370`
(session accessors, session starts, career table init), `FUN_00408c30`
(scheduler spawn gate), `FUN_0041fc50` / `FUN_0044bf70` (menu commit handlers),
`FUN_00457c90` / `FUN_00411ca0` (career select screen), `FUN_0044ce50` (city
intro), `FUN_004a6d20` / `FUN_004a6e60` (weight table + scheduler),
`Reference/SimCopterOriginalGame/cities/`, `tweak/career.twk`. Strings pulled
straight out of the PE `.data` where the export lacked them.

## The mode global `DAT_00518d50`

Every session accessor branches on it, so it is *the* game-mode selector:

| Value | Meaning | Opened by |
| ---: | --- | --- |
| 0 | nothing running (all session state zeroed) | `FUN_00407e20` |
| 1 | single city | `FUN_004080c0(cityName)` |
| 2 | career | `FUN_00407f30(cityIndex)` |

The two live modes keep parallel state blocks and parallel city records, and
every getter/setter picks between them:

| | mode 1 | mode 2 (career) |
| --- | --- | --- |
| session block | `DAT_00518cf8` | `DAT_00518d6c` |
| city record | `DAT_00518cd0` (one record) | `DAT_00518dcc + city * 0x50` |
| city index | n/a | `DAT_00518d64` |

Session block fields (offsets from the block base):

| Off | Meaning | Accessors |
| --- | --- | --- |
| +0x40 | cash | get `FUN_00407a70`, add `FUN_00407a90` (clamps at 0) |
| +0x44 | 0x10 at session start — unidentified | — |
| +0x48 | 3 at session start — unidentified | — |
| +0x50 | score/points | get `FUN_00407ac0`, set `FUN_00407ae0`, add `FUN_00407b00` |
| +0x54 | 0 at session start — unidentified | — |

**Both session starts hand the player $1000 and 0 points.** `FUN_004080c0` also
copies career City0's first nine dwords into the mode-1 record as the default
tuning, then lets the city file override all nine from its own 0x24-byte
(9 dword) `0x5eeeeee` resource — i.e. a shipped/user city can carry its own
difficulty and weight vector.

## Career city record (`DAT_00518dcc + city * 0x50`)

The first nine dwords are exactly `career.twk` `Ctrl0..Ctrl8`; the rest is
hardcoded in `FUN_00408370`, not in the tweak file.

| Off | Field |
| --- | --- |
| +0x00 | Difficulty (0-3); tier = this + 1 (`DAT_004f9740`) |
| +0x04..+0x1c | the seven weights: Fire Crime Rescue Riot Traffic MedEvac Transport |
| +0x20 | Day or Night; copied to `DAT_004f9720` when the city is entered |
| +0x24/+0x28/+0x2c | **up to three successor cities** (-1 = fewer) |
| +0x30/+0x34/+0x38 | a second successor trio, all -1 in the shipped table |
| +0x3c | the city's own index |
| +0x40 | map base name: `"city0"`.."city29" (`0x4f0378`, `0x4f0380`, ...) |
| +0x44 | Points Needed (`career.twk` Ctrl9), read by `FUN_00407b30` |
| +0x48 | $ Earned (`career.twk` Ctrl10), read by `FUN_00407b80`, clamped >= 1 |

So the career is **not** a straight 0..29 walk: it is a branching graph. City0
offers {3,4,1}, City1 {3,4,5}, City2 {1,4,5}, and `FUN_00407f30` seeds the very
first choice with {0,1,2}. `FUN_00411ca0` hands the current record's +0x24 trio
to the select screen (`FUN_00457c90`, screen id 0x7d7), which shows 1-3 entries
depending on how many are >= 0. `FUN_0044bf70` case 0x7d7 commits the pick:
`FUN_00408210(city)` when a career is already running, `FUN_00407f30` for a new
one, then `FUN_0044ce50` plays that city's intro (`cityride.bmp` + movie).

`FUN_00408210` (enter city) adopts the record, republishes the successor trio,
loads the map via `FUN_00479940`, and **zeroes the session score** (+0x50).

### City -> map binding (was an open follow-up)

`FUN_00407f30` / `FUN_00408210` copy record +0x40, append `PTR_DAT_004f8290`
= `".sc2"`, and resolve it through `FUN_00432ab0(7, ...)` — search-path class 7
= `"cities\career\"` (`0x4f8fcc`). On disk:
`Reference/SimCopterOriginalGame/cities/career/city0.sc2 .. city29.sc2`, exactly
30 files. Confirmed: **career city N is `cities\career\cityN.sc2`.**

## There is no "missions off" mode

`FUN_004a6e60`'s only spawn gate is `DAT_005812b4 = FUN_00408c30()`, and
`FUN_00408c30` is *not* a UI test despite the remake's inherited
`IsModalUiActive` name: it returns 1 only when `DAT_00518d50 == 2` and the
session score has reached the current city's Points Needed. In other words new
missions stop arriving once the career city is won, and nothing else ever
suppresses them. The shell menus ran in their own screen loop with the
simulation stopped rather than gating the scheduler.

What the data *can* express is a city that spawns nothing: `FUN_004a6d20`
compares the seven weights' sum against 1.0 and, when it is lower, writes zeros
into the whole cumulative table `DAT_00581738[1..7]`, so every one of
`FUN_004a6e60`'s bucket comparisons fails. That is what the remake's debug
free-roam mode uses (see `ASimCopterMissionSystemActor::BeginDebugSession`), and
it is also why a user city with all-zero weights would be a quiet city in the
original.

## The main menu's item set

`Confirmed` by the shipped help file (`help/English/37ref.htm`, "The Main Menu"), which matches the
five `main1.bmp`..`main5.bmp` button graphics and the session starts above:

| Item | What it does |
| --- | --- |
| New Career Game | "You'll choose one of three cities", then a fly-through, then the helipad. The three cities are the record's successor trio, seeded {0,1,2} by `FUN_00407f30`. |
| Open Career Game | Resume a saved career (`FUN_0041fcd0`, kind 3/4 through `FUN_0041fc50`). |
| New User Game | A file dialog for any SimCity 2000 / Network Edition / SCURK city; "You'll begin at the city's airport. If the city has no airport, one will be built just outside the southeast corner" -> `FUN_004080c0`, mode 1. |
| Open User Game | Resume a saved user city. |
| Quit | Exit. |

The in-flight Settings panel (`38ref.htm`) is a separate screen; its "Leave City" item is the only
documented way back to the main menu, which is what the remake's `SimMainMenu` command mirrors.

## Menu screens (for reference, not ported)

The shell is bitmap-driven with screen ids: 0x7d2/0x7d3 (play menu,
`playmenu.bmp`), 0x7d7 (career select, `career.bmp` + `carsel.wav`), 0x7d4-0x7d8
(option rows), 0x7d9/0x7da (back/quit), 0x7de (quit confirm), 0x7df, 0x7e0,
0x7e1. `FUN_0041fc50` dispatches city loads by kind: 1 = validate then start a
single city, 5 = start a single city directly, 3/4 = `FUN_0041fcd0` (the path
that also writes the mode global — saved-game restore). None of this layout is
worth porting; the remake's debug menu is its own Slate widget.
