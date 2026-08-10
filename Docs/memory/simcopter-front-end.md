# SimCopter front end — main menu and career city select

*"The main menu is five text controls on main1.bmp, and the whole layout is one 0x54-byte stack
descriptor — read it with dump-asm, not from the decompile."*

*Recorded 2026-07-31.*

Ported as `SSimCopterMainMenu`, `SSimCopterCareerSelect`, `SSimCopterMessageBox`,
`SSimCopterUserCityPicker` and the shared `SimCopterFrontEnd` scaffolding (all `Private/UI`),
driven by `ASimCopterMainMenuGameMode`, which is the remake's `FUN_00449cb0` / `FUN_0044c710`.
Career graph in `SimCopterCareerProgression`. Full function-by-function decode with citations:
`Docs/scratchpad/mainmenu-DECODED.md`.

## The four things that will mislead you

1. **The whole main menu is one descriptor, and the decompile hides it.** `FUN_00411900` builds a
   0x54-byte struct on the stack — first string id, item count, x, first y, stride, font height,
   two colours — and `FUN_0045e5f0` copies it into the page. Ghidra aliases those slots exactly as
   it does for the Check-up dialog ([[simcopter-checkup-menu]]); `dump-asm` states every one.
   Values: **items 55..59, five of them, x 116, first y 42, stride 64, font height 26**. There is
   no per-item layout code at all — `FUN_0045e920` is generic and serves the play menu too.

2. **`FUN_0045f670` loads main5.bmp into +0x214 and main4.bmp into +0x218**, i.e. the opposite
   order to the one the decompile reads like. Get it backwards and the arrow keys go where the
   lamps belong. Both bitmaps are two columns; the **selected** row adds one column width (0x3c
   for main4, 0x27 for main5) to its source left and right, which lights the lamp and presses the
   key. The first lamp row is 56 px tall and the rest are 64 — that is what makes the column meet
   itself down the page.

3. **The three career-screen cities are `{0, 1, 2}` only for a *new* career.** `FUN_00411ca0`
   passes the current record's successor trio and `FUN_00457c90` falls back to `{0,1,2}` when it
   gets NULL — and it gets NULL when the "new career" flag `app+0xb0` is set, which is exactly what
   main-menu item 0 sets. Cancel exists only in that case too; a career *advancement* gets one
   centred OK.

4. **The career is a branching graph with its own level field, and `career.twk` has none of it.**
   `FUN_00408370` fills 30 records at **0x518dc8** (four bytes below the base the older session
   note starts from). `+0x00` is a **career level 0..11** feeding STRINGTABLE 290..301, and
   `+0x28..+0x30` the successor trio. The ladder is three cities wide to level 5, two to level 9,
   then one. It does **not** always climb: City0 offers City1, which is on the same level.
   Rebuilt by `Docs/scratchpad/parse_career_table.py`, transcribed into
   `SimCopterCareerProgression`.

## What each menu item does — `FUN_0044c710`

0 New Career Game → career select. 1/2/3 → a `GetOpenFileName` dialog (`*.scc` / `*.sc2` / `*.scu`)
then straight into the city. 4 Quit → **no confirmation**; string 3 "Are you sure you want to
quit?" belongs to the in-game Settings screen, not here. **Esc on the main menu is Quit** —
`FUN_0044c710` rewrites message 0x3ea to item 4. A 300-second idle timeout starts the attract demo
(unported).

## Layout, colours, sound

Coordinates are the original's 640x480 screen. main1.bmp sits at **(2,29)**, main2.bmp at (289,0)
and main3.bmp at (427,315) — those two are hose decorations that meet the screen edges, given
degenerate 1x1 rects because the control sizes itself from the bitmap.

Colours are written a byte at a time, so they are Win32 `COLORREF` (low byte red): items
**RGB(128,133,74)**, selected **RGB(234,239,154)**; the career readouts **RGB(181,240,0)**.

Sound is standalone objects, not the 130-slot table: **menuback.wav loops** under the main menu
(`Play(1,1)`, stopped by the page's `[vt+0xfc]`), **menu.wav** ticks on every selection change,
**career.wav** on opening the career screen and **carsel.wav** on every arrow key there — even
when the wheel does not move.

`FUN_0045fc60`'s hit test is `29 < x < 394` and the item control's own top and bottom, and a text
control is only as tall as its font, so **a row catches the pointer over its 26 px text band, not
over the whole 64 px plate**. Ported as-is.

The career screen's selection glow is **carsel.bmp**, a 557x743 sheet holding two copies of the
three panel frames in the *page's own coordinates*: glowing at y+0, plain at **y+360**. Each panel
is four border strips, left/top/right/bottom, because the middle is where the preview movie plays.

## Main-menu sky movie (authentic port, full-screen extension)

The installed/RIP `SMK/MENUSKY.SMK` is a zero-byte CD stub; it is **not** evidence that the movie
was absent. `FUN_0044d070` resolves `menusky.smk` through the CD-data path, opens it on a 0x27c-byte
movie object, binds `DAT_00519cc0` (the display palette), and writes 1 to movie+8 (loop). The
original CD file is SMK2, **640x480, 201 frames at exactly 71 ms/frame**: a 14.271-second loop.
Full evidence is in `Docs/scratchpad/menusky-DECODED.md`.

`Tools/Unreal/BakeMenuSky.py` verifies those values and produces a gitignored H.264 MP4 for Unreal
without changing frame count or timing. `SSimCopterMenuCloudBackdrop` keeps the exact movie centred
under the exact 4:3 menu art. Its largest always-clear live sky crop, `(427,29)-(640,315)`, repeats
behind that frame so wider viewports are animated edge to edge without stretching the authentic
composition or exposing the movie's cyan compression matte.

## Remake divergences (all deliberate)

- **Career preview movies are transcoded, not decoded at runtime.** Each career panel runs the
  authentic `city<N>_s.smk` loop (`FUN_00407c50(2, ...)` appends `_s.smk`). UE cannot decode
  Smacker directly, so `Tools/Unreal/BakeCareerPreviews.py` preserves each loop's 200x108 image,
  75 frames and 71 ms cadence in `Content/Generated/Movies/Career/CITY<N>_S.mp4`, which remains
  gitignored alongside the user's original data.
- **No file dialog.** `SSimCopterUserCityPicker` lists the same `.sc2` files on menu4.bmp — the
  original's keyboard-shortcut list page — keeping title string 40. Its rectangles are *measured*
  off that bitmap, not decoded, because the original never lays a file list on it. Display labels
  prefer the city's internal `CNAM` chunk, fall back to the filename, remove the extension and all
  punctuation except dashes and single quotes, strip a trailing `SC2` marker even when it is
  embedded in `CNAM`, and normalize the result to title case; selection still returns the untouched
  source path.
- **Open Career/User Game use an in-app saved-game picker.** The original routes both items through
  a Win32 file dialog (`*.scc` for careers and `*.scu` for user games). The remake instead lists
  its versioned SaveGame slots on `menu4.bmp`, filtered to the same career/user split. See
  [[simcopter-save-load]].
- The old debug menu's "start with mission N" extras are gone; `SimLoadMission` in the city level
  already covers them.

## Verified

The career preview restoration built clean on 2026-08-08 and all seven `SimCopter.FrontEnd.*`
tests passed. `BakeCareerPreviews.py` validated all 30 generated MP4s against the authentic
200x108, 75-frame, 71 ms source loops. Each live media texture is drawn through a rounded Slate
mesh with an 8 px alpha-feather. CARSEL's selection glow is painted over it through a second mask:
the opaque centre is a genuinely hollow rounded opening with its own 4 px feather, so only the
frame and glow remain above the movie. The media playback and adjusted readout placement were not
verified on screen.

The authentic menu sky update built clean on 2026-08-01, and all six `SimCopter.FrontEnd.*` tests
passed, including the new decoded movie timing/aspect/full-screen-extension coverage. The generated
movie itself was probed as H.264, 640x480, 201 frames, `1000/71` fps, 14.271 seconds. It was not
verified on screen.

Built clean, all 128 automation tests pass (five new ones under `SimCopter.FrontEnd.*` cover both
selection wheels, the two layouts and the career graph), and every rectangle above was drawn back
over the original BMPs before any of it was written — `Docs/scratchpad/overlay_*.py`. The screens
were then run and screenshotted (`Docs/scratchpad/shoot_frontend_screens.ps1`, shots in
`Docs/scratchpad/mainmenu-art/`): all four land exactly on the printed furniture. Two things the
overlays could not have caught and the screenshots did: Slate's default `SListView` paints an
opaque background, which covered the picker's printed paper, and dark text on the career panels'
orange plate is unreadable at that size.

## Sliders: there are none here

The original's slider dialogs — City Settings (`cityset.bmp` + `slidcity.bmp`, strings 330..340),
Graphics (`render.bmp`) and Sound (`sound.bmp`, strings 130..142) — all hang off the **in-game**
Settings screen (`playmenu.bmp`, control 0x7d3, strings 60..67, routed by `FUN_0044c9e0`). Nothing
reachable from the main menu has one. When that screen is ported, `SSimCopterCheckupSlider` is the
template ([[simcopter-checkup-menu]]).
