# SimCopter "Check-up" repair/refuel menu

*"The Damage and Fuel sliders are in DOLLARS, and heli[0x34] is hit points, not damage."*

*Recorded 2026-07-30.*

Ported as `FSimCopterCheckup` (`Public/Game/SimCopterCheckup.h`) + `SSimCopterCheckupMenu`
(`Private/UI`), raised by `ASimCopterHelicopterPawn::UpdateCheckupOffer`. Full function-by-function
decode with citations: `Docs/scratchpad/checkup-menu-DECODED.md`.

## Text

STRINGTABLE ids **590..598**, English block — resources, *not* `.rdata`, same as the hangar shell
([[simcopter-hangar-shell]]): 590 `Check-up`, 591 `Funds:`, 592 `Total Cost:`, 593 `Damage`,
594 `Fuel`, 595 `Teargas`, 596 `OK`, 597 `Cancel`, 598 `Cost:`.

## The two things that will mislead you

1. **The Damage and Fuel sliders are denominated in dollars**, not hit points or gallons. Their
   maxima are the price of a full repair / full tank; the appliers convert dollars back. Only the
   Teargas slider counts units, at **$50 a canister** (`FUN_0048a570`), capped at **10**
   (`FUN_0048a560`).

2. **`heli[0x34]` is HIT POINTS, not accumulated damage** — it starts at MaxDamage and falls. The
   repair cost is `(MaxDamage - hitPoints) * dollarsPerPoint` and repairing *adds*. The remake's
   `FSimCopterFlightModel::HitPoints` already had this right; its separate float `CurrentDamage` is
   a HUD counter and has to be kept in step by hand.

## Prices

`heli.twk` already exposes both rates: **"Repair Rate"** = `DAT_00504130` = dollars per hit point,
**"Fuel Cost"** = `DAT_00504134` = dollars per gallon (both 16.16).

**Servicing anywhere but the airport costs triple.** `FUN_0048a380`/`FUN_0048a480` multiply the
slider maximum by 3 when `FUN_004823a0(heli.x, heli.y, 0xf6, 2)` fails — XBLD 0xf6 is the airport
terminal and radius 2 covers the twelve pads ringing it ([[simcopter-airport-spawn]]).

**The repair applier divides the dollars back by 3 off-airport; the fuel applier does not.** That
asymmetry is in the original (`FUN_0048a3e0` has the check, `FUN_0048a4e0` has none), so
off-airport fuel is only nominally tripled — the player just moves the slider less far. Ported
verbatim; do not "fix" it.

`FUN_0048a3e0` also ends with `if (hitPoints < 4) hitPoints = 0;` — a repair leaving fewer than
four points zeroes them.

## When it opens — `FUN_00444750`

Tested every frame from `FUN_00449850`: view mode 3, standing on the airport, and either **≥ $21**
(`0x15`) of repair or fuel outstanding, or the launcher fitted with **< 5** canisters. The port
adds a once-per-touchdown latch the original does not have — the original re-tests every frame and
has no way to dismiss the panel and stay parked, so without it Cancel would reopen instantly.

**Playable remake divergence (2026-07-30): every airport landing opens the panel.** The original
threshold remains implemented and tested as `ShouldOffer`, but automatic gameplay uses
`ShouldOpenOnAirportLanding`, whose service policy gate is only `bAtAirport`. Runtime additionally
requires a player-controlled helicopter that has actually been airborne: initial pad placement,
an unpossessed helicopter ticking at game start, and entering a parked helicopter are not
landings. The panel refuses to construct until `FlapArt` can load the original BMPs, and the latch
is set only after `OpenCheckupMenu` succeeds.

Airport eligibility uses the airport builder's exact twelve perimeter pad tiles around the middle
2x2 hangar plot. It does not depend on finding a surviving terminal XBLD in a radius scan.

## Layout — read the ASM, not the decompile

**The decompile aliases the rectangles; the assembly states them outright.** Ghidra reuses the
same four stack slots across all sixteen of `FUN_00443c20`'s controls, so its C output shows one
rectangle over and over and looks unrecoverable. It is not: each control writes its four dwords as
plain `MOV dword ptr [ESP + n], imm` stores immediately before it is constructed, and the string
id and the font/justify/colour setters follow. `Docs/scratchpad/parse_checkup_rects.py` walks the
`dump-asm` listing and prints all sixteen; `overlay_checkup_rects.py` draws them back over the page
art, where every one lands exactly on the printed furniture. **This generalises — reach for
`dump-asm` before measuring a bitmap for any of the original's dialogs.** The one trap: a `PUSH`
between the stores shifts every later displacement by four, so the slots pair up by *address
order*, not by the literal offsets in the listing.

Two things the earlier bitmap-measured layout got wrong, both now fixed:

- **`Total Cost:` is in the TOP well, not the bottom one.** The top well carries all four readouts
  on two lines — title (112,36)-(344,66), then `Funds:` (72,70)-(148,85) with its number at
  (154,70)-(206,85) and `Total Cost:` (236,70)-(328,85) with its number at (332,70)-(380,85). Each
  number sits in its own box directly after its label, not pushed out to the panel edge.
- **The recess along the bottom is the button tray.** OK and Cancel are given degenerate 1x1
  rects — the control sizes itself from BUTTON.BMP's 100x28 frames — so only the origin matters:
  (186,390) and (288,390), side by side. Not stacked in the colour-keyed corner notch.

Tracks are 26x202 at (91,108), **(191,176)** and (333,108). The Fuel track is the one to get
wrong: it is dropped 68 px below its neighbours *and* sits left of the panel's centre line, so
treating it as "the middle of a symmetrical row" puts its thumb off the printed groove.

Fonts come from `[vt+0xe0]`: **30 for the title, 14 for everything else**. Those are Windows font
heights (cell, internal leading included), so the Slate point sizes that fill the same boxes are
about three quarters of them. `[vt+0xe4]` centres every label and the three cost readouts; the
Funds and Total numbers are the only controls that never get the call.

## The sliders: SLIDERTV.BMP is the thumb, SLIDCHK.BMP is the bar

`FUN_0040af00` takes a bar bitmap and a thumb bitmap, and stores each into its own field
(`+0x2b` bar, `+0x18` thumb). `FUN_00443c20` passes **SLIDCHK.BMP as the bar and NULL as the
thumb**, and a null thumb falls back to the constructor's default pair — `SLIDERTH.BMP` for a
horizontal control, **`SLIDERTV.BMP`** for a vertical one. All three Check-up sliders are
vertical, so all three use SLIDERTV.BMP: 22x18, a grey metal cap with a red stripe across it, no
colour key.

The remake draws only the thumb. CHECKUP.BMP already prints the same recessed track at all three
control rects, and the loose SLIDCHK.BMP is 193 px tall against a 202 px rect, so painting it over
the page would only shift the rivets out of register.

**Do not try to do this with a styled `SSlider`.** It lays a vertical slider out as a horizontal
one and applies a -90° render transform to the whole thing, which rotates the thumb bitmap too —
the 22x18 cap ends up on its end with the stripe running vertically. `SSimCopterCheckupSlider` is
a small `SLeafWidget` instead; its travel maths is static and unit-tested
(`SimCopter.Checkup.SliderTravel`).

`SimCheckup` on the possessed pawn raises the panel without flying to the airport.

## Remake visual tuning

As of 2026-07-30 the thumb is intentionally rendered at 1.5x (33x27). The decoded 26x202 track
rectangles remain the source of truth. Its visual midpoint is tuned 3.5 px right and 5.5 px up
from the decoded mathematical centreline to meet the actual bright groove in CHECKUP.BMP. The
top and bottom travel limits are each inset by half the rendered thumb height (13.5 px), making
the groove terminate at the thumb's red midpoint rather than its outer edge.

The Damage and Teargas label blocks are shifted down 4 px, Fuel is shifted down 7 px, and each
cost line is lower still to leave a visible gap below its type label. These are explicit remake
visual adjustments over the decoded overlapping text rectangles.
