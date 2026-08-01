# SimCopter save/load

*"Keep the original's Save, Save As, and career/user Open behavior, but version the remake's
payload honestly instead of labelling an incomplete archive `.scc` or `.scu`."*

*Recorded 2026-07-31.*

The implementation is `USimCopterSaveSubsystem` plus `USimCopterSaveGame`, with
`SSimCopterSaveNameDialog` in the in-city Settings flow and `SSimCopterSaveGamePicker` in the
front end. The subsystem survives the level change from `/Game/MainMenu` to the selected city and
applies loaded state around the normal city-entry path.

## Original contract

- Settings item 4 follows `FUN_00407c30`: a session that already has a named path calls
  `SaveGame` (`FUN_004200e0`); an unnamed one enters Save As (`FUN_00420670`). Item 5 always enters
  Save As. Return 0 reports STRINGTABLE 48, **"Game saved!"**.
- Leaving the city first uses STRINGTABLE 11, then asks STRINGTABLE 49, **"Do you want to save the
  game?"**. Yes saves or asks for a name; No leaves without saving.
- Main-menu item 1 opens a career `*.scc`; item 3 opens a user-game `*.scu`. The load path is
  `FUN_0041fc50` / `FUN_0041fcd0`, which rejects files whose type marker does not match the chosen
  career/user item.
- `FUN_004200e0` writes a chunked archive headed by CPTR and mode-specific CRER/USER data, followed
  by CFILE, CINF/UINF, CSET, BOMB and CSUM data. BOMB is the live-world side of the save: missions,
  actors, and mutable city objects do not belong to the small career header.

The function-level Settings and main-menu evidence remains in
`Docs/scratchpad/settings-DECODED.md` and `Docs/scratchpad/mainmenu-DECODED.md`. The save and load
functions above were also re-decompiled directly through `ghidra-bridge` before this port.

## Remake archive

The remake writes Unreal SaveGame archives below `SimCopterRemake/Saved/SaveGames` with managed
slot names of the form `SimCopter_C_<name>_<crc>.sav` or `SimCopter_U_<name>_<crc>.sav`. The display
name is stored separately, so filesystem sanitizing never changes what the picker shows. A save
contains a format magic and version and is structurally validated before travel.

These are deliberately **not byte-compatible `.scc` or `.scu` files**. Advertising compatibility
would be false until every original chunk, especially BOMB, is implemented. Career and user slots
remain separate and the two Open menu items only list the matching kind, preserving the original's
visible contract.

## State currently persisted (format version 2)

- session kind and city (career index or portable user-city filename);
- cash, score, session elapsed time, and the full live city-tuning record;
- owned-helicopter mask, depreciation values, and career log;
- active helicopter type, purchased equipment, tear-gas ammunition, selected tool, fuel, and
  damage.

Version 2 also carries a pointer-free live-world payload split by the same runtime owners that
already simulate the port:

- the mission scheduler/PRNG, all 30 mission records, all flame and fire-object slots, active
  message log, hospital targets/handoffs, fire-truck jet sweep, and the mission-owned smoke/ember
  effect pool;
- every traffic-owned person and vehicle, including dispatch-only service vehicles: transform,
  movement/route state, criminal/fire state, figure animation, complete BHAV stack/locals/
  attributes/LFSR, mission id, carrier/selection links, dispatch slots, whole-map records, traffic
  PRNG, and the hospital-roof replacement cooldown;
- planes, boats, train, wrecks, train-roof riders, UFO-abduction target, ambient PRNG, and the
  ambient crash/debris effect pool;
- the helicopter transform and fixed-point flight integrator, damage/fuel/engine state, camera
  view and smoothing, winch chain, selected bucket/harness, water load, spotlight, tool cooldowns,
  seat manifest, and the active water/tear-gas/Apache projectile pools;
- demolished building origins (replayed through the city actor so its meshes and traffic XBLD
  grid agree); and
- exact player possession. An on-foot save also carries movement mode/velocity, view and figure
  frame, jump state, and the stable identity of the real mission person being carried.

Actor pointers are never archived. Traffic gives each recreated ground agent a stable save
identity and every carrier, selection, dispatch crew, seat, handoff, and carried-person reference
relinks through that identity. The identity deliberately survives repeated load -> save -> load
cycles; transient UObject names can gain suffixes while old level objects await collection.

On load, the normal session path first resolves and loads the `.sc2` city. After
`StartCityJobsSession` has initialized its runtime owners, the save subsystem restores the durable
mission/career header and pauses the mission, traffic, and ambient actors. The normal airport and
hangar placement runs before live state is replayed in dependency order: demolished city objects,
aircraft, traffic/people and their pointer links, ambient vehicles, mission actor bookkeeping, then
player possession/on-foot state. Ticks resume only after the complete saved frame exists. Loading
into the helicopter selects the saved camera immediately rather than blending from the temporary
airport pawn.

## Deliberate boundary

This is a BOMB-equivalent serializer for the systems the remake currently ports, not a byte-for-byte
port of the original chunk writer. It still does **not** make remake saves compatible with `.scc`
or `.scu`, and must not be advertised that way. Each runtime owner has its own magic/version and
rejects a malformed or future byte stream instead of interpreting raw UObject memory. Legacy
format-version-1 remake saves remain header-only and use the old airport-start restore path.

## UI and console paths

- Settings **Save Game** overwrites the current slot, or opens the name dialog for a new session.
- Settings **Save Game As** always opens the name dialog.
- Leave City restores the original second save prompt and only leaves after a successful requested
  save; cancelling Save As returns to Settings.
- Main-menu **Open Career Game** and **Open User Game** show newest-first filtered pickers with city,
  cash, score, and UTC timestamp.
- `SimSaveGame [name]` saves from a city; no name means overwrite the current slot.
- `SimLoadGame <slot>` loads a managed slot from the main menu and accepts either kind after archive
  validation.

## Verification

`SimCopter.SaveGame.*` automation covers name/slot normalization, archive serialization and version
rejection, kind isolation, and the version-2 blob fields. Mission/effect owner round trips have
focused automation coverage. The implementation is verified through the standard C++ build and
headless automation; it has not been driven visually in-game.
