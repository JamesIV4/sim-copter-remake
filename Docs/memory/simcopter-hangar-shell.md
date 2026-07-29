# SimCopter hangar shell

*"Where the hangar shell's text and layout actually live (exe STRINGTABLE, not .rdata), and the traps in the catalog/upgrades/inventory art"*

*Recorded 2026-07-26; ported into the repo 2026-07-29.*

Ported 2026-07-26 as `ASimCopterHangar` + `SSimCopterHangarMenu` (Private/UI) + `USimCopterHangarArt`
+ `SimCopterHangarShop` + `USimCopterCareerSubsystem`.

**The shell's text is in the exe's Win32 STRINGTABLE, not in .rdata.** Grepping
`.ghidra-exports/_strings.json` for "Bambi" or "Catalog" finds nothing — those live in RT_STRING
resources. Dump them by parsing the PE resource directory (script pattern in the session
scratchpad; ~60 lines of stdlib Python). English is resource language 1033 with ids 100..599;
the other five languages are the *same* ids plus a per-language block offset (French +2000,
German +3300, Italian +4000, Spanish +5000, Dutch +6000).

Id map worth keeping: 100-102/125-128 hangar buttons, 400-408 helicopter names *by runtime type*,
410-414 equipment names in inventory-column order, 420-422 + 439-441 letterheads, 430-444 catalog
labels/buttons, 460-467 History, 470-477 Specialties, 480-487 Description (all three **by catalog
row**), 490-494 upgrade blurbs, 500-521 weekday/month names, 530-532 log buttons, 570-587 mission
type names.

**The hangar goes on the airport's terminal plot** - the middle 2x2 of the block, i.e.
`SimCopterAirport::GetTerminalTile`, which the airport port already resolves
([[simcopter-airport-spawn]]). That is where the original puts its own airport building (XBLD 0xf6
-> GEO object 0x096 on base 0x165); standing the hangar beside the block instead just gives the
airport two buildings. Demolish the terminal first with `bLeaveRubble=false` - object 0x165 is a
flat 2x2 slab exactly on the apron, so leaving it z-fights the hangar floor - and lift the floor
2 cm, because with the slab gone it would then z-fight the terrain instead. Height needs no trace:
`FUN_004829f0` flattens the whole 4x4 to one sample, so the pad table's Z *is* the plot's Z (and a
trace there would find the terminal's own roof).

**Original textures for the building** are page **40** of `BMP/SIM3D.BMP`: object 0x096's fourteen
faces use cells 20..23 for its walls (cell 23 on five of them), and pad object 0x08b uses cell 61
for its slab. Its roof faces are type 15 - flat-shaded, no texture at all - so a roof cell has to
be borrowed (52 is the airport's gravel). Decode the page with the GEO pack's own CMAP palette
(sim3d1.max + SIM3D.BMP), `ExtractAtlasTile` cuts the 32x32 cell, and `M_SimCopterLitTexture`'s
`Texture` parameter takes it; set `AddressX/Y = TA_Wrap` and tile UV0 by length/170cm to match the
original's own UV density (its UVs run to 3.0 across a 478 cm side).

Art traps (all measured off the shipped BMPs, cross-checked against `FUN_0042a7d0`/`FUN_0042b840`):

- **There is no hangar building in the original.** `dhangar.bmp` is a rendered still and the shell
  is reached from a menu, so the hangar *mesh* is a remake invention - only the plot, the height
  and the skin come from the original.
- All nine `CAT_*T.BMP` tab strips draw the **same eight tabs in catalog-row order at the same x
  positions** — only the page corner behind them moves. One hit-rect table serves all nine. Tabs
  span x 12..467 inside the strip, and the strip goes at page (80, 420), which lands them exactly
  over the row printed on `catalog.bmp` at x 92..547.
- `CATALOGE.BMP` already has its three letterheads **and** all five equipment photos printed on it
  — draw only the blurbs. `INVNTORY.BMP` does **not**: its header band is blank paper and the
  diagonal column labels have only the rules, so both are runtime text (rotate the labels -45°).
- The upgrades page reads **column-major** (left column top-to-bottom, then right). That is what
  makes `FUN_0042d840`'s `{0, 1, 3, 4, 2}` row->equipment-index table line up; row order left-to-
  right does not.
- Helicopter prices are heli.twk's `New Cost` control (Ctrl11 in every shipped section), not a
  .data constant — the per-type block's `+0x44` is only a placeholder until `FUN_00489e20` runs.
- Palette index **254** (cyan) is the colour key across the sprite bitmaps (button strips, the
  tick), same as PEOPLE1. Full-page backgrounds load opaque.

Two engine traps hit on the way:

- `UPrimitiveComponent::OnComponentBeginOverlap` did not fire for a pawn that **teleported** into
  the trigger (console `BugItGo`, and presumably a respawn). The actor now also re-checks the box
  in a 0.15s tick — keep both paths.
- `FVector2D` is double-precision in UE5, so `TestEqual(TEXT(".."), Vec.X, SomeFloatExpr)` is
  ambiguous. Cast the expected side to `double`.

Verification recipe is [[simcopter-ingame-verification]]; `BugItGo <x> <y> <z> <pitch> <yaw> <roll>`
through the `~` console is the way to move the camera, since synthesized keys never reach gameplay
input.
