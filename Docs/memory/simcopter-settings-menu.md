# SimCopter Settings screen — the in-game options menu and its four sub-dialogs

*"The Settings menu is one stack descriptor with TWO variants, and the only difference between
them is whether the first row is used — which is why an item's command id is the same either way."*

*Recorded 2026-07-31.*

Ported as `SSimCopterSettingsMenu`, `SSimCopterCitySettings`, `SSimCopterSoundSettings`,
`SSimCopterGraphicsSettings`, `SSimCopterControlSettings` (all `Private/UI`) on the shared
`SimCopterFrontEnd` scaffolding, driven by `ASimCopterPlayerController`, which is the remake's
`FUN_0044c9e0`. Settings are stored in `USimCopterSettings`. Full function-by-function decode with
citations: `Docs/scratchpad/settings-DECODED.md`. Front-end sibling: [[simcopter-front-end]].

## The five things that will mislead you

1. **The menu has two variants and the command base absorbs the difference.** `FUN_00437d10`
   writes first-string-id, item-count, **command base** and first-y as a pair: `DAT_00518d50 == 1`
   gives string 60 / 8 items / base 0 / y 64, anything else string 61 / 7 items / base **1** /
   y 104. `playmenu.bmp` prints eight plates either way; the short list just starts on the second.
   Read the base and an item's id is stable — miss it and every career-game item is off by one.

2. **`DAT_00518d50 == 1` means a USER game, and that is why City Settings comes and goes.**
   `FUN_00407bb0` returns the editable global block `0x518cd0` for a user game and the running
   city's own **career record** (`0x518dcc + city * 0x50`) otherwise. A career's rates are fixed,
   so the item is simply not built.

3. **The Sound dialog is the radio head unit, not a mixer.** Strings 131..136 (Dispatch, Sound
   Effects, Vehicle Sounds, Classical, Rock, Techno) look like channel names but `FUN_0043f7c0`
   never uses them — it builds five text controls and they are 130, 138, 137, 139, 140. What is
   really there: the game-volume fader, the radio's volume, the tuner, and three toggles.

4. **The pause is reference counted.** `FUN_004346c0` bumps `app+0xbc` and only pauses on the
   0->1 edge; `[vt+0x44]` is the matching resume. Opening a sub-dialog pauses *again* and its
   handler resumes on the way out, so a port that pauses and unpauses per screen unpauses the game
   underneath its own menu.

5. **The one horizontal slider in the game is here.** Every other slider in SimCopter is vertical;
   the Game Volume fader (192x32 at page 120,334) is not, and `FUN_0040af00` picks SLIDERTH.BMP
   rather than SLIDERTV.BMP for its thumb. `SSimCopterCheckupSlider` grew an `Orientation`
   argument for it ([[simcopter-checkup-menu]]).

## Routing — `FUN_0044c9e0`, app vtable **+0x88**

Ghidra never made a function at 0x44c9e0, so it comes from capstone (`asm-0044c9e0.txt`), reached
from `FUN_0044bf70` on control id 0x7d3. Items, which are also STRINGTABLE 60..67:

0 City Settings -> `cityset.bmp` 0x7d8 · 1 Graphics -> `render.bmp` 0x7d5 · 2 Sound ->
`sound.bmp` 0x7d6 · 3 Controls -> `input.bmp` 0x7d4 · 4/5 Save / Save As · 6 Leave City, a modal
Yes/No on string **11** then a second on string **49** · 7 Continue. **Esc is byte-for-byte
Continue** — the 0x3ea branch resumes and closes, exactly what item 7 does.

`FUN_00435140` / `FUN_004352f0(controlId, stringId, flags)` are the message-box helpers; flags
**0x20002** is the two-button Yes/No form and **1** the one-button OK. `FUN_004352f0` is the modal
one that returns 2 for Yes.

## Layout

Same descriptor and same two colours as the main menu (olive RGB(128,133,74), highlighted
RGB(234,239,154)). `playmenu.bmp` sits at **(50,10)**; items at x 102, stride 40, font 26. The
sub-dialogs all take a degenerate (0,0,1,1) rect, so each page lands centred at its own size.

**City Settings** — eight vertical sliders, 26x202 at y 96..298, evenly spaced at x 42, 111, 179,
248, 316, 385, 454, 522, command ids 3..10. Ranges from `FUN_0040bb20`/`FUN_0040bb50`:
**slider 0 is Difficulty over 0..3, the other seven are 0..100**. `slidcity.bmp` is exactly 26x202,
the rect's own size, so unlike the Check-up dialog's SLIDCHK the loose track art **is** drawn.
Label-to-slider pairing is construction order, which is also `FSimCopterCareerCity`'s field order —
Difficulty, then Fire, Crime, Rescue, Riot, Traffic, MedEvac, Transport — and the geometry agrees
(an even slider's label shares its LEFT edge, an odd one's its RIGHT).

**Sound** — Game Volume (cmd 6, horizontal) and the radio's volume (cmd 11) both **320..10000**;
the tuner (cmd 10) **0..2**. The original maps volume onto the slider logarithmically
(`FUN_00440020` / `FUN_00440130`, five doubles at 0x4f2180..0x4f21a0); the remake's mixer already
indexes volume linearly in [0,10000], so the port is linear over the original's own end points.

## Remake divergences (all deliberate)

- **The Graphics page carries Unreal's settings, not the original's.** `render.bmp`'s options
  (building textures, ground textures, sky, fog closeness, three display-resolution modes) are all
  1996 concessions to hardware; this project renders the whole city from a handful of runtime
  static meshes and 8-bit palette rasters, so porting the switches would give the player five
  controls that do nothing. The page keeps the furniture, the OK/Cancel plates at the decoded
  positions and its place in the menu, and lists DLSS super resolution + quality mode, DLSS Frame
  Generation + how many frames it generates, HUD Scale, and every display and scalability setting
  `UGameUserSettings` owns. The original's layout is decoded anyway, in the scratchpad note.
- **The Controls page lists bindings instead of drawing a keyboard.** `keyboard.bmp` (506x188) is
  hit-tested key by key and tinted with `keylight.bmp`; **that per-key rect table is not in the
  Ghidra exports** — it sits in an unanalysed gap the way the cockpit flap click-boxes do
  ([[simcopter-cockpit-flaps]]) and needs a byte scan. The page keeps input.bmp, its four buttons
  at the decoded positions and the instruction well, and rebinds the remake's own `UInputSettings`
  mappings, which is the functionality the original delivers. Its Defaults button parses
  `Config/DefaultInput.ini` **directly**, not through `FConfigCacheIni` — the hierarchy already has
  the player's saved `Input.ini` on top, which is exactly what "defaults" has to ignore.
- **Save Game / Save Game As refuse in the message box**, as the main menu's Open items do; there
  is no save system. Leave City therefore skips the original's second confirm (string 49) and goes
  straight to the front end, which is what answering No does.
- **The Settings key is the remake's choice.** App command 0x3f is not in `input.cfg` — the file
  binds 78 commands and 0x3f is not among them — so it is wired in the window procedure. The port
  binds Escape, which is also the key the page itself answers as Continue.

## Verified

Built clean, all 134 automation tests pass (six new under `SimCopter.Settings.*` cover the
two-variant item map, both page layouts, the eight slider ranges and their round trip, the volume
mapping, the new horizontal slider axis and the DefaultInput.ini parse). Every rectangle was drawn
back over the original page art before any of it was written
(`Docs/scratchpad/overlay_settings_rects.py`), then the pages were run and screenshotted
(`Docs/scratchpad/shoot_settings_screens.ps1`, shots in `Docs/scratchpad/settings-art/`).

Two things only the screenshots could have caught:

- **`-log` opens a console window that becomes the process's "main" window**, so a screenshot
  script that trusts `MainWindowHandle` photographs the log. Enumerate the process's top-level
  windows and take the biggest non-console one — and pin it `HWND_TOPMOST`, because
  `SetForegroundWindow` is refused when the caller does not already own the foreground and fails
  silently.
- **Cancelling the Graphics page must not re-apply the ini.** `LoadSettings` + `ApplySettings`
  reverts to the last *saved* state, which is not the state on entry whenever something overrode
  it — `-ResX`/`-ResY` is the obvious case, and cancelling resized the window mid-run even though
  the resolution row was never touched. The page now snapshots the fields it can change and puts
  those back, and only touches the resolution when it actually moved it.
